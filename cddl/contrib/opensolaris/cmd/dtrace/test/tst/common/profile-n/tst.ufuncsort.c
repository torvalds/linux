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

typedef void f(int x);

static void
f_a(int i)
{
}

static void
f_b(int i)
{
}

static void
f_c(int i)
{
}

static void
f_d(int i)
{
}

static void
f_e(int i)
{
}

static void
fN(f func, int i)
{
	func(i);
}

int
main()
{
	fN(f_a, 1);
	fN(f_b, 2);
	fN(f_c, 3);
	fN(f_d, 4);
	fN(f_e, 5);
	fN(f_a, 11);
	fN(f_c, 13);
	fN(f_d, 14);
	fN(f_a, 101);
	fN(f_c, 103);
	fN(f_c, 1003);

	return (0);
}
