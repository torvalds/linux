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
 *   Positive test for switch buffer policy.
 *
 * SECTION: Buffers and Buffering/switch Policy;
 *	Buffers and Buffering/Buffer Sizes;
 *	Options and Tunables/bufsize;
 *	Options and Tunables/bufpolicy;
 *	Options and Tunables/switchrate
 */

/*
 * We assume that a printf() of an integer stores at least 8 bytes, and no more
 * than 16 bytes.
 */
#pragma D option bufpolicy=switch
#pragma D option bufsize=32
#pragma D option switchrate=500msec
#pragma D option quiet

int n;
int i;

tick-1sec
/n < 10/
{
	printf("%d\n", i);
	i++;
}

tick-1sec
/n < 10/
{
	n++;
}

tick-1sec
/n == 10/
{
	exit(0);
}
