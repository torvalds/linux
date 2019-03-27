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
 * Copyright (c) 2011, Joyent, Inc. All rights reserved.
 */

#pragma D option quiet

tick-1ms
/i++ <= 100/
{
	@two = llquantize(i, 2, 0, 6, 2);
	@three = llquantize(i, 3, 0, 1, 9);
	@four = llquantize(i, 4, 0, 1, 4);
	@five = llquantize(i, 5, 0, 1, 25);
	@six = llquantize(i, 6, 0, 3, 12);
	@seven = llquantize(i, 7, 0, 1, 7);
	@eight = llquantize(i, 8, 0, 1, 16);
	@nine = llquantize(i, 9, 0, 1, 9);
	@ten = llquantize(i, 10, 0, 1, 10);
}

tick-1ms
/i > 100/
{
	exit(0);
}
