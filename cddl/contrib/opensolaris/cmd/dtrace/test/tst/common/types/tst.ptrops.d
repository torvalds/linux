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
 *	Verify relational operators with pointers
 *
 * SECTION: Types, Operators, and Expressions/Relational Operators;
 *	Types, Operators, and Expressions/Logical Operators;
 *	Types, Operators, and Expressions/Precedence
 *
 */

#pragma D option quiet


BEGIN
{
	ptr_1 = &`kmem_flags;
	ptr_2 = (&`kmem_flags) + 1;
	ptr_3 = (&`kmem_flags) - 1 ;
}

tick-1
/ptr_1 >= ptr_2 || ptr_2 <= ptr_1 || ptr_1 == ptr_2/
{
	printf("Shouldn't end up here (1)\n");
	printf("ptr_1 = %x ptr_2 = %x ptr_3 = %x\n",
		(int) ptr_1, (int) ptr_2, (int) ptr_3);
	exit(1);
}

tick-1
/ptr_3 > ptr_1 || ptr_1 < ptr_3 || ptr_3 == ptr_1/
{
	printf("Shouldn't end up here (2)\n");
	printf("ptr_1 = %x ptr_2 = %x ptr_3 = %x\n",
		(int) ptr_1, (int) ptr_2, (int) ptr_3);
	exit(1);
}

tick-1
/ptr_3 > ptr_2 || ptr_1 < ptr_2 ^^ ptr_3 == ptr_2 && !(ptr_1 != ptr_2)/
{
	exit(0);
}
