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
 *   Positive test for ring buffer policy.
 *
 * SECTION: Buffers and Buffering/ring Policy;
 *	Buffers and Buffering/Buffer Sizes;
 *	Options and Tunables/bufsize;
 *	Options and Tunables/bufpolicy;
 *	Options and Tunables/switchrate
 */

/*
 * We assume that a trace() of an integer stores at least 8 bytes.  If ring
 * buffering is not working properly, this trace() will induce a drop, and the
 * counter won't be incremented.  We set the switchrate to one second just to
 * sure that a high switchrate doesn't mask broken ring buffers.
 */
#pragma D option bufpolicy=ring
#pragma D option bufsize=50
#pragma D option switchrate=1sec

int n;
int i;

tick-10msec
/n < 300/
{
	trace(i);
	i++;
}

tick-10msec
/n < 300/
{
	n++;
}

tick-10msec
/n == 300/
{
	exit(2);
}

END
{
	exit(i == 300 ? 0 : 1);
}
