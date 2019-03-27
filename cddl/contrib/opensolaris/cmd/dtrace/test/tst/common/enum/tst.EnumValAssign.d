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

/*
 * ASSERTION:
 * Test the D enumerations with and without initilialization of the identifiers.
 * Also test for values with negative integer assignments, expressions and
 * fractions.
 *
 * SECTION: Type and Constant Definitions/Enumerations
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet

enum colors {
	RED,
	ORANGE = 5 + 5,
	YELLOW = 2,
	GREEN,
	BLUE = GREEN + ORANGE,
 	PINK = 5/4,
	INDIGO = -2,
	VIOLET
};

profile:::tick-1sec
/(0 == RED) && (10 == ORANGE) && (2 == YELLOW) && (3 == GREEN) &&
    (13 == BLUE) && (1 == PINK) && (-2 == INDIGO) && (-1 == VIOLET)/
{
	exit(0);
}
