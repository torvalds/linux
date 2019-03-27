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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "ed.h"

const char *errmsg = "";

/* get_compiled_pattern: return pointer to compiled pattern from command
   buffer */
pattern_t *
get_compiled_pattern(void)
{
	static pattern_t *expr = NULL;
	static char error[1024];

	char *exprs;
	char delimiter;
	int n;

	if ((delimiter = *ibufp) == ' ') {
		errmsg = "invalid pattern delimiter";
		return NULL;
	} else if (delimiter == '\n' || *++ibufp == '\n' || *ibufp == delimiter) {
		if (!expr)
			errmsg = "no previous pattern";
		return expr;
	} else if ((exprs = extract_pattern(delimiter)) == NULL)
		return NULL;
	/* buffer alloc'd && not reserved */
	if (expr && !patlock)
		regfree(expr);
	else if ((expr = (pattern_t *) malloc(sizeof(pattern_t))) == NULL) {
		fprintf(stderr, "%s\n", strerror(errno));
		errmsg = "out of memory";
		return NULL;
	}
	patlock = 0;
	if ((n = regcomp(expr, exprs, 0))) {
		regerror(n, expr, error, sizeof error);
		errmsg = error;
		free(expr);
		return expr = NULL;
	}
	return expr;
}


/* extract_pattern: copy a pattern string from the command buffer; return
   pointer to the copy */
char *
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
			if ((nd = parse_char_class(nd + 1)) == NULL) {
				errmsg = "unbalanced brackets ([])";
				return NULL;
			}
			break;
		case '\\':
			if (*++nd == '\n') {
				errmsg = "trailing backslash (\\)";
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
char *
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
