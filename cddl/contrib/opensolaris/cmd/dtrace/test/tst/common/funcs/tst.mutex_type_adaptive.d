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
 *  	mutex_type_adaptive() should return a non-zero value if the
 *	mutex is an adaptive one.
 *
 * SECTION: Actions and Subroutines/mutex_type_adaptive()
 */


#pragma D option quiet

BEGIN
{
	i = 0;
	ret = -99;
}

lockstat:::adaptive-acquire
{
	ret = mutex_type_adaptive((struct mtx *)arg0);
	i++;
}

tick-1
/ret == 1/
{
	exit(0);
}

tick-1
/i == 100 && ret == 0/
{
	printf("mutex_type_adaptive returned 0, expected non-zero\n");
	exit(1);
}

tick-1
/i == 100 && ret == -99/
{
	printf("No adaptive_mutexs called in the time this test was run.\n");
	exit(1);
}
