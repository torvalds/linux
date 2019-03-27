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
 * ASSERTION: test that all 10 arguments are what we expect them to be.
 *
 * SECTION: pid provider
 */

#pragma D option destructive

BEGIN
{
	/*
	 * Wait no more than a second for the first call to getpid(2).
	 */
	timeout = timestamp + 1000000000;
}

syscall::getpid:return
/pid == $1/
{
	i = 0;
	raise(SIGUSR1);
	/*
	 * Wait half a second after raising the signal.
	 */
	timeout = timestamp + 500000000;
}

pid$1:a.out:go:entry
/arg0 == 0 && arg1 == 1 && arg2 == 2 && arg3 == 3 && arg4 == 4 &&
arg5 == 5 && arg6 == 6 && arg7 == 7 && arg8 == 8 && arg9 == 9/
{
	exit(0);
}

pid$1:a.out:go:entry
{
	printf("wrong args: %d %d %d %d %d %d %d %d %d %d", arg0, arg1, arg2,
	    arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	exit(1);
}

profile:::tick-4
/timestamp > timeout/
{
	trace("test timed out");
	exit(1);
}
