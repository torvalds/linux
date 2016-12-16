/*
 * arch/arm/mach-spear13xx/spear1340.c
 *
 * SPEAr1340 machine source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr1340: " fmt

#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include "generic.h"

static void __init spear1340_dt_init(void)
{
	platform_device_register_simple("spear-cpufreq", -1, NULL, 0);
}

static const char * const spear1340_dt_board_compat[] = {
	"st,spear1340",
	"st,spear1340-evb",
	NULL,
};

DT_MACHINE_START(SPEAR1340_DT, "ST SPEAr1340 SoC with Flattened Device Tree")
	.smp		=	smp_ops(spear13xx_smp_ops),
	.map_io		=	spear13xx_map_io,
	.init_time	=	spear13xx_timer_init,
	.init_machine	=	spear1340_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear1340_dt_board_compat,
MACHINE_END
