/* vi: set sw=4 ts=4: */
/*
 * echo implementation for busybox - used as a helper for testsuite/*
 * on systems lacking "echo -en"
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Original copyright notice is retained at the end of this file.
 */

/* BB_AUDIT SUSv3 compliant -- unless configured as fancy echo. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/echo.html */

/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Because of behavioral differences, implemented configurable SUSv3
 * or 'fancy' gnu-ish behaviors.  Also, reduced size and fixed bugs.
 * 1) In handling '\c' escape, the previous version only suppressed the
 *     trailing newline.  SUSv3 specifies _no_ output after '\c'.
 * 2) SUSv3 specifies that octal escapes are of the form \0{#{#{#}}}.
 *    The previous version did not allow 4-digit octals.
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>

#define WANT_HEX_ESCAPES 1

/* Usual "this only works for ascii compatible encodings" disclaimer. */
#undef _tolower
#define _tolower(X) ((X)|((char) 0x20))

static char bb_process_escape_sequence(const char **ptr)
{
	static const char charmap[] = {
		'a',  'b',  'f',  'n',  'r',  't',  'v',  '\\', 0,
		'\a', '\b', '\f', '\n', '\r', '\t', '\v', '\\', '\\' };

	const char *p;
	const char *q;
	unsigned int num_digits;
	unsigned int r;
	unsigned int n;
	unsigned int d;
	unsigned int base;

	num_digits = n = 0;
	base = 8;
	q = *ptr;

#ifdef WANT_HEX_ESCAPES
	if (*q == 'x') {
		++q;
		base = 16;
		++num_digits;
	}
#endif

	do {
		d = (unsigned char)(*q) - '0';
#ifdef WANT_HEX_ESCAPES
		if (d >= 10) {
			d = (unsigned char)(_tolower(*q)) - 'a' + 10;
		}
#endif

		if (d >= base) {
#ifdef WANT_HEX_ESCAPES
			if ((base == 16) && (!--num_digits)) {
/*				return '\\'; */
				--q;
			}
#endif
			break;
		}

		r = n * base + d;
		if (r > UCHAR_MAX) {
			break;
		}

		n = r;
		++q;
	} while (++num_digits < 3);

	if (num_digits == 0) {	/* mnemonic escape sequence? */
		p = charmap;
		do {
			if (*p == *q) {
				q++;
				break;
			}
		} while (*++p);
		n = *(p + (sizeof(charmap)/2));
	}

	*ptr = q;

	return (char) n;
}


int main(int argc, char **argv)
{
	const char *arg;
	const char *p;
	char nflag = 1;
	char eflag = 0;

	/* We must check that stdout is not closed. */
	if (dup2(1, 1) != 1)
		return -1;

	while (1) {
		arg = *++argv;
		if (!arg)
			goto newline_ret;
		if (*arg != '-')
			break;

		/* If it appears that we are handling options, then make sure
		 * that all of the options specified are actually valid.
		 * Otherwise, the string should just be echoed.
		 */
		p = arg + 1;
		if (!*p)	/* A single '-', so echo it. */
			goto just_echo;

		do {
			if (!strrchr("neE", *p))
				goto just_echo;
		} while (*++p);

		/* All of the options in this arg are valid, so handle them. */
		p = arg + 1;
		do {
			if (*p == 'n')
				nflag = 0;
			if (*p == 'e')
				eflag = '\\';
		} while (*++p);
	}
 just_echo:
	while (1) {
		/* arg is already == *argv and isn't NULL */
		int c;

		if (!eflag) {
			/* optimization for very common case */
			fputs(arg, stdout);
		} else while ((c = *arg++)) {
			if (c == eflag) {	/* Check for escape seq. */
				if (*arg == 'c') {
					/* '\c' means cancel newline and
					 * ignore all subsequent chars. */
					goto ret;
				}
				{
					/* Since SUSv3 mandates a first digit of 0, 4-digit octals
					* of the form \0### are accepted. */
					if (*arg == '0') {
						/* NB: don't turn "...\0" into "...\" */
						if (arg[1] && ((unsigned char)(arg[1]) - '0') < 8) {
							arg++;
						}
					}
					/* bb_process_escape_sequence handles NUL correctly
					 * ("...\" case. */
					c = bb_process_escape_sequence(&arg);
				}
			}
			putchar(c);
		}

		arg = *++argv;
		if (!arg)
			break;
		putchar(' ');
	}

 newline_ret:
	if (nflag) {
		putchar('\n');
	}
 ret:
	return fflush(NULL);
}

/*-
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 * 3. <BSD Advertising Clause omitted per the July 22, 1999 licensing change
 *              ftp://ftp.cs.berkeley.edu/pub/4bsd/README.Impt.License.Change>
 *
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
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
 *
 *      @(#)echo.c      8.1 (Berkeley) 5/31/93
 */
