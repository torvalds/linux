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

#pragma D option quiet
#pragma D option destructive
#pragma D option dynvarsize=1m

struct bar {
	pid_t pid;
	struct thread *curthread;
};

self struct bar foo[int];

syscall:::entry
/!self->foo[0].pid/
{
	self->foo[0].pid = pid;
	self->foo[0].curthread = curthread;
}

syscall:::entry
/self->foo[0].pid != pid/
{
	printf("expected %d, found %d (found curthread %p, curthread is %p)\n",
	    pid, self->foo[0].pid, self->foo[0].curthread, curthread);
	exit(1);
}

tick-100hz
{
	system("date > /dev/null")
}

tick-1sec
/i++ == 10/
{
	exit(0);
}
