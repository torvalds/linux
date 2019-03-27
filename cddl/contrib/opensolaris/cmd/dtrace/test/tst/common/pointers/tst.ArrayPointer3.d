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
 * ASSERTION: In D, the an array variable can be assigned to a pointer.
 *
 * SECTION: Pointers and Arrays/Pointer and Array Relationship
 *
 * NOTES:
 *
 */

#pragma D option quiet

int array[5];
int *p;

BEGIN
{
	array[0] = 100;
	array[1] = 200;
	array[2] = 300;
	array[3] = 400;
	array[4] = 500;

	p = array;

	printf("array[0]: %d\tp[0]: %d\n", array[0], p[0]);
	printf("array[1]: %d\tp[1]: %d\n", array[1], p[1]);
	printf("array[2]: %d\tp[2]: %d\n", array[2], p[2]);
	printf("array[3]: %d\tp[3]: %d\n", array[3], p[3]);
	printf("array[4]: %d\tp[4]: %d\n", array[4], p[4]);

	exit(0);

}

END
/(array[0] != p[0]) || (array[1] != p[1]) || (array[2] != p[2]) ||
    (array[3] != p[3]) || (array[4] != p[4])/
{
	printf("Error");
	exit(1);
}



