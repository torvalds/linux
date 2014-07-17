/*
 * Device Tree support for Rockchip RK3288
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
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
#include <linux/cpuidle.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/pmu.h>
#include <asm/cpuidle.h>
#include <asm/cputype.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "cpu_axi.h"
#include "loader.h"
#define CPU 312X
#include "sram.h"
#include "pm.h"

#define RK312x_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK312X_##name##_PHYS), \
		.length		= RK312X_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

static const char * const rk312x_dt_compat[] __initconst = {
	"rockchip,rk312x",
	NULL,
};

static struct map_desc rk312x_io_desc[] __initdata = {
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK312X_UART2_PHYS, RK312X_UART_SIZE),
	RK312x_DEVICE(TIMER),
};

#define RK312X_TIMER5_VIRT (RK_TIMER_VIRT + 0xa0)

static void __init rk312x_dt_map_io(void)
{
	rockchip_soc_id = ROCKCHIP_SOC_RK3126;

	iotable_init(rk312x_io_desc, ARRAY_SIZE(rk312x_io_desc));
	debug_ll_io_init();

	/* enable timer5 for core */
	writel_relaxed(0, RK312X_TIMER5_VIRT + 0x10);
	dsb();
	writel_relaxed(0xFFFFFFFF, RK312X_TIMER5_VIRT + 0x00);
	writel_relaxed(0xFFFFFFFF, RK312X_TIMER5_VIRT + 0x04);
	dsb();
	writel_relaxed(1, RK312X_TIMER5_VIRT + 0x10);
	dsb();
}

static void __init rk312x_dt_init_timer(void)
{
	clocksource_of_init();
}

static void __init rk312x_reserve(void)
{
}

static void __init rk312x_init_late(void)
{
}

static void rk312x_restart(char mode, const char *cmd)
{
}

DT_MACHINE_START(RK312X_DT, "Rockchip RK312X")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk312x_dt_map_io,
	.init_time	= rk312x_dt_init_timer,
	.dt_compat	= rk312x_dt_compat,
	.init_late	= rk312x_init_late,
	.reserve	= rk312x_reserve,
	.restart	= rk312x_restart,
MACHINE_END

