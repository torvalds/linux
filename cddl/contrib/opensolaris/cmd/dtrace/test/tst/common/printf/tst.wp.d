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
 *  Test a variety of fixed and dynamic format widths combined with precisions.
 *
 * SECTION: Output Formatting/printf()
 */

#pragma D option quiet

BEGIN
{
	printf("\n");
	x = 0;

	printf("%0.0s\n", "hello");
	printf("%1.1s\n", "hello");
	printf("%2.2s\n", "hello");
	printf("%3.3s\n", "hello");
	printf("%4.4s\n", "hello");
	printf("%5.5s\n", "hello");
	printf("%6.6s\n", "hello");
	printf("%7.7s\n", "hello");
	printf("%8.8s\n", "hello");
	printf("%9.9s\n", "hello");

	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");
	printf("%*.*s\n", x, x++, "hello");

	exit(0);
}
