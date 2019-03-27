/*-
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

#ifndef lint
static char sccsid[] = "@(#)dump.c	8.1 (Berkeley) 8/31/94";
#endif /* not lint */

#include <ctype.h>
#include <stdio.h>

static void
parse(FILE *fp)
{
	int ch, s1, s2, s3;

#define	TESTD(s) {							\
	if ((s = getc(fp)) == EOF)					\
		return;							\
	if (!isdigit(s))						\
		continue;						\
}
#define	TESTP {								\
	if ((ch = getc(fp)) == EOF)					\
		return;							\
	if (ch != '|')							\
		continue;						\
}
#define	MOVEC(t) {							\
	do {								\
		if ((ch = getc(fp)) == EOF)				\
			return;						\
	} while (ch != (t));						\
}
	for (;;) {
		MOVEC('"');
		TESTD(s1);
		TESTD(s2);
		TESTD(s3);
		TESTP;
		putchar('"');
		putchar(s1);
		putchar(s2);
		putchar(s3);
		putchar('|');
		for (;;) {		/* dump to end quote. */
			if ((ch = getc(fp)) == EOF)
				return;
			putchar(ch);
			if (ch == '"')
				break;
			if (ch == '\\') {
				if ((ch = getc(fp)) == EOF)
					return;
				putchar(ch);
			}
		}
		putchar('\n');
	}
}

int
main(int argc, char *argv[])
{
	FILE *fp;

	for (; *argv != NULL; ++argv) {
		if ((fp = fopen(*argv, "r")) == NULL) {
			perror(*argv);
			return (1);
		}
		parse(fp);
		(void)fclose(fp);
	}
	return (0);
}
