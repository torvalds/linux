/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2017 Mark Johnston <markj@FreeBSD.org>
 */

/*
 * A regression test for FreeBSD r322773. 100^9 fits in 64 bits, but
 * llquantize() will create buckets up to 100^{10}, which does not fit.
 */

BEGIN
{
	@ = llquantize(0, 100, 0, 9, 100);
	exit(0);
}
