/*
	st2gpx.c

	Extract data from MS Streets & Trips .est and Autoroute .axe files in GPX format.

    Copyright (C) 2003 James Sherring, james_sherring@yahoo.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA


	This app depends on istorage & istorage-make from Pabs (pabs3@zip.to)
	and James Clark's Expat xml parser from http://www.libexpat.org/.

*/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <string.h>

#include <malloc.h>
#include <crtdbg.h>


// #include <getopt.h>
#include "getopt.h"
#include "gpx.h"
#include "st2gpx.h"
#include "annotations.h"
#include "journey.h"
#include "properties.h"
#include "contents.h"
#include "ppinutil.h"
#include "pushpins.h"

struct st2gpx_options opts;

void * xmalloc(size_t size)
{
	void *obj;
	if (size<0)
	{
		fprintf(stderr, "**** Error: trying to malloc %d bytes ****/n");
		debug_pause();
		exit(-1);
	}
//#ifdef   _DEBUG
//	obj = _malloc_dbg(size, _NORMAL_BLOCK, __FILE__, __LINE__ );
//#else
	obj = malloc(size);
//#endif
	if (!obj)
	{
		fprintf(stderr, "Unable to allocate %d bytes of memory.\n", size);
		debug_pause();
		exit(-1);
	}

#ifdef _DEBUG
#ifdef MEMTRACE
	printf("Malloc'd %d bytes at %lx\n", size, obj);
#endif
#endif

	return obj;
}

void * xrealloc(void* ptr, size_t size)
{
	void *obj;
	if (size<0)
	{
		fprintf(stderr, "**** Error: trying to malloc %d bytes ****/n");
		debug_pause();
		exit(-1);
	}

#ifdef _DEBUG
#ifdef MEMTRACE
	printf("reallocing from %lx\n", ptr);
#endif
#endif

//#ifdef   _DEBUG
//	obj = _realloc_dbg(ptr, size, _NORMAL_BLOCK, __FILE__, __LINE__ );
//#else
	obj = realloc(ptr, size);
//#endif
	if (!obj)
	{
		fprintf(stderr, "Unable to (re)allocate %d bytes of memory.\n", size);
		debug_pause();
		exit(-1);
	}

#ifdef _DEBUG
#ifdef MEMTRACE
	printf("realloc'd %d bytes at %lx\n", size, obj);
#endif
#endif

	return obj;
}

void xfree(void * obj)
{
//#ifdef   _DEBUG
//	_free_dbg(obj, _NORMAL_BLOCK);
//#else

#ifdef _DEBUG
#ifdef MEMTRACE
	printf("freeing mem at %lx\n", obj);
#endif
#endif

	free(obj);
// #endif
}

char * str2ascii(char* str)
{
	int i;
	int len=strlen(str);
	unsigned char * ustr = (unsigned char*)str;
	for(i=0; i<len; i++)
		// FIXME saxcount complains that 0x1c is an invalid character, what else??
		if ( (ustr[i]>127) || (ustr[i]==0x1c) )
		{
			printf("Converting non-ascii char %c to space.\n", ustr[i]);
			str[i]=' ';
		}
	return str;
}

char * strappend(char* str1, char* str2)
// create a new string 
{
	int len1=strlen(str1);
	int len2=strlen(str2);
	char* nw = (char*)xmalloc(len1 + len2 +1);
	strcpy(nw, str1);
	strcpy(nw+len1, str2);
	return nw;
}

int readbytes(FILE* file, char* buf, int bytes2read)
{
	int i;
	i = fread(buf, 1, bytes2read, file);
	if (i<bytes2read)
	{
 		if (feof(file))
			printf("Unexpected end of file\n");
		else if (ferror(file))
			perror("Unexpected error while reading from file");
        printf("Read %d of a required %d bytes\n", i, bytes2read);
        printf("Dumping the buffer read before unexpected EOF or error\n");
        printbuf(buf,i);
        return(i);
	}

	if (opts.verbose_flag > 5)
	{
		printf("Readbytes: Read %d bytes from file\n",bytes2read);
		printbuf(buf, bytes2read);
		fflush(stdout);
	}
	return(i);
}

void show_usage()
{
	printf("st2gpx - Export data from MS Streets & Trips and Autoroute to GPX format\n\n");
	// FIXME update this line
	printf("Usage: st2gpx [-hr] [-v verbose-level] [-g gpx-in-file] [-G gpx-out-file]");
	printf("              [-m mpst-in-file] [-M pcx5-out-file] [-F st-mod-file] stfile\n\n");
	printf("-h : Help (this text)\n");
	printf("-r : Export drawn-lines as routes instead of tracks\n");
	printf("-g gpx-in-file    : Import data from GPX XML format gpx-in-file\n");
	printf("-G gpx-out-file   : Write output to GPX XML format gpx-out-file\n");
	printf("-F st-mod-file    : Write modified stfile with imported data\n");
	printf("                    to (new) st-mod-file.\n");
	printf("-m mpst-in-file   : Import data from Garmin MapSource text-export mpst-in-file\n");
	printf("-M pcx5-out-file  : Write output to Garmin MapSource importable pcx5-out-file\n");
	printf("\n");
	printf("export hint:\t st2gpx stfile (or use drag & drop)\n");
	printf("import hint:\t st2gpx -g gpx-in-file -F st-mod-file st-template-file\n");
//	printf("\n");
	debug_pause();
	printf("Debugging options:\n");
	printf("-e : Explore data further\n");
	printf("-v [n]              : Set debugging verbosity to 'n' (0-6, default 2)\n");
	printf("-u userdata-file    : Process pushpins in (mdb) file userdata-file\n");
	printf("-j journey-file     : Process Journey in file journey-file\n");
	printf("-a annotations-file : Process Annotations in file annotations-file\n");
	printf("-p properties-file  : Analyse OLE properties-file\n");

	debug_pause();
	exit(0);
}

void xsystem(char* syscmd)
{
	int status;
	printf("%s \n", syscmd);
	_flushall();
	status = system(syscmd);
	if (status)
		fprintf(stderr, "system call returned an error\n");
}

main(int argc, char** argv)
{
	int c;

	char* jour_in_file_name=NULL;
	char* annot_in_file_name=NULL;
	char* ppin_in_file_name=NULL;
	char* gpx_in_file_name=NULL;
	char* gpx_out_file_name=NULL;
	char* import_file_name=NULL;
	char* properties_file_name=NULL;

	char* contents_dir_name=NULL;
	char source_full_path[_MAX_PATH];
	char contents_full_path[_MAX_PATH];

	char* mpst_in_file_name=NULL;
	char* pcx5_out_file_name=NULL;

	char syscmd[1000];
    char cmddrv[_MAX_DRIVE];
    char cmddir[_MAX_DIR];
    char cmdfilename[_MAX_PATH];
    char cmdext[_MAX_EXT];
	char * cmdpath=NULL;


	struct pushpin_safelist* ppplist=NULL;
	struct journey* jour=NULL;
	struct annotations* annots=NULL;
	struct gpx_data* all_gpx=NULL;
	struct ole_property_set * strips_properties=NULL;
	struct ole_property * prop = NULL;
	struct contents * conts;

	char* temp_str=NULL;

//	int verify_eof_flag = 0;

	opts.explore_flag=0;
	opts.use_gpx_route=0;
	opts.verbose_flag=2;
	opts.source_file_name=NULL;
	opts.debug_wait_flag=0;
	opts.st_version_num = 0;
	opts.MapName = NULL;


#ifdef MEMCHK
	// Call _CrtCheckMemory at every allocation and deallocation request.
	SET_CRT_DEBUG_FIELD(_CRTDBG_CHECK_ALWAYS_DF);
	// Keep freed memory blocks in the heaps linked list, assign them the _FREE_BLOCK type, 
	// and fill them with the byte value 0xDD.
	SET_CRT_DEBUG_FIELD(_CRTDBG_DELAY_FREE_MEM_DF);
	// Perform automatic leak checking at program exit via a call to _CrtDumpMemoryLeaks 
	// and generate an error report if the application failed to free all the memory 
	// it allocated.
	SET_CRT_DEBUG_FIELD(_CRTDBG_LEAK_CHECK_DF);
#endif

#ifdef DEBUG_STDOUT
   _CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE );
   _CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDOUT );
   _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE );
   _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDOUT );
   _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
   _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDOUT );
#endif

  // _CrtSetBreakAlloc(136);


	while (1)
    {
		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt(argc, argv, "hu:j:a:g:G:m:M:F:p:rv::ew");

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c)
        {
        case 0:
			/* If this option set a flag, do nothing else now. */
			break;

        case 'h':
			show_usage();
			break;

        case 'u':
			// read a UserData (pushpin) file directly
			free(ppin_in_file_name);
			ppin_in_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(ppin_in_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Analysing UserData (pushpin) stream in file %s\n\n", jour_in_file_name);
           break;

       case 'j':
			// read a Journey file directly
			free(jour_in_file_name);
			jour_in_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(jour_in_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Analysing Journey stream in file %s\n\n", jour_in_file_name);
            break;

        case 'a':
			// read an annotation file directly
			annot_in_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(annot_in_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Analysing Annotation stream in file %s\n\n", annot_in_file_name);
            break;

        case 'g':
			gpx_in_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(gpx_in_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Reading GPX from file %s\n\n", gpx_in_file_name);
            break;

        case 'G':
			gpx_out_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(gpx_out_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Writing GPX output to file %s\n\n", gpx_out_file_name);
            break;

        case 'm':
			mpst_in_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(mpst_in_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Reading Garmin MapSource Text format from file %s\n\n", mpst_in_file_name);
            break;

        case 'M':
			pcx5_out_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(pcx5_out_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Writing Garmin MapSource importable pcx5 output to file %s\n\n", pcx5_out_file_name);
            break;

        case 'F':
			import_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(import_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Writing modified s&t file with imported gpx data to file %s\n\n", import_file_name);
            break;

        case 'p':
			properties_file_name = (char*)xmalloc(strlen(optarg)+1);
			strcpy(properties_file_name, optarg);
			if (opts.verbose_flag > 1)
				printf("Analysing OLE Properties from file %s\n\n", properties_file_name);
            break;

        case 'r':
			opts.use_gpx_route=1;
            break;

		case 'v':
			if (optarg==NULL)
				opts.verbose_flag=5;
			else
				opts.verbose_flag = atoi(optarg);
			break;

        case 'e':
			opts.explore_flag = 1;
			break;

        case 'w':
			opts.debug_wait_flag = 1;
			break;

        case '?':
			/* getopt_long already printed an error message. */
			show_usage();
			break;

        default:
			show_usage();
			abort ();
        }
    }

	if (optind == argc-1)
	{
			opts.source_file_name = (char*)xmalloc(strlen(argv[optind])+1);
			strcpy(opts.source_file_name, argv[optind]);
			if (opts.verbose_flag > 1)
				printf("Analysing autoroute file %s\n\n", opts.source_file_name);
			if (ppin_in_file_name==NULL)
				ppin_in_file_name = strappend(opts.source_file_name, ".Contents\\UserData.mdb");
			if (jour_in_file_name==NULL)
				jour_in_file_name = strappend(opts.source_file_name, ".Contents\\Journey");
			if (annot_in_file_name==NULL)
				annot_in_file_name = strappend(opts.source_file_name, ".Contents\\Annotations");
			if ((gpx_out_file_name==NULL) && (gpx_in_file_name==NULL))
				gpx_out_file_name = strappend(opts.source_file_name, ".gpx");
	}
	else if (optind < argc-1)
	{
	    printf("Unrecognised option %s\n", argv[optind+1]);
		show_usage();

	}
	else
	{
		if (opts.verbose_flag > 1)
			printf("Not analysing any core S&T or autoroute file\n");
	}

	if (opts.source_file_name)
	{
		// Open the compound file using the istorage utility
		// I probably should do this in a library and use streams instead of files

		// Find the path for istorage from the path in argv[0]

		// this is not ANSI
		_splitpath(argv[0], cmddrv, cmddir, cmdfilename, cmdext);
		//sprintf(cmdpath, "%s%s", cmddrv, cmddir);
		cmdpath = strappend(cmddrv, cmddir);

		_fullpath(source_full_path, opts.source_file_name, _MAX_PATH);

		sprintf(syscmd, "%sistorage\\istorage.exe \"%s\"", cmdpath, source_full_path);
		xsystem(syscmd);

		printf("*****************************************************************\n");
		printf("Finished istorage command\n");

		sprintf(syscmd, "rename \"%s.Contents\\UserData.\" UserData.mdb", opts.source_file_name);
		xsystem(syscmd);
	}

	// ***************************
	// begin processing the files
	// ***************************

	// Read GPX import file
	if (gpx_in_file_name)
	{
		all_gpx = process_gpx_in_file(gpx_in_file_name);
		if (all_gpx==NULL)
			printf("Didn't read any usable data from %s ???\n",gpx_in_file_name);
		printf("Read %d waypoints, %d routes and %d tracks from file %s\n", all_gpx->wpt_list_count, all_gpx->rte_list_count, all_gpx->trk_list_count, gpx_in_file_name);
		printf("Importing this data as %d lines\n", all_gpx->rte_list_count + all_gpx->trk_list_count);
	}

	// Read Mapsource text-export file.
	// Does it make any sense to try and merge with all_gpx from above? Not for now...
	if (mpst_in_file_name)
	{
		gpx_data_delete(all_gpx);
		all_gpx = read_mpstext(mpst_in_file_name);
		if (all_gpx==NULL)
			printf("Didn't read any usable data from %s ???\n",mpst_in_file_name);
		printf("Read %d waypoints, %d routes and %d tracks from file %s\n",
				all_gpx->wpt_list_count, all_gpx->rte_list_count, 
				all_gpx->trk_list_count, mpst_in_file_name);
		printf("Importing this data as %d lines\n", 
				all_gpx->rte_list_count + all_gpx->trk_list_count);
	}

	// ole properties from S&T source file
	if (opts.source_file_name)
	{
		strips_properties=read_ole_properties(opts.source_file_name, NULL);
		prop = get_propterty(strips_properties, 0x60002);
		if ((prop!=NULL) && (prop->buf != NULL) )
		{
			opts.st_version_num = *(int*)(prop->buf); 
			printf("Autoroute/S&T version in %s is %d\n", opts.source_file_name, opts.st_version_num);
		}
		prop = get_propterty(strips_properties, 0x10000);
		if ((prop!=NULL) && (prop->buf != NULL) )
		{
			opts.MapName = (WCHAR*)(prop->buf + 4); 
			wprintf(L"MapName is %ls\n", opts.MapName);
			if(wcscmp(opts.MapName, L"USA")==0)
				opts.isUSA=1;
			else if(wcscmp(opts.MapName, L"EUR")!=0 )
				printf("Unknown map type, assuming EUR\n");
		}
	}

	// check the contents stream
	//if(opts.explore_flag)
	{
		temp_str = strappend(opts.source_file_name, ".Contents\\Contents");
		conts = read_contents(temp_str);
		free(temp_str);
		temp_str=NULL;
	}

	// ole properties from any file, just for debuging
	if (properties_file_name)
		ole_property_set_delete(read_ole_properties(NULL,opts.source_file_name));

	// read the data in the S&T source file
	if (ppin_in_file_name)
		ppplist = process_pushpin_file(ppin_in_file_name);
	if (jour_in_file_name)
		jour = process_journey_stream(jour_in_file_name, ppplist);
	if (annot_in_file_name)
		annots = process_annotations_stream(annot_in_file_name);

	// export GPX
	if ( (gpx_out_file_name) && (all_gpx==NULL) )
		gpx_write_all(gpx_out_file_name, ppplist, jour, annots);

	// export Mapsource pcx5
	if (pcx5_out_file_name)
		pcx5_export(pcx5_out_file_name, ppplist, jour, annots);

	// Merge the data in the S&T source file with any imported GPX/mapsource data
	if ((all_gpx!=NULL) && (opts.source_file_name!=NULL))
	{
		merge_gpx_annot(annots, all_gpx);
		if (annots==NULL)
			printf("After merging data, dont have any annotations???\n");
		printf("After merging data, there are %d annotations\n", annots->num_annotations);
		//print_annotations(annots);
		write_annotations(annots, annot_in_file_name);
		//print_annotations(annots);

		// ********************
		// This is experimantal
		// ********************
		temp_str = strappend(opts.source_file_name, ".Contents\\Contents");
		write_pushpins_from_gpx(ppin_in_file_name, all_gpx, conts, temp_str);
		free(temp_str);
		temp_str=NULL;
	}

	// create the s&t/autoroute file from the modified parts
	if ((opts.source_file_name!=NULL) && (all_gpx!=NULL) && (import_file_name!=NULL))
	{
		// Actually, we should allow NULL import_file_name and invent a sensible name

		sprintf(syscmd, "rename %s.Contents\\UserData.mdb UserData.", opts.source_file_name);
		xsystem(syscmd);

		contents_dir_name=(char*)xmalloc(strlen(opts.source_file_name)+20);
		sprintf(contents_dir_name, "%s.Contents", opts.source_file_name);

		_fullpath(contents_full_path, contents_dir_name, _MAX_PATH);

		sprintf(syscmd, "%sistorage\\istorage-make.exe \"%s\"", cmdpath, contents_full_path);
		xsystem(syscmd);

		printf("*****************************************************************\n");
		printf("Finished istorage-make command\n");

		sprintf(syscmd, "del \"%s\"", import_file_name);
		xsystem(syscmd);

		_splitpath(opts.source_file_name, cmddrv, cmddir, cmdfilename, cmdext);
		sprintf(syscmd, "move \"%s.Contents.ole\" \"%s\"", opts.source_file_name, import_file_name);
		xsystem(syscmd);

	}

    // Clean up the compound file directory
	if (opts.source_file_name)
	{
		{
			sprintf(syscmd, "echo y|del \"%s.Contents\"", opts.source_file_name);
			xsystem(syscmd);

			sprintf(syscmd, "rmdir \"%s.Contents\"", opts.source_file_name);
			xsystem(syscmd);
		}
	}

	// free variables. Not really necessary.
	annotations_delete(annots);
	pushpin_safelist_delete(ppplist);
	journey_delete(jour);
	gpx_data_delete(all_gpx);
	ole_property_set_delete(strips_properties);
	contents_delete(conts);

	xfree(cmdpath);
	xfree(ppin_in_file_name);
	xfree(jour_in_file_name);
	xfree(annot_in_file_name);
	xfree(gpx_in_file_name);
	xfree(gpx_out_file_name);
	xfree(pcx5_out_file_name);
	xfree(import_file_name);
	xfree(opts.source_file_name);
	xfree(contents_dir_name);
	xfree(mpst_in_file_name);
	
	if (opts.verbose_flag>5)
		printf("Done freeing all\n");

	//debug_show_sizes();
	//debug_pause();
	//printf("exiting main\n");

	_flushall();

#ifdef _DEBUG
//	_CrtDumpMemoryLeaks();  
#endif
	debug_pause();
}
