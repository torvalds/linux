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
 * Pointer arithmetic implicitly adjusts the underlying address by
 * multiplying or dividing the operands by the size of the type referenced
 * by the pointer.
 *
 * SECTION: Pointers and Arrays/Pointer Arithmetic
 *
 * NOTES:
 *
 */

#pragma D option quiet

int *x;
int *y;
int a[5];

BEGIN
{
	x = &a[0];
	y = &a[2];

	printf("y-x: %x\n", (int) (y-x));
	exit(0);
}

END
/(2 != (int) (y-x)) /
{
	printf("Error");
	exit(1);
}
