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

#pragma	ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION:
 *	Verify relational operators with enumerations
 *
 * SECTION: Types, Operators, and Expressions/Relational Operators
 *
 */

#pragma D option quiet

enum numbers_1 {
	zero,
	one,
	two
};

enum numbers_2 {
	null,
	first,
	second
};

tick-1
/zero >= one || second <= first || zero == second/
{
	printf("Shouldn't end up here (1)\n");
	printf("zero = %d; one = %d; two = %d", zero, one, two);
	printf("null = %d; first = %d; second = %d", null, first, second);
	exit(1);
}

tick-1
/second < one || two > second || null == first/
{
	printf("Shouldn't end up here (2)\n");
	printf("zero = %d; one = %d; two = %d", zero, one, two);
	printf("null = %d; first = %d; second = %d", null, first, second);
	exit(1);
}

tick-1
/first < two && second > one && one != two && zero != first/
{
	exit(0);
}
