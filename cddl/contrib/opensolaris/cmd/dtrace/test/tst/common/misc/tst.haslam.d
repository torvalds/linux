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
 * ASSERTION: test for off-by-one error in the format lookup code
 *
 * SECTION: Aggregations/Aggregations; Misc
 */

/*
 * A script from Jon Haslam that induced an off-by-one error in the
 * format lookup code.
 */
BEGIN
{
	start = timestamp;
	allocd = 0;
	numallocs = 0;
	numfrees = 0;
	numtids = 0;
}

syscall:::entry
{
	@sys[tid] = sum(tid);
}

END
{
	printf("%s, %s, %s, %d numtids", "hhh", "jjj", "ggg", numtids );
	printa(@sys);
}

tick-1sec
/n++ == 5/
{
	exit(0);
}
