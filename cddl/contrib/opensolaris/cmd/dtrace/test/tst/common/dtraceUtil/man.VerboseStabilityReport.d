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
 * Using -v option with dtrace utility produces a program stability report
 * showing the minimum interface stability and dependency level for
 * the specified D programs.
 *
 * SECTION: dtrace Utility/-s Option;
 * 	dtrace Utility/-v Option
 *
 * NOTES: Use this file as
 * /usr/sbin/dtrace -vs man.VerboseStabilityReport.d
 *
 */

BEGIN
{
	printf("This test should compile: %d\n", 2);
	exit(0);
}
