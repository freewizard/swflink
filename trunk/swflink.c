/*
	SWFLink
	Du Song <freewizard@gmail.com>
	a tool to merge swf generated by MTASC

	Based on swftools by Matthias Kramm <kramm@quiss.org> 

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
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../lib/rfxswf.h"
#include "../lib/args.h"
#include "../lib/log.h"
#include "../config.h"

struct config_t
{
	int before;
	int loglevel;
};
struct config_t config;

char * main_filename = 0;
char * lib_filename[128];
int libCount = 0;
char * outputname = "output.swf";

int args_callback_option(char*name,char*val) {
	if (!strcmp(name, "o"))
	{
		outputname = val;
		return 1;
	}
	else if (!strcmp(name, "v"))
	{
		config.loglevel ++;
		return 0;
	}
	else if (!strcmp(name, "z"))
	{
		return 0;
	}
	else if (!strcmp(name, "V"))
	{	
		printf("swflink - $Rev$\n");
		exit(0);
	}
	else 
	{
		fprintf(stderr, "Unknown option: -%s\n", name);
		exit(1);
	}
}

static struct options_t options[] = {
	{"o", "output"},
	{"v", "verbose"},
	{"b", "before"},
	{0,0}
};

int args_callback_longoption(char*name,char*val) {
	return args_long2shortoption(options, name, val);
}

int args_callback_command(char*name, char*val) {
	char*myname = strdup(name);
	char*filename;
	filename = strchr(myname, '=');
	if(filename) {
		*filename = 0;
		filename++;
	} else {
	// argument has no explicit name field. guess one from the file name
		char*path = strrchr(myname, '/');
		char*ext = strrchr(myname, '.');
		if(!path) path = myname;
		else path ++;
		if(ext) *ext = 0;
		myname = path;
		filename = name;
	}

	if(!main_filename) {
		main_filename = filename;
	} else {		 
		msg("<verbose> library entity %s (named \"%s\")\n", filename, myname);

		lib_filename[libCount] = filename;
		libCount ++;
	}
	return 0;
}

void args_callback_usage(char *name)
{
	printf("\n");
	printf("Usage: %s [-v] [-z] [-o output.swf] main.swf library1.swf [... libraryN.swf]\n", name);
	printf("\n");
	printf("-o , --output <outputfile>     explicitly specify output file. (otherwise, output.swf will be used)\n");
	printf("-v , --verbose                 Be verbose. Use more than one -v for greater effect \n");
	//printf("-b , --before <object id>      Insert before <object id>, this id must exist in main.swf\n");
	printf("\n");
}

static char defineBitmap[65536];

static int filtered_tags_in_lib[] = 
{
	ST_SETBACKGROUNDCOLOR,
	ST_DEFINEFONTNAME,
	ST_FILEATTRIBUTES,
	ST_PROTECT,
	ST_SCRIPTLIMITS,
	ST_ENABLEDEBUGGER,
	ST_ENABLEDEBUGGER2,
	ST_DOACTION,
	ST_SHOWFRAME,
	-1
};

static char tag_allowed(int id)
{
	int t=0;
	while(filtered_tags_in_lib[t]>=0)
	{
		if(filtered_tags_in_lib[t] == id) 
			return 0;
		t++;
	}
	return 1; 
}

void do_link(SWF*mainSwf, SWF*libSwf, SWF*newSwf)
{
	if(!mainSwf->fileVersion && libSwf)
		mainSwf->fileVersion = libSwf->fileVersion;

	mainSwf->fileAttributes |= libSwf->fileAttributes;

	swf_FoldAll(mainSwf);
	swf_FoldAll(libSwf);

	memset(defineBitmap, 0, sizeof(defineBitmap));

	char* depths;
	int t;
	TAG*tag;
	TAG*mtag,*stag;

	tag = mainSwf->firstTag;
	while(tag)
	{
		if(swf_isDefiningTag(tag)) {
			int defineid = swf_GetDefineID(tag);
			msg("<debug> [main]  tagid %02x defines object %d", tag->id, defineid);
			defineBitmap[defineid] = 1;
		}
		tag = tag->next;
	}

	swf_Relocate(libSwf, defineBitmap);

	memcpy(newSwf, mainSwf, sizeof(SWF));

	tag = newSwf->firstTag = swf_InsertTag(0, ST_REFLEX); // to be removed later

	mtag = mainSwf->firstTag;
	while(mtag && !swf_isDefiningTag(mtag) && mtag->id!=ST_END)
	{
		int num=1;
		msg("<debug> [main]  write tag %02x (%d bytes in body)", 
			mtag->id, mtag->len);
		tag = swf_InsertTag(tag, mtag->id);
		swf_SetBlock(tag, mtag->data, mtag->len);
		mtag = mtag->next;
	}

	stag = libSwf->firstTag;
	while(stag && stag->id!=ST_END)
	{
		if(tag_allowed(stag->id)) {
			msg("<debug> [lib]   write tag %02x (%d bytes in body)", 
				stag->id, stag->len);
			tag = swf_InsertTag(tag, stag->id);
			swf_SetBlock(tag, stag->data, stag->len);
		} else {
			msg("<debug> [lib]   ignore tag %02x (%d bytes in body)", 
				stag->id, stag->len);
		}
		stag = stag->next;	
	}

	while(mtag && mtag->id!=ST_END)
	{
		int num=1;
		msg("<debug> [main]  write tag %02x (%d bytes in body)", 
			mtag->id, mtag->len);
		tag = swf_InsertTag(tag, mtag->id);
		swf_SetBlock(tag, mtag->data, mtag->len);
		mtag = mtag->next;
	}

	tag = swf_InsertTag(tag, ST_END);

	swf_DeleteTag(newSwf, tag);
}


int main(int argn, char *argv[])
{
	int fi;
	SWF mainSwf;
	SWF libSwf;
	SWF newSwf;
	int t;

	config.loglevel = 2; 
	config.before = -1;

	processargs(argn, argv);
	initLog(0,-1,0,0,-1,config.loglevel);

	int ret;
	msg("<verbose> main %s \n", main_filename);
	fi = open(main_filename, O_RDONLY|O_BINARY);
	if(fi<0) {
		msg("<fatal> Failed to open %s\n", main_filename);
		exit(1);
	}
	ret = swf_ReadSWF(fi, &mainSwf);
	if(ret<0) {
		msg("<fatal> Failed to read from %s\n", main_filename);
		exit(1);
	}

	msg("<debug> Read %d bytes from main\n", ret);
	close(fi);

	for(t=0;t<libCount;t++) {
		msg("<verbose> library (%d) %s\n", t+1, lib_filename[t]);
	}

	if (!libCount)
	{
		msg("<error> You must have at least one library.");
		return 0;
	}
	t = libCount;
	{
		t--;
		msg("<notice> Linking %s to %s", lib_filename[t], main_filename);
		int ret;
		fi = open(lib_filename[t], O_RDONLY|O_BINARY);
		if(!fi) {
			msg("<fatal> Failed to open %s\n", lib_filename[t]);
			exit(1);
		}
		ret = swf_ReadSWF(fi, &libSwf);
		if(ret<0) {
			msg("<fatal> Failed to read from %s\n", lib_filename[t]);
			exit(1);
		}
		msg("<debug> Read %d bytes from libSwf file %s\n", ret, lib_filename[t]);
		close(fi);
		do_link(&mainSwf, &libSwf, &newSwf);
		mainSwf = newSwf;
	} while (t>0);

	if(!newSwf.fileVersion)
		newSwf.fileVersion = 4;

	fi = open(outputname, O_BINARY|O_RDWR|O_TRUNC|O_CREAT, 0666);

	if(1) { // force zlib
		if(newSwf.fileVersion < 6)
			newSwf.fileVersion = 6;
		newSwf.compressed = 1;
		swf_WriteSWF(fi, &newSwf);
	} else {
		newSwf.compressed = -1; // don't compress
		swf_WriteSWF(fi, &newSwf);
	}
	close(fi);
	return 0;
}