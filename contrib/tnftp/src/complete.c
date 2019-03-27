/*	$NetBSD: complete.c,v 1.10 2009/05/20 12:53:47 lukem Exp $	*/
/*	from	NetBSD: complete.c,v 1.46 2009/04/12 10:18:52 lukem Exp	*/

/*-
 * Copyright (c) 1997-2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
__RCSID(" NetBSD: complete.c,v 1.46 2009/04/12 10:18:52 lukem Exp  ");
#endif /* not lint */

/*
 * FTP user program - command and file completion routines
 */

#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#endif	/* tnftp */

#include "ftp_var.h"

#ifndef NO_EDITCOMPLETE

static int	     comparstr		(const void *, const void *);
static unsigned char complete_ambiguous	(char *, int, StringList *);
static unsigned char complete_command	(char *, int);
static unsigned char complete_local	(char *, int);
static unsigned char complete_option	(char *, int);
static unsigned char complete_remote	(char *, int);

static int
comparstr(const void *a, const void *b)
{
	return (strcmp(*(const char * const *)a, *(const char * const *)b));
}

/*
 * Determine if complete is ambiguous. If unique, insert.
 * If no choices, error. If unambiguous prefix, insert that.
 * Otherwise, list choices. words is assumed to be filtered
 * to only contain possible choices.
 * Args:
 *	word	word which started the match
 *	list	list by default
 *	words	stringlist containing possible matches
 * Returns a result as per el_set(EL_ADDFN, ...)
 */
static unsigned char
complete_ambiguous(char *word, int list, StringList *words)
{
	char insertstr[MAXPATHLEN];
	char *lastmatch, *p;
	size_t i, j;
	size_t matchlen, wordlen;

	wordlen = strlen(word);
	if (words->sl_cur == 0)
		return (CC_ERROR);	/* no choices available */

	if (words->sl_cur == 1) {	/* only once choice available */
		p = words->sl_str[0] + wordlen;
		if (*p == '\0')		/* at end of word? */
			return (CC_REFRESH);
		ftpvis(insertstr, sizeof(insertstr), p, strlen(p));
		if (el_insertstr(el, insertstr) == -1)
			return (CC_ERROR);
		else
			return (CC_REFRESH);
	}

	if (!list) {
		matchlen = 0;
		lastmatch = words->sl_str[0];
		matchlen = strlen(lastmatch);
		for (i = 1 ; i < words->sl_cur ; i++) {
			for (j = wordlen ; j < strlen(words->sl_str[i]); j++)
				if (lastmatch[j] != words->sl_str[i][j])
					break;
			if (j < matchlen)
				matchlen = j;
		}
		if (matchlen > wordlen) {
			ftpvis(insertstr, sizeof(insertstr),
			    lastmatch + wordlen, matchlen - wordlen);
			if (el_insertstr(el, insertstr) == -1)
				return (CC_ERROR);
			else
				return (CC_REFRESH_BEEP);
		}
	}

	putc('\n', ttyout);
	qsort(words->sl_str, words->sl_cur, sizeof(char *), comparstr);
	list_vertical(words);
	return (CC_REDISPLAY);
}

/*
 * Complete a command
 */
static unsigned char
complete_command(char *word, int list)
{
	struct cmd *c;
	StringList *words;
	size_t wordlen;
	unsigned char rv;

	words = ftp_sl_init();
	wordlen = strlen(word);

	for (c = cmdtab; c->c_name != NULL; c++) {
		if (wordlen > strlen(c->c_name))
			continue;
		if (strncmp(word, c->c_name, wordlen) == 0)
			ftp_sl_add(words, ftp_strdup(c->c_name));
	}

	rv = complete_ambiguous(word, list, words);
	if (rv == CC_REFRESH) {
		if (el_insertstr(el, " ") == -1)
			rv = CC_ERROR;
	}
	sl_free(words, 1);
	return (rv);
}

/*
 * Complete a local file
 */
static unsigned char
complete_local(char *word, int list)
{
	StringList *words;
	char dir[MAXPATHLEN];
	char *file;
	DIR *dd;
	struct dirent *dp;
	unsigned char rv;
	size_t len;

	if ((file = strrchr(word, '/')) == NULL) {
		dir[0] = '.';
		dir[1] = '\0';
		file = word;
	} else {
		if (file == word) {
			dir[0] = '/';
			dir[1] = '\0';
		} else
			(void)strlcpy(dir, word, file - word + 1);
		file++;
	}
	if (dir[0] == '~') {
		char *p;

		if ((p = globulize(dir)) == NULL)
			return (CC_ERROR);
		(void)strlcpy(dir, p, sizeof(dir));
		free(p);
	}

	if ((dd = opendir(dir)) == NULL)
		return (CC_ERROR);

	words = ftp_sl_init();
	len = strlen(file);

	for (dp = readdir(dd); dp != NULL; dp = readdir(dd)) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

#if defined(DIRENT_MISSING_D_NAMLEN)
		if (len > strlen(dp->d_name))
			continue;
#else
		if (len > dp->d_namlen)
			continue;
#endif
		if (strncmp(file, dp->d_name, len) == 0) {
			char *tcp;

			tcp = ftp_strdup(dp->d_name);
			ftp_sl_add(words, tcp);
		}
	}
	closedir(dd);

	rv = complete_ambiguous(file, list, words);
	if (rv == CC_REFRESH) {
		struct stat sb;
		char path[MAXPATHLEN];

		(void)strlcpy(path, dir,		sizeof(path));
		(void)strlcat(path, "/",		sizeof(path));
		(void)strlcat(path, words->sl_str[0],	sizeof(path));

		if (stat(path, &sb) >= 0) {
			char suffix[2] = " ";

			if (S_ISDIR(sb.st_mode))
				suffix[0] = '/';
			if (el_insertstr(el, suffix) == -1)
				rv = CC_ERROR;
		}
	}
	sl_free(words, 1);
	return (rv);
}
/*
 * Complete an option
 */
static unsigned char
complete_option(char *word, int list)
{
	struct option *o;
	StringList *words;
	size_t wordlen;
	unsigned char rv;

	words = ftp_sl_init();
	wordlen = strlen(word);

	for (o = optiontab; o->name != NULL; o++) {
		if (wordlen > strlen(o->name))
			continue;
		if (strncmp(word, o->name, wordlen) == 0)
			ftp_sl_add(words, ftp_strdup(o->name));
	}

	rv = complete_ambiguous(word, list, words);
	if (rv == CC_REFRESH) {
		if (el_insertstr(el, " ") == -1)
			rv = CC_ERROR;
	}
	sl_free(words, 1);
	return (rv);
}

/*
 * Complete a remote file
 */
static unsigned char
complete_remote(char *word, int list)
{
	static StringList *dirlist;
	static char	 lastdir[MAXPATHLEN];
	StringList	*words;
	char		 dir[MAXPATHLEN];
	char		*file, *cp;
	size_t		 i;
	unsigned char	 rv;
	char		 cmdbuf[MAX_C_NAME];
	char		*dummyargv[3] = { NULL, NULL, NULL };

	(void)strlcpy(cmdbuf, "complete", sizeof(cmdbuf));
	dummyargv[0] = cmdbuf;
	dummyargv[1] = dir;

	if ((file = strrchr(word, '/')) == NULL) {
		dir[0] = '\0';
		file = word;
	} else {
		cp = file;
		while (*cp == '/' && cp > word)
			cp--;
		(void)strlcpy(dir, word, cp - word + 2);
		file++;
	}

	if (dirchange || dirlist == NULL ||
	    strcmp(dir, lastdir) != 0) {		/* dir not cached */
		const char *emesg;

		if (dirlist != NULL)
			sl_free(dirlist, 1);
		dirlist = ftp_sl_init();

		mflag = 1;
		emesg = NULL;
		while ((cp = remglob(dummyargv, 0, &emesg)) != NULL) {
			char *tcp;

			if (!mflag)
				continue;
			if (*cp == '\0') {
				mflag = 0;
				continue;
			}
			tcp = strrchr(cp, '/');
			if (tcp)
				tcp++;
			else
				tcp = cp;
			tcp = ftp_strdup(tcp);
			ftp_sl_add(dirlist, tcp);
		}
		if (emesg != NULL) {
			fprintf(ttyout, "\n%s\n", emesg);
			return (CC_REDISPLAY);
		}
		(void)strlcpy(lastdir, dir, sizeof(lastdir));
		dirchange = 0;
	}

	words = ftp_sl_init();
	for (i = 0; i < dirlist->sl_cur; i++) {
		cp = dirlist->sl_str[i];
		if (strlen(file) > strlen(cp))
			continue;
		if (strncmp(file, cp, strlen(file)) == 0)
			ftp_sl_add(words, cp);
	}
	rv = complete_ambiguous(file, list, words);
	sl_free(words, 0);
	return (rv);
}

/*
 * Generic complete routine
 */
unsigned char
complete(EditLine *cel, int ch)
{
	static char word[FTPBUFLEN];
	static size_t lastc_argc, lastc_argo;

	struct cmd *c;
	const LineInfo *lf;
	int dolist, cmpltype;
	size_t celems, len;

	lf = el_line(cel);
	len = lf->lastchar - lf->buffer;
	if (len >= sizeof(line))
		return (CC_ERROR);
	(void)strlcpy(line, lf->buffer, len + 1);
	cursor_pos = line + (lf->cursor - lf->buffer);
	lastc_argc = cursor_argc;	/* remember last cursor pos */
	lastc_argo = cursor_argo;
	makeargv();			/* build argc/argv of current line */

	if (cursor_argo >= sizeof(word))
		return (CC_ERROR);

	dolist = 0;
			/* if cursor and word is same, list alternatives */
	if (lastc_argc == cursor_argc && lastc_argo == cursor_argo
	    && strncmp(word, margv[cursor_argc] ? margv[cursor_argc] : "",
			cursor_argo) == 0)
		dolist = 1;
	else if (cursor_argc < (size_t)margc)
		(void)strlcpy(word, margv[cursor_argc], cursor_argo + 1);
	word[cursor_argo] = '\0';

	if (cursor_argc == 0)
		return (complete_command(word, dolist));

	c = getcmd(margv[0]);
	if (c == (struct cmd *)-1 || c == 0)
		return (CC_ERROR);
	celems = strlen(c->c_complete);

		/* check for 'continuation' completes (which are uppercase) */
	if ((cursor_argc > celems) && (celems > 0)
	    && isupper((unsigned char) c->c_complete[celems-1]))
		cursor_argc = celems;

	if (cursor_argc > celems)
		return (CC_ERROR);

	cmpltype = c->c_complete[cursor_argc - 1];
	switch (cmpltype) {
		case 'c':			/* command complete */
		case 'C':
			return (complete_command(word, dolist));
		case 'l':			/* local complete */
		case 'L':
			return (complete_local(word, dolist));
		case 'n':			/* no complete */
		case 'N':			/* no complete */
			return (CC_ERROR);
		case 'o':			/* local complete */
		case 'O':
			return (complete_option(word, dolist));
		case 'r':			/* remote complete */
		case 'R':
			if (connected != -1) {
				fputs("\nMust be logged in to complete.\n",
				    ttyout);
				return (CC_REDISPLAY);
			}
			return (complete_remote(word, dolist));
		default:
			errx(1, "complete: unknown complete type `%c'",
			    cmpltype);
			return (CC_ERROR);
	}
	/* NOTREACHED */
}

#endif /* !NO_EDITCOMPLETE */
