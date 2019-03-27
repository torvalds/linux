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
 *  Test the basics of all the format conversions in the printf dictionary.
 *
 * SECTION: Output Formatting/printf()
 *
 * NOTES:
 *  floats and wchar_t strings missing
 */

#pragma D option quiet

BEGIN
{
	i = (int)'a';

	printf("\n");

	printf("%%a = %a\n", &`malloc);
	printf("%%c = %c\n", i);
	printf("%%d = %d\n", i);
	printf("%%hd = %hd\n", (short)i);
	printf("%%hi = %hi\n", (short)i);
	printf("%%ho = %ho\n", (ushort_t)i);
	printf("%%hu = %hu\n", (ushort_t)i);
	printf("%%hx = %hx\n", (ushort_t)i);
	printf("%%hX = %hX\n", (ushort_t)i);
	printf("%%i = %i\n", i);
	printf("%%lc = %lc\n", i);
	printf("%%ld = %ld\n", (long)i);
	printf("%%li = %li\n", (long)i);
	printf("%%lo = %lo\n", (ulong_t)i);
	printf("%%lu = %lu\n", (ulong_t)i);
	printf("%%lx = %lx\n", (ulong_t)i);
	printf("%%lX = %lX\n", (ulong_t)i);
	printf("%%o = %o\n", (uint_t)i);
	printf("%%p = %p\n", (void *)i);
	printf("%%s = %s\n", "hello");
	printf("%%u = %u\n", (uint_t)i);
	printf("%%wc = %wc\n", i);
	printf("%%x = %x\n", (uint_t)i);
	printf("%%X = %X\n", (uint_t)i);

	exit(0);
}
