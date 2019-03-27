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
 * Copyright (c) 2016, Joyent, Inc. All rights reserved.
 */

#pragma D option strsize=16k

char *k;

BEGIN
{
	j = probeprov;
	k = j;
	k[0] = 'D';
	k[1] = 'T';
}

BEGIN
{
	trace(stringof(k));
	exit(k == "DTrace" ? 0 : 1);
}
