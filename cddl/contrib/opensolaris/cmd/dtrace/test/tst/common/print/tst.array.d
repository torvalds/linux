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

typedef struct bar {
	int alpha;
} bar_t;

typedef struct foo {
	int a[3];
	char b[30];
	bar_t c[2];
	char d[3];
} foo_t;

BEGIN
{
	this->f = (foo_t *)alloca(sizeof (foo_t));

	this->f->a[0] = 1;
	this->f->a[1] = 2;
	this->f->a[2] = 3;
	this->f->b[0] = 'a';
	this->f->b[1] = 'b';
	this->f->b[2] = 0;
	this->f->c[0].alpha = 5;
	this->f->c[1].alpha = 6;
	this->f->c[2].alpha = 7;
	this->f->d[0] = 4;
	this->f->d[1] = 0;
	this->f->d[2] = 0;

	print(this->f->a);
	print(this->f->b);
	print(this->f->c);
	print(*this->f);

	exit(0);
}
