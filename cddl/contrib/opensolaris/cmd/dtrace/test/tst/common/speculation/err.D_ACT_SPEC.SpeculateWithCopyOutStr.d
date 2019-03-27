#!/usr/sbin/dtrace -ws
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
 * Destructive actions may never be speculative.
 *
 * SECTION: Speculative Tracing/Using a Speculation
 * SECTION: dtrace(1M) Utility/ -w option
 */
#pragma D option quiet

string str;
char a[2];
uintptr_t addr;
size_t maxlen;
BEGIN
{
	self->i = 0;
	addr = (uintptr_t) &a[0];
	maxlen = 10;
	var = speculation();
}

BEGIN
{
	speculate(var);
	printf("Speculation ID: %d", var);
	self->i++;
	copyoutstr(str, addr, maxlen);
}

BEGIN
{
	printf("This test should not have compiled\n");
	exit(0);
}

ERROR
{
	exit(0);
}
