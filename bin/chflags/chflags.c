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
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static char sccsid[] = "@(#)chflags.c	8.5 (Berkeley) 4/1/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t siginfo;

static void usage(void);

static void
siginfo_handler(int sig __unused)
{

	siginfo = 1;
}

int
main(int argc, char *argv[])
{
	FTS *ftsp;
	FTSENT *p;
	u_long clear, newflags, set;
	long val;
	int Hflag, Lflag, Rflag, fflag, hflag, vflag, xflag;
	int ch, fts_options, oct, rval;
	char *flags, *ep;

	Hflag = Lflag = Rflag = fflag = hflag = vflag = xflag = 0;
	while ((ch = getopt(argc, argv, "HLPRfhvx")) != -1)
		switch (ch) {
		case 'H':
			Hflag = 1;
			Lflag = 0;
			break;
		case 'L':
			Lflag = 1;
			Hflag = 0;
			break;
		case 'P':
			Hflag = Lflag = 0;
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'h':
			hflag = 1;
			break;
		case 'v':
			vflag++;
			break;
		case 'x':
			xflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

	if (argc < 2)
		usage();

	(void)signal(SIGINFO, siginfo_handler);

	if (Rflag) {
		if (hflag)
			errx(1, "the -R and -h options may not be "
			    "specified together.");
		if (Lflag) {
			fts_options = FTS_LOGICAL;
		} else {
			fts_options = FTS_PHYSICAL;

			if (Hflag) {
				fts_options |= FTS_COMFOLLOW;
			}
		}
	} else if (hflag) {
		fts_options = FTS_PHYSICAL;
	} else {
		fts_options = FTS_LOGICAL;
	}
	if (xflag)
		fts_options |= FTS_XDEV;

	flags = *argv;
	if (*flags >= '0' && *flags <= '7') {
		errno = 0;
		val = strtol(flags, &ep, 8);
		if (val < 0)
			errno = ERANGE;
		if (errno)
                        err(1, "invalid flags: %s", flags);
                if (*ep)
                        errx(1, "invalid flags: %s", flags);
		set = val;
                oct = 1;
	} else {
		if (strtofflags(&flags, &set, &clear))
                        errx(1, "invalid flag: %s", flags);
		clear = ~clear;
		oct = 0;
	}

	if ((ftsp = fts_open(++argv, fts_options , 0)) == NULL)
		err(1, NULL);

	for (rval = 0; (p = fts_read(ftsp)) != NULL;) {
		int atflag;

		if ((fts_options & FTS_LOGICAL) ||
		    ((fts_options & FTS_COMFOLLOW) &&
		    p->fts_level == FTS_ROOTLEVEL))
			atflag = 0;
		else
			atflag = AT_SYMLINK_NOFOLLOW;

		switch (p->fts_info) {
		case FTS_D:	/* Change it at FTS_DP if we're recursive. */
			if (!Rflag)
				fts_set(ftsp, p, FTS_SKIP);
			continue;
		case FTS_DNR:			/* Warn, chflags. */
			warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
			rval = 1;
			break;
		case FTS_ERR:			/* Warn, continue. */
		case FTS_NS:
			warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
			rval = 1;
			continue;
		default:
			break;
		}
		if (oct)
			newflags = set;
		else
			newflags = (p->fts_statp->st_flags | set) & clear;
		if (newflags == p->fts_statp->st_flags)
			continue;
		if (chflagsat(AT_FDCWD, p->fts_accpath, newflags,
		    atflag) == -1 && !fflag) {
			warn("%s", p->fts_path);
			rval = 1;
		} else if (vflag || siginfo) {
			(void)printf("%s", p->fts_path);
			if (vflag > 1 || siginfo)
				(void)printf(": 0%lo -> 0%lo",
				    (u_long)p->fts_statp->st_flags,
				    newflags);
			(void)printf("\n");
			siginfo = 0;
		}
	}
	if (errno)
		err(1, "fts_read");
	exit(rval);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: chflags [-fhvx] [-R [-H | -L | -P]] flags file ...\n");
	exit(1);
}
