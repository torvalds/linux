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
 * Can dereference a void * pointer only by casting it to another type.
 *
 * SECTION: Pointers and Arrays/Generic Pointers
 */

#pragma D option quiet

void *p;

int array[3];

BEGIN
{
	array[0] = 234;
	array[1] = 334;
	array[2] = 434;

	p = &array[0];
	newp = (int *) p;

	printf("array[0]: %d, newp: %d\n", array[0], *newp);
	exit(0);
}

END
/234 != *newp/
{
	exit(1);
}
