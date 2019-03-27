/* undo.c: This file contains the undo routines for the ed line editor */
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


#define USIZE 100				/* undo stack size */
static undo_t *ustack = NULL;			/* undo stack */
static long usize = 0;				/* stack size variable */
static long u_p = 0;				/* undo stack pointer */

/* push_undo_stack: return pointer to initialized undo node */
undo_t *
push_undo_stack(int type, long from, long to)
{
	undo_t *t;

#if defined(sun) || defined(NO_REALLOC_NULL)
	if (ustack == NULL &&
	    (ustack = (undo_t *) malloc((usize = USIZE) * sizeof(undo_t))) == NULL) {
		fprintf(stderr, "%s\n", strerror(errno));
		errmsg = "out of memory";
		return NULL;
	}
#endif
	t = ustack;
	if (u_p < usize ||
	    (t = (undo_t *) realloc(ustack, (usize += USIZE) * sizeof(undo_t))) != NULL) {
		ustack = t;
		ustack[u_p].type = type;
		ustack[u_p].t = get_addressed_line_node(to);
		ustack[u_p].h = get_addressed_line_node(from);
		return ustack + u_p++;
	}
	/* out of memory - release undo stack */
	fprintf(stderr, "%s\n", strerror(errno));
	errmsg = "out of memory";
	clear_undo_stack();
	free(ustack);
	ustack = NULL;
	usize = 0;
	return NULL;
}


/* USWAP: swap undo nodes */
#define USWAP(x,y) { \
	undo_t utmp; \
	utmp = x, x = y, y = utmp; \
}


long u_current_addr = -1;	/* if >= 0, undo enabled */
long u_addr_last = -1;		/* if >= 0, undo enabled */

/* pop_undo_stack: undo last change to the editor buffer */
int
pop_undo_stack(void)
{
	long n;
	long o_current_addr = current_addr;
	long o_addr_last = addr_last;

	if (u_current_addr == -1 || u_addr_last == -1) {
		errmsg = "nothing to undo";
		return ERR;
	} else if (u_p)
		modified = 1;
	get_addressed_line_node(0);	/* this get_addressed_line_node last! */
	SPL1();
	for (n = u_p; n-- > 0;) {
		switch(ustack[n].type) {
		case UADD:
			REQUE(ustack[n].h->q_back, ustack[n].t->q_forw);
			break;
		case UDEL:
			REQUE(ustack[n].h->q_back, ustack[n].h);
			REQUE(ustack[n].t, ustack[n].t->q_forw);
			break;
		case UMOV:
		case VMOV:
			REQUE(ustack[n - 1].h, ustack[n].h->q_forw);
			REQUE(ustack[n].t->q_back, ustack[n - 1].t);
			REQUE(ustack[n].h, ustack[n].t);
			n--;
			break;
		default:
			/*NOTREACHED*/
			;
		}
		ustack[n].type ^= 1;
	}
	/* reverse undo stack order */
	for (n = u_p; n-- > (u_p + 1)/ 2;)
		USWAP(ustack[n], ustack[u_p - 1 - n]);
	if (isglobal)
		clear_active_list();
	current_addr = u_current_addr, u_current_addr = o_current_addr;
	addr_last = u_addr_last, u_addr_last = o_addr_last;
	SPL0();
	return 0;
}


/* clear_undo_stack: clear the undo stack */
void
clear_undo_stack(void)
{
	line_t *lp, *ep, *tl;

	while (u_p--)
		if (ustack[u_p].type == UDEL) {
			ep = ustack[u_p].t->q_forw;
			for (lp = ustack[u_p].h; lp != ep; lp = tl) {
				unmark_line_node(lp);
				tl = lp->q_forw;
				free(lp);
			}
		}
	u_p = 0;
	u_current_addr = current_addr;
	u_addr_last = addr_last;
}
