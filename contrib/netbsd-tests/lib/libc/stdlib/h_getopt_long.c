/*	$NetBSD: h_getopt_long.c,v 1.1 2011/01/01 23:56:49 pgoyette Exp $	*/

/*-
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
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

#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __FreeBSD__
/*
 * Needed to avoid libutil.h pollution in stdio.h, which causes grief with
 * with hexdump(3) in lib/libc/db/h_hash.c
 */
#include <libutil.h>
#endif

#define SKIPWS(p)	while (isspace((int)(*p))) p++
#define	WS	"\t\n "

int
main(int argc, char *argv[])
{
	size_t len, lineno = 0;
	char *line, *eptr, *longopt, *ptr, *optstring = NULL, *result = NULL;
	char buf[1024];
	char *args[128];
	char arg[256];
	int nargs = -1;
	int c;
	int nlongopts = 0;
	int maxnlongopts = 0;
	int *longopt_flags = NULL;
	struct option *longopts = NULL;

	while ((line = fparseln(stdin, &len, &lineno, NULL, 0)) != NULL) {
		if (strncmp(line, "optstring:", 10) == 0) {
			if (optstring)
				free(optstring);
			optstring = strtok(&line[11], WS);
			if (optstring == NULL)
			    errx(1, "missing optstring at line %ld",
				(unsigned long)lineno);
			optstring = strdup(optstring);
		} else if (strncmp(line, "longopts:", 9) == 0) {
			if (longopts) {
				int i;
				for (i = 0; i < nlongopts; i++)
					if (longopts[i].name != NULL)
						free(__UNCONST(longopts[i].name));
				free(longopts);
			}
			if (longopt_flags)
				free(longopt_flags);
			nlongopts = 0;
			ptr = strtok(&line[10], WS);
			if (ptr == NULL)
				errx(1, "missing longopts at line %ld",
				    (unsigned long)lineno);
			maxnlongopts = strtoul(ptr, &eptr, 10);
			if (*eptr != '\0')
				warnx("garbage in longopts at line %ld",
				    (unsigned long)lineno);
			maxnlongopts++;		/* space for trailer */
			longopts =
			    (struct option *)calloc(sizeof(struct option),
						    maxnlongopts);
			if (longopts == NULL)
				err(1, "calloc");
			longopt_flags = (int *)calloc(sizeof(int), maxnlongopts);
			if (longopt_flags == NULL)
				err(1, "calloc");
		} else if (strncmp(line, "longopt:", 8) == 0) {
			if (longopts == NULL)
				errx(1, "longopt: without longopts at line %ld",
				    (unsigned long)lineno);
			if (nlongopts >= maxnlongopts)
				errx(1, "longopt: too many options at line %ld",
				    (unsigned long)lineno);
			/* name */
			ptr = &line[9];
			SKIPWS(ptr);
			longopt = strsep(&ptr, ",");
			if (longopt == NULL)
				errx(1, "missing longopt at line %ld",
				    (unsigned long)lineno);
			longopts[nlongopts].name = strdup(longopt);
			/* has_arg */
			SKIPWS(ptr);
			longopt = strsep(&ptr, ",");
			if (*longopt != '\0') {
				if (strncmp(longopt, "0", 1) == 0 ||
				    strncmp(longopt, "no_argument", 2) == 0)
					longopts[nlongopts].has_arg = no_argument;
				else if (strncmp(longopt, "1", 1) == 0 ||
					 strncmp(longopt, "required_argument", 8) == 0)
					longopts[nlongopts].has_arg = required_argument;
				else if (strncmp(longopt, "2", 1) == 0 ||
					 strncmp(longopt, "optional_argument", 8) == 0)
					longopts[nlongopts].has_arg = optional_argument;
				else
					errx(1, "unknown has_arg %s at line %ld",
					    longopt, (unsigned long)lineno);
			}
			/* flag */
			SKIPWS(ptr);
			longopt = strsep(&ptr, ",");
			if (*longopt != '\0' &&
			    strncmp(longopt, "NULL", 4) != 0)
				longopts[nlongopts].flag = &longopt_flags[nlongopts];
			/* val */
			SKIPWS(ptr);
			longopt = strsep(&ptr, ",");
			if (*longopt == '\0')
				errx(1, "missing val at line %ld",
				    (unsigned long)lineno);
			if (*longopt != '\'') {
				longopts[nlongopts].val =
			            (int)strtoul(longopt, &eptr, 10);
				if (*eptr != '\0')
					errx(1, "invalid val at line %ld",
					    (unsigned long)lineno);
			} else
				longopts[nlongopts].val = (int)longopt[1];
			nlongopts++;
		} else if (strncmp(line, "args:", 5) == 0) {
			for (; nargs >= 0; nargs--) {
				if (args[nargs] != NULL)
					free(args[nargs]);
			}
			args[nargs = 0] = strtok(&line[6], WS);
			if (args[nargs] == NULL)
				errx(1, "Missing args");

			args[nargs] = strdup(args[nargs]);
			while ((args[++nargs] = strtok(NULL, WS)) != NULL)
				args[nargs] = strdup(args[nargs]);
		} else if (strncmp(line, "result:", 7) == 0) {
			int li;
			buf[0] = '\0';
			optind = optreset = 1;
			if (result)
				free(result);
			result = strtok(&line[8], WS);
			if (result == NULL)
				errx(1, "missing result at line %ld",
				    (unsigned long)lineno);
			if (optstring == NULL)
				errx(1, "result: without optstring");
			if (longopts == NULL || nlongopts == 0)
				errx(1, "result: without longopts");
			result = strdup(result);
			if (nargs == -1)
				errx(1, "result: without args");
			li = -2;
			while ((c = getopt_long(nargs, args, optstring,
				       	longopts, &li)) != -1)  {
				if (c == ':')
					errx(1, "`:' found as argument char");
				if (li == -2) {
					ptr = strchr(optstring, c);
					if (ptr == NULL) {
						snprintf(arg, sizeof(arg),
						    "!%c,", c);
						strcat(buf, arg);
						continue;
					}
					if (ptr[1] != ':')
						snprintf(arg, sizeof(arg),
						    "%c,", c);
					else
						snprintf(arg, sizeof(arg),
						    "%c=%s,", c, optarg);
				} else {
					switch (longopts[li].has_arg) {
					case no_argument:
						snprintf(arg, sizeof(arg), "-%s,",
						    longopts[li].name);
						break;
					case required_argument:
						snprintf(arg, sizeof(arg),
						    "-%s=%s,",
						    longopts[li].name, optarg);
						break;
					case optional_argument:
						snprintf(arg, sizeof(arg),
						    "-%s%s%s,",
						    longopts[li].name,
						    (optarg)? "=" : "",
						    (optarg)? optarg : "");
						break;
					default:
						errx(1, "internal error");
					}
				}
				strcat(buf, arg);
				li = -2;
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
		} else
			errx(1, "unknown directive at line %ld",
			    (unsigned long)lineno);
		free(line);
	}
	return 0;
}
