/*	$NetBSD: getopt.c,v 1.29 2014/06/05 22:00:22 christos Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#if !defined(HAVE_GETOPT) || defined(WANT_GETOPT_LONG) || defined(BROKEN_GETOPT)
#include <sys/cdefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define	BADCH	(int)'?'
#define	BADARG	(int)':'
#define	EMSG	""

int	opterr = 1,		/* if error message should be printed */
	optind = 1,		/* index into parent argv vector */
	optopt = BADCH,		/* character checked for validity */
	optreset;		/* reset getopt */
char	*optarg;		/* argument associated with option */

/*
 * getopt --
 *	Parse argc/argv argument vector.
 */
int
getopt(int nargc, char * const nargv[], const char *ostr)
{
	extern char *__progname;
	static const char *place = EMSG; 	/* option letter processing */
	char *oli;				/* option letter list index */

#ifndef BSD4_4
	if (!__progname) {
		if (__progname = strrchr(nargv[0], '/'))
			++__progname;
		else
			__progname = nargv[0];
	}
#endif
	
	if (optreset || *place == 0) {		/* update scanning pointer */
		optreset = 0;
		place = nargv[optind];
		if (optind >= nargc || *place++ != '-') {
			/* Argument is absent or is not an option */
			place = EMSG;
			return (-1);
		}
		optopt = *place++;
		if (optopt == '-' && *place == 0) {
			/* "--" => end of options */
			++optind;
			place = EMSG;
			return (-1);
		}
		if (optopt == 0) {
			/* Solitary '-', treat as a '-' option
			   if the program (eg su) is looking for it. */
			place = EMSG;
			if (strchr(ostr, '-') == NULL)
				return -1;
			optopt = '-';
		}
	} else
		optopt = *place++;

	/* See if option letter is one the caller wanted... */
	if (optopt == ':' || (oli = strchr(ostr, optopt)) == NULL) {
		if (*place == 0)
			++optind;
		if (opterr && *ostr != ':')
			(void)fprintf(stderr,
			    "%s: unknown option -- %c\n", __progname, optopt);
		return (BADCH);
	}

	/* Does this option need an argument? */
	if (oli[1] != ':') {
		/* don't need argument */
		optarg = NULL;
		if (*place == 0)
			++optind;
	} else {
		/* Option-argument is either the rest of this argument or the
		   entire next argument. */
		if (*place)
			optarg = __UNCONST(place);
		else if (oli[2] == ':')
			/*
			 * GNU Extension, for optional arguments if the rest of
			 * the argument is empty, we return NULL
			 */
			optarg = NULL;
		else if (nargc > ++optind)
			optarg = nargv[optind];
		else {
			/* option-argument absent */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (opterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    __progname, optopt);
			return (BADCH);
		}
		place = EMSG;
		++optind;
	}
	return (optopt);			/* return option letter */
}
#endif
#ifdef MAIN
#ifndef BSD4_4
char *__progname;
#endif

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int c;
	char *opts = argv[1];

	--argc;
	++argv;
	
	while ((c = getopt(argc, argv, opts)) != EOF) {
		switch (c) {
		case '-':
			if (optarg)
				printf("--%s ", optarg);
			break;
		case '?':
			exit(1);
			break;
		default:
			if (optarg)
				printf("-%c %s ", c, optarg);
			else
				printf("-%c ", c);
			break;
		}
	}

	if (optind < argc) {
		printf("-- ");
		for (; optind < argc; ++optind) {
			printf("%s ", argv[optind]);
		}
	}
	printf("\n");
	exit(0);
}
#endif
