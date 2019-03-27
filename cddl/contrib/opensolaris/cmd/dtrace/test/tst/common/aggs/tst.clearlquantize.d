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


/*
 * ASSERTION:
 * 	Positive lquantize()/clear() test
 *
 * SECTION: Aggregations/Aggregations
 *
 * NOTES:
 *	Verifies that printing a clear()'d aggregation with an lquantize()
 *	aggregation function doesn't cause problems.
 *
 */


#pragma D option switchrate=50ms
#pragma D option aggrate=1ms
#pragma D option quiet

tick-100ms
{
	x++;
	@a["linear"] = lquantize(x, 0, 100, 1);
	@b["exp"] = quantize(x);
}

tick-100ms
/(x % 5) == 0 && y++ < 5/
{
	printa(@a);
	printa(@b);
	clear(@a);
	clear(@b);
}

tick-100ms
/(x % 5) == 0 && y == 5/
{
	exit(0);
}
