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
 * Check %d v. %i v. %u.
 */

#pragma D option quiet

uint16_t x;
int16_t y;

BEGIN
{
	x = 0xffffffff;
	y = 0xffffffff;

	printf("%d %i %u\n", x, x, x);
	printf("%d %i %u\n", y, y, y);

	exit(0);
}
