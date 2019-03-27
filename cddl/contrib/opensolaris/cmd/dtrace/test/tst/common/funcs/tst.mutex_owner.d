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
 *  	mutex_owner() should return a pointer to the kernel thread holding
 *	the mutex.
 *
 * SECTION: Actions and Subroutines/mutex_owner()
 *
 * NOTES: This assertion can't be verified so we'll just call it.
 */




#pragma D option quiet

struct thread *ptr;

BEGIN
{
	i = 0;
}

lockstat:::adaptive-acquire
{

	ptr = mutex_owner((struct mtx *)arg0);
	i++;
}

tick-1
/i > 5/
{
	exit(0);
}

