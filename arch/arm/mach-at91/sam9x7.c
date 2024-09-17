// SPDX-License-Identifier: GPL-2.0+
/*
 * Setup code for SAM9X7.
 *
 * Copyright (C) 2023 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Varshini Rajendran <varshini.rajendran@microchip.com>
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>

#include "generic.h"

static void __init sam9x7_init(void)
{
	of_platform_default_populate(NULL, NULL, NULL);

	sam9x7_pm_init();
}

static const char * const sam9x7_dt_board_compat[] __initconst = {
	"microchip,sam9x7",
	NULL
};

DT_MACHINE_START(sam9x7_dt, "Microchip SAM9X7")
	/* Maintainer: Microchip */
	.init_machine	= sam9x7_init,
	.dt_compat	= sam9x7_dt_board_compat,
MACHINE_END
