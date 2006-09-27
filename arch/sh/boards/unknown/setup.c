/*
 * linux/arch/sh/boards/unknown/setup.c
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Setup code for an unknown machine (internal peripherials only)
 *
 * This is the simplest of all boards, and serves only as a quick and dirty
 * method to start debugging a new board during bring-up until proper board
 * setup code is written.
 */
#include <linux/init.h>
#include <asm/machvec.h>

struct sh_machine_vector mv_unknown __initmv = {
	.mv_name		= "Unknown",
};
ALIAS_MV(unknown)
