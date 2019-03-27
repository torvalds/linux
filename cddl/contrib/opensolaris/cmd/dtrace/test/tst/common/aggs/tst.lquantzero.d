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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet


BEGIN
{
	a = 7;
	b = 13;
	val = (-a * b) + a;
}

tick-1ms
{
	incr = val % b;
	val += a;
}

tick-1ms
/val == 0/
{
	val += a;
}

tick-1ms
/incr != 0/
{
	i++;
	@one[i] = lquantize(0, 10, 20, 1, incr);
	@two[i] = lquantize(0, 1, 20, 5, incr);
	@three[i] = lquantize(0, 0, 20, 1, incr);
	@four[i] = lquantize(0, -10, 10, 1, incr);
	@five[i] = lquantize(0, -10, 0, 1, incr);
	@six[i] = lquantize(0, -10, -1, 1, incr);
	@seven[i] = lquantize(0, -10, -2, 1, incr);
}

tick-1ms
/incr == 0/
{
	printf("Zero below the range:\n");
	printa(@one);
	printf("\n");

	printf("Zero just below the range:\n");
	printa(@two);
	printf("\n");

	printf("Zero at the bottom of the range:\n");
	printa(@three);
	printf("\n");

	printf("Zero within the range:\n");
	printa(@four);
	printf("\n");

	printf("Zero at the top of the range:\n");
	printa(@five);
	printf("\n");

	printf("Zero just above the range:\n");
	printa(@six);
	printf("\n");

	printf("Zero above the range:\n");
	printa(@seven);
	printf("\n");

	exit(0);
}
