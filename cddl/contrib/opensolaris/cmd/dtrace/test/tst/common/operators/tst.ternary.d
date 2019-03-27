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
 *  Test the ternary operator.  Test left-hand side true, right-hand side true,
 *  and multiple nested instances of the ternary operator.
 *
 * SECTION:  Types, Operators, and Expressions/Conditional Expressions
 */

#pragma D option quiet

BEGIN
{
	x = 0;
	printf("x is %s\n", x == 0 ? "zero" : "one");
	x = 1;
	printf("x is %s\n", x == 0 ? "zero" : "one");
	x = 2;
	printf("x is %s\n", x == 0 ? "zero" : x == 1 ? "one" : "two");
	exit(0);
}
