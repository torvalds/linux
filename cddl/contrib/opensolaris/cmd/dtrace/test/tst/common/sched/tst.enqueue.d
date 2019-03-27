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

#pragma D option switchrate=100hz
#pragma D option destructive

sched:::enqueue
/pid == 0 && args[1]->pr_pid == $pid/
{
	self->one = 1;
}

sched:::enqueue
/self->one && args[2]->cpu_id >= 0 && args[2]->cpu_id <= `max_cpuid/
{
	self->two = 1;
}

sched:::enqueue
/self->two && args[0]->pr_lwpid > 0/
{
	exit(0);
}
