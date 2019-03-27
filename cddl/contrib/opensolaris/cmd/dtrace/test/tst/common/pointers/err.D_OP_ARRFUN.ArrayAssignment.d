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
 * ASSERTION: Arrays may not be assigned as a whole in D.
 *
 * SECTION: Pointers and Arrays/Pointer and Array Relationship
 *
 * NOTES:
 *
 */

#pragma D option quiet

int array1[3];
int array2[3];

BEGIN
{
	array1[0] = 200;
	array1[1] = 400;
	array1[2] = 600;

	array2[0] = 300;
	array2[1] = 500;
	array2[2] = 700;

	array2 = array1;

	printf("array1[0]: %d\tarray2[0]: %d\n", array1[0], array2[0]);
	printf("array1[1]: %d\tarray2[1]: %d\n", array1[1], array2[1]);
	printf("array1[2]: %d\tarray2[2]: %d\n", array1[2], array2[2]);

	exit(0);

}
