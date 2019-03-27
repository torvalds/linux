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
 *   We set our buffer size absurdly low to prevent a flood of errors that we
 *   don't care about.  We set our statusrate to be infinitely short to cause
 *   lots of activity by the DTrace process.
 *
 * SECTION: Actions and Subroutines/copyin();
 *	Options and Tunables/bufsize;
 *	Options and Tunables/bufpolicy;
 *	Options and Tunables/statusrate
 */


#pragma D option bufsize=32
#pragma D option bufpolicy=ring
#pragma D option statusrate=1nsec

syscall:::entry
{
	n++;
	trace(copyin(rand(), 1));
}

syscall:::entry
{
	trace(copyin(rand() | 1, 1));
}

syscall:::entry
{
	trace(copyin(NULL, 1));
}

dtrace:::ERROR
{
	err++;
}

tick-1sec
/sec++ == 10/
{
	exit(2);
}

END
/n == 0 || err == 0/
{
	exit(1);
}

END
/n != 0 && err != 0/
{
	exit(0);
}
