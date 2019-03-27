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
 * Copyright 2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating a FIFO queue
 */

#include <stdlib.h>

#include "fifo.h"
#include "memory.h"

typedef struct fifonode {
	void *fn_data;
	struct fifonode *fn_next;
} fifonode_t;

struct fifo {
	fifonode_t *f_head;
	fifonode_t *f_tail;
};

fifo_t *
fifo_new(void)
{
	fifo_t *f;

	f = xcalloc(sizeof (fifo_t));

	return (f);
}

/* Add to the end of the fifo */
void
fifo_add(fifo_t *f, void *data)
{
	fifonode_t *fn = xmalloc(sizeof (fifonode_t));

	fn->fn_data = data;
	fn->fn_next = NULL;

	if (f->f_tail == NULL)
		f->f_head = f->f_tail = fn;
	else {
		f->f_tail->fn_next = fn;
		f->f_tail = fn;
	}
}

/* Remove from the front of the fifo */
void *
fifo_remove(fifo_t *f)
{
	fifonode_t *fn;
	void *data;

	if ((fn = f->f_head) == NULL)
		return (NULL);

	data = fn->fn_data;
	if ((f->f_head = fn->fn_next) == NULL)
		f->f_tail = NULL;

	free(fn);

	return (data);
}

/*ARGSUSED*/
static void
fifo_nullfree(void *arg)
{
	/* this function intentionally left blank */
}

/* Free an entire fifo */
void
fifo_free(fifo_t *f, void (*freefn)(void *))
{
	fifonode_t *fn = f->f_head;
	fifonode_t *tmp;

	if (freefn == NULL)
		freefn = fifo_nullfree;

	while (fn) {
		(*freefn)(fn->fn_data);

		tmp = fn;
		fn = fn->fn_next;
		free(tmp);
	}

	free(f);
}

int
fifo_len(fifo_t *f)
{
	fifonode_t *fn;
	int i;

	for (i = 0, fn = f->f_head; fn; fn = fn->fn_next, i++);

	return (i);
}

int
fifo_empty(fifo_t *f)
{
	return (f->f_head == NULL);
}

int
fifo_iter(fifo_t *f, int (*iter)(void *data, void *arg), void *arg)
{
	fifonode_t *fn;
	int rc;
	int ret = 0;

	for (fn = f->f_head; fn; fn = fn->fn_next) {
		if ((rc = iter(fn->fn_data, arg)) < 0)
			return (-1);
		ret += rc;
	}

	return (ret);
}
