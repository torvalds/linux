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
 * 	printa() test with/without tuple, with multiple aggregations and
 * 	mismatched format strings, and sorting options.
 *
 * SECTION: Aggregations/Aggregations; Output Formatting, printa()
 *
 * NOTES: This test attempts to cover all multi-agg printa() corner cases.
 */

#pragma D option quiet

BEGIN
{
	@ = count();
	@a[1, 2] = sum(1);
	@b[1, 2] = sum(2);
	@a[1, 3] = sum(3);
	@b[2, 3] = sum(4);
	@c = sum(3);
	@d = sum(4);
	setopt("aggsortpos", "1");
	setopt("aggsortrev");
	printa(@);
	printa("count: %@d\n", @);
	printa(" ", @);
	printa(@a);
	printa(@a, @b);
	printa("%@d %@d\n", @c, @d);
	printa("%@d %@d\n", @c, @d);
	printa("%@d\n", @c, @d);

	printa("[%d, %d] %@d %@d\n", @a, @b);
	setopt("aggsortkey");
	printa("[%d, %d] %@d %@d\n", @a, @b);
	setopt("aggsortpos", "0");
	setopt("aggsortkeypos", "1");
	printa("[%d, %d] %@d %@d\n", @a, @b);

	printa("%@d %@d [%d, %d]\n", @a, @b);
	printa("[%d, %d]\n", @a, @b);
	printa("[%d]\n", @a, @b);
	printa("[%d] %@d %@d\n", @a, @b);
	printa("%@d %@d\n", @a, @b);
	printa("%@d %@d %@d\n", @a, @b);
	printa("[%d] %@d %@d %@d\n", @a, @b);
	printa("[%d, %d] %@d %@d %@d\n", @a, @b);
	exit(0);
}
