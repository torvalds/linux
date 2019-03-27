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
 * Test that there is no value of 'size' which can be passed to copyin
 * to cause mischief.  The somewhat odd order of operations ensures
 * that we test both size = 0 and size = 0xfff...fff
 */
#include <sys/types.h>


#if defined(_LP64)
#define MAX_BITS 63
size_t size;
#else
#define MAX_BITS 31
size_t size;
#endif

syscall:::
/pid == $pid/
{
	printf("size = 0x%lx\n", (ulong_t)size);
}

syscall:::
/pid == $pid/
{
	tracemem(copyin(curthread->t_procp->p_user.u_envp, size), 10);
}

syscall:::
/pid == $pid && size > (1 << MAX_BITS)/
{
	exit(0);
}

syscall:::
/pid == $pid/
{
	size = (size << 1ULL) | 1ULL;
}
