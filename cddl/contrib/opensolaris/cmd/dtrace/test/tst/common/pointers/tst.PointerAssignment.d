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
 * ASSERTION: Pointers assignment simply copies over the pointer address in the
 * variable on the right hand side into the variable on the left hand side.
 *
 * SECTION: Pointers and Arrays/Pointer and Array Relationship
 *
 * NOTES:
 *
 */

#pragma D option quiet

int array[3];
int *ptr1;
int *ptr2;

BEGIN
{
	array[0] = 200;
	array[1] = 400;
	array[2] = 600;

	ptr1 = array;
	ptr2 = ptr1;

	ptr2[0] = 400;
	ptr2[1] = 800;
	ptr2[2] = 1200;

	printf("array[0]: %d\tptr2[0]: %d\n", array[0], ptr2[0]);
	printf("array[1]: %d\tptr2[1]: %d\n", array[1], ptr2[1]);
	printf("array[2]: %d\tptr2[2]: %d\n", array[2], ptr2[2]);

	exit(0);

}

END
/(array[0] != 400) || (array[1] != 800) || (array[2] != 1200)/
{
	printf("Error");
	exit(1);
}
