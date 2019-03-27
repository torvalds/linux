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
 *  Test the different kinds of associative scalar references.
 *
 * SECTION: Variables/Associative Arrays
 *
 * NOTES:
 *  In particular, we test accessing a DTrace associative array
 *  defined with scalar type (first ref that forces creation as both global
 *  and TLS), and DTrace associative array scalar subsequent references
 *  (both global and TLS).
 *
 */

#pragma D option quiet

BEGIN
{
	i = 0;
}

tick-10ms
/i != 5/
{
	x[123, "foo"] = 123;
	self->x[456, "bar"] = 456;
	i++;
}

tick-10ms
/i != 5/
{
	printf("x[] = %d\n", x[123, "foo"]);
	printf("self->x[] = %d\n", self->x[456, "bar"]);
}

tick-10ms
/i == 5/
{
	exit(0);
}
