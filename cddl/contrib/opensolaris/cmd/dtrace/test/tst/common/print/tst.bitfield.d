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
	int a:4;
	int b:7;
	int c:1;
	int d:2;
} foo_t;

BEGIN
{
	this->s = (foo_t *)alloca(sizeof (foo_t));

	this->s->a = 1;
	this->s->b = 5;
	this->s->c = 0;
	this->s->d = 2;

	print(*this->s);

	exit(0);
}
