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
 * Copyright 2018 Domagoj Stolfa <domagoj.stolfa@cl.cam.ac.uk>.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION:
 * 	collect jailname at every fbt probe and at every firing of a
 *	high-frequency profile probe
 */

fbt:::
{
	@a[jailname] = count();
}

profile-4999hz
{
	@a[jailname] = count();
}

tick-1sec
/n++ == 10/
{
	exit(0);
}
