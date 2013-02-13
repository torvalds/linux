/*
 * arch/arm/mach-tegra/board-dt-tegra30.c
 *
 * NVIDIA Tegra30 device tree board support
 *
 * Copyright (C) 2011, 2013, NVIDIA Corporation
 *
 * Derived from:
 *
 * arch/arm/mach-tegra/board-dt-tegra20.c
 *
 * Copyright (C) 2010 Secret Lab Technologies, Ltd.
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clocksource.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>

#include "board.h"
#include "common.h"
#include "iomap.h"

static void __init tegra30_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *tegra30_dt_board_compat[] = {
	"nvidia,tegra30",
	NULL
};

DT_MACHINE_START(TEGRA30_DT, "NVIDIA Tegra30 (Flattened Device Tree)")
	.smp		= smp_ops(tegra_smp_ops),
	.map_io		= tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq	= tegra_dt_init_irq,
	.init_time	= clocksource_of_init,
	.init_machine	= tegra30_dt_init,
	.init_late	= tegra_init_late,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= tegra30_dt_board_compat,
MACHINE_END
