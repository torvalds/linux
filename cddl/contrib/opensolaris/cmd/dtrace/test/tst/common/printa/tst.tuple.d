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
 *  Test the use of tuple arguments in the printa() format.
 *
 * SECTION: Output Formatting/printa()
 *
 * NOTES:
 *  We confirm that we can consume fewer arguments than in the tuple, all
 *  the way up to the exact number.
 */

#pragma D option quiet

BEGIN
{
	@a[1, 2, 3, 4, 5] = count();
	printf("\n");

	printa("%@u: -\n", @a);
	printa("%@u: %d\n", @a);
	printa("%@u: %d %d\n", @a);
	printa("%@u: %d %d %d\n", @a);
	printa("%@u: %d %d %d %d\n", @a);
	printa("%@u: %d %d %d %d %d\n", @a);

	exit(0);
}
