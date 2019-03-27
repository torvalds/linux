/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "ar.h"

ELFTC_VCSID("$Id: util.c 3174 2015-03-27 17:13:41Z emaste $");

static void	bsdar_vwarnc(struct bsdar *, int code,
		    const char *fmt, va_list ap);
static void	bsdar_verrc(struct bsdar *bsdar, int code,
		    const char *fmt, va_list ap);

static void
bsdar_vwarnc(struct bsdar *bsdar, int code, const char *fmt, va_list ap)
{

	fprintf(stderr, "%s: warning: ", bsdar->progname);
	vfprintf(stderr, fmt, ap);
	if (code != 0)
		fprintf(stderr, ": %s", strerror(code));
	fprintf(stderr, "\n");
}

void
bsdar_warnc(struct bsdar *bsdar, int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	bsdar_vwarnc(bsdar, code, fmt, ap);
	va_end(ap);
}

static void
bsdar_verrc(struct bsdar *bsdar, int code, const char *fmt, va_list ap)
{

	fprintf(stderr, "%s: fatal: ", bsdar->progname);
	vfprintf(stderr, fmt, ap);
	if (code != 0)
		fprintf(stderr, ": %s", strerror(code));
	fprintf(stderr, "\n");
}

void
bsdar_errc(struct bsdar *bsdar, int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	bsdar_verrc(bsdar, code, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#define	AR_STRMODE_SIZE	12
const char *
bsdar_strmode(mode_t m)
{
	static char buf[AR_STRMODE_SIZE];

#if ELFTC_HAVE_STRMODE
	/* Use the system's strmode(3). */
	strmode(m, buf);
	return buf;

#else
	char c;

	/*
	 * The first character of the string denotes the type of the
	 * entry.
	 */
	if (S_ISBLK(m))
		c = 'b';
	else if (S_ISCHR(m))
		c = 'c';
	else if (S_ISDIR(m))
		c = 'd';
#if	defined(S_ISFIFO)
	else if (S_ISFIFO(m))
		c = 'p';
#endif
#if	defined(S_ISLNK)
	else if (S_ISLNK(m))
		c = 'l';
#endif
	else if (S_ISREG(m))
		c = '-';
#if	defined(S_ISSOCK)
	else if (S_ISSOCK(m))
		c = 's';
#endif
	else
		c = '?';
	buf[0] = c;

	/* The next 3 characters show permissions for the owner. */
	buf[1] = (m & S_IRUSR) ? 'r' : '-';
	buf[2] = m & S_IWUSR ? 'w' : '-';
	if (m & S_ISUID)
		c = (m & S_IXUSR) ? 's' : 'S';
	else
		c = (m & S_IXUSR) ? 'x' : '-';
	buf[3] = c;

	/* The next 3 characters describe permissions for the group. */
	buf[4] = (m & S_IRGRP) ? 'r' : '-';
	buf[5] = m & S_IWGRP ? 'w' : '-';
	if (m & S_ISGID)
		c = (m & S_IXGRP) ? 's' : 'S';
	else
		c = (m & S_IXGRP) ? 'x' : '-';
	buf[6] = c;


	/* The next 3 characters describe permissions for others. */
	buf[7] = (m & S_IROTH) ? 'r' : '-';
	buf[8] = m & S_IWOTH ? 'w' : '-';
	if (m & S_ISVTX)	/* sticky bit */
		c = (m & S_IXOTH) ? 't' : 'T';
	else
		c = (m & S_IXOTH) ? 'x' : '-';
	buf[9] = c;

	/* End the string with a blank and NUL-termination. */
	buf[10] = ' ';
	buf[11] = '\0';

	return buf;
#endif	/* !ELTC_HAVE_STRMODE */
}

int
bsdar_is_pseudomember(struct bsdar *bsdar, const char *name)
{
	/*
	 * The "__.SYMDEF" member is special in the BSD format
	 * variant.
	 */
	if (bsdar->options & AR_BSD)
		return (strcmp(name, AR_SYMTAB_NAME_BSD) == 0);
	else
		/*
		 * The names "/ " and "// " are special in the SVR4
		 * variant.
		 */
		return (strcmp(name, AR_STRINGTAB_NAME_SVR4) == 0 ||
		    strcmp(name, AR_SYMTAB_NAME_SVR4) == 0);
}
