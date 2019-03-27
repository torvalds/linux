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
 * ASSERTION:
 *
 * SECTION:
 *
 * NOTES:
 *
 */
BEGIN
{
	/* Timeout after 5 seconds */
	timeout = timestamp + 5000000000;
}

test_prov$1:::place
/arg0 == 10 && arg1 == 4/
{
	exit(0);
}

test_prov$1:::place
{
	printf("args are %d, %d; should be 10, 4", arg0, arg1);
	exit(1);
}

profile:::tick-1
/timestamp > timeout/
{
	trace("test timed out");
	exit(1);
}
