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
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

/*
 * ASSERTION:
 *   The smallest base we will accept is Base 2.
 *
 * SECTION: Actions and Subroutines/strtoll()
 */

#pragma D option quiet

BEGIN
{
	printf("%d\n", strtoll("0", 1));
	exit(0);
}

ERROR
{
	exit(1);
}
