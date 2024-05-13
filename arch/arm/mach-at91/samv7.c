// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Setup code for SAMv7x
 *
 *  Copyright (C) 2013 Atmel,
 *                2016 Andras Szemzo <szemzo.andras@gmail.com>
 */
#include <asm/mach/arch.h>

static const char *const samv7_dt_board_compat[] __initconst = {
	"atmel,samv7",
	NULL
};

DT_MACHINE_START(samv7_dt, "Atmel SAMV7")
	.dt_compat	= samv7_dt_board_compat,
MACHINE_END
