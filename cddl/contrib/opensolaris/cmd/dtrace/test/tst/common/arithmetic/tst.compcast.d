/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * Test compile-time casting between integer types of different size.
 */

#pragma D option quiet

int64_t x;

BEGIN
{
	x = (int32_t)(int16_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	x = (int32_t)(uint16_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	x = (uint32_t)(int16_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	x = (uint32_t)(uint16_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	printf("\n");

	x = (int16_t)(int32_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	x = (int16_t)(uint32_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	x = (uint16_t)(int32_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);
	x = (uint16_t)(uint32_t)0xfff0;
	printf("%16x %20d %20u\n", x, x, x);

	exit(0);
}
