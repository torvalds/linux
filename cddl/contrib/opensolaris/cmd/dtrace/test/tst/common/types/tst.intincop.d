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
 *	Verify increment operator using integers
 *
 * SECTION: Type and Constant Definitions/Enumerations
 *
 */

#pragma D option quiet


BEGIN
{
	int_orig = 100;
	int_pos = 100+1;
	int_neg = 100-1;

	int_pos_before = ++int_orig;
	int_orig = 100;
	int_neg_before = --int_orig;
	int_orig = 100;
	int_pos_after = int_orig++;
	int_orig = 100;
	int_neg_after = int_orig--;
	int_orig = 100;

}

tick-1
/int_pos_before  == int_pos && int_neg_before == int_neg &&
	int_pos_after == int_orig && int_pos_after == int_orig/
{
	exit(0);
}


tick-1
/int_pos_before  != int_pos || int_neg_before != int_neg ||
	int_pos_after != int_orig || int_pos_after != int_orig/
{
	exit(1);
}
