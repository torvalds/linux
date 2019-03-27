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
 * Copyright 2013 (c) Joyent, Inc. All rights reserved.
 */

/*
 * This test tries to make sure that we have CTF data for a type that only this
 * binary would reasonably have. In this case, the
 * season_7_lisa_the_vegetarian_t.
 */
#include <unistd.h>

typedef struct season_7_lisa_the_vegetarian {
	int fr_salad;
} season_7_lisa_the_vegetarian_t;

int
sleeper(season_7_lisa_the_vegetarian_t *lp)
{
	for (;;) {
		sleep(lp->fr_salad);
	}
	/*NOTREACHED*/
	return (0);
}

int
main(void)
{
	season_7_lisa_the_vegetarian_t l;
	l.fr_salad = 100;

	sleeper(&l);

	return (0);
}
