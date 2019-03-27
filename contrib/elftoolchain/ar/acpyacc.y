%{
/*-
 * Copyright (c) 2008 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/mman.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libelftc.h"

#include "ar.h"

ELFTC_VCSID("$Id");


#define TEMPLATE "arscp.XXXXXXXX"

struct list {
	char		*str;
	struct list	*next;
};


extern int	yylex(void);
extern int	yyparse(void);

static void	yyerror(const char *);
static void	arscp_addlib(char *archive, struct list *list);
static void	arscp_addmod(struct list *list);
static void	arscp_clear(void);
static void	arscp_create(char *in, char *out);
static void	arscp_delete(struct list *list);
static void	arscp_dir(char *archive, struct list *list, char *rlt);
static void	arscp_end(int eval);
static void	arscp_extract(struct list *list);
static void	arscp_free_argv(void);
static void	arscp_free_mlist(struct list *list);
static void	arscp_list(void);
static struct list *arscp_mlist(struct list *list, char *str);
static void	arscp_mlist2argv(struct list *list);
static int	arscp_mlist_len(struct list *list);
static void	arscp_open(char *fname);
static void	arscp_prompt(void);
static void	arscp_replace(struct list *list);
static void	arscp_save(void);
static int	arscp_target_exist(void);

extern int		 lineno;

static struct bsdar	*bsdar;
static char		*target;
static char		*tmpac;
static int		 interactive;
static int		 verbose;

%}

%token ADDLIB
%token ADDMOD
%token CLEAR
%token CREATE
%token DELETE
%token DIRECTORY
%token END
%token EXTRACT
%token LIST
%token OPEN
%token REPLACE
%token VERBOSE
%token SAVE
%token LP
%token RP
%token COMMA
%token EOL
%token <str> FNAME
%type <list> mod_list

%union {
	char		*str;
	struct list	*list;
}

%%

begin
	: { arscp_prompt(); } ar_script
	;

ar_script
	: cmd_list
	|
	;

mod_list
	: FNAME { $$ = arscp_mlist(NULL, $1); }
	| mod_list separator FNAME { $$ = arscp_mlist($1, $3); }
	;

separator
	: COMMA
	|
	;

cmd_list
	: rawcmd
	| cmd_list rawcmd
	;

rawcmd
	: cmd EOL { arscp_prompt(); }
	;

cmd
	: addlib_cmd
	| addmod_cmd
	| clear_cmd
	| create_cmd
	| delete_cmd
	| directory_cmd
	| end_cmd
	| extract_cmd
	| list_cmd
	| open_cmd
	| replace_cmd
	| verbose_cmd
	| save_cmd
	| invalid_cmd
	| empty_cmd
	| error
	;

addlib_cmd
	: ADDLIB FNAME LP mod_list RP { arscp_addlib($2, $4); }
	| ADDLIB FNAME { arscp_addlib($2, NULL); }
	;

addmod_cmd
	: ADDMOD mod_list { arscp_addmod($2); }
	;

clear_cmd
	: CLEAR { arscp_clear(); }
	;

create_cmd
	: CREATE FNAME { arscp_create(NULL, $2); }
	;

delete_cmd
	: DELETE mod_list { arscp_delete($2); }
	;

directory_cmd
	: DIRECTORY FNAME { arscp_dir($2, NULL, NULL); }
	| DIRECTORY FNAME LP mod_list RP { arscp_dir($2, $4, NULL); }
	| DIRECTORY FNAME LP mod_list RP FNAME { arscp_dir($2, $4, $6); }
	;

end_cmd
	: END { arscp_end(EXIT_SUCCESS); }
	;

extract_cmd
	: EXTRACT mod_list { arscp_extract($2); }
	;

list_cmd
	: LIST { arscp_list(); }
	;

open_cmd
	: OPEN FNAME { arscp_open($2); }
	;

replace_cmd
	: REPLACE mod_list { arscp_replace($2); }
	;

save_cmd
	: SAVE { arscp_save(); }
	;

verbose_cmd
	: VERBOSE { verbose = !verbose; }
	;

empty_cmd
	:
	;

invalid_cmd
	: FNAME { yyerror(NULL); }
	;

%%

/* ARGSUSED */
static void
yyerror(const char *s)
{

	(void) s;
	printf("Syntax error in archive script, line %d\n", lineno);
}

/*
 * The arscp_open() function will first open an archive and check its
 * validity. If the archive format is valid, it will call
 * arscp_create() to create a temporary copy of the archive.
 */
static void
arscp_open(char *fname)
{
	struct archive		*a;
	struct archive_entry	*entry;
	int			 r;

	if ((a = archive_read_new()) == NULL)
		bsdar_errc(bsdar, 0, "archive_read_new failed");
	archive_read_support_format_ar(a);
	AC(archive_read_open_filename(a, fname, DEF_BLKSZ));
	if ((r = archive_read_next_header(a, &entry)))
		bsdar_warnc(bsdar, 0, "%s", archive_error_string(a));
	AC(archive_read_close(a));
	ACV(archive_read_free(a));
	if (r != ARCHIVE_OK)
		return;
	arscp_create(fname, fname);
}

/*
 * Create an archive.
 *
 * If the parameter 'in' is NULL (the 'CREATE' command), a new empty
 * archive will be created.  If the parameter 'in' is not NULL (the
 * 'OPEN' command), the resulting archive will be a modified version
 * of the existing archive.
 */
static void
arscp_create(char *in, char *out)
{
	struct archive		*a;
	int			 ifd, ofd;

	/* Delete the previously created temporary archive, if any. */
	if (tmpac) {
		if (unlink(tmpac) < 0)
			bsdar_errc(bsdar, errno, "unlink failed");
		free(tmpac);
	}

	tmpac = strdup(TEMPLATE);
	if (tmpac == NULL)
		bsdar_errc(bsdar, errno, "strdup failed");
	if ((ofd = mkstemp(tmpac)) < 0)
		bsdar_errc(bsdar, errno, "mkstemp failed");

	if (in) {
		/*
		 * The 'OPEN' command creates a temporary copy of the
		 * input archive.
		 */
		if ((ifd = open(in, O_RDONLY)) < 0 ||
		    elftc_copyfile(ifd, ofd) < 0) {
			bsdar_warnc(bsdar, errno, "'OPEN' failed");
			(void) close(ofd);
			if (ifd != -1)
				(void) close(ifd);
			return;
		}
		(void) close(ifd);
		(void) close(ofd);
	} else {
		/*
		 * The 'CREATE' command creates an "empty" archive (an
		 * archive consisting only of the archive header).
		 */
		if ((a = archive_write_new()) == NULL)
			bsdar_errc(bsdar, 0, "archive_write_new failed");
		archive_write_set_format_ar_svr4(a);
		AC(archive_write_open_fd(a, ofd));
		AC(archive_write_close(a));
		ACV(archive_write_free(a));
	}

	/* Override the previous target, if any. */
	if (target)
		free(target);

	target = out;
	bsdar->filename = tmpac;
}

/*
 * Add all modules of an archive to the current archive.  If the
 * parameter 'list' is not NULL, only those modules specified by
 * 'list' will be added.
 */
static void
arscp_addlib(char *archive, struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	bsdar->addlib = archive;
	ar_write_archive(bsdar, 'A');
	arscp_free_argv();
	arscp_free_mlist(list);
}

/*
 * Add modules to the current archive.
 */
static void
arscp_addmod(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_write_archive(bsdar, 'q');
	arscp_free_argv();
	arscp_free_mlist(list);
}

/*
 * Delete modules from the current archive.
 */
static void
arscp_delete(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_write_archive(bsdar, 'd');
	arscp_free_argv();
	arscp_free_mlist(list);
}

/*
 * Extract modules from the current archive.
 */
static void
arscp_extract(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_read_archive(bsdar, 'x');
	arscp_free_argv();
	arscp_free_mlist(list);
}

/*
 * List the contents of an archive (simple mode).
 */
static void
arscp_list(void)
{

	if (!arscp_target_exist())
		return;
	bsdar->argc = 0;
	bsdar->argv = NULL;
	/* Always verbose. */
	bsdar->options |= AR_V;
	ar_read_archive(bsdar, 't');
	bsdar->options &= ~AR_V;
}

/*
 * List the contents of an archive (advanced mode).
 */
static void
arscp_dir(char *archive, struct list *list, char *rlt)
{
	FILE	*out;

	/* If rlt != NULL, redirect the output to it. */
	out = NULL;
	if (rlt) {
		out = bsdar->output;
		if ((bsdar->output = fopen(rlt, "w")) == NULL)
			bsdar_errc(bsdar, errno, "fopen %s failed", rlt);
	}

	bsdar->filename = archive;
	if (list)
		arscp_mlist2argv(list);
	else {
		bsdar->argc = 0;
		bsdar->argv = NULL;
	}
	if (verbose)
		bsdar->options |= AR_V;
	ar_read_archive(bsdar, 't');
	bsdar->options &= ~AR_V;

	if (rlt) {
		if (fclose(bsdar->output) == EOF)
			bsdar_errc(bsdar, errno, "fclose %s failed", rlt);
		bsdar->output = out;
		free(rlt);
	}
	free(archive);
	bsdar->filename = tmpac;
	arscp_free_argv();
	arscp_free_mlist(list);
}


/*
 * Replace modules in the current archive.
 */
static void
arscp_replace(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_write_archive(bsdar, 'r');
	arscp_free_argv();
	arscp_free_mlist(list);
}

/*
 * Rename the temporary archive to the target archive.
 */
static void
arscp_save(void)
{
	mode_t mask;

	if (target) {
		if (rename(tmpac, target) < 0)
			bsdar_errc(bsdar, errno, "rename failed");
		/*
		 * Because mkstemp() creates temporary files with mode
		 * 0600, we set target archive's mode as per the
		 * process umask.
		 */
		mask = umask(0);
		umask(mask);
		if (chmod(target, 0666 & ~mask) < 0)
			bsdar_errc(bsdar, errno, "chmod failed");
		free(tmpac);
		free(target);
		tmpac = NULL;
		target= NULL;
		bsdar->filename = NULL;
	} else
		bsdar_warnc(bsdar, 0, "no open output archive");
}

/*
 * Discard the contents of the current archive. This is achieved by
 * invoking the 'CREATE' cmd on the current archive.
 */
static void
arscp_clear(void)
{
	char		*new_target;

	if (target) {
		new_target = strdup(target);
		if (new_target == NULL)
			bsdar_errc(bsdar, errno, "strdup failed");
		arscp_create(NULL, new_target);
	}
}

/*
 * Quit ar(1). Note that the 'END' cmd will not 'SAVE' the current
 * archive before exiting.
 */
static void
arscp_end(int eval)
{

	if (target)
		free(target);
	if (tmpac) {
		if (unlink(tmpac) == -1)
			bsdar_errc(bsdar, errno, "unlink %s failed", tmpac);
		free(tmpac);
	}

	exit(eval);
}

/*
 * Check if a target was specified, i.e, whether an 'OPEN' or 'CREATE'
 * had been issued by the user.
 */
static int
arscp_target_exist(void)
{

	if (target)
		return (1);

	bsdar_warnc(bsdar, 0, "no open output archive");
	return (0);
}

/*
 * Construct the list of modules.
 */
static struct list *
arscp_mlist(struct list *list, char *str)
{
	struct list *l;

	l = malloc(sizeof(*l));
	if (l == NULL)
		bsdar_errc(bsdar, errno, "malloc failed");
	l->str = str;
	l->next = list;

	return (l);
}

/*
 * Calculate the length of an mlist.
 */
static int
arscp_mlist_len(struct list *list)
{
	int len;

	for(len = 0; list; list = list->next)
		len++;

	return (len);
}

/*
 * Free the space allocated for a module list.
 */
static void
arscp_free_mlist(struct list *list)
{
	struct list *l;

	/* Note: list->str was freed in arscp_free_argv(). */
	for(; list; list = l) {
		l = list->next;
		free(list);
	}
}

/*
 * Convert a module list to an 'argv' array.
 */
static void
arscp_mlist2argv(struct list *list)
{
	char	**argv;
	int	  i, n;

	n = arscp_mlist_len(list);
	argv = malloc(n * sizeof(*argv));
	if (argv == NULL)
		bsdar_errc(bsdar, errno, "malloc failed");

	/* Note that module names are stored in reverse order. */
	for(i = n - 1; i >= 0; i--, list = list->next) {
		if (list == NULL)
			bsdar_errc(bsdar, errno, "invalid mlist");
		argv[i] = list->str;
	}

	bsdar->argc = n;
	bsdar->argv = argv;
}

/*
 * Free the space allocated for an argv array and its elements.
 */
static void
arscp_free_argv(void)
{
	int i;

	for(i = 0; i < bsdar->argc; i++)
		free(bsdar->argv[i]);

	free(bsdar->argv);
}

/*
 * Show a prompt if we are in interactive mode.
 */
static void
arscp_prompt(void)
{

	if (interactive) {
		printf("AR >");
		fflush(stdout);
	}
}

/*
 * The main function implementing script mode.
 */
void
ar_mode_script(struct bsdar *ar)
{

	bsdar = ar;
	interactive = isatty(fileno(stdin));
	while(yyparse()) {
		if (!interactive)
			arscp_end(EXIT_FAILURE);
	}

	/* Script ends without END */
	arscp_end(EXIT_SUCCESS);
}
