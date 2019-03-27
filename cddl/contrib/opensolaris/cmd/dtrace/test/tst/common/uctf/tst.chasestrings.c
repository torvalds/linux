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
 * This test takes data from the current binary which is basically running in a
 * loop between two functions and our goal is to have two unique types that they
 * contain which we can print.
 */

#include <unistd.h>

typedef struct zelda_info {
	char	*zi_gamename;
	int	zi_ndungeons;
	char	*zi_villain;
	int	zi_haszelda;
} zelda_info_t;

static int
has_princess(zelda_info_t *z)
{
	return (z->zi_haszelda);
}

static int
has_dungeons(zelda_info_t *z)
{
	return (z->zi_ndungeons != 0);
}

static const char *
has_villain(zelda_info_t *z)
{
	return (z->zi_villain);
}

int
main(void)
{
	zelda_info_t oot;
	zelda_info_t la;
	zelda_info_t lttp;

	oot.zi_gamename = "Ocarina of Time";
	oot.zi_ndungeons = 10;
	oot.zi_villain = "Ganondorf";
	oot.zi_haszelda = 1;

	la.zi_gamename = "Link's Awakening";
	la.zi_ndungeons = 9;
	la.zi_villain = "Nightmare";
	la.zi_haszelda = 0;

	lttp.zi_gamename = "A Link to the Past";
	lttp.zi_ndungeons = 12;
	lttp.zi_villain = "Ganon";
	lttp.zi_haszelda = 1;

	for (;;) {
		(void) has_princess(&oot);
		(void) has_dungeons(&la);
		(void) has_villain(&lttp);
		sleep(1);
	}

	return (0);
}
