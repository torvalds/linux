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
 * Copyright 2012, Joyent, Inc.  All rights reserved.
 */

/*
 * ASSERTION:
 *   json() run time must be bounded above by strsize.  This test makes strsize
 *   small and deliberately overflows it to prove we bail and return NULL in
 *   the event that we run off the end of the string.
 *
 */

#pragma D option quiet
#pragma D option strsize=18

BEGIN
{
	in = "{\"a\":         1024}"; /* length == 19 */
	out = json(in, "a");
	printf("|%s|\n%s\n\n", in, out != NULL ? out : "<NULL>");

	in = "{\"a\": 1024}"; /* length == 11 */
	out = json(in, "a");
	printf("|%s|\n%s\n\n", in, out != NULL ? out : "<NULL>");

	in = "{\"a\":false,\"b\":true}"; /* length == 20 */
	out = json(in, "b");
	printf("|%s|\n%s\n\n", in, out != NULL ? out : "<NULL>");

	in = "{\"a\":false,\"b\":20}"; /* length == 18 */
	out = json(in, "b");
	printf("|%s|\n%s\n\n", in, out != NULL ? out : "<NULL>");

	exit(0);
}

ERROR
{
	exit(1);
}
