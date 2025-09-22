/*	$OpenBSD: date.c,v 1.60 2024/04/28 16:43:15 florian Exp $	*/
/*	$NetBSD: date.c,v 1.11 1995/09/07 06:21:05 jtc Exp $	*/

/*
 * Copyright (c) 1985, 1987, 1988, 1993
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

#include <sys/types.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

extern	char *__progname;

time_t tval;
int jflag;
int slidetime;

static void setthetime(char *, const char *);
static void badformat(void);
static void __dead usage(void);

int
main(int argc, char *argv[])
{
	const char *errstr;
	struct tm *tp;
	int ch, rflag;
	char *format, buf[1024], *outzone = NULL;
	const char *pformat = NULL;

	rflag = 0;
	while ((ch = getopt(argc, argv, "af:jr:uz:")) != -1)
		switch(ch) {
		case 'a':
			slidetime = 1;
			break;
		case 'f':		/* parse with strptime */
			pformat = optarg;
			break;
		case 'j':		/* don't set */
			jflag = 1;
			break;
		case 'r':		/* user specified seconds */
			rflag = 1;
			tval = strtonum(optarg, LLONG_MIN, LLONG_MAX, &errstr);
			if (errstr)
				errx(1, "seconds is %s: %s", errstr, optarg);
			break;
		case 'u':		/* do everything in UTC */
			if (setenv("TZ", "UTC", 1) == -1)
				err(1, "cannot unsetenv TZ");
			break;
		case 'z':
			outzone = optarg;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!rflag && time(&tval) == -1)
		err(1, "time");

	format = "%a %b %e %H:%M:%S %Z %Y";

	/* allow the operands in any order */
	if (*argv && **argv == '+') {
		format = *argv + 1;
		argv++;
		argc--;
	}

	if (*argv) {
		setthetime(*argv, pformat);
		argv++;
		argc--;
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (*argv && **argv == '+') {
		format = *argv + 1;
		argc--;
	}

	if (argc > 0)
		errx(1, "too many arguments");

	if (outzone)
		setenv("TZ", outzone, 1);

	tp = localtime(&tval);
	if (tp == NULL)
		errx(1, "conversion error");
	(void)strftime(buf, sizeof(buf), format, tp);
	(void)printf("%s\n", buf);
	return 0;
}

#define	ATOI2(ar)	((ar) += 2, ((ar)[-2] - '0') * 10 + ((ar)[-1] - '0'))
void
setthetime(char *p, const char *pformat)
{
	struct tm *lt, tm;
	struct timeval tv;
	char *dot, *t;
	time_t now;
	int yearset = 0;

	/* Let us set the time even if logwtmp would fail. */
	unveil("/var/log/wtmp", "w");
	if (pledge("stdio settime wpath", NULL) == -1)
		err(1, "pledge");

	lt = localtime(&tval);
	if (lt == NULL)
		errx(1, "conversion error");

	lt->tm_isdst = -1;			/* correct for DST */

	if (pformat) {
		tm = *lt;
		if (strptime(p, pformat, &tm) == NULL) {
			fprintf(stderr, "trouble %s %s\n", p, pformat);
			badformat();
		}
		lt = &tm;
	} else {
		for (t = p, dot = NULL; *t; ++t) {
			if (isdigit((unsigned char)*t))
				continue;
			if (*t == '.' && dot == NULL) {
				dot = t;
				continue;
			}
			badformat();
		}

		if (dot != NULL) {			/* .SS */
			*dot++ = '\0';
			if (strlen(dot) != 2)
				badformat();
			lt->tm_sec = ATOI2(dot);
			if (lt->tm_sec > 61)
				badformat();
		} else
			lt->tm_sec = 0;

		switch (strlen(p)) {
		case 12:				/* cc */
			lt->tm_year = (ATOI2(p) * 100) - 1900;
			yearset = 1;
			/* FALLTHROUGH */
		case 10:				/* yy */
			if (!yearset) {
				/* mask out current year, leaving only century */
				lt->tm_year = ((lt->tm_year / 100) * 100);
			}
			lt->tm_year += ATOI2(p);
			/* FALLTHROUGH */
		case 8:					/* mm */
			lt->tm_mon = ATOI2(p);
			if ((lt->tm_mon > 12) || !lt->tm_mon)
				badformat();
			--lt->tm_mon;			/* time struct is 0 - 11 */
			/* FALLTHROUGH */
		case 6:					/* dd */
			lt->tm_mday = ATOI2(p);
			if ((lt->tm_mday > 31) || !lt->tm_mday)
				badformat();
			/* FALLTHROUGH */
		case 4:					/* HH */
			lt->tm_hour = ATOI2(p);
			if (lt->tm_hour > 23)
				badformat();
			/* FALLTHROUGH */
		case 2:					/* MM */
			lt->tm_min = ATOI2(p);
			if (lt->tm_min > 59)
				badformat();
			break;
		default:
			badformat();
		}
	}

	/* convert broken-down time to UTC clock time */
	if (pformat != NULL && strstr(pformat, "%s") != NULL)
		tval = timegm(lt);
	else
		tval = mktime(lt);
	if (tval == -1)
		errx(1, "specified date is outside allowed range");

	if (jflag)
		return;

	/* set the time */
	if (slidetime) {
		if ((now = time(NULL)) == -1)
			err(1, "time");
		tv.tv_sec = tval - now;
		tv.tv_usec = 0;
		if (adjtime(&tv, NULL) == -1)
			err(1, "adjtime");
	} else {
#ifndef SMALL
		logwtmp("|", "date", "");
#endif
		tv.tv_sec = tval;
		tv.tv_usec = 0;
		if (settimeofday(&tv, NULL))
			err(1, "settimeofday");
#ifndef SMALL
		logwtmp("{", "date", "");
#endif
	}

	if ((p = getlogin()) == NULL)
		p = "???";
	syslog(LOG_AUTH | LOG_NOTICE, "date set by %s", p);
}

static void
badformat(void)
{
	warnx("illegal time format");
	usage();
}

static void __dead
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-aju] [-f pformat] [-r seconds]\n"
	    "\t[-z output_zone] [+format] [[[[[[cc]yy]mm]dd]HH]MM[.SS]]\n",
	    __progname);
	exit(1);
}
