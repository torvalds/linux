/*	$OpenBSD: pwd.c,v 1.14 2015/10/09 01:37:06 deraadt Exp $	*/
/*	$NetBSD: pwd.c,v 1.22 2011/08/29 14:51:19 joerg Exp $	*/

/*
 * Copyright (c) 1991, 1993, 1994
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

#include <sys/stat.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char *__progname;
static char *getcwd_logical(void);
__dead static void usage(void);

int
main(int argc, char *argv[])
{
	int ch, lFlag = 0;
	const char *p;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "LP")) != -1) {
		switch (ch) {
		case 'L':
			lFlag = 1;
			break;
		case 'P':
			lFlag = 0;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (lFlag)
		p = getcwd_logical();
	else
		p = NULL;
	if (p == NULL)
		p = getcwd(NULL, 0);

	if (p == NULL)
		err(EXIT_FAILURE, NULL);

	puts(p);

	exit(EXIT_SUCCESS);
}

static char *
getcwd_logical(void)
{
	char *pwd, *p;
	struct stat s_pwd, s_dot;

	/* Check $PWD -- if it's right, it's fast. */
	pwd = getenv("PWD");
	if (pwd == NULL)
		return NULL;
	if (pwd[0] != '/')
		return NULL;

	/* check for . or .. components, including trailing ones */
	for (p = pwd; *p != '\0'; p++)
		if (p[0] == '/' && p[1] == '.') {
			if (p[2] == '.')
				p++;
			if (p[2] == '\0' || p[2] == '/')
				return NULL;
		}

	if (stat(pwd, &s_pwd) == -1 || stat(".", &s_dot) == -1)
		return NULL;
	if (s_pwd.st_dev != s_dot.st_dev || s_pwd.st_ino != s_dot.st_ino)
		return NULL;
	return pwd;
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-LP]\n", __progname);
	exit(EXIT_FAILURE);
}
