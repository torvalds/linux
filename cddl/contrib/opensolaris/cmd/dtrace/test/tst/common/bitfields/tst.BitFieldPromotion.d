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
 * ASSERTION: Bit-field will be automatically promoted to the next largest
 * integer type for use in any expression and then the value assigned will
 * warp around the maximum number assignable to the data type.
 *
 * SECTION: Structs and Unions/Bit-Fields
 */

#pragma D option quiet

struct bitRecord{
	int a : 1;
	int b : 15;
	int c : 31;
} var;

BEGIN
{
	var.a = 256;
	var.b = 65536;
	var.c = 4294967296;

	printf("bitRecord.a: %d\nbitRecord.b: %d\nbitRecord.c: %d\n",
	var.a, var.b, var.c);
	exit(0);
}

END
/(0 != var.a) || (0 != var.b) || (0 != var.c)/
{
	exit(1);
}
