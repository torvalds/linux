/*	$NetBSD: debug.c,v 1.3 2017/01/14 00:50:56 christos Exp $	*/

/*-
 * Copyright (c) 1993 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* Don't sort these! */
#include "utils.h"
#include "regex2.h"

#include "test_regex.h"

#ifdef __NetBSD__
static void s_print(struct re_guts *, FILE *);
static char *regchar(int);

/*
 * regprint - print a regexp for debugging
 */
void
regprint(regex_t *r, FILE *d)
{
	struct re_guts *g = r->re_g;
	int c;
	int last;
	int nincat[NC];

	fprintf(d, "%ld states, %zu categories", (long)g->nstates,
							g->ncategories);
	fprintf(d, ", first %ld last %ld", (long)g->firststate,
						(long)g->laststate);
	if (g->iflags&USEBOL)
		fprintf(d, ", USEBOL");
	if (g->iflags&USEEOL)
		fprintf(d, ", USEEOL");
	if (g->iflags&BAD)
		fprintf(d, ", BAD");
	if (g->nsub > 0)
		fprintf(d, ", nsub=%ld", (long)g->nsub);
	if (g->must != NULL)
		fprintf(d, ", must(%ld) `%*s'", (long)g->mlen, (int)g->mlen,
								g->must);
	if (g->backrefs)
		fprintf(d, ", backrefs");
	if (g->nplus > 0)
		fprintf(d, ", nplus %ld", (long)g->nplus);
	fprintf(d, "\n");
	s_print(g, d);
	for (size_t i = 0; i < g->ncategories; i++) {
		nincat[i] = 0;
		for (c = CHAR_MIN; c <= CHAR_MAX; c++)
			if (g->categories[c] == i)
				nincat[i]++;
	}
	fprintf(d, "cc0#%d", nincat[0]);
	for (size_t i = 1; i < g->ncategories; i++)
		if (nincat[i] == 1) {
			for (c = CHAR_MIN; c <= CHAR_MAX; c++)
				if (g->categories[c] == i)
					break;
			fprintf(d, ", %zu=%s", i, regchar(c));
		}
	fprintf(d, "\n");
	for (size_t i = 1; i < g->ncategories; i++)
		if (nincat[i] != 1) {
			fprintf(d, "cc%zu\t", i);
			last = -1;
			for (c = CHAR_MIN; c <= CHAR_MAX+1; c++)	/* +1 does flush */
				if (c <= CHAR_MAX && g->categories[c] == i) {
					if (last < 0) {
						fprintf(d, "%s", regchar(c));
						last = c;
					}
				} else {
					if (last >= 0) {
						if (last != c-1)
							fprintf(d, "-%s",
								regchar(c-1));
						last = -1;
					}
				}
			fprintf(d, "\n");
		}
}

/*
 * s_print - print the strip for debugging
 */
static void
s_print(struct re_guts *g, FILE *d)
{
	sop *s;
	cset *cs;
	int done = 0;
	sop opnd;
	int col = 0;
	ssize_t last;
	sopno offset = 2;
#	define	GAP()	{	if (offset % 5 == 0) { \
					if (col > 40) { \
						fprintf(d, "\n\t"); \
						col = 0; \
					} else { \
						fprintf(d, " "); \
						col++; \
					} \
				} else \
					col++; \
				offset++; \
			}

	if (OP(g->strip[0]) != OEND)
		fprintf(d, "missing initial OEND!\n");
	for (s = &g->strip[1]; !done; s++) {
		opnd = OPND(*s);
		switch (OP(*s)) {
		case OEND:
			fprintf(d, "\n");
			done = 1;
			break;
		case OCHAR:
			if (strchr("\\|()^$.[+*?{}!<> ", (char)opnd) != NULL)
				fprintf(d, "\\%c", (char)opnd);
			else
				fprintf(d, "%s", regchar((char)opnd));
			break;
		case OBOL:
			fprintf(d, "^");
			break;
		case OEOL:
			fprintf(d, "$");
			break;
		case OBOW:
			fprintf(d, "\\{");
			break;
		case OEOW:
			fprintf(d, "\\}");
			break;
		case OANY:
			fprintf(d, ".");
			break;
		case OANYOF:
			fprintf(d, "[(%ld)", (long)opnd);
			cs = &g->sets[opnd];
			last = -1;
			for (size_t i = 0; i < g->csetsize+1; i++)	/* +1 flushes */
				if (CHIN(cs, i) && i < g->csetsize) {
					if (last < 0) {
						fprintf(d, "%s", regchar(i));
						last = i;
					}
				} else {
					if (last >= 0) {
						if (last != (ssize_t)i - 1)
							fprintf(d, "-%s",
							    regchar(i - 1));
						last = -1;
					}
				}
			fprintf(d, "]");
			break;
		case OBACK_:
			fprintf(d, "(\\<%ld>", (long)opnd);
			break;
		case O_BACK:
			fprintf(d, "<%ld>\\)", (long)opnd);
			break;
		case OPLUS_:
			fprintf(d, "(+");
			if (OP(*(s+opnd)) != O_PLUS)
				fprintf(d, "<%ld>", (long)opnd);
			break;
		case O_PLUS:
			if (OP(*(s-opnd)) != OPLUS_)
				fprintf(d, "<%ld>", (long)opnd);
			fprintf(d, "+)");
			break;
		case OQUEST_:
			fprintf(d, "(?");
			if (OP(*(s+opnd)) != O_QUEST)
				fprintf(d, "<%ld>", (long)opnd);
			break;
		case O_QUEST:
			if (OP(*(s-opnd)) != OQUEST_)
				fprintf(d, "<%ld>", (long)opnd);
			fprintf(d, "?)");
			break;
		case OLPAREN:
			fprintf(d, "((<%ld>", (long)opnd);
			break;
		case ORPAREN:
			fprintf(d, "<%ld>))", (long)opnd);
			break;
		case OCH_:
			fprintf(d, "<");
			if (OP(*(s+opnd)) != OOR2)
				fprintf(d, "<%ld>", (long)opnd);
			break;
		case OOR1:
			if (OP(*(s-opnd)) != OOR1 && OP(*(s-opnd)) != OCH_)
				fprintf(d, "<%ld>", (long)opnd);
			fprintf(d, "|");
			break;
		case OOR2:
			fprintf(d, "|");
			if (OP(*(s+opnd)) != OOR2 && OP(*(s+opnd)) != O_CH)
				fprintf(d, "<%ld>", (long)opnd);
			break;
		case O_CH:
			if (OP(*(s-opnd)) != OOR1)
				fprintf(d, "<%ld>", (long)opnd);
			fprintf(d, ">");
			break;
		default:
			fprintf(d, "!%d(%d)!", OP(*s), opnd);
			break;
		}
		if (!done)
			GAP();
	}
}

/*
 * regchar - make a character printable
 */
static char *			/* -> representation */
regchar(int ch)
{
	static char buf[10];

	if (isprint(ch) || ch == ' ')
		sprintf(buf, "%c", ch);
	else
		sprintf(buf, "\\%o", ch);
	return(buf);
}
#else
void
regprint(regex_t *r, FILE *d)
{

}
#endif
