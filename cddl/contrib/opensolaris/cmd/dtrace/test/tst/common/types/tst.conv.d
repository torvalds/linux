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
 * 	positive type conversion checks
 *
 * SECTION: Types, Operators, and Expressions/Type Conversions
 *
 * NOTES: not all type conversions are checked.  A lot of this section
 * 	is tested within other tests.
 */

#pragma D option quiet

unsigned int i;
char c;
short s;
long l;
long long ll;

BEGIN
{
/* char -> int */
	c = 'A';
	i = c;
	printf("c is %c i is %d\n", c, i);

/* int -> char */

	i = 1601;
	c = i;
	printf("i is %d c is %c\n", i, c);

/* char -> short */
	c = 'A';
	s = c;
	printf("c is %c s is %d\n", c, s);

/* short -> char */

	s = 1601;
	c = s;
	printf("s is %d c is %c\n", s, c);

/* int -> short */

	i = 1601;
	s = i;
	printf("i is %d s is %d\n", i, s);

/* short -> int */

	s = 1601;
	i = s;
	printf("s is %d i is %d\n", s, i);

/* int -> long long */

	i = 4294967295;
	ll = i;
	printf("i is %d ll is %x\n", i, ll);

/* long long -> int */

	ll = 8589934591;
	i = ll;
	printf("ll is %d i is %x\n", ll, i);

/* char -> long long */

	c = 'A';
	ll = c;
	printf("c is %c ll is %x\n", c, ll);

/* long long -> char */

	ll = 8589934401;
	c = ll;
	printf("ll is %x c is %c\n", ll, c);

	exit(0);
}

