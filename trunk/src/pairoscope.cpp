/***************************************************************************
 *   Copyright (C) 2009 by David Larson   *
 *   dlarson@linus31   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
//DONE Make BAM/SAM Alignment Loading
//TODO Add exclusion of specific maq tags
//FIXME Properly handle regions that buffer outside the chromosome
//FIXME Odd spacing
//FIXME occasional silent failures?
//TODO Allow specific transcript requests
//TODO Allow configuration file


#include <iostream>
#include <cstdlib>
#include <vector>
#include <set>
#include "YGenomeView.h"
#include "YMatePair.h"
#include <zlib.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <stdio.h>
#include <errno.h>
#include "YAlignmentFetcher.h"
#include "YTranscriptFetcher.h"

using namespace std;

static int pairoscope_usage() {
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage:   pairoscope [options] <align.bam> <chr> <start> <end> <align2.bam> <chr2> <start2> <end2> \n\n");
    fprintf(stderr, "Options: -q INT    minimum mapping quality [0]\n");
    fprintf(stderr, "         -p FLAG   output in pdf instead of png\n");
    fprintf(stderr, "         -b INT    size of buffer around region to include [100]\n");
    fprintf(stderr, "         -n FLAG   print the normal pairs too\n");  
    fprintf(stderr, "         -o STRING filename of the output png\n");
    fprintf(stderr, "         -W INT    Width of the document [1024]\n");
    fprintf(stderr, "         -H INT    Height of the document [768]\n");
    fprintf(stderr, "         -g STRING bam file of exons for gene models\n");
    fprintf(stderr, "         -f STRING list of types of maq flags for display\n");

    return 1;
}


int main(int argc, char *argv[])
{
    //DONE add command line handling

    extern int optind; //for getopt
    int c;  //for return value of getopt
    int min_qual = 0; //minimum alignment quality to report
    int buffer = 0;   //number of base pairs to pad the requested region with on either side  
    char *filename = (char *) "graph.png";  //default name of the file if none is specified
    bool print_normal = false, pdf = false; //booleans to track whether it will draw normal reads and whether it will generate a pdf instead of a png
    int doc_width = 1024, doc_height = 768; //default height and weight of the document in pixels
    char *gene_bam_file = NULL;             //for storing the filename of the annotation bam
    char *flags = NULL;                     //string of comma separated flags for selecting certain reads for display

    //get the command line options
    while((c = getopt(argc, argv, "q:b:npo:W:H:g:f:")) >= 0) {
        switch (c) {
            case 'q': 
                min_qual = atoi(optarg); 
                break;
            case 'b': 
                buffer = atoi(optarg); 
                break;
            case 'o':
                filename = optarg;
                break;
            case 'n':
                print_normal = true;
                break;
            case 'W':
                doc_width = atoi(optarg);
                break;
            case 'H':
                doc_height = atoi(optarg);
                break;
            case 'p':
                pdf = true;   
                filename = (char *) "graph.pdf";
                break; 
            case 'g':
                gene_bam_file = optarg;
                break;    
            case 'f':
                flags = strdup(optarg); 
                break;    
            default: 
                return pairoscope_usage();
        }
    }

    if((argc-optind) % 4 || argc-optind == 0) {
        if(flags) {
            free(flags);
        }
        return pairoscope_usage();
    }
    
    int regions = (argc-optind) / 4;    //calculate the number of regions
    
    //define types to simplify the actual vector declarations
    typedef vector<int> depth_buffer;   
    typedef vector<YTranscript*> transcript_buffer;
    
    vector<depth_buffer> depth(regions);    //create a vector of vectors to store depth
    vector<YMatePair*> mappedReads;         //vector of mapped reads
    vector<transcript_buffer> transcripts(regions); //vector of vectors to sotre transcripts
    hash_map_char<YMatePair*> unpaired_reads;   //hash to store reads that the mate has not been found. For matching up mates across translocations
    
    YAlignmentFetcher fetcher(min_qual,buffer,print_normal);    //alignment fetcher object

    set<int> flags_to_fetch;   //set of flags to display
    
    //Parse the requested flags
    if(flags) {
        char *flag = strtok(flags,",");
        if(!flag) {
            flag = flags;   //only a single non delimited string
        }
        do {
            flags_to_fetch.insert(atoi(flag));
        } while(flag = strtok(NULL,","));
    }

    if(flags) {
        free(flags);
    }

    cairo_surface_t *surface;
    cairo_t *cr;

    if(pdf) {
        //create a pdf surface if requested
        surface = cairo_pdf_surface_create(filename, doc_width, doc_height);
    }
    else {
        //create a png surface by default
        surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, doc_width, doc_height);
    }

    cr = cairo_create (surface);    //create the surface graphics context

    //Paint the page white
    cairo_set_source_rgb(cr, 1,1,1);
    cairo_paint(cr);

    //Define the size of the page
    YRect page(0,0,doc_width,doc_height);

    //Create the document on the (whole) page
    YGenomeView document(cr,page, &mappedReads);

    //parse reads for each region
    for(int i = 0; i < regions; i++) {
        bool return_value = false;
        
        //fetch the reads from the bam file
        return_value = fetcher.fetchBAMAlignments(argv[optind], argv[optind+1], atoi(argv[optind+2]), atoi(argv[optind+3]), &(depth[i]), &mappedReads, &unpaired_reads, &flags_to_fetch);
        if(return_value) {
            //add the region to the document for display
            document.addRegion((const char*) argv[optind+1], (unsigned) atoi(argv[optind+2]) - buffer, (unsigned int) atoi(argv[optind+3]) + buffer, &(depth[i]));
        }
        else {
            for( vector<YMatePair*>::iterator itr = mappedReads.begin(); itr != mappedReads.end(); ++itr) {
                delete *itr;
            }
            //we do not need to remove unpaired reads too as those are just references to actual objects in the vector 
            cairo_destroy (cr);
            cairo_surface_destroy (surface);
            return EXIT_FAILURE;
        }
        if(gene_bam_file ) {
            bool return_value = false;
            YTranscriptFetcher geneFetcher(buffer);
            return_value = geneFetcher.fetchBAMTranscripts(gene_bam_file, argv[optind+1], atoi(argv[optind+2]), atoi(argv[optind+3]), &(transcripts[i]));
            if(return_value) {
                document.addGeneTrack((const char*) argv[optind+1], (unsigned) atoi(argv[optind+2]) - buffer, (unsigned int) atoi(argv[optind+3]) + buffer, &(transcripts[i]));
            }
            else {               
                for(int j = 0; j != transcripts.size(); j++) {
                    transcript_buffer buffer = transcripts[j];
                    for( transcript_buffer::iterator itr = buffer.begin(); itr != buffer.end(); ++itr) {
                        delete *itr;
                    }
                }
                cairo_destroy (cr);
                cairo_surface_destroy (surface);
                return EXIT_FAILURE;
            }
        }
        optind = optind + 4;

    }


    document.display();
    //free all the reads! FREEEEEEDDDOOOOOMMM! 
    for( vector<YMatePair*>::iterator itr = mappedReads.begin(); itr != mappedReads.end(); ++itr) {
        delete *itr;
    }
    //delete transcripts
    for(int j = 0; j != transcripts.size(); j++) {
        transcript_buffer buffer = transcripts[j];
        for( transcript_buffer::iterator itr = buffer.begin(); itr != buffer.end(); ++itr) {
            delete *itr;
        }
    }
    cairo_destroy (cr);
    if(!pdf) {
        cairo_surface_write_to_png (surface, filename);
    }
    cairo_surface_destroy (surface);



    return EXIT_SUCCESS;
}

