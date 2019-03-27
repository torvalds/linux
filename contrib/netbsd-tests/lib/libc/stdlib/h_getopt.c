/*	$NetBSD: h_getopt.c,v 1.1 2011/01/01 23:56:49 pgoyette Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#ifdef __FreeBSD__
/*
 * Needed to avoid libutil.h pollution in stdio.h, which causes grief with
 * with hexdump(3) in lib/libc/db/h_hash.c
 */
#include <libutil.h>
#endif

#define	WS	"\t\n "
#define	debug	0

int
main(int argc, char *argv[])
{
	size_t len, lineno = 0;
	char *line, *ptr, *optstring = NULL, *result = NULL;
	char buf[1024];
	char *args[100];
	char arg[100];
	int nargs = -1;
	int c;

	while ((line = fparseln(stdin, &len, &lineno, NULL, 0)) != NULL) {
		if (strncmp(line, "load:", 5) == 0) {
			if (optstring)
				free(optstring);
			optstring = strtok(&line[6], WS);
			if (optstring == NULL)
			    errx(1, "missing optstring at line %ld",
				(unsigned long)lineno);
			optstring = strdup(optstring);
			if (debug)
				fprintf(stderr, "optstring = %s\n", optstring);
		} else if (strncmp(line, "args:", 5) == 0) {
			for (; nargs >= 0; nargs--) {
				if (args[nargs] != NULL)
					free(args[nargs]);
			}
			args[nargs = 0] = strtok(&line[6], WS);
			if (args[nargs] == NULL)
				errx(1, "missing args at line %ld",
				    (unsigned long)lineno);

			args[nargs] = strdup(args[nargs]);
			while ((args[++nargs] = strtok(NULL, WS)) != NULL)
				args[nargs] = strdup(args[nargs]);
			if (debug) {
				int i = 0;
				for (i = 0; i < nargs; i++)
					fprintf(stderr, "argv[%d] = %s\n", i,
					    args[i]);
			}
		} else if (strncmp(line, "result:", 7) == 0) {
			buf[0] = '\0';
			optind = optreset = 1;
			if (result)
				free(result);
			result = strtok(&line[8], WS);
			if (result == NULL)
				errx(1, "missing result at line %ld",
				    (unsigned long)lineno);
			result = strdup(result);
			if (nargs == -1)
				errx(1, "result: without args:");
			if (debug)
				fprintf(stderr, "result = %s\n", result);
			while ((c = getopt(nargs, args, optstring)) != -1)  {
				if (c == ':')
					err(1, "`:' found as argument char");
				if ((ptr = strchr(optstring, c)) == NULL) {
					snprintf(arg, sizeof(arg), "!%c,", c);
					strcat(buf, arg);
					continue;
				}
				if (ptr[1] != ':')
					snprintf(arg, sizeof(arg), "%c,", c);
				else
					snprintf(arg, sizeof(arg), "%c=%s,",
					    c, optarg);
				strcat(buf, arg);
			}
			len = strlen(buf);
			if (len > 0) {
				buf[len - 1] = '|';
				buf[len] = '\0';
			} else {
				buf[0] = '|';
				buf[1] = '\0';
			}
			snprintf(arg, sizeof(arg), "%d", nargs - optind);
			strcat(buf, arg);
			if (strcmp(buf, result) != 0)
				errx(1, "`%s' != `%s'", buf, result);
		}
		free(line);
	}
	return 0;
}
