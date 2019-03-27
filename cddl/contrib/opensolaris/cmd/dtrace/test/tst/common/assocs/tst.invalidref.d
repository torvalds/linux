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
 * Test to ensure that invalid stores to a global associative array
 * are caught correctly.
 */

#pragma D option quiet

int last_cmds[int][4];

BEGIN
{
	errors = 0;
	forward = 0;
	backward = 0;
}

tick-1s
/!forward/
{
	forward = 1;
	last_cmds[1][4] = 0xdeadbeef;
}

tick-1s
/!backward/
{
	backward = 1;
	last_cmds[1][-5] = 0xdeadbeef;
}

tick-1s
/errors > 1/
{
	exit(0);
}

tick-1s
/n++ > 5/
{
	exit(1);
}

ERROR
/arg4 == DTRACEFLT_BADADDR/
{
	errors++;
}
