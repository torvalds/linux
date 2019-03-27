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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ASSERTION:
 *     Positive stddev() test
 *
 * SECTION: Aggregations/Aggregations
 *
 * NOTES: This is a simple verifiable positive test of the stddev() function.
 *     printa() for one aggregation, default printing behavior for the other
 *     so that we exercise both code paths.
 */

#pragma D option quiet

BEGIN
{
	@a = stddev(5000000000);
	@a = stddev(5000000100);
	@a = stddev(5000000200);
	@a = stddev(5000000300);
	@a = stddev(5000000400);
	@a = stddev(5000000500);
	@a = stddev(5000000600);
	@a = stddev(5000000700);
	@a = stddev(5000000800);
	@a = stddev(5000000900);
	@b = stddev(-5000000000);
	@b = stddev(-5000000100);
	@b = stddev(-5000000200);
	@b = stddev(-5000000300);
	@b = stddev(-5000000400);
	@b = stddev(-5000000500);
	@b = stddev(-5000000600);
	@b = stddev(-5000000700);
	@b = stddev(-5000000800);
	@b = stddev(-5000000900);
	printa("%@d\n", @a);
	exit(0);
}
