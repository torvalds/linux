/*	$OpenBSD: re.c,v 1.19 2018/06/19 12:36:18 martijn Exp $	*/
/*	$NetBSD: re.c,v 1.14 1995/03/21 09:04:48 cgd Exp $	*/

/* re.c: This file contains the regular expression interface routines for
   the ed line editor. */
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"

static char *extract_pattern(int);
static char *parse_char_class(char *);

extern int patlock;


/* get_compiled_pattern: return pointer to compiled pattern from command
   buffer */
regex_t *
get_compiled_pattern(void)
{
	static regex_t *exp = NULL;
	char errbuf[128] = "";

	char *exps;
	char delimiter;
	int n;

	if ((delimiter = *ibufp) == ' ') {
		seterrmsg("invalid pattern delimiter");
		return NULL;
	} else if (delimiter == '\n' || *++ibufp == '\n' || *ibufp == delimiter) {
		if (!exp)
			seterrmsg("no previous pattern");
		return exp;
	} else if ((exps = extract_pattern(delimiter)) == NULL)
		return NULL;
	/* buffer alloc'd && not reserved */
	if (exp && !patlock)
		regfree(exp);
	else if ((exp = malloc(sizeof(regex_t))) == NULL) {
		perror(NULL);
		seterrmsg("out of memory");
		return NULL;
	}
	patlock = 0;
	if ((n = regcomp(exp, exps, 0)) != 0) {
		regerror(n, exp, errbuf, sizeof errbuf);
		seterrmsg(errbuf);
		free(exp);
		return exp = NULL;
	}
	return exp;
}


/* extract_pattern: copy a pattern string from the command buffer; return
   pointer to the copy */
static char *
extract_pattern(int delimiter)
{
	static char *lhbuf = NULL;	/* buffer */
	static int lhbufsz = 0;		/* buffer size */

	char *nd;
	int len;

	for (nd = ibufp; *nd != delimiter && *nd != '\n'; nd++)
		switch (*nd) {
		default:
			break;
		case '[':
			if ((nd = parse_char_class(++nd)) == NULL) {
				seterrmsg("unbalanced brackets ([])");
				return NULL;
			}
			break;
		case '\\':
			if (*++nd == '\n') {
				seterrmsg("trailing backslash (\\)");
				return NULL;
			}
			break;
		}
	len = nd - ibufp;
	REALLOC(lhbuf, lhbufsz, len + 1, NULL);
	memcpy(lhbuf, ibufp, len);
	lhbuf[len] = '\0';
	ibufp = nd;
	return (isbinary) ? NUL_TO_NEWLINE(lhbuf, len) : lhbuf;
}


/* parse_char_class: expand a POSIX character class */
static char *
parse_char_class(char *s)
{
	int c, d;

	if (*s == '^')
		s++;
	if (*s == ']')
		s++;
	for (; *s != ']' && *s != '\n'; s++)
		if (*s == '[' && ((d = *(s+1)) == '.' || d == ':' || d == '='))
			for (s++, c = *++s; *s != ']' || c != d; s++)
				if ((c = *s) == '\n')
					return NULL;
	return  (*s == ']') ? s : NULL;
}
