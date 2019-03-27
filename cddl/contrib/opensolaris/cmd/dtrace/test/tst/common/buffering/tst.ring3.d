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
 *	Options and Tunables/bufpolicy
 */

/*
 * We make some regrettable assumptions about the implementation in this test.
 * First, we assume that each entry for the printf() of an int takes _exactly_
 * eight bytes (four bytes for the EPID, four bytes for the payload).  Second,
 * we assume that by allocating storage for n + 1 records, we will get exactly
 * n.  Here is why:  the final predicate that evaluates to false will reserve
 * space that it won't use.  This act of reservation will advance the wrapped
 * offset.  That record won't be subsequently used, but the wrapped offset has
 * advanced.  (And in this case, that old record is clobbered by the exit()
 * anyway.)  Thirdly:  we rely on t_cpu/cpu_id.  Finally:  we rely on being
 * able to run on the CPU that we first ran on.
 */
#pragma D option bufpolicy=ring
#pragma D option bufsize=40
#pragma D option quiet

int n;

BEGIN
{
	cpuid = -1;
}

tick-10msec
/cpuid == -1/
{
	cpuid = curthread->t_cpu->cpu_id;
}

tick-10msec
/curthread->t_cpu->cpu_id == cpuid && n < 100/
{
	printf("%d\n", n++);
}

tick-10msec
/n == 100/
{
	exit(0);
}
