// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Setup code for SAMv7x
 *
 *  Copyright (C) 2013 Atmel,
 *                2016 Andras Szemzo <szemzo.andras@gmail.com>
 */
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include "generic.h"

static const char *const samv7_dt_board_compat[] __initconst = {
	"atmel,samv7",
	NULL
};

DT_MACHINE_START(samv7_dt, "Atmel SAMV7")
	.dt_compat	= samv7_dt_board_compat,
MACHINE_END
