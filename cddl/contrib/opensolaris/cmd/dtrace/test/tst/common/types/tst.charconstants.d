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
 *	verify the use of char constants
 *
 * SECTION: Types, Operators, and Expressions/Constants
 *
 */


#pragma	ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet


BEGIN
{
	char_1 = 'a';
	char_2 = '\"';
	char_3 = '\"\ba';
	char_4 = '\?';
	char_5 = '\'';
	char_6 = '\\';
	char_7 = '\0103';
	char_8 = '\x4E';
	char_9 = '\c';		/* Note - this is not an escape sequence */
	char_10 = 'ab\"d';
	char_11 = 'a\bcdefgh';

	printf("decimal value = %d; character value = %c\n", char_1, char_1);
	printf("decimal value = %d; character value = %c\n", char_2, char_2);
	printf("decimal value = %d; character value = %c\n", char_3, char_3);
	printf("decimal value = %d; character value = %c\n", char_4, char_4);
	printf("decimal value = %d; character value = %c\n", char_5, char_5);
	printf("decimal value = %d; character value = %c\n", char_6, char_6);
	printf("decimal value = %d; character value = %c\n", char_7, char_7);
	printf("decimal value = %d; character value = %c\n", char_8, char_8);
	printf("decimal value = %d; character value = %c\n", char_9, char_9);
	printf("decimal value = %d; character value = %c\n", char_10, char_10);
	printf("decimal value = %d\n", char_11);

	exit(0);
}
