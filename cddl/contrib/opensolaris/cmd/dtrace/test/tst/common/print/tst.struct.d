/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 2011 by Delphix. All rights reserved.
 */

#pragma D option quiet

typedef struct forward forward_t;

typedef struct foo {
	int a;
	void *b;
	struct {
		uint64_t alpha;
		uint64_t beta;
	} c;
	ushort_t d;
	int e;
	forward_t *f;
	void (*g)();
} foo_t;

BEGIN
{
	this->s = (foo_t *)alloca(sizeof (foo_t));

	this->s->a = 1;
	this->s->b = (void *)2;
	this->s->c.alpha = 3;
	this->s->c.beta = 4;
	this->s->d = 5;
	this->s->e = 6;
	this->s->f = (void *)7;
	this->s->g = (void *)8;

	print(*this->s);

	exit(0);
}
