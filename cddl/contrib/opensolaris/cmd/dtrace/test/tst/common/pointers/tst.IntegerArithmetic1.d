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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION:
 * Integer arithmetic can be performed on pointers by casting to uintptr_t.
 *
 * SECTION: Pointers and Arrays/Generic Pointers
 *
 * NOTES:
 *
 */

#pragma D option quiet

int array[3];
uintptr_t uptr;
int *p;
int *q;
int *r;

BEGIN
{
	array[0] = 20;
	array[1] = 40;
	array[2] = 80;

	uptr = (uintptr_t) &array[0];

	p = (int *) (uptr);
	q = (int *) (uptr + 4);
	r = (int *) (uptr + 8);

	printf("array[0]: %d\t*p: %d\n", array[0], *p);
	printf("array[1]: %d\t*q: %d\n", array[1], *q);
	printf("array[2]: %d\t*r: %d\n", array[2], *r);

	exit(0);
}

END
/(20 != *p) || (40 != *q) || (80 != *r)/
{
	printf("Error");
	exit(1);
}

