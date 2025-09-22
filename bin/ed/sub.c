/*	$OpenBSD: sub.c,v 1.18 2016/10/11 06:54:05 martijn Exp $	*/
/*	$NetBSD: sub.c,v 1.4 1995/03/21 09:04:50 cgd Exp $	*/

/* sub.c: This file contains the substitution routines for the ed
   line editor */
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

#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"

static char *extract_subst_template(void);
static int substitute_matching_text(regex_t *, line_t *, int, int);
static int apply_subst_template(char *, regmatch_t *, int, int);

static char *rhbuf;		/* rhs substitution buffer */
static int rhbufsz;		/* rhs substitution buffer size */
static int rhbufi;		/* rhs substitution buffer index */

/* extract_subst_tail: extract substitution tail from the command buffer */
int
extract_subst_tail(int *flagp, int *np)
{
	char delimiter;

	*flagp = *np = 0;
	if ((delimiter = *ibufp) == '\n') {
		rhbufi = 0;
		*flagp = GPR;
		return 0;
	} else if (extract_subst_template() == NULL)
		return  ERR;
	else if (*ibufp == '\n') {
		*flagp = GPR;
		return 0;
	} else if (*ibufp == delimiter)
		ibufp++;
	if ('1' <= *ibufp && *ibufp <= '9') {
		STRTOI(*np, ibufp);
		return 0;
	} else if (*ibufp == 'g') {
		ibufp++;
		*flagp = GSG;
		return 0;
	}
	return 0;
}


/* extract_subst_template: return pointer to copy of substitution template
   in the command buffer */
static char *
extract_subst_template(void)
{
	int n = 0;
	int i = 0;
	char c;
	char delimiter = *ibufp++;

	if (*ibufp == '%' && *(ibufp + 1) == delimiter) {
		ibufp++;
		if (!rhbuf)
			seterrmsg("no previous substitution");
		return rhbuf;
	}
	while (*ibufp != delimiter) {
		REALLOC(rhbuf, rhbufsz, i + 2, NULL);
		if ((c = rhbuf[i++] = *ibufp++) == '\n' && *ibufp == '\0') {
			i--, ibufp--;
			break;
		} else if (c != '\\')
			;
		else if ((rhbuf[i++] = *ibufp++) != '\n')
			;
		else if (!isglobal) {
			while ((n = get_tty_line()) == 0 ||
			    (n > 0 && ibuf[n - 1] != '\n'))
				clearerr(stdin);
			if (n < 0)
				return NULL;
		}
	}
	REALLOC(rhbuf, rhbufsz, i + 1, NULL);
	rhbuf[rhbufi = i] = '\0';
	return  rhbuf;
}


static char *rbuf;		/* substitute_matching_text buffer */
static int rbufsz;		/* substitute_matching_text buffer size */

/* search_and_replace: for each line in a range, change text matching a pattern
   according to a substitution template; return status  */
int
search_and_replace(regex_t *pat, int gflag, int kth)
{
	undo_t *up;
	char *txt;
	char *eot;
	int lc;
	int xa = current_addr;
	int nsubs = 0;
	line_t *lp;
	int len;

	current_addr = first_addr - 1;
	for (lc = 0; lc <= second_addr - first_addr; lc++) {
		lp = get_addressed_line_node(++current_addr);
		if ((len = substitute_matching_text(pat, lp, gflag, kth)) < 0)
			return ERR;
		else if (len) {
			up = NULL;
			if (delete_lines(current_addr, current_addr) < 0)
				return ERR;
			txt = rbuf;
			eot = rbuf + len;
			SPL1();
			do {
				if ((txt = put_sbuf_line(txt)) == NULL) {
					SPL0();
					return ERR;
				} else if (up)
					up->t = get_addressed_line_node(current_addr);
				else if ((up = push_undo_stack(UADD,
				    current_addr, current_addr)) == NULL) {
					SPL0();
					return ERR;
				}
			} while (txt != eot);
			SPL0();
			nsubs++;
			xa = current_addr;
		}
	}
	current_addr = xa;
	if  (nsubs == 0 && !(gflag & GLB)) {
		seterrmsg("no match");
		return ERR;
	} else if ((gflag & (GPR | GLS | GNP)) &&
	    display_lines(current_addr, current_addr, gflag) < 0)
		return ERR;
	return 0;
}


/* substitute_matching_text: replace text matched by a pattern according to
   a substitution template; return length of rbuf if changed, 0 if unchanged, or
   ERR on error */
static int
substitute_matching_text(regex_t *pat, line_t *lp, int gflag, int kth)
{
	int off = 0;
	int changed = 0;
	int matchno = 0;
	int i = 0;
	int nempty = -1;
	regmatch_t rm[SE_MAX];
	char *txt;
	char *eot, *eom;

	if ((eom = txt = get_sbuf_line(lp)) == NULL)
		return ERR;
	if (isbinary)
		NUL_TO_NEWLINE(txt, lp->len);
	eot = txt + lp->len;
	if (!regexec(pat, txt, SE_MAX, rm, 0)) {
		do {
/* Don't do a 0-length match directly after a non-0-length */
			if (rm[0].rm_eo == nempty) {
				rm[0].rm_so++;
				rm[0].rm_eo = lp->len;
				continue;
			}
			if (!kth || kth == ++matchno) {
				changed = 1;
				i = rm[0].rm_so - (eom - txt);
				REALLOC(rbuf, rbufsz, off + i, ERR);
				if (isbinary)
					NEWLINE_TO_NUL(eom,
					    rm[0].rm_eo - (eom - txt));
				memcpy(rbuf + off, eom, i);
				off += i;
				if ((off = apply_subst_template(txt, rm, off,
				    pat->re_nsub)) < 0)
					return ERR;
				eom = txt + rm[0].rm_eo;
				if (kth)
					break;
			}
			if (rm[0].rm_so == rm[0].rm_eo)
				rm[0].rm_so = rm[0].rm_eo + 1;
			else
				nempty = rm[0].rm_so = rm[0].rm_eo;
			rm[0].rm_eo = lp->len;
		} while (rm[0].rm_so < lp->len && (gflag & GSG || kth) &&
		    !regexec(pat, txt, SE_MAX, rm, REG_STARTEND | REG_NOTBOL));
		i = eot - eom;
		REALLOC(rbuf, rbufsz, off + i + 2, ERR);
		if (isbinary)
			NEWLINE_TO_NUL(eom, i);
		memcpy(rbuf + off, eom, i);
		memcpy(rbuf + off + i, "\n", 2);
	}
	return changed ? off + i + 1 : 0;
}


/* apply_subst_template: modify text according to a substitution template;
   return offset to end of modified text */
static int
apply_subst_template(char *boln, regmatch_t *rm, int off, int re_nsub)
{
	int j = 0;
	int k = 0;
	int n;
	char *sub = rhbuf;

	for (; sub - rhbuf < rhbufi; sub++)
		if (*sub == '&') {
			j = rm[0].rm_so;
			k = rm[0].rm_eo;
			REALLOC(rbuf, rbufsz, off + k - j, ERR);
			while (j < k)
				rbuf[off++] = boln[j++];
		} else if (*sub == '\\' && '1' <= *++sub && *sub <= '9' &&
		    (n = *sub - '0') <= re_nsub) {
			j = rm[n].rm_so;
			k = rm[n].rm_eo;
			REALLOC(rbuf, rbufsz, off + k - j, ERR);
			while (j < k)
				rbuf[off++] = boln[j++];
		} else {
			REALLOC(rbuf, rbufsz, off + 1, ERR);
			rbuf[off++] = *sub;
		}
	REALLOC(rbuf, rbufsz, off + 1, ERR);
	rbuf[off] = '\0';
	return off;
}
