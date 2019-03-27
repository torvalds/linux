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
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * ASSERTION:
 *   Positive test for fill buffer policy.
 *
 * SECTION: Buffers and Buffering/fill Policy;
 *	Buffers and Buffering/Buffer Sizes;
 *	Options and Tunables/bufsize;
 *	Options and Tunables/bufpolicy;
 *	Options and Tunables/statusrate
 */
/*
 * This is a brute-force way of testing fill buffers.  We assume that
 * each printf() stores 16 bytes (4x 32-bit words for EPID, timestamp
 * lo, timestamp hi, and the variable i).  Because each fill buffer is
 * per-CPU, we must fill up our buffer in one series of enablings on a
 * single CPU.
 */
#pragma D option bufpolicy=fill
#pragma D option bufsize=128
#pragma D option statusrate=10ms
#pragma D option quiet

int i;

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
{
	printf("%d\n", i++);
}

tick-10ms
/i >= 100/
{
	exit(1);
}
