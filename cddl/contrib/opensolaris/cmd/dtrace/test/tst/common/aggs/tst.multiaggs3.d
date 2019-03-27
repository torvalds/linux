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
 *     Test multiple aggregations and overriding default order with
 *     printa() statements.
 *
 * SECTION: Aggregations/Aggregations;
 *     Aggregations/Output
 *
 * NOTES: This is a simple verifiable test.
 *
 */

#pragma D option quiet

BEGIN
{
	i = 0;
}

tick-10ms
/i < 1000/
{
	@a = count();
	@b = avg(i);
	@c = sum(i);
	@d = min(i);
	@e = max(i);
	@f = quantize(i);
	@g = lquantize(i, 0, 1000, 100);
	@h = stddev(i);

	i += 100;
}

tick-10ms
/i == 1000/
{
	printa("%@d\n", @h);
	printa("%@d\n", @g);
	printa("%@d\n", @f);
	printa("%@d\n", @e);
	printa("%@d\n", @d);
	printa("%@d\n", @c);
	printa("%@d\n", @b);
	printa("%@d\n", @a);

	exit(0);
}
