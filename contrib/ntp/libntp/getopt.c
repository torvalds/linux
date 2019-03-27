/*
 * getopt - get option letter from argv
 *
 * This is a version of the public domain getopt() implementation by
 * Henry Spencer, changed for 4.3BSD compatibility (in addition to System V).
 * It allows rescanning of an option list by setting optind to 0 before
 * calling, which is why we use it even if the system has its own (in fact,
 * this one has a unique name so as not to conflict with the system's).
 * Thanks to Dennis Ferguson for the appropriate modifications.
 *
 * This file is in the Public Domain.
 */

/*LINTLIBRARY*/

#include <config.h>
#include <stdio.h>

#include "ntp_stdlib.h"

#ifdef	lint
#undef	putc
#define	putc	fputc
#endif	/* lint */

char	*ntp_optarg;	/* Global argument pointer. */
int	ntp_optind = 0;	/* Global argv index. */
int	ntp_opterr = 1;	/* for compatibility, should error be printed? */
int	ntp_optopt;	/* for compatibility, option character checked */

static char	*scan = NULL;	/* Private scan pointer. */
static const char	*prog = "amnesia";

/*
 * Print message about a bad option.
 */
static int
badopt(
	const char *mess,
	int ch
	)
{
	if (ntp_opterr) {
		fputs(prog, stderr);
		fputs(mess, stderr);
		(void) putc(ch, stderr);
		(void) putc('\n', stderr);
	}
	return ('?');
}

int
ntp_getopt(
	int argc,
	char *argv[],
	const char *optstring
	)
{
	register char c;
	register const char *place;

	prog = argv[0];
	ntp_optarg = NULL;

	if (ntp_optind == 0) {
		scan = NULL;
		ntp_optind++;
	}
	
	if (scan == NULL || *scan == '\0') {
		if (ntp_optind >= argc
		    || argv[ntp_optind][0] != '-'
		    || argv[ntp_optind][1] == '\0') {
			return (EOF);
		}
		if (argv[ntp_optind][1] == '-'
		    && argv[ntp_optind][2] == '\0') {
			ntp_optind++;
			return (EOF);
		}
	
		scan = argv[ntp_optind++]+1;
	}

	c = *scan++;
	ntp_optopt = c & 0377;
	for (place = optstring; place != NULL && *place != '\0'; ++place)
	    if (*place == c)
		break;

	if (place == NULL || *place == '\0' || c == ':' || c == '?') {
		return (badopt(": unknown option -", c));
	}

	place++;
	if (*place == ':') {
		if (*scan != '\0') {
			ntp_optarg = scan;
			scan = NULL;
		} else if (ntp_optind >= argc) {
			return (badopt(": option requires argument -", c));
		} else {
			ntp_optarg = argv[ntp_optind++];
		}
	}

	return (c & 0377);
}
