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
 *	Verify increment/decrement operator using pointers
 *
 * SECTION: Types, Operators, and Expressions/Increment and Decrement Operators
 *
 */

#pragma D option quiet


BEGIN
{
	ptr_orig = &`kmem_flags;
	ptr_pos = &`kmem_flags+1;
	ptr_neg = &`kmem_flags-1;

	ptr_pos_before = ++ptr_orig;
	ptr_orig = &`kmem_flags;
	ptr_neg_before = --ptr_orig;

	ptr_orig = &`kmem_flags;
	ptr_pos_after = ptr_orig++;
	ptr_orig = &`kmem_flags;
	ptr_neg_after = ptr_orig--;
	ptr_orig = &`kmem_flags;

}

tick-1
/ptr_pos_before  == ptr_pos && ptr_neg_before == ptr_neg &&
	ptr_pos_after == ptr_orig && ptr_pos_after == ptr_orig/
{
	exit(0);
}


tick-1
/ptr_pos_before  != ptr_pos || ptr_neg_before != ptr_neg ||
	ptr_pos_after != ptr_orig || ptr_pos_after != ptr_orig/
{
	exit(1);
}

