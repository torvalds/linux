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
 * ASSERTION:
 * Create inline names from aliases created using typedef.
 *
 * SECTION: Type and Constant Definitions/Inlines
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet


struct record {
	char c;
	int i;
};

struct record rec1;
inline struct record rec2 = rec1;

union var {
	char c;
	int i;
};

union var un1;
inline union var un2 = un1;


BEGIN
{
	rec1.c = 'c';
	rec1.i = 10;

	un1.c = 'd';

	printf("rec1.c: %c\nrec1.i:%d\nun1.c: %c\n", rec1.c, rec1.i, un1.c);
	printf("rec2.c: %c\nrec2.i:%d\nun2.c: %c\n", rec2.c, rec2.i, un2.c);
	exit(0);
}
