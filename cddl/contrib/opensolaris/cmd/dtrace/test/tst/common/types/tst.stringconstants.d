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
 *	verify the use of char type
 *
 * SECTION: Types, Operators, and Expressions/Constants
 */

#pragma	ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet

string string_1;

BEGIN
{
	string_1 = "abcd\n\n\nefg";
	string_2 = "abc\"\t\044\?\x4D";
	string_3 = "\?\\\'\"\0";

	printf("string_1 = %s\n", string_1);
	printf("string_2 = %s\n", string_2);
	printf("string_3 = %s\n", string_3);

	exit(0);
}
