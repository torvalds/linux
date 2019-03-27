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
 *	Verify assignment operators
 *
 * SECTION: Types, Operators, and Expressions/Assignment Operators
 *
 */

#pragma D option quiet


BEGIN
{
	int_1 = 0x100;
	int_2 = 0xf0f0;
	int_3 = 0x0f0f;

	intn = 0x1;

	intn += int_1;
	printf("%x\n", intn);
	intn -= int_1;
	printf("%x\n", intn);
	intn *= int_1;
	printf("%x\n", intn);
	intn /= int_1;
	printf("%x\n", intn);
	intn %= int_1;
	printf("%x\n", intn);
	printf("\n");

	intb = 0x0000;

	intb |= (int_2 | intb);
	printf("%x\n", intb);
	intb &= (int_2 | int_3);
	printf("%x\n", intb);
	intb ^= (int_2 | int_3);
	printf("%x\n", intb);
	intb |= ~(intb);
	printf("%x\n", intb);
	printf("\n");

	intb = int_2;

	printf("%x\n", intb);
	intb <<= 3;
	printf("%x\n", intb);
	intb >>= 3;
	printf("%x\n", intb);

	exit(0);

}
