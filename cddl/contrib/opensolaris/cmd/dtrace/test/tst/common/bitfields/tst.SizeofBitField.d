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
 * ASSERTION: C and D compilers try to pack bits as efficiently as possible.
 *
 * SECTION: Structs and Unions/Bit-Fields
 */

#pragma D option quiet

struct bitRecord1 {
	int a : 1;
} var1;

struct bitRecord2 {
	int a : 1;
	int b : 3;
} var2;

struct bitRecord3 {
	int a : 1;
	int b : 3;
	int c : 3;
} var3;

struct bitRecord4 {
	int a : 1;
	int b : 3;
	int c : 3;
	int d : 3;
} var4;

struct bitRecord5 {
	int c : 12;
	int a : 10;
	int b : 3;
} var5;

struct bitRecord6 {
	int a : 20;
	int b : 3;
	int c : 12;
} var6;

struct bitRecord7 {
	long c : 32;
	long long d: 9;
	int e: 1;
} var7;

struct bitRecord8 {
	char a : 2;
	short b : 12;
	long c : 32;
} var8;

struct bitRecord12 {
	int a : 30;
	int b : 30;
	int c : 32;
} var12;

BEGIN
{
	printf("sizeof (bitRecord1): %d\n", sizeof (var1));
	printf("sizeof (bitRecord2): %d\n", sizeof (var2));
	printf("sizeof (bitRecord3): %d\n", sizeof (var3));
	printf("sizeof (bitRecord4): %d\n", sizeof (var4));
	printf("sizeof (bitRecord5): %d\n", sizeof (var5));
	printf("sizeof (bitRecord6): %d\n", sizeof (var6));
	printf("sizeof (bitRecord7): %d\n", sizeof (var7));
	printf("sizeof (bitRecord8): %d\n", sizeof (var8));
	printf("sizeof (bitRecord12): %d\n", sizeof (var12));
	exit(0);
}

END
/(1 != sizeof (var1)) || (2 != sizeof (var2)) || (3 != sizeof (var3)) ||
    (4 != sizeof (var4)) || (5 != sizeof (var5)) || (6 != sizeof (var6))
    || (7 != sizeof (var7)) || (8 != sizeof (var8)) || (12 != sizeof (var12))/
{
	exit(1);
}
