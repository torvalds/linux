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

int schedules;
int executes;

/*
 * This script is a bit naughty:  it's assuming the implementation of the
 * VM system's page scanning.  If this implementation changes -- either by
 * changing the function that scans pages or by making that scanning
 * multithreaded -- this script will break.
 */
fbt::timeout:entry
/args[0] == (void *)&genunix`schedpaging/
{
	schedules++;
}

fbt::schedpaging:entry
/executes == 10/
{
	printf("%d schedules, %d executes\n", schedules, executes);
	exit(executes == schedules ? 0 : 1);
}

fbt::schedpaging:entry
{
	executes++;
}

