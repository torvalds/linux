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
 * When a struct is used as a key for an associative array, the key is formed
 * by using the values of the members of the struct variable and not the
 * address of the struct variable.
 *
 * SECTION: Structs and Unions/Structs
 *
 */

#pragma D option quiet

struct record {
	int position;
	char content;
};

struct record r1;
struct record r2;

BEGIN
{
	r1.position = 1;
	r1.content = 'a';

	r2.position = 1;
	r2.content = 'a';

	assoc_array[r1] = 1000;
	assoc_array[r2] = 2000;

	printf("assoc_array[r1]: %d\n", assoc_array[r1]);
	printf("assoc_array[r2]: %d\n", assoc_array[r2]);

	exit(0);
}

END
/assoc_array[r1] != assoc_array[r2]/
{
	printf("Error");
	exit(1);
}
