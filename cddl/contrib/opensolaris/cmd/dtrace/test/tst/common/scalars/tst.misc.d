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

#pragma	ident	"%Z%%M%	%I%	%E% SMI" 

/*
 * ASSERTION:
 *  Test the different kinds of integer scalar references.  In particular, we
 *  test accessing a kernel executable scalar, kernel scoped scalar, DTrace
 *  scalar first ref that forces creation (both global and TLS), and DTrace
 *  scalar subsequent reference (both global and TLS).
 *
 * SECTION:  Variables/External Variables
 *
 */

BEGIN
{
	printf("\nkmem_flags = 0x%x\n", `kmem_flags);
	printf("ufs`ufs_allow_shared_writes = %d\n",
	    ufs`ufs_allow_shared_writes);
	x = 123;
	printf("x = %u\n", x);
	self->x = 456;
	printf("self->x = %u\n", self->x);
	exit(0);
}
