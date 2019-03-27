/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: cfopt.c,v 1.3 2000/07/11 01:51:49 bp Exp $
 */
#include <sys/param.h>

#include <stdio.h>
#include <string.h>

#include <cflib.h>

extern char *__progname;

int	cf_opterr = 1,		/* if error message should be printed */
	cf_optind = 1,		/* index into parent argv vector */
	cf_optopt,			/* character checked for validity */
	cf_optreset;		/* reset getopt */
const char* cf_optarg;		/* argument associated with option */

#define	BADCH	(int)'?'
#define	BADARG	(int)':'
#define	EMSG	""

int
cf_getopt(nargc, nargv, ostr)
	int nargc;
	char * const *nargv;
	const char *ostr;
{
	static const char *place = EMSG;		/* option letter processing */
	char *oli;				/* option letter list index */
	int tmpind;

	if (cf_optreset || !*place) {		/* update scanning pointer */
		cf_optreset = 0;
		tmpind = cf_optind;
		while (1) {
			if (tmpind >= nargc) {
				place = EMSG;
				return (-1);
			}
			if (*(place = nargv[tmpind]) != '-') {
				tmpind++;
				continue;	/* lookup next option */
			}
			if (place[1] && *++place == '-') {	/* found "--" */
				cf_optind = ++tmpind;
				place = EMSG;
				return (-1);
			}
			cf_optind = tmpind;
			break;
		}
	}					/* option letter okay? */
	if ((cf_optopt = (int)*place++) == (int)':' ||
	    !(oli = strchr(ostr, cf_optopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means -1.
		 */
		if (cf_optopt == (int)'-')
			return (-1);
		if (!*place)
			++cf_optind;
		if (cf_opterr && *ostr != ':')
			(void)fprintf(stderr,
			    "%s: illegal option -- %c\n", __progname, cf_optopt);
		return (BADCH);
	}
	if (*++oli != ':') {			/* don't need argument */
		cf_optarg = NULL;
		if (!*place)
			++cf_optind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			cf_optarg = place;
		else if (nargc <= ++cf_optind) {	/* no arg */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (cf_opterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    __progname, cf_optopt);
			return (BADCH);
		}
	 	else				/* white space */
			cf_optarg = nargv[cf_optind];
		place = EMSG;
		++cf_optind;
	}
	return (cf_optopt);			/* dump back option letter */
}
