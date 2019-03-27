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

#pragma D option quiet

typedef struct pancakes {
	int i;
	string s;
	struct timespec t;
} pancakes_t;

translator pancakes_t < void *V > {
	i = 2 * 10;
	s = strjoin("I like ", "pancakes");
	t = *(struct timespec *)`dtrace_zero;
};

BEGIN
{
	print(*(xlate < pancakes_t * > ((void *)NULL)));
}

BEGIN
{
	exit(0);
}
