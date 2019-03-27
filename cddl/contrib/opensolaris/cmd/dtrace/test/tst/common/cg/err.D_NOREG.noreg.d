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
 * Compile some code that requires exactly 9 registers. This should run out
 * of registers.
 *
 * Changes to the code generator might cause this test to succeeed in which
 * case the code should be changed to another sequence that exhausts the
 * available internal registers.
 *
 * Note that this and err.baddif.d should be kept in sync.
 */

BEGIN
{
	a = 4;
	trace((a + a) * ((a + a) * ((a + a) * ((a + a) * ((a + a) *
	    ((a + a) * (a + a)))))));
}

BEGIN
{
	exit(0);
}
