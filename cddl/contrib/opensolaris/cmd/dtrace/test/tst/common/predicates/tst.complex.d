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
 * 	Complex operations and if,then test.
 *	Call 'n' permutation and combination of operations over if,then.
 *	Match expected output in tst.complex.d.out
 *
 * SECTION: Program Structure/Predicates
 *
 */

#pragma D option quiet

BEGIN
{
	i = 0;
	j = 0;
}

tick-10ms
/i < 10/
{
	i++;
	j++;
	printf("\n\n%d\n------\n", i);
}

tick-10ms
/i == 5 || i == 10/
{
	printf("i == 5 (or) i == 10\n");
}

tick-10ms
/i <= 5/
{
	printf("i <= 5\n");
}

tick-10ms
/j >= 5/
{
	printf("j >= 5\n");
}

tick-10ms
/j >= 5 || i <= 5/
{
	printf("i >= 5 || j >= 5\n");
}

tick-10ms
/j >= 5 && i <= 5/
{
	printf("j >= 5 && i <= 55\n");
}

tick-10ms
/i < 5/
{
	printf("i < 5\n");
}

tick-10ms
/i == 2 || j == 2/
{
	printf("i == 2 (or) j == 2\n");
}

tick-10ms
/i == 2 && j == 2/
{
	printf("i == 2 (and) j == 2\n");
}

tick-10ms
/j != 10/
{
	printf("j != 10\n");
}

tick-10ms
/j == 5 || i == 2/
{
	printf("j == 5 || i == 2\n");
}

tick-10ms
/j == 5 && i == 2/
{
	printf("j == 5 && i == 2\n");
}

tick-10ms
/i == 10/
{
	printf("i == 10\n");
	exit(0);
}
