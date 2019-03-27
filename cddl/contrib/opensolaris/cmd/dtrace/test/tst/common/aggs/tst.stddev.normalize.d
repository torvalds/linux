/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2017 Panzura.  All rights reserved.
 */

/*
 * ASSERTION:
 *   Positive test for normalization() of stddev()
 *
 * SECTION: Aggregations/Normalization
 *
 */

#pragma D option quiet
#pragma D option aggrate=1ms
#pragma D option switchrate=50ms

BEGIN
{
	i = 0;
}

tick-100ms
/i < 11/
{
	@ = stddev(i * 100);
	i++;
}

tick-100ms
/i == 11/
{
	printf("normalized data:\n");
	normalize(@, 10);
	exit(0);
}
