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
 * ASSERTION: Cannot apply offsetof operator to a bit-field member.
 *
 * SECTION: Structs and Unions/Bit-Fields
 *
 */
#pragma D option quiet

struct bitRecord{
	int a : 17;
	int b : 3;
	int c : 12;
} var;

BEGIN
{
	printf("offsetof(struct bitRecord, a): %d\n",
	offsetof(struct bitRecord, a));
	printf("offsetof(struct bitRecord, b): %d\n",
	offsetof(struct bitRecord, b));
	printf("offsetof(struct bitRecord, c): %d\n",
	offsetof(struct bitRecord, c));

	exit(0);
}
