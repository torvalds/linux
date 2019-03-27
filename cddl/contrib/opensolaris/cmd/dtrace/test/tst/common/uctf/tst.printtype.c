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
 * The point of this is to use print() on various functions to make sure that we
 * can print basic structures. Note that we purposefully are making sure that
 * there are no pointers here.
 */
#include <unistd.h>

typedef struct final_fantasy_info {
	int ff_gameid;
	int ff_partysize;
	int ff_hassummons;
} final_fantasy_info_t;

static int
ff_getgameid(final_fantasy_info_t *f)
{
	return (0);
}

static int
ff_getpartysize(final_fantasy_info_t *f)
{
	return (0);
}

static int
ff_getsummons(final_fantasy_info_t *f)
{
	return (0);
}

int
main(void)
{
	final_fantasy_info_t ffiii, ffx, ffi;

	ffi.ff_gameid = 1;
	ffi.ff_partysize = 4;
	ffi.ff_hassummons = 0;

	ffiii.ff_gameid = 6;
	ffiii.ff_partysize = 4;
	ffiii.ff_hassummons = 1;

	ffx.ff_gameid = 10;
	ffx.ff_partysize = 3;
	ffx.ff_hassummons = 1;

	for (;;) {
		ff_getgameid(&ffi);
		ff_getpartysize(&ffx);
		ff_getsummons(&ffiii);
		sleep(1);
	}
	/*NOTREACHED*/
	return (0);
}
