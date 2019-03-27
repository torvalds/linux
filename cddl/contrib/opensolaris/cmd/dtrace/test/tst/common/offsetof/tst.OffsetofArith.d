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
 * ASSERTION: offsetof can be used anywhere in a D program that an integer
 * constant can be used.
 *
 * SECTION: Structs and Unions/Member Sizes and Offsets
 *
 * NOTES:
 *
 */

#pragma D option quiet

typedef struct record {
	char c;
	int x;
	int y;
} record_t;

BEGIN
{

	add = offsetof(record_t, c) + offsetof(record_t, x) +
	    offsetof(record_t, y);
	sub = offsetof(record_t, y) - offsetof(record_t, x);
	mul = offsetof(record_t, x) * offsetof(record_t, c);
	div = offsetof(record_t, y) / offsetof(record_t, x);

	printf("offsetof(record_t, c) = %d\n", offsetof(record_t, c));
	printf("offsetof(record_t, x) = %d\n", offsetof(record_t, x));
	printf("offsetof(record_t, y) = %d\n", offsetof(record_t, y));

	printf("Addition of offsets (c+x+y)= %d\n", add);
	printf("Subtraction of offsets (y-x)= %d\n", sub);
	printf("Multiplication of offsets (x*c) = %d\n", mul);
	printf("Division of offsets (y/x) = %d\n", div);

	exit(0);
}

END
/(8 != offsetof(record_t, y)) || (4 != offsetof(record_t, x)) ||
    (0 != offsetof(record_t, c)) || (12 != add)  || (4 != sub) || (0 != mul)
    || (2 != div)/
{
	exit(1);
}
