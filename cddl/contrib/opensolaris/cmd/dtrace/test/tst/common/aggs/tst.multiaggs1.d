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
 *     Multiple aggregates can be used within the same D script.
 *
 * SECTION: Aggregations/Aggregations
 *
 */

#pragma D option quiet

BEGIN
{
	time_1 = timestamp;
	i = 0;
}

tick-10ms
/i <= 10/
{
	time_2 = timestamp;
	new_time = time_2 - time_1;
	@a[pid] = max(new_time);
	@b[pid] = min(new_time);
	@c[pid] = avg(new_time);
	@d[pid] = sum(new_time);
	@e[pid] = quantize(new_time);
	@f[pid] = stddev(new_time);
	@g[timestamp] = max(new_time);
	@h[timestamp] = quantize(new_time);
	@i[timestamp] = lquantize(new_time, 0, 10000, 1000);

	time_1 = time_2;
	i++;
}

tick-10ms
/i == 10/
{
	exit(0);
}
