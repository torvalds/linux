/*
 * Copyright (C) 2015 ROCKCHIP, Inc.
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
#include <linux/rockchip/cpu_axi.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/pmu.h>
#include <asm/cputype.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "loader.h"
#define CPU 3228
#include "sram.h"
#include <linux/rockchip/cpu.h>

#define RK3228_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3228_##name##_PHYS), \
		.length		= RK3228_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

static const char * const rk3228_dt_compat[] __initconst = {
	"rockchip,rk3228",
	NULL,
};

static struct map_desc rk3228_io_desc[] __initdata = {
	RK3228_DEVICE(CRU),
	RK3228_DEVICE(GRF),
	RK3228_DEVICE(TIMER),
	RK3228_DEVICE(EFUSE),
	RK3228_DEVICE(CPU_AXI_BUS),
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK3228_UART2_PHYS, RK3228_UART_SIZE),
	RK_DEVICE(RK_DDR_VIRT, RK3228_DDR_PCTL_PHYS, RK3228_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3228_DDR_PCTL_SIZE, RK3228_DDR_PHY_PHYS,
		  RK3228_DDR_PHY_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(0), RK3228_GPIO0_PHYS, RK3228_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(1), RK3228_GPIO1_PHYS, RK3228_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(2), RK3228_GPIO2_PHYS, RK3228_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(3), RK3228_GPIO3_PHYS, RK3228_GPIO_SIZE),
	RK_DEVICE(RK_GIC_VIRT, RK3228_GIC_DIST_PHYS, RK3228_GIC_DIST_SIZE),
	RK_DEVICE(RK_GIC_VIRT + RK3228_GIC_DIST_SIZE, RK3228_GIC_CPU_PHYS,
		  RK3228_GIC_CPU_SIZE),
	RK_DEVICE(RK_PWM_VIRT, RK3228_PWM_PHYS, RK3228_PWM_SIZE),
};

void __init rk3228_dt_map_io(void)
{
	rockchip_soc_id = ROCKCHIP_SOC_RK3228;

	iotable_init(rk3228_io_desc, ARRAY_SIZE(rk3228_io_desc));
	debug_ll_io_init();

	rockchip_efuse_init();
}

static void __init rk3228_dt_init_timer(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
	of_dvfs_init();
}

static void __init rk3228_reserve(void)
{
	/* reserve memory for uboot */
	rockchip_uboot_mem_reserve();

	/* reserve memory for ION */
	rockchip_ion_reserve();
}

static void __init rk3228_init_late(void)
{
	if (rockchip_jtag_enabled)
		clk_prepare_enable(clk_get_sys(NULL, "clk_jtag"));
}

static void rk3228_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

	/* for loader */
	writel_relaxed(boot_flag, RK_PMU_VIRT + RK3228_GRF_OS_REG0);
	/* for linux */
	writel_relaxed(boot_mode, RK_PMU_VIRT + RK3228_GRF_OS_REG1);

	dsb();

	/* pll enter slow mode */
	writel_relaxed(0x11010000, RK_CRU_VIRT + RK3228_CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK_CRU_VIRT + RK3228_CRU_GLB_SRST_SND_VALUE);
	dsb();
}

DT_MACHINE_START(RK3228_DT, "Rockchip RK3228")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3228_dt_map_io,
	.init_time	= rk3228_dt_init_timer,
	.dt_compat	= rk3228_dt_compat,
	.init_late	= rk3228_init_late,
	.reserve	= rk3228_reserve,
	.restart	= rk3228_restart,
MACHINE_END

