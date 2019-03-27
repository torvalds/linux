/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)fmt.c	8.4 (Berkeley) 4/15/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#include "ps.h"

static char *cmdpart(char *);
static char *shquote(char **);

static char *
shquote(char **argv)
{
	long arg_max;
	static size_t buf_size;
	size_t len;
	char **p, *dst, *src;
	static char *buf = NULL;

	if (buf == NULL) {
		if ((arg_max = sysconf(_SC_ARG_MAX)) == -1)
			errx(1, "sysconf _SC_ARG_MAX failed");
		if (arg_max >= LONG_MAX / 4 || arg_max >= (long)(SIZE_MAX / 4))
			errx(1, "sysconf _SC_ARG_MAX preposterously large");
		buf_size = 4 * arg_max + 1;
		if ((buf = malloc(buf_size)) == NULL)
			errx(1, "malloc failed");
	}

	if (*argv == NULL) {
		buf[0] = '\0';
		return (buf);
	}
	dst = buf;
	for (p = argv; (src = *p++) != NULL; ) {
		if (*src == '\0')
			continue;
		len = (buf_size - 1 - (dst - buf)) / 4;
		strvisx(dst, src, strlen(src) < len ? strlen(src) : len,
		    VIS_NL | VIS_CSTYLE);
		while (*dst != '\0')
			dst++;
		if ((buf_size - 1 - (dst - buf)) / 4 > 0)
			*dst++ = ' ';
	}
	/* Chop off trailing space */
	if (dst != buf && dst[-1] == ' ')
		dst--;
	*dst = '\0';
	return (buf);
}

static char *
cmdpart(char *arg0)
{
	char *cp;

	return ((cp = strrchr(arg0, '/')) != NULL ? cp + 1 : arg0);
}

const char *
fmt_argv(char **argv, char *cmd, char *thread, size_t maxlen)
{
	size_t len;
	char *ap, *cp;

	if (argv == NULL || argv[0] == NULL) {
		if (cmd == NULL)
			return ("");
		ap = NULL;
		len = maxlen + 3;
	} else {
		ap = shquote(argv);
		len = strlen(ap) + maxlen + 4;
	}
	cp = malloc(len);
	if (cp == NULL)
		errx(1, "malloc failed");
	if (ap == NULL) {
		if (thread != NULL) {
			asprintf(&ap, "%s/%s", cmd, thread);
			sprintf(cp, "[%.*s]", (int)maxlen, ap);
			free(ap);
		} else
			sprintf(cp, "[%.*s]", (int)maxlen, cmd);
	} else if (strncmp(cmdpart(argv[0]), cmd, maxlen) != 0)
		sprintf(cp, "%s (%.*s)", ap, (int)maxlen, cmd);
	else
		strcpy(cp, ap);
	return (cp);
}
