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
 *   If the switch rate is set properly, there should be no drops.
 *
 * SECTION: Buffers and Buffering/switch Policy;
 *	Options and Tunables/bufsize;
 *	Options and Tunables/switchrate
 */

/*
 * We rely on the fact that at least 8 bytes must be stored per iteration
 * (EPID plus data), but that no more than 40 bytes are stored per iteration.
 * We are going to let this run for ten seconds.  If the switch rate
 * is being set properly, there should be no drops.  Note that this test
 * (regrettably) may be scheduling sensitive -- but it should only fail on
 * the most pathological systems.
 */
#pragma D option bufsize=40
#pragma D option switchrate=10msec
#pragma D option quiet

int n;

tick-100msec
/n < 100/
{
	printf("%10d\n", n++);
}

tick-100msec
/n == 100/
{
	exit(0);
}
