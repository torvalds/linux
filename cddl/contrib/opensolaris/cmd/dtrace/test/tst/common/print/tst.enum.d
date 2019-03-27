/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

#pragma D option quiet

enum simpson {
	homer,
	marge,
	bart,
	lisa,
	maggie,
	snowball_ii,
	santas_little_helper
};

BEGIN
{
	print(bart);
	print((enum simpson)4);
	print(snowball_ii);
	exit(0);
}
