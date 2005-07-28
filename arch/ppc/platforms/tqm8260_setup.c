/*
 * arch/ppc/platforms/tqm8260_setup.c
 *
 * TQM8260 platform support
 *
 * Author: Allen Curtis <acurtis@onz.com>
 * Derived from: m8260_setup.c by Dan Malek, MVista
 *
 * Copyright 2002 Ones and Zeros, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>

#include <asm/mpc8260.h>
#include <asm/cpm2.h>
#include <asm/machdep.h>

static int
tqm8260_set_rtc_time(unsigned long time)
{
	((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcnt = time;
	((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcntsc = 0x3;

	return(0);
}

static unsigned long
tqm8260_get_rtc_time(void)
{
	return ((cpm2_map_t *)CPM_MAP_ADDR)->im_sit.sit_tmcnt;
}

void __init
m82xx_board_init(void)
{
	/* Anything special for this platform */
	ppc_md.set_rtc_time	= tqm8260_set_rtc_time;
	ppc_md.get_rtc_time	= tqm8260_get_rtc_time;
}
