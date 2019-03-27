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
 *	Verify integer type aliases
 *
 * SECTION: Types, Operators, and Expressions/Data Types and Sizes
 */

#pragma	ident	"%Z%%M%	%I%	%E% SMI"


BEGIN
{
	printf("sizeof (int8_t) = %u\n", sizeof (int8_t));
	printf("sizeof (int16_t) = %u\n", sizeof (int16_t));
	printf("sizeof (int32_t) = %u\n", sizeof (int32_t));
	printf("sizeof (int64_t) = %u\n", sizeof (int64_t));
	printf("sizeof (intptr_t) = %u\n", sizeof (intptr_t));
	printf("sizeof (uint8_t) = %u\n", sizeof (uint8_t));
	printf("sizeof (uint16_t) = %u\n", sizeof (uint16_t));
	printf("sizeof (uint32_t) = %u\n", sizeof (uint32_t));
	printf("sizeof (uint64_t) = %u\n", sizeof (uint64_t));
	printf("sizeof (uintptr_t) = %u\n", sizeof (uintptr_t));

	exit(0);
}

