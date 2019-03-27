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

#pragma D option aggpack
#pragma D option encoding=ascii
#pragma D option quiet

BEGIN
{
	@x = quantize(1 << 32);
	@y[1] = quantize(1);
	@z["mumble"] = quantize(1);
	@xx["foo", (char)1, (short)2, (long)3] = quantize(1);

	@neg = lquantize(-10, -10, 20, 1, -1);
	@neg = lquantize(-5, -10, 20, 1, 1);
	@neg = lquantize(0, -10, 20, 1, 1);

	i = 0;
}

tick-1ms
{
	@a[i] = quantize(0, i);
	@a[i] = quantize(1, 100 - i);
	i++;
}

tick-1ms
/i > 100/
{
	exit(0);
}

END
{
	setopt("aggzoom", "true");
	printa(@neg);
	setopt("aggzoom", "false");
	printa(@neg);
}
