/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2001 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating stacks
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "stack.h"
#include "memory.h"

#define	STACK_SEEDSIZE	5

struct stk {
	int st_nument;
	int st_top;
	void **st_data;

	void (*st_free)(void *);
};

stk_t *
stack_new(void (*freep)(void *))
{
	stk_t *sp;

	sp = xmalloc(sizeof (stk_t));
	sp->st_nument = STACK_SEEDSIZE;
	sp->st_top = -1;
	sp->st_data = xmalloc(sizeof (void *) * sp->st_nument);
	sp->st_free = freep;

	return (sp);
}

void
stack_free(stk_t *sp)
{
	int i;

	if (sp->st_free) {
		for (i = 0; i <= sp->st_top; i++)
			sp->st_free(sp->st_data[i]);
	}
	free(sp->st_data);
	free(sp);
}

void *
stack_pop(stk_t *sp)
{
	assert(sp->st_top >= 0);

	return (sp->st_data[sp->st_top--]);
}

void *
stack_peek(stk_t *sp)
{
	if (sp->st_top == -1)
		return (NULL);

	return (sp->st_data[sp->st_top]);
}

void
stack_push(stk_t *sp, void *data)
{
	sp->st_top++;

	if (sp->st_top == sp->st_nument) {
		sp->st_nument += STACK_SEEDSIZE;
		sp->st_data = xrealloc(sp->st_data,
		    sizeof (void *) * sp->st_nument);
	}

	sp->st_data[sp->st_top] = data;
}

int
stack_level(stk_t *sp)
{
	return (sp->st_top + 1);
}
