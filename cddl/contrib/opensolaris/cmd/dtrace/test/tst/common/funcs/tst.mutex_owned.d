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
 *  	mutex_owned() should return a non-zero value if the calling
 *	thread currently holds the mutex.
 *
 * SECTION: Actions and Subroutines/mutex_owned()
 */

#pragma D option quiet

lockstat:::adaptive-acquire
{
	this->owned = mutex_owned((struct mtx *)arg0);
	this->owner = mutex_owner((struct mtx *)arg0);
}

lockstat:::adaptive-acquire
/!this->owned/
{
	printf("mutex_owned() returned 0, expected non-zero\n");
	exit(1);
}

lockstat:::adaptive-acquire
/this->owner != curthread/
{
	printf("current thread is not current owner of owned lock\n");
	exit(1);
}

lockstat:::adaptive-acquire
{
	exit(0);
}
