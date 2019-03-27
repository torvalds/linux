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
 * 	positive check conditional expressions
 *
 * SECTION: Types, Operators, and Expressions/Conditional Expressions
 *
 * NOTES: these tests are from the User's Guide
 */

#pragma D option quiet


BEGIN
{
	i = 0;
	x = i == 0 ? "zero" : "non-zero";

	c = 'd';
	hexval = (c >= '0' && c <= '9') ? c - '0':
		(c >= 'a' && c <= 'z') ? c + 10 - 'a' : c + 10 - 'A';
}

tick-1
/x == "zero" && hexval == 13/
{
	exit(0);
}

tick-1
/x != "zero" || hexval != 13/
{
	exit(1);
}
