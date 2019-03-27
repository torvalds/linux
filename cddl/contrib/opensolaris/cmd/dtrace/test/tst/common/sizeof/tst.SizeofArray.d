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
 * ASSERTION: sizeof returns the size in bytes of any D expression or data
 * type. For a simpler array, the sizeof on the array variable itself gives
 * the sum total of memory allocated to the array in bytes. With individual
 * members of the array it gives their respective sizes.
 *
 * SECTION: Structs and Unions/Member Sizes and Offsets
 *
 */
#pragma D option quiet

int array[5];

BEGIN
{
	array[0] = 010;
	array[1] = 100;
	array[2] = 210;

	printf("sizeof (array): %d\n", sizeof (array));
	printf("sizeof (array[0]): %d\n", sizeof (array[0]));
	printf("sizeof (array[1]): %d\n", sizeof (array[1]));
	printf("sizeof (array[2]): %d\n", sizeof (array[2]));

	exit(0);
}

END
/(20 != sizeof (array)) || (4 != sizeof (array[0])) || (4 != sizeof (array[1]))
    || (4 != sizeof (array[2]))/
{
	exit(1);
}
