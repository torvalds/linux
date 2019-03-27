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
 * ASSERTION: test for assertion failure in the ring buffer code
 *
 * SECTION: Buffers and Buffering/ring Policy; Misc
 */

#pragma ident	"@(#)tst.roch.d	1.2	03/08/11 SMI"

/*
 * A script from Roch Bourbonnais that induced an assertion failure in the
 * ring buffer code.
 */
#pragma D option strsize=16
#pragma D option bufsize=10K
#pragma D option bufpolicy=ring

fbt:::entry
/(self->done == 0) && (curthread->t_cpu->cpu_intr_actv == 0) /
{
	self->done = 1;
	printf(" %u 0x%llX %d %d comm:%s csathr:%lld", timestamp,
	    (long long)curthread, pid, tid,
	    execname, (long long)stackdepth);
	stack(20);
}

fbt:::return
/(self->done == 0) && (curthread->t_cpu->cpu_intr_actv == 0) /
{
	self->done = 1;
	printf(" %u 0x%llX %d %d comm:%s csathr:%lld", timestamp,
	    (long long) curthread, pid, tid,
	    execname, (long long) stackdepth);
	stack(20);
}

fbt:::entry
{
	printf(" %u 0x%llX %d %d ", timestamp,
	    (long long)curthread, pid, tid);
}

fbt:::return
{
	printf(" %u 0x%llX %d %d tag:%d off:%d ", timestamp,
	    (long long)curthread, pid, tid, (int)arg1, (int)arg0);
}

mtx_lock:adaptive-acquire
{
	printf(" %u 0x%llX %d %d lock:0x%llX", timestamp,
	    (long long)curthread, pid, tid, arg0);
}

mtx_unlock:adaptive-release
{
	printf(" %u 0x%llX %d %d lock:0x%llX", timestamp,
	    (long long) curthread, pid, tid, arg0);
}

tick-1sec
/n++ == 10/
{
	exit(0);
}
