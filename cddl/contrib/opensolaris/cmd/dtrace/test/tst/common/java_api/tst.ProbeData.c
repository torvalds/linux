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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

typedef void f(char *);

static void
f_a(char *a)
{
}

static void
f_b(char *a)
{
}

static void
f_c(char *a)
{
}

static void
f_d(char *a)
{
}

static void
f_e(char *a)
{
}

static void
fN(f func, char *a, int i)
{
	func(a);
}

static void
fN2(f func, char *a, int i)
{
	func(a);
}

int
main()
{
	/*
	 * Avoid length of 1, 2, 4, or 8 bytes so DTrace will treat the data as
	 * a byte array.
	 */
	char a[] = {(char)-7, (char)201, (char)0, (char)0, (char)28, (char)1};
	char b[] = {(char)84, (char)69, (char)0, (char)0, (char)28, (char)0};
	char c[] = {(char)84, (char)69, (char)0, (char)0, (char)28, (char)1};
	char d[] = {(char)-7, (char)201, (char)0, (char)0, (char)29, (char)0};
	char e[] = {(char)84, (char)69, (char)0, (char)0, (char)28, (char)0};

	fN(f_a, a, 1);
	fN(f_b, b, 0);
	fN(f_d, d, 102);
	fN2(f_e, e, -2);
	fN(f_c, c, 0);
	fN(f_a, a, -1);
	fN(f_d, d, 101);
	fN(f_e, e, -2);
	fN(f_e, e, 2);
	fN2(f_e, e, 2);

	return (0);
}
