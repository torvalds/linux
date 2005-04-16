/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include "init.h"

/* Modified from create_mem_file and start_debugger */
static char *tempdir = NULL;

static void __init find_tempdir(void)
{
	char *dirs[] = { "TMP", "TEMP", "TMPDIR", NULL };
	int i;
	char *dir = NULL;

	if(tempdir != NULL) return;	/* We've already been called */
	for(i = 0; dirs[i]; i++){
		dir = getenv(dirs[i]);
		if((dir != NULL) && (*dir != '\0'))
			break;
	}
	if((dir == NULL) || (*dir == '\0')) 
		dir = "/tmp";

	tempdir = malloc(strlen(dir) + 2);
	if(tempdir == NULL){
		fprintf(stderr, "Failed to malloc tempdir, "
			"errno = %d\n", errno);
		return;
	}
	strcpy(tempdir, dir);
	strcat(tempdir, "/");
}

int make_tempfile(const char *template, char **out_tempname, int do_unlink)
{
	char tempname[MAXPATHLEN];
	int fd;

	find_tempdir();
	if (*template != '/')
		strcpy(tempname, tempdir);
	else
		*tempname = 0;
	strcat(tempname, template);
	fd = mkstemp(tempname);
	if(fd < 0){
		fprintf(stderr, "open - cannot create %s: %s\n", tempname, 
			strerror(errno));
		return -1;
	}
	if(do_unlink && (unlink(tempname) < 0)){
		perror("unlink");
		return -1;
	}
	if(out_tempname){
		*out_tempname = strdup(tempname);
		if(*out_tempname == NULL){
			perror("strdup");
			return -1;
		}
	}
	return(fd);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
