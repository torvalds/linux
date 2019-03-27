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
 *	Verify relational operators with strings
 *
 * SECTION: Types, Operators, and Expressions/Relational Operators
 *
 */

#pragma D option quiet


BEGIN
{
	string_1 = "abcde";
	string_2 = "aabcde";
	string_3 = "abcdef";
}

tick-1
/string_1 <= string_2 || string_2 >= string_1 || string_1 == string_2/
{
	printf("Shouldn't end up here (1)\n");
	printf("string_1 = %s string_2 = %s string_3 = %s\n",
		string_1, string_2, string_3);
	exit(1);
}

tick-1
/string_3 < string_1 || string_1 > string_3 || string_3 == string_1/
{
	printf("Shouldn't end up here (2)\n");
	printf("string_1 = %s string_2 = %s string_3 = %s n",
		string_1, string_2, string_3);
	exit(1);
}

tick-1
/string_3 > string_1 && string_1 > string_2 &&
	string_1 != string_2 && string_2 != string_3/
{
	exit(0);
}
