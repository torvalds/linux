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
 * Copyright (c) 2013 Joyent, Inc.  All rights reserved.
 */

#pragma D option encoding=ascii
#pragma D option quiet

tick-1ms
/i++ < 30/
{
	@[1] = lquantize(i, 0, 40, 1, 1000);
	@[2] = lquantize(i, 0, 40, 1, 1000);
	@[3] = lquantize(i, 0, 40, 1, 1000);
}

tick-1ms
/i == 40/
{
	@[1] = lquantize(0, 0, 40, 1, 1);
	@[1] = lquantize(i, 0, 40, 1, 2000);
	@[2] = lquantize(0, 0, 40, 1, 1);
	@[2] = lquantize(i, 0, 40, 1, 2000);
	@[3] = lquantize(0, 0, 40, 1, 1);
	@[3] = lquantize(i, 0, 40, 1, 2000);

	printa(@);
	setopt("aggpack");
	printa(@);
	setopt("aggzoom");
	printa(@);
	exit(0);
}
