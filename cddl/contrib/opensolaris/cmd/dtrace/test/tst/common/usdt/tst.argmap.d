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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION: Verify that argN and args[N] variables are properly remapped.
 */

BEGIN
{
	/* Timeout after 5 seconds */
	timeout = timestamp + 5000000000;
}

test_prov$1:::place
/arg0 != 4 || arg1 != 10 || arg2 != 10 || arg3 != 4/
{
	printf("args are %d, %d, %d, %d; should be 4, 10, 10, 4",
	    arg0, arg1, arg2, arg3);
	exit(1);
}

test_prov$1:::place
/args[0] != 4 || args[1] != 10 || args[2] != 10 || args[3] != 4/
{
	printf("args are %d, %d, %d, %d; should be 4, 10, 10, 4",
	    args[0], args[1], args[2], args[3]);
	exit(1);
}

test_prov$1:::place
{
	exit(0);
}

profile:::tick-1
/timestamp > timeout/
{
	trace("test timed out");
	exit(1);
}
