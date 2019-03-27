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
 *  Test printf() with simple integer arguments, using a variety of
 *  sizes and thereby exercise the automatic size extension feature.
 *
 * SECTION: Output Formatting/printf()
 *
 */

#pragma D option quiet

BEGIN
{
	printf("\n%d\n", (char)0x1234567890abcdef);
	printf("%d\n", (short)0x1234567890abcdef);
	printf("%d\n", (int)0x1234567890abcdef);
	printf("%d\n", (long long)0x1234567890abcdef);

	printf("\n%d\n", (unsigned char)0x1234567890abcdef);
	printf("%d\n", (unsigned short)0x1234567890abcdef);
	printf("%d\n", (unsigned int)0x1234567890abcdef);
	printf("%d\n", (unsigned long long)0x1234567890abcdef);

	printf("\n%u\n", (char)0x1234567890abcdef);
	printf("%u\n", (short)0x1234567890abcdef);
	printf("%u\n", (int)0x1234567890abcdef);
	printf("%u\n", (long long)0x1234567890abcdef);

	printf("\n%u\n", (unsigned char)0x1234567890abcdef);
	printf("%u\n", (unsigned short)0x1234567890abcdef);
	printf("%u\n", (unsigned int)0x1234567890abcdef);
	printf("%u\n", (unsigned long long)0x1234567890abcdef);

	printf("\n%x\n", (unsigned char)0x1234567890abcdef);
	printf("%x\n", (unsigned short)0x1234567890abcdef);
	printf("%x\n", (unsigned int)0x1234567890abcdef);
	printf("%x\n", (unsigned long long)0x1234567890abcdef);

	printf("\n%x\n", (char)0x1234567890abcdef);
	printf("%x\n", (short)0x1234567890abcdef);
	printf("%x\n", (int)0x1234567890abcdef);
	printf("%x\n", (long long)0x1234567890abcdef);

	printf("\n%o\n", (unsigned char)0x1234567890abcdef);
	printf("%o\n", (unsigned short)0x1234567890abcdef);
	printf("%o\n", (unsigned int)0x1234567890abcdef);
	printf("%o\n", (unsigned long long)0x1234567890abcdef);

	printf("\n%o\n", (char)0x1234567890abcdef);
	printf("%o\n", (short)0x1234567890abcdef);
	printf("%o\n", (int)0x1234567890abcdef);
	printf("%o\n", (long long)0x1234567890abcdef);

	printf("\n%p\n", (void *)0x12345678);
	printf("%p\n", (int *)0x90abcdef);
	printf("%p\n", (uintptr_t)0x67890abc);

	exit(0);
}
