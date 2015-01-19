/*
 * arch/arm/mach-meson6tv/board-meson6tv-common.c
 *
 * Copyright (C) 2014 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/amlogic/of_lm.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/io.h>
#include <linux/io.h>
#include <plat/lm.h>
#include <plat/cpu.h>
#ifdef CONFIG_SMP
#include <mach/smp.h>
#endif
#include <plat/irq.h>

#include "common.h"

extern void meson_common_restart(char mode,const char *cmd);
extern unsigned long long aml_reserved_start;
extern unsigned long long aml_reserved_end;

struct map_desc __initdata meson6tv_board_io_desc[1];

static void __init meson6tv_map_board_io(void)
{
	meson6tv_board_io_desc[0].virtual = PAGE_ALIGN(__phys_to_virt(aml_reserved_start)),
	meson6tv_board_io_desc[0].pfn = __phys_to_pfn(aml_reserved_start),
	meson6tv_board_io_desc[0].length = aml_reserved_end - aml_reserved_start + 1,
	meson6tv_board_io_desc[0].type = MT_MEMORY_NONCACHED,
	iotable_init(meson6tv_board_io_desc,ARRAY_SIZE(meson6tv_board_io_desc));
}

static void __init meson6tv_map_io(void)
{
	meson6tv_map_default_io();
	meson6tv_map_board_io();
}

static void __init meson6tv_init_early(void)
{

	meson_cpu_version_init();
	/*
	 * Mali or some USB devices allocate their coherent buffers from atomic
	 * context. Increase size of atomic coherent pool to make sure such
	 * the allocations won't fail.
	 */
	init_dma_coherent_pool_size(SZ_4M);
}

static void __init meson6tv_init_irq(void)
{
	meson_init_gic_irq();
}

static struct of_device_id mxs_of_platform_bus_ids[] = {
	{.compatible = "simple-bus",},
	{},
};

#ifdef CONFIG_OF_LM
static struct of_device_id mxs_of_lm_bus_ids[] = {
	{.compatible = "logicmodule-bus",},
	{},
};
#endif
#if 0
static void meson6tv_power_off(void)
{
	printk("triggering power off\n");
	kernel_restart("charging_reboot");
}
#endif
static void __init meson6tv_dt_init_machine(void)
{
	struct device *parent;
	parent = get_device(&platform_bus);

	of_platform_populate(NULL, mxs_of_platform_bus_ids, NULL, parent);
#ifdef CONFIG_OF_LM
	of_lm_populate(NULL,mxs_of_lm_bus_ids,NULL,NULL);
#endif

	//of_platform_populate(NULL, of_default_bus_match_table,
	//aml_meson6_auxdata_lookup, NULL);
	//pm_power_off = meson6tv_power_off;
}

static const char __initdata *m6tv_common_boards_compat[] = {
	"AMLOGIC,8726_MX",
	"AMLOGIC,8726_MXS",
	"AMLOGIC,8726_MXL",
	NULL,
};

DT_MACHINE_START(AML8726_MX, "Amlogic Meson6TV")
	.smp		= smp_ops(meson6tv_smp_ops),
	.map_io		= meson6tv_map_io,	// dt - 1
	.init_early	= meson6tv_init_early,	// dt - 2
	.init_irq	= meson6tv_init_irq,	// dt - 3
	.init_time	= meson6tv_timer_init,	// dt - 4
	.init_machine	= meson6tv_dt_init_machine,
	.dt_compat	= m6tv_common_boards_compat,
	.restart    = meson_common_restart,
MACHINE_END

