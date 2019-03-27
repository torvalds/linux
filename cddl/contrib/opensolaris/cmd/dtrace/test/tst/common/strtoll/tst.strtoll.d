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
 *   Test the strtoll() subroutine.
 *
 * SECTION: Actions and Subroutines/strtoll()
 */

#pragma D option quiet

BEGIN
{

	/* minimum base (2) and maximum base (36): */
	printf("%d\n", strtoll("0", 2));
	printf("%d\n", strtoll("1", 36));

	/* simple tests: */
	printf("%d\n", strtoll("0x20", 16));
	printf("%d\n", strtoll("-32", 10));
	printf("%d\n", strtoll("010", 8));
	printf("%d\n", strtoll("101010", 2));

	/* INT64_MIN and INT64_MAX: */
	printf("%d\n", strtoll("9223372036854775807"));
	printf("%d\n", strtoll("-9223372036854775808"));
	printf("%d\n", strtoll("0777777777777777777777", 8));
	printf("%d\n", strtoll("-01000000000000000000000", 8));

	/* wrapping: */
	printf("%d\n", strtoll("1000000000000000000000", 8));
	printf("%d\n", strtoll("-1000000000000000000001", 8));

	/* hex without prefix: */
	printf("%d\n", strtoll("baddcafe", 16));

	/* stopping at first out-of-base character: */
	printf("%d\n", strtoll("12j", 10));
	printf("%d\n", strtoll("102", 2));

	/* base 36: */
	printf("%d\n", strtoll("-0DTrace4EverZ", 36));

	/* base 10 is assumed: */
	printf("%d\n", strtoll("1985"));
	printf("%d\n", strtoll("-2012"));

	/* empty string: */
	printf("%d\n", strtoll(""));

	exit(0);
}
