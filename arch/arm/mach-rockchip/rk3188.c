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
#include "cpu.h"
#include "cpu_axi.h"
#include "grf.h"
#include "iomap.h"
#include "sram.h"

#define RK3188_DEVICE(name) \
	{ \
		.virtual	= (unsigned long) RK_##name##_VIRT, \
		.pfn		= __phys_to_pfn(RK3188_##name##_PHYS), \
		.length		= RK3188_##name##_SIZE, \
		.type		= MT_DEVICE, \
	}

static struct map_desc rk3188_io_desc[] __initdata = {
	RK3188_DEVICE(CRU),
	RK3188_DEVICE(GRF),
	RK3188_DEVICE(PMU),
	RK3188_DEVICE(ROM),
	RK3188_DEVICE(EFUSE),
	RK3188_DEVICE(CPU_AXI_BUS),
	{
		.virtual	= (unsigned long) RK_DDR_VIRT,
		.pfn		= __phys_to_pfn(RK3188_DDR_PCTL_PHYS),
		.length		= RK3188_DDR_PCTL_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long) RK_DDR_VIRT + RK3188_DDR_PCTL_SIZE,
		.pfn		= __phys_to_pfn(RK3188_DDR_PUBL_PHYS),
		.length		= RK3188_DDR_PUBL_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long) RK_GPIO_VIRT(0),
		.pfn		= __phys_to_pfn(RK3188_GPIO0_PHYS),
		.length		= RK3188_GPIO_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long) RK_GPIO_VIRT(1),
		.pfn		= __phys_to_pfn(RK3188_GPIO1_PHYS),
		.length		= RK3188_GPIO_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long) RK_GPIO_VIRT(2),
		.pfn		= __phys_to_pfn(RK3188_GPIO2_PHYS),
		.length		= RK3188_GPIO_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long) RK_GPIO_VIRT(3),
		.pfn		= __phys_to_pfn(RK3188_GPIO3_PHYS),
		.length		= RK3188_GPIO_SIZE,
		.type		= MT_DEVICE,
	},
};

static void __init rk3188_dt_map_io(void)
{
	preset_lpj = 11996091ULL / 2;
	iotable_init(rk3188_io_desc, ARRAY_SIZE(rk3188_io_desc));
	debug_ll_io_init();

	rockchip_soc_id = ROCKCHIP_SOC_RK3188;
	if (readl_relaxed(RK_ROM_VIRT + 0x27f0) == 0x33313042
	 && readl_relaxed(RK_ROM_VIRT + 0x27f4) == 0x32303133
	 && readl_relaxed(RK_ROM_VIRT + 0x27f8) == 0x30313331
	 && readl_relaxed(RK_ROM_VIRT + 0x27fc) == 0x56313031)
		rockchip_soc_id = ROCKCHIP_SOC_RK3188PLUS;

	/* rki2c is used instead of old i2c */
	writel_relaxed(0xF800F800, RK_GRF_VIRT + RK3188_GRF_SOC_CON1);
}

static void __init rk3188_dt_init_timer(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

static const char * const rk3188_dt_compat[] = {
	"rockchip,rk3188",
	NULL,
};

DT_MACHINE_START(RK3188_DT, "Rockchip RK3188 (Flattened Device Tree)")
	.smp		= smp_ops(rockchip_smp_ops),
	.map_io		= rk3188_dt_map_io,
	.init_time	= rk3188_dt_init_timer,
	.dt_compat	= rk3188_dt_compat,
MACHINE_END

#define CPU 3188
char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk3188_pie_init(void)
{
	int err;

	if (!cpu_is_rk3188())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk3188);
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
arch_initcall(rk3188_pie_init);
