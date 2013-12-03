/*
 * Device Tree support for Rockchip RK3188
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "core.h"
#include "cpu_axi.h"

static void __init rk3188_dt_map_io(void)
{
	preset_lpj = 11996091ULL / 2;
	debug_ll_io_init();
}

static void __init rk3188_dt_timer_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

static void __init rk3188_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const rk3188_dt_compat[] = {
	"rockchip,rk3188",
	NULL,
};

DT_MACHINE_START(RK3188_DT, "Rockchip RK3188 (Flattened Device Tree)")
	//.nr_irqs        = 32*10,
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3188_dt_map_io,
	.init_machine	= rk3188_dt_init,
	.init_time	= rk3188_dt_timer_init,
	.dt_compat	= rk3188_dt_compat,
MACHINE_END
