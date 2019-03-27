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
 * Declaring an associative array with a struct to be its key type and trying to
 * index with another struct having the same composition throws an error.
 *
 * SECTION: Structs and Unions/Structs
 *
 */

#pragma D option quiet

struct record {
	int position;
	char content;
};

struct pirate {
	int position;
	char content;
};

struct record r1;
struct record r2;
struct pirate p1;
struct pirate p2;

BEGIN
{
	r1.position = 1;
	r1.content = 'a';

	r2.position = 2;
	r2.content = 'b';

	p1.position = 1;
	p1.content = 'a';

	p2.position = 2;
	p2.content = 'b';

	assoc_array[r1] = 1000;
	assoc_array[r2] = 2000;
	assoc_array[p1] = 3333;
	assoc_array[p2] = 4444;

	printf("assoc_array[r1]: %d\n",  assoc_array[r1]);
	printf("assoc_array[r2]: %d\n",  assoc_array[r2]);
	printf("assoc_array[p1]: %d\n",  assoc_array[p1]);
	printf("assoc_array[p2]: %d\n",  assoc_array[p2]);

	exit(0);
}
