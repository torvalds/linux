/*	$OpenBSD: io.c,v 1.26 2024/07/16 05:01:10 deraadt Exp $	*/
/*	$NetBSD: io.c,v 1.2 1995/03/21 09:04:43 cgd Exp $	*/

/* io.c: This file contains the i/o routines for the ed line editor */
/*-
 * Copyright (c) 1993 Andrew Moore, Talke Studio.
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
 */

#include <regex.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"

static int read_stream(FILE *, int);
static int get_stream_line(FILE *);
static int write_stream(FILE *, int, int);
static int put_stream_line(FILE *, char *, int);

extern int scripted;

/* read_file: read a named file/pipe into the buffer; return line count */
int
read_file(char *fn, int n)
{
	FILE *fp;
	int size;


	fp = (*fn == '!') ? popen(fn + 1, "r") : fopen(strip_escapes(fn), "r");
	if (fp == NULL) {
		perror(fn);
		seterrmsg("cannot open input file");
		return ERR;
	} else if ((size = read_stream(fp, n)) < 0)
		return ERR;
	 else if ((*fn == '!') ?  pclose(fp) == -1 : fclose(fp) == EOF) {
		perror(fn);
		seterrmsg("cannot close input file");
		return ERR;
	}
	if (!scripted)
		printf("%d\n", size);
	return current_addr - n;
}


static char *sbuf;		/* file i/o buffer */
static int sbufsz;		/* file i/o buffer size */
int newline_added;		/* if set, newline appended to input file */

/* read_stream: read a stream into the editor buffer; return status */
static int
read_stream(FILE *fp, int n)
{
	line_t *lp = get_addressed_line_node(n);
	undo_t *up = NULL;
	unsigned int size = 0;
	int o_newline_added = newline_added;
	int o_isbinary = isbinary;
	int appended = (n == addr_last);
	int len;

	isbinary = newline_added = 0;
	for (current_addr = n; (len = get_stream_line(fp)) > 0; size += len) {
		SPL1();
		if (put_sbuf_line(sbuf) == NULL) {
			SPL0();
			return ERR;
		}
		lp = lp->q_forw;
		if (up)
			up->t = lp;
		else if ((up = push_undo_stack(UADD, current_addr,
		    current_addr)) == NULL) {
			SPL0();
			return ERR;
		}
		SPL0();
	}
	if (len < 0)
		return ERR;
	if (appended && size && o_isbinary && o_newline_added)
		fputs("newline inserted\n", stderr);
	else if (newline_added && (!appended || (!isbinary && !o_isbinary)))
		fputs("newline appended\n", stderr);
	if (isbinary && newline_added && !appended)
	    	size += 1;
	if (!size)
		newline_added = 1;
	newline_added = appended ? newline_added : o_newline_added;
	isbinary = isbinary | o_isbinary;
	return size;
}

/* get_stream_line: read a line of text from a stream; return line length */
static int
get_stream_line(FILE *fp)
{
	int c;
	int i = 0;

	while (((c = getc(fp)) != EOF || (!feof(fp) &&
	    !ferror(fp))) && c != '\n') {
		REALLOC(sbuf, sbufsz, i + 1, ERR);
		if (!(sbuf[i++] = c))
			isbinary = 1;
	}
	REALLOC(sbuf, sbufsz, i + 2, ERR);
	if (c == '\n')
		sbuf[i++] = c;
	else if (ferror(fp)) {
		perror(NULL);
		seterrmsg("cannot read input file");
		return ERR;
	} else if (i) {
		sbuf[i++] = '\n';
		newline_added = 1;
	}
	sbuf[i] = '\0';
	return (isbinary && newline_added && i) ? --i : i;
}


/* write_file: write a range of lines to a named file/pipe; return line count */
int
write_file(char *fn, char *mode, int n, int m)
{
	FILE *fp;
	int size;

	fp = (*fn == '!') ? popen(fn+1, "w") : fopen(strip_escapes(fn), mode);
	if (fp == NULL) {
		perror(fn);
		seterrmsg("cannot open output file");
		return ERR;
	} else if ((size = write_stream(fp, n, m)) < 0)
		return ERR;
	 else if ((*fn == '!') ?  pclose(fp) == -1 : fclose(fp) == EOF) {
		perror(fn);
		seterrmsg("cannot close output file");
		return ERR;
	}
	if (!scripted)
		printf("%d\n", size);
	return n ? m - n + 1 : 0;
}


/* write_stream: write a range of lines to a stream; return status */
static int
write_stream(FILE *fp, int n, int m)
{
	line_t *lp = get_addressed_line_node(n);
	unsigned int size = 0;
	char *s;
	int len;

	for (; n && n <= m; n++, lp = lp->q_forw) {
		if ((s = get_sbuf_line(lp)) == NULL)
			return ERR;
		len = lp->len;
		if (n != addr_last || !isbinary || !newline_added)
			s[len++] = '\n';
		if (put_stream_line(fp, s, len) < 0)
			return ERR;
		size += len;
	}
	return size;
}


/* put_stream_line: write a line of text to a stream; return status */
static int
put_stream_line(FILE *fp, char *s, int len)
{
	while (len--) {
		if (fputc(*s, fp) == EOF) {
			perror(NULL);
			seterrmsg("cannot write file");
			return ERR;
		}
		s++;
	}
	return 0;
}

/* get_extended_line: get a an extended line from stdin */
char *
get_extended_line(int *sizep, int nonl)
{
	static char *cvbuf = NULL;		/* buffer */
	static int cvbufsz = 0;			/* buffer size */

	int l, n;
	char *t = ibufp;

	while (*t++ != '\n')
		;
	if ((l = t - ibufp) < 2 || !has_trailing_escape(ibufp, ibufp + l - 1)) {
		*sizep = l;
		return ibufp;
	}
	*sizep = -1;
	REALLOC(cvbuf, cvbufsz, l, NULL);
	memcpy(cvbuf, ibufp, l);
	*(cvbuf + --l - 1) = '\n'; 	/* strip trailing esc */
	if (nonl)
		l--; 			/* strip newline */
	for (;;) {
		if ((n = get_tty_line()) < 0)
			return NULL;
		else if (n == 0 || ibuf[n - 1] != '\n') {
			seterrmsg("unexpected end-of-file");
			return NULL;
		}
		REALLOC(cvbuf, cvbufsz, l + n, NULL);
		memcpy(cvbuf + l, ibuf, n);
		l += n;
		if (n < 2 || !has_trailing_escape(cvbuf, cvbuf + l - 1))
			break;
		*(cvbuf + --l - 1) = '\n'; 	/* strip trailing esc */
		if (nonl) l--; 			/* strip newline */
	}
	REALLOC(cvbuf, cvbufsz, l + 1, NULL);
	cvbuf[l] = '\0';
	*sizep = l;
	return cvbuf;
}


/* get_tty_line: read a line of text from stdin; return line length */
int
get_tty_line(void)
{
	int oi = 0;
	int i = 0;
	int c;

	for (;;) {
		if (sighup)
			handle_hup();
		switch (c = getchar()) {
		default:
			oi = 0;
			REALLOC(ibuf, ibufsz, i + 2, ERR);
			if (!(ibuf[i++] = c)) isbinary = 1;
			if (c != '\n')
				continue;
			lineno++;
			ibuf[i] = '\0';
			ibufp = ibuf;
			return i;
		case EOF:
			if (sighup)
				handle_hup();
			if (ferror(stdin)) {
				perror("stdin");
				seterrmsg("cannot read stdin");
				clearerr(stdin);
				ibufp = NULL;
				return ERR;
			} else {
				clearerr(stdin);
				if (i != oi) {
					oi = i;
					continue;
				} else if (i)
					ibuf[i] = '\0';
				ibufp = ibuf;
				return i;
			}
		}
	}
}



#define ESCAPES "\a\b\f\n\r\t\v\\$"
#define ESCCHARS "abfnrtv\\$"

extern int rows;
extern int cols;

/* put_tty_line: print text to stdout */
int
put_tty_line(char *s, int l, int n, int gflag)
{
	int col = 0;
	char *cp;

	if (gflag & GNP) {
		printf("%d\t", n);
		col = 8;
	}
	for (; l--; s++) {
		if ((gflag & GLS) && ++col > cols) {
			fputs("\\\n", stdout);
			col = 1;
		}
		if (gflag & GLS) {
			if (31 < *s && *s < 127 && *s != '\\' && *s != '$')
				putchar(*s);
			else {
				putchar('\\');
				col++;
				if (*s && (cp = strchr(ESCAPES, *s)) != NULL)
					putchar(ESCCHARS[cp - ESCAPES]);
				else {
					putchar((((unsigned char) *s & 0300) >> 6) + '0');
					putchar((((unsigned char) *s & 070) >> 3) + '0');
					putchar(((unsigned char) *s & 07) + '0');
					col += 2;
				}
			}

		} else
			putchar(*s);
	}
	if (gflag & GLS)
		putchar('$');
	putchar('\n');
	return 0;
}
