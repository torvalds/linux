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
 *     Test the basic formatting of all the supported kinds of aggregations.
 *
 * SECTION: Output Formatting/printa()
 *
 */

#pragma D option quiet

BEGIN
{
	@a = avg(1);
	@b = count();
	@c = lquantize(1, 1, 10);
	@d = max(1);
	@e = min(1);
	@f = sum(1);
	@g = quantize(1);
	@h = stddev(1);

	printa("@a = %@u\n", @a);
	printa("@b = %@u\n", @b);
	printa("@c = %@d\n", @c);
	printa("@d = %@u\n", @d);
	printa("@e = %@u\n", @e);
	printa("@f = %@u\n", @f);
	printa("@g = %@d\n", @g);
	printa("@h = %@d\n", @h);

	exit(0);
}
