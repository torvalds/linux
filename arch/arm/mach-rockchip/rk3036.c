/*
 * Device Tree support for Rockchip RK3036
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
#define CPU 3036
#include "sram.h"
#include "pm.h"

#define RK3036_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3036_##name##_PHYS), \
		.length		= RK3036_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}


#define RK3036_TIMER5_VIRT (RK_TIMER_VIRT + 0xa0)

static const char * const rk3036_dt_compat[] __initconst = {
	"rockchip,rk3036",
	NULL,
};

static struct map_desc rk3036_io_desc[] __initdata = {
	RK3036_DEVICE(TIMER),
};

static void __init rk3036_dt_map_io(void)
{
	rockchip_soc_id = ROCKCHIP_SOC_RK3036;

	iotable_init(rk3036_io_desc, ARRAY_SIZE(rk3036_io_desc));
        debug_ll_io_init();
	/* enable timer5 for core */
	writel_relaxed(0, RK3036_TIMER5_VIRT + 0x10);
	dsb();
	writel_relaxed(0xFFFFFFFF, RK3036_TIMER5_VIRT + 0x00);
	writel_relaxed(0xFFFFFFFF, RK3036_TIMER5_VIRT + 0x04);
	dsb();
	writel_relaxed(1, RK3036_TIMER5_VIRT + 0x10);
	dsb();
}

static int rk3036_sys_set_power_domain(enum pmu_power_domain pd, bool on)
{
    return 0;
}

static bool rk3036_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
    return 0;
}

static int rk3036_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
    return 0;
}

static void __init rk3036_dt_init_timer(void)
{
        rockchip_pmu_ops.set_power_domain = rk3036_sys_set_power_domain;
        rockchip_pmu_ops.power_domain_is_on = rk3036_pmu_power_domain_is_on;
        rockchip_pmu_ops.set_idle_request = rk3036_pmu_set_idle_request;
	clocksource_of_init();
}

static void __init rk3036_reserve(void)
{
}

static void __init rk3036_init_late(void)
{
}

static void rk3036_restart(char mode, const char *cmd)
{
}

DT_MACHINE_START(RK3036_DT, "Rockchip RK3036")
	.dt_compat	= rk3036_dt_compat,
	.smp		= smp_ops(rockchip_smp_ops),
	.reserve	= rk3036_reserve,
	.map_io		= rk3036_dt_map_io,
	.init_time	= rk3036_dt_init_timer,
	.init_late	= rk3036_init_late,
	.restart	= rk3036_restart,
MACHINE_END
