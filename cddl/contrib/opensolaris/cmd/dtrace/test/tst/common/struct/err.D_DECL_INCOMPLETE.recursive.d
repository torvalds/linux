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

/*
 * ASSERTION: Recursive naming of structures should produce compiler error.
 *
 * SECTION: Structs and Unions/Structs
 *
 */


#pragma ident	"%Z%%M%	%I%	%E% SMI"
#pragma D option quiet

struct record {
	struct record rec;
	int position;
	char content;
};

struct record r1;
struct record r2;

BEGIN
{
	r1.position = 1;
	r1.content = 'a';

	r2.position = 2;
	r2.content = 'b';

	printf("r1.position: %d\nr1.content: %c\n", r1.position, r1.content);
	printf("r2.position: %d\nr2.content: %c\n", r2.position, r2.content);

	exit(0);
}
