/*	$NetBSD: vis.c,v 1.22 2013/02/20 17:04:45 christos Exp $	*/

/*-
 * Copyright (c) 1989, 1993
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

#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1989, 1993\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)vis.c	8.1 (Berkeley) 6/6/93";
#endif
__RCSID("$NetBSD: vis.c,v 1.22 2013/02/20 17:04:45 christos Exp $");
#endif /* not lint */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <limits.h>
#include <unistd.h>
#include <err.h>
#include <vis.h>

#include "extern.h"

static int eflags, fold, foldwidth = 80, none, markeol;
#ifdef DEBUG
int debug;
#endif
static const char *extra = "";

static void process(FILE *);

int
main(int argc, char *argv[])
{
	FILE *fp;
	int ch;
	int rval;

	while ((ch = getopt(argc, argv, "bcde:F:fhlmnostw")) != -1)
		switch((char)ch) {
		case 'b':
			eflags |= VIS_NOSLASH;
			break;
		case 'c':
			eflags |= VIS_CSTYLE;
			break;
#ifdef DEBUG
		case 'd':
			debug++;
			break;
#endif
		case 'e':
			extra = optarg;
			break;
		case 'F':
			if ((foldwidth = atoi(optarg)) < 5) {
				errx(1, "can't fold lines to less than 5 cols");
				/* NOTREACHED */
			}
			markeol++;
			break;
		case 'f':
			fold++;		/* fold output lines to 80 cols */
			break;		/* using hidden newline */
		case 'h':
			eflags |= VIS_HTTPSTYLE;
			break;
		case 'l':
			markeol++;	/* mark end of line with \$ */
			break;
		case 'm':
			eflags |= VIS_MIMESTYLE;
			if (foldwidth == 80)
				foldwidth = 76;
			break;
		case 'n':
			none++;
			break;
		case 'o':
			eflags |= VIS_OCTAL;
			break;
		case 's':
			eflags |= VIS_SAFE;
			break;
		case 't':
			eflags |= VIS_TAB;
			break;
		case 'w':
			eflags |= VIS_WHITE;
			break;
		case '?':
		default:
			(void)fprintf(stderr, 
			    "Usage: %s [-bcfhlmnostw] [-e extra]" 
			    " [-F foldwidth] [file ...]\n", getprogname());
			return 1;
		}

	if ((eflags & (VIS_HTTPSTYLE|VIS_MIMESTYLE)) ==
	    (VIS_HTTPSTYLE|VIS_MIMESTYLE))
		errx(1, "Can't specify -m and -h at the same time");

	argc -= optind;
	argv += optind;

	rval = 0;

	if (*argv)
		while (*argv) {
			if ((fp = fopen(*argv, "r")) != NULL) {
				process(fp);
				(void)fclose(fp);
			} else {
				warn("%s", *argv);
				rval = 1;
			}
			argv++;
		}
	else
		process(stdin);
	return rval;
}
	
static void
process(FILE *fp)
{
	static int col = 0;
	static char nul[] = "\0";
	char *cp = nul + 1;	/* so *(cp-1) starts out != '\n' */
	wint_t c, c1, rachar;
	char mbibuff[2 * MB_LEN_MAX + 1]; /* max space for 2 wchars */
	char buff[4 * MB_LEN_MAX + 1]; /* max encoding length for one char */
	int mbilen, cerr = 0, raerr = 0;
	
        /*
         * The input stream is considered to be multibyte characters.
         * The input loop will read this data inputing one character,
	 * possibly multiple bytes, at a time and converting each to
	 * a wide character wchar_t.
         *
	 * The vis(3) functions, however, require single either bytes
	 * or a multibyte string as their arguments.  So we convert
	 * our input wchar_t and the following look-ahead wchar_t to
	 * a multibyte string for processing by vis(3).
         */

	/* Read one multibyte character, store as wchar_t */
	c = getwc(fp);
	if (c == WEOF && errno == EILSEQ) {
		/* Error in multibyte data.  Read one byte. */
		c = (wint_t)getc(fp);
		cerr = 1;
	}
	while (c != WEOF) {
		/* Clear multibyte input buffer. */
		memset(mbibuff, 0, sizeof(mbibuff));
		/* Read-ahead next multibyte character. */
		if (!cerr)
			rachar = getwc(fp);
		if (cerr || (rachar == WEOF && errno == EILSEQ)) {
			/* Error in multibyte data.  Read one byte. */
			rachar = (wint_t)getc(fp);
			raerr = 1;
		}
		if (none) {
			/* Handle -n flag. */
			cp = buff;
			*cp++ = c;
			if (c == '\\')
				*cp++ = '\\';
			*cp = '\0';
		} else if (markeol && c == '\n') {
			/* Handle -l flag. */
			cp = buff;
			if ((eflags & VIS_NOSLASH) == 0)
				*cp++ = '\\';
			*cp++ = '$';
			*cp++ = '\n';
			*cp = '\0';
		} else {
			/*
			 * Convert character using vis(3) library.
			 * At this point we will process one character.
			 * But we must pass the vis(3) library this
			 * character plus the next one because the next
			 * one is used as a look-ahead to decide how to
			 * encode this one under certain circumstances.
			 *
			 * Since our characters may be multibyte, e.g.,
			 * in the UTF-8 locale, we cannot use vis() and
			 * svis() which require byte input, so we must
			 * create a multibyte string and use strvisx().
			 */
			/* Treat EOF as a NUL char. */
			c1 = rachar;
			if (c1 == WEOF)
				c1 = L'\0';
			/*
			 * If we hit a multibyte conversion error above,
			 * insert byte directly into string buff because
			 * wctomb() will fail.  Else convert wchar_t to
			 * multibyte using wctomb().
			 */
			if (cerr) {
				*mbibuff = (char)c;
				mbilen = 1;
			} else
				mbilen = wctomb(mbibuff, c);
			/* Same for look-ahead character. */
			if (raerr)
				mbibuff[mbilen] = (char)c1;
			else
				wctomb(mbibuff + mbilen, c1);
			/* Perform encoding on just first character. */
			(void) strsenvisx(buff, 4 * MB_LEN_MAX, mbibuff,
			    1, eflags, extra, &cerr);
		}

		cp = buff;
		if (fold) {
#ifdef DEBUG
			if (debug)
				(void)printf("<%02d,", col);
#endif
			col = foldit(cp, col, foldwidth, eflags);
#ifdef DEBUG
			if (debug)
				(void)printf("%02d>", col);
#endif
		}
		do {
			(void)putchar(*cp);
		} while (*++cp);
		c = rachar;
		cerr = raerr;
	}
	/*
	 * terminate partial line with a hidden newline
	 */
	if (fold && *(cp - 1) != '\n')
		(void)printf(eflags & VIS_MIMESTYLE ? "=\n" : "\\\n");
}
