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
 * 	verify that integer constants can be written in decimal
 *      octal or hexadecimal
 *
 * SECTION: Types, Operators, and Expressions/Constants
 */

#pragma	ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet


BEGIN
{
	decimal = 12345;
	octal = 012345;
	hexadecimal_1 = 0x12345;
	hexadecimal_2 = 0X12345;

	printf("%d %d %d %d", decimal, octal, hexadecimal_1, hexadecimal_2);
	printf("%d %o %x %x", decimal, octal, hexadecimal_1, hexadecimal_2);

	exit(0);
}

