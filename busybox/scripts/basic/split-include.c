/*
 * split-include.c
 *
 * Copyright abandoned, Michael Chastain, <mailto:mec@shout.net>.
 * This is a C version of syncdep.pl by Werner Almesberger.
 *
 * This program takes autoconf.h as input and outputs a directory full
 * of one-line include files, merging onto the old values.
 *
 * Think of the configuration options as key-value pairs.  Then there
 * are five cases:
 *
 *    key      old value   new value   action
 *
 *    KEY-1    VALUE-1     VALUE-1     leave file alone
 *    KEY-2    VALUE-2A    VALUE-2B    write VALUE-2B into file
 *    KEY-3    -           VALUE-3     write VALUE-3  into file
 *    KEY-4    VALUE-4     -           write an empty file
 *    KEY-5    (empty)     -           leave old empty file alone
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ERROR_EXIT(strExit)						\
    {									\
	const int errnoSave = errno;					\
	fprintf(stderr, "%s: ", str_my_name);				\
	errno = errnoSave;						\
	perror((strExit));						\
	exit(1);							\
    }



int main(int argc, const char * argv [])
{
    const char * str_my_name;
    const char * str_file_autoconf;
    const char * str_dir_config;

    FILE * fp_config;
    FILE * fp_target;
    FILE * fp_find;

    int buffer_size;

    char * line;
    char * old_line;
    char * list_target;
    char * ptarget;

    struct stat stat_buf;

    /* Check arg count. */
    if (argc != 3)
    {
	fprintf(stderr, "%s: wrong number of arguments.\n", argv[0]);
	exit(1);
    }

    str_my_name       = argv[0];
    str_file_autoconf = argv[1];
    str_dir_config    = argv[2];

    /* Find a buffer size. */
    if (stat(str_file_autoconf, &stat_buf) != 0)
	ERROR_EXIT(str_file_autoconf);
    buffer_size = 2 * stat_buf.st_size + 4096;

    /* Allocate buffers. */
    if ( (line        = malloc(buffer_size)) == NULL
    ||   (old_line    = malloc(buffer_size)) == NULL
    ||   (list_target = malloc(buffer_size)) == NULL )
	ERROR_EXIT(str_file_autoconf);

    /* Open autoconfig file. */
    if ((fp_config = fopen(str_file_autoconf, "r")) == NULL)
	ERROR_EXIT(str_file_autoconf);

    /* Make output directory if needed. */
    if (stat(str_dir_config, &stat_buf) != 0)
    {
	if (mkdir(str_dir_config, 0755) != 0)
	    ERROR_EXIT(str_dir_config);
    }

    /* Change to output directory. */
    if (chdir(str_dir_config) != 0)
	ERROR_EXIT(str_dir_config);

    /* Put initial separator into target list. */
    ptarget = list_target;
    *ptarget++ = '\n';

    /* Read config lines. */
    while (fgets(line, buffer_size, fp_config))
    {
	const char * str_config;
	int is_same;
	int itarget;

	if (line[0] != '#')
	    continue;
	if ((str_config = strstr(line, " CONFIG_")) == NULL)
	    continue;

	/* We found #define CONFIG_foo or #undef CONFIG_foo.
	 * Make the output file name. */
	str_config += sizeof(" CONFIG_") - 1;
	for (itarget = 0; !isspace(str_config[itarget]); itarget++)
	{
	    int c = (unsigned char) str_config[itarget];
	    if (isupper(c)) c = tolower(c);
	    if (c == '_')   c = '/';
	    ptarget[itarget] = c;
	}
	ptarget[itarget++] = '.';
	ptarget[itarget++] = 'h';
	ptarget[itarget++] = '\0';

	/* Check for existing file. */
	is_same = 0;
	if ((fp_target = fopen(ptarget, "r")) != NULL)
	{
	    fgets(old_line, buffer_size, fp_target);
	    if (fclose(fp_target) != 0)
		ERROR_EXIT(ptarget);
	    if (!strcmp(line, old_line))
		is_same = 1;
	}

	if (!is_same)
	{
	    /* Auto-create directories. */
	    int islash;
	    for (islash = 0; islash < itarget; islash++)
	    {
		if (ptarget[islash] == '/')
		{
		    ptarget[islash] = '\0';
		    if (stat(ptarget, &stat_buf) != 0
		    &&  mkdir(ptarget, 0755)     != 0)
			ERROR_EXIT( ptarget );
		    ptarget[islash] = '/';
		}
	    }

	    /* Write the file. */
	    if ((fp_target = fopen(ptarget,  "w")) == NULL)
		ERROR_EXIT(ptarget);
	    fputs(line, fp_target);
	    if (ferror(fp_target) || fclose(fp_target) != 0)
		ERROR_EXIT(ptarget);
	}

	/* Update target list */
	ptarget += itarget;
	*(ptarget-1) = '\n';
    }

    /*
     * Close autoconfig file.
     * Terminate the target list.
     */
    if (fclose(fp_config) != 0)
	ERROR_EXIT(str_file_autoconf);
    *ptarget = '\0';

    /*
     * Fix up existing files which have no new value.
     * This is Case 4 and Case 5.
     *
     * I re-read the tree and filter it against list_target.
     * This is crude.  But it avoids data copies.  Also, list_target
     * is compact and contiguous, so it easily fits into cache.
     *
     * Notice that list_target contains strings separated by \n,
     * with a \n before the first string and after the last.
     * fgets gives the incoming names a terminating \n.
     * So by having an initial \n, strstr will find exact matches.
     */

    fp_find = popen("find * -type f -name \"*.h\" -print", "r");
    if (fp_find == 0)
	ERROR_EXIT( "find" );

    line[0] = '\n';
    while (fgets(line+1, buffer_size, fp_find))
    {
	if (strstr(list_target, line) == NULL)
	{
	    /*
	     * This is an old file with no CONFIG_* flag in autoconf.h.
	     */

	    /* First strip the \n. */
	    line[strlen(line)-1] = '\0';

	    /* Grab size. */
	    if (stat(line+1, &stat_buf) != 0)
		ERROR_EXIT(line);

	    /* If file is not empty, make it empty and give it a fresh date. */
	    if (stat_buf.st_size != 0)
	    {
		if ((fp_target = fopen(line+1, "w")) == NULL)
		    ERROR_EXIT(line);
		if (fclose(fp_target) != 0)
		    ERROR_EXIT(line);
	    }
	}
    }

    if (pclose(fp_find) != 0)
	ERROR_EXIT("find");

    return 0;
}
