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
 * 	Positive test for stop
 *
 * SECTION: Actions and Subroutines/stop()
 */

#pragma D option destructive

BEGIN
{
	/*
	 * Wait no more than a second for the process to call getpid().
	 */
	timeout = timestamp + 1000000000;
	stopped = 0;
}

syscall::getpid:entry
/pid == $1 && stopped == 1/
{
	trace("looks like it's still going");
	exit(1);
}

syscall::getpid:entry
/pid == $1 && stopped == 0/
{
	stop();
	trace("stopped");
	stopped = 1;
	/*
	 * Wait for a quarter second before declaring victory.
	 */
	timeout = timestamp + 1000000000 / 4;
}

profile:::tick-8
/timestamp > timeout && stopped == 1/
{
	trace("looks like it really stopped");
	exit(0);
}

profile:::tick-8
/timestamp > timeout/
{
	trace("timed out");
	exit(1);
}
