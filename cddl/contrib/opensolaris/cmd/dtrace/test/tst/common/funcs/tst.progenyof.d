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
 *	progenyof() should return non-zero if the pid passed is in the
 #	progeny of the calling process.
 *
 * SECTION: Actions and Subroutines/progenyof()
 *
 */

#pragma D option quiet


BEGIN
{
	res_1 = -1;
	res_2 = -1;
	res_3 = -1;

	res_1 = progenyof($ppid);	/* this will always be true */
	res_2  = progenyof($ppid + 1);  /* this will always be false */
	res_3 = progenyof(1);		/* this will always be true */
}


tick-1
/res_1 > 0 && res_2 == 0 && res_3 > 0/
{
	exit(0);
}

tick-1
/res_1 <= 0 || res_2 != 0 || res_3 <= 0/
{
	exit(1);
}
