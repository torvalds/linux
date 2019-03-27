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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION: Verify that argN (1..7) variables are properly remapped.
 */

BEGIN
{
	/* Timeout after 5 seconds */
	timeout = timestamp + 5000000000;
	ignore = $1;
}

ERROR
{
	printf("sdt:::test failed.\n");
	exit(1);
}

test:::sdttest
/arg0 != 1 || arg1 != 2 || arg2 != 3 || arg3 != 4 || arg4 != 5 || arg5 != 6 ||
    arg6 != 7/
{
	printf("sdt arg mismatch\n\n");
	printf("args are  : %d, %d, %d, %d, %d, %d, %d\n", arg0, arg1, arg2,
	    arg3, arg4, arg5, arg6);
	printf("should be : 1, 2, 3, 4, 5, 6, 7\n");
	exit(1);
}

test:::sdttest
{
	exit(0);
}

profile:::tick-1
/timestamp > timeout/
{
	trace("test timed out");
	exit(1);
}
