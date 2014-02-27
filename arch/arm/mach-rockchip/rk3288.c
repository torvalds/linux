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
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "common.h"
#include "cpu.h"
#include "cpu_axi.h"
#include "cru.h"
#include "grf.h"
#include "iomap.h"
#include "loader.h"
#include "pmu.h"
#include "sram.h"
#include "dvfs.h"

#define RK3288_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3288_##name##_PHYS), \
		.length		= RK3288_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

static struct map_desc rk3288_io_desc[] __initdata = {
	RK3288_DEVICE(CRU),
	RK3288_DEVICE(GRF),
	RK_DEVICE(RK_GRF_VIRT + RK3288_GRF_SIZE, RK3288_SGRF_PHYS, RK3288_SGRF_SIZE),
	RK3288_DEVICE(PMU),
	RK3288_DEVICE(ROM),
	RK3288_DEVICE(EFUSE),
	RK_DEVICE(RK_DDR_VIRT, RK3288_DDR_PCTL0_PHYS, RK3288_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3288_DDR_PCTL_SIZE, RK3288_DDR_PUBL0_PHYS, RK3288_DDR_PUBL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + RK3288_DDR_PCTL_SIZE + RK3288_DDR_PUBL_SIZE, RK3288_DDR_PCTL1_PHYS, RK3288_DDR_PCTL_SIZE),
	RK_DEVICE(RK_DDR_VIRT + 2 * RK3288_DDR_PCTL_SIZE + RK3288_DDR_PUBL_SIZE, RK3288_DDR_PUBL1_PHYS, RK3288_DDR_PUBL_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(0), RK3288_GPIO0_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(1), RK3288_GPIO1_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(2), RK3288_GPIO2_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(3), RK3288_GPIO3_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(4), RK3288_GPIO4_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(5), RK3288_GPIO5_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(6), RK3288_GPIO6_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(7), RK3288_GPIO7_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_GPIO_VIRT(8), RK3288_GPIO8_PHYS, RK3288_GPIO_SIZE),
	RK_DEVICE(RK_DEBUG_UART_VIRT, RK3288_UART_DBG_PHYS, RK3288_UART_SIZE),
};

static void __init rk3288_boot_mode_init(void)
{
	u32 flag = readl_relaxed(RK_PMU_VIRT + RK3288_PMU_SYS_REG0);
	u32 mode = readl_relaxed(RK_PMU_VIRT + RK3288_PMU_SYS_REG1);

	if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER)) {
		mode = BOOT_MODE_RECOVERY;
	}
	rockchip_boot_mode_init(flag, mode);
#ifdef CONFIG_RK29_WATCHDOG
	writel_relaxed(BOOT_MODE_WATCHDOG, RK_PMU_VIRT + RK3288_PMU_SYS_REG1);
#endif
}

static void __init rk3288_dt_map_io(void)
{
	iotable_init(rk3288_io_desc, ARRAY_SIZE(rk3288_io_desc));
	debug_ll_io_init();

	rockchip_soc_id = ROCKCHIP_SOC_RK3288;

	/* rkpwm is used instead of old pwm */
	//writel_relaxed(0x00010001, RK_GRF_VIRT + RK3288_GRF_SOC_CON2);

	rk3288_boot_mode_init();
}

static bool rk3288_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	return true;
}

static int rk3288_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	return 0;
}

static int rk3288_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	return 0;
}

static void __init rk3288_dt_init_timer(void)
{
	rockchip_pmu_ops.set_power_domain = rk3288_pmu_set_power_domain;
	rockchip_pmu_ops.power_domain_is_on = rk3288_pmu_power_domain_is_on;
	rockchip_pmu_ops.set_idle_request = rk3288_pmu_set_idle_request;
	of_clk_init(NULL);
	clocksource_of_init();
	of_dvfs_init();
}

static const char * const rk3288_dt_compat[] __initconst = {
	"rockchip,rk3288",
	NULL,
};

static void rk3288_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

	writel_relaxed(boot_flag, RK_PMU_VIRT + RK3288_PMU_SYS_REG0);	// for loader
	writel_relaxed(boot_mode, RK_PMU_VIRT + RK3288_PMU_SYS_REG1);	// for linux
	dsb();

	writel_relaxed(0xeca8, RK_CRU_VIRT + RK3288_CRU_GLB_SRST_SND_VALUE);
	dsb();
}

DT_MACHINE_START(RK3288_DT, "RK30board")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3288_dt_map_io,
	.init_time	= rk3288_dt_init_timer,
	.dt_compat	= rk3288_dt_compat,
	.init_late	= rockchip_suspend_init,
	.restart	= rk3288_restart,
MACHINE_END

#define CPU 3288
char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk3288_pie_init(void)
{
	int err;

	if (!cpu_is_rk3288())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk3288);
	if (IS_ERR(rockchip_pie_chunk)) {
		err = PTR_ERR(rockchip_pie_chunk);
		pr_err("%s: failed to load section %d\n", __func__, err);
		rockchip_pie_chunk = NULL;
		return err;
	}

	rockchip_sram_virt = kern_to_pie(rockchip_pie_chunk, &__pie_common_start[0]);
	rockchip_sram_stack = kern_to_pie(rockchip_pie_chunk, (char *) DATA(sram_stack) + sizeof(DATA(sram_stack)));

	return 0;
}
arch_initcall(rk3288_pie_init);
