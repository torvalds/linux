#!/usr/bin/env ksh -p

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
 *
 * ASSERTION:
 * Testing -F option with several probes.
 *
 * SECTION: dtrace Utility/-F Option
 *
 * NOTES: Manually verify using:
 * "/usr/sbin/dtrace -F -s man.CoalesceTrace.d" on command line.
 *
 * Verify that the for the indent characters are -> <- for non-syscall
 * entry/return pairs (e.g. fbt ones) and => <= for syscall ones and
 * | for profile ones.
 *
 */

BEGIN
{
	i = 0;
	j = 0;
	k = 0;
}

syscall::read:
{
	printf("syscall: %d\n", i++);
}

fbt:genunix:read:
{
	printf("fbt: %d\n", j++);
}

profile:::tick-10sec
{
	printf("profile: %d\n", k++);
}
