// SPDX-License-Identifier: GPL-2.0+
/*
 * Setup code for SAM9X60.
 *
 * Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Claudiu Beznea <claudiu.beznea@microchip.com>
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/system_misc.h>

#include "generic.h"

static void __init sam9x60_init(void)
{
	of_platform_default_populate(NULL, NULL, NULL);

	sam9x60_pm_init();
}

static const char *const sam9x60_dt_board_compat[] __initconst = {
	"microchip,sam9x60",
	NULL
};

DT_MACHINE_START(sam9x60_dt, "Microchip SAM9X60")
	/* Maintainer: Microchip */
	.init_machine	= sam9x60_init,
	.dt_compat	= sam9x60_dt_board_compat,
MACHINE_END
