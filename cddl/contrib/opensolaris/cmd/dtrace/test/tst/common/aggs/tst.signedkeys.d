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
 * 	Signed integer keys print and sort as expected.
 *
 * SECTION: Aggregations, Printing Aggregations
 *
 * NOTES: DTrace sorts integer keys as unsigned values, yet prints 32-
 * and 64-bit integers as signed values. Since the Java DTrace API is
 * expected to emulate this behavior, this test was added to ensure that
 * the behavior is preserved. Consistency with trace() output is also
 * tested.
 */

#pragma D option quiet
#pragma D option aggsortkey

BEGIN
{
	trace((char)-2);
	trace("\n");
	trace((char)-1);
	trace("\n");
	trace((char)0);
	trace("\n");
	trace((char)1);
	trace("\n");
	trace((char)2);
	trace("\n");
	trace("\n");

	trace((short)-2);
	trace("\n");
	trace((short)-1);
	trace("\n");
	trace((short)0);
	trace("\n");
	trace((short)1);
	trace("\n");
	trace((short)2);
	trace("\n");
	trace("\n");

	trace(-2);
	trace("\n");
	trace(-1);
	trace("\n");
	trace(0);
	trace("\n");
	trace(1);
	trace("\n");
	trace(2);
	trace("\n");
	trace("\n");

	trace((long long)-2);
	trace("\n");
	trace((long long)-1);
	trace("\n");
	trace((long long)0);
	trace("\n");
	trace((long long)1);
	trace("\n");
	trace((long long)2);
	trace("\n");

	@i8[(char)-2] = sum(-2);
	@i8[(char)-1] = sum(-1);
	@i8[(char)0] = sum(0);
	@i8[(char)1] = sum(1);
	@i8[(char)2] = sum(2);

	@i16[(short)-2] = sum(-2);
	@i16[(short)-1] = sum(-1);
	@i16[(short)0] = sum(0);
	@i16[(short)1] = sum(1);
	@i16[(short)2] = sum(2);

	@i32[-2] = sum(-2);
	@i32[-1] = sum(-1);
	@i32[0] = sum(0);
	@i32[1] = sum(1);
	@i32[2] = sum(2);

	@i64[(long long)-2] = sum(-2);
	@i64[(long long)-1] = sum(-1);
	@i64[(long long)0] = sum(0);
	@i64[(long long)1] = sum(1);
	@i64[(long long)2] = sum(2);

	exit(0);
}
