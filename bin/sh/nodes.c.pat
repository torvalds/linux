/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 *	@(#)nodes.c.pat	8.2 (Berkeley) 5/4/95
 * $FreeBSD$
 */

#include <sys/param.h>
#include <stdlib.h>
#include <stddef.h>
/*
 * Routine for dealing with parsed shell commands.
 */

#include "shell.h"
#include "nodes.h"
#include "memalloc.h"
#include "mystring.h"


struct nodesize {
	int     blocksize;	/* size of structures in function */
	int     stringsize;	/* size of strings in node */
};

struct nodecopystate {
	pointer block;		/* block to allocate function from */
	char   *string;		/* block to allocate strings from */
};

%SIZES


static void calcsize(union node *, struct nodesize *);
static void sizenodelist(struct nodelist *, struct nodesize *);
static union node *copynode(union node *, struct nodecopystate *);
static struct nodelist *copynodelist(struct nodelist *, struct nodecopystate *);
static char *nodesavestr(const char *, struct nodecopystate *);


struct funcdef {
	unsigned int refcount;
	union node n;
};

/*
 * Make a copy of a parse tree.
 */

struct funcdef *
copyfunc(union node *n)
{
	struct nodesize sz;
	struct nodecopystate st;
	struct funcdef *fn;

	if (n == NULL)
		return NULL;
	sz.blocksize = offsetof(struct funcdef, n);
	sz.stringsize = 0;
	calcsize(n, &sz);
	fn = ckmalloc(sz.blocksize + sz.stringsize);
	fn->refcount = 1;
	st.block = (char *)fn + offsetof(struct funcdef, n);
	st.string = (char *)fn + sz.blocksize;
	copynode(n, &st);
	return fn;
}


union node *
getfuncnode(struct funcdef *fn)
{
	return fn == NULL ? NULL : &fn->n;
}


static void
calcsize(union node *n, struct nodesize *result)
{
	%CALCSIZE
}



static void
sizenodelist(struct nodelist *lp, struct nodesize *result)
{
	while (lp) {
		result->blocksize += ALIGN(sizeof(struct nodelist));
		calcsize(lp->n, result);
		lp = lp->next;
	}
}



static union node *
copynode(union node *n, struct nodecopystate *state)
{
	union node *new;

	%COPY
	return new;
}


static struct nodelist *
copynodelist(struct nodelist *lp, struct nodecopystate *state)
{
	struct nodelist *start;
	struct nodelist **lpp;

	lpp = &start;
	while (lp) {
		*lpp = state->block;
		state->block = (char *)state->block +
		    ALIGN(sizeof(struct nodelist));
		(*lpp)->n = copynode(lp->n, state);
		lp = lp->next;
		lpp = &(*lpp)->next;
	}
	*lpp = NULL;
	return start;
}



static char *
nodesavestr(const char *s, struct nodecopystate *state)
{
	const char *p = s;
	char *q = state->string;
	char   *rtn = state->string;

	while ((*q++ = *p++) != '\0')
		continue;
	state->string = q;
	return rtn;
}


void
reffunc(struct funcdef *fn)
{
	if (fn)
		fn->refcount++;
}


/*
 * Decrement the reference count of a function definition, freeing it
 * if it falls to 0.
 */

void
unreffunc(struct funcdef *fn)
{
	if (fn) {
		fn->refcount--;
		if (fn->refcount > 0)
			return;
		ckfree(fn);
	}
}
