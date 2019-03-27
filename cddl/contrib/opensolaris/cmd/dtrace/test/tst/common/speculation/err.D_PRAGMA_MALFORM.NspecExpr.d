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
 * ASSERTION:
 * Using an expression in the pragma for nspec throws a D_PRAGMA_MALFORM
 * error.
 *
 * SECTION: Speculative Tracing/Options and Tuning;
 *		Options and Tunables/nspec
 *
 */

#pragma D option quiet
#pragma D option cleanrate=3000hz
#pragma D option nspec=24 * 44

BEGIN
{
	var1 = 0;
	var2 = 0;
	var3 = 0;
}

BEGIN
{
	var1 = speculation();
	printf("Speculation ID: %d\n", var1);
	var2 = speculation();
	printf("Speculation ID: %d\n", var2);
	var3 = speculation();
	printf("Speculation ID: %d\n", var3);
}

BEGIN
/var1 && var2 && (!var3)/
{
	printf("Succesfully got two speculative buffers");
	exit(0);
}

BEGIN
/(!var1) || (!var2) || var3/
{
	printf("Test failed");
	exit(1);
}

ERROR
{
	exit(1);
}
