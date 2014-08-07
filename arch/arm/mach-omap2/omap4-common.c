/*
 * OMAP4 specific common source file.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Author:
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/platform_device.h>
#include <linux/memblock.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/export.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/irqchip/irq-crossbar.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/memblock.h>
#include <asm/smp_twd.h>

#include "omap-wakeupgen.h"
#include "soc.h"
#include "iomap.h"
#include "common.h"
#include "mmc.h"
#include "prminst44xx.h"
#include "prcm_mpu44xx.h"
#include "omap4-sar-layout.h"
#include "omap-secure.h"
#include "sram.h"

#ifdef CONFIG_CACHE_L2X0
static void __iomem *l2cache_base;
#endif

static void __iomem *sar_ram_base;
static void __iomem *gic_dist_base_addr;
static void __iomem *twd_base;

#define IRQ_LOCALTIMER		29

#ifdef CONFIG_OMAP4_ERRATA_I688
/* Used to implement memory barrier on DRAM path */
#define OMAP4_DRAM_BARRIER_VA			0xfe600000

void __iomem *dram_sync, *sram_sync;

static phys_addr_t paddr;
static u32 size;

void omap_bus_sync(void)
{
	if (dram_sync && sram_sync) {
		writel_relaxed(readl_relaxed(dram_sync), dram_sync);
		writel_relaxed(readl_relaxed(sram_sync), sram_sync);
		isb();
	}
}
EXPORT_SYMBOL(omap_bus_sync);

/* Steal one page physical memory for barrier implementation */
int __init omap_barrier_reserve_memblock(void)
{

	size = ALIGN(PAGE_SIZE, SZ_1M);
	paddr = arm_memblock_steal(size, SZ_1M);

	return 0;
}

void __init omap_barriers_init(void)
{
	struct map_desc dram_io_desc[1];

	dram_io_desc[0].virtual = OMAP4_DRAM_BARRIER_VA;
	dram_io_desc[0].pfn = __phys_to_pfn(paddr);
	dram_io_desc[0].length = size;
	dram_io_desc[0].type = MT_MEMORY_RW_SO;
	iotable_init(dram_io_desc, ARRAY_SIZE(dram_io_desc));
	dram_sync = (void __iomem *) dram_io_desc[0].virtual;
	sram_sync = (void __iomem *) OMAP4_SRAM_VA;

	pr_info("OMAP4: Map 0x%08llx to 0x%08lx for dram barrier\n",
		(long long) paddr, dram_io_desc[0].virtual);

}
#else
void __init omap_barriers_init(void)
{}
#endif

void gic_dist_disable(void)
{
	if (gic_dist_base_addr)
		writel_relaxed(0x0, gic_dist_base_addr + GIC_DIST_CTRL);
}

void gic_dist_enable(void)
{
	if (gic_dist_base_addr)
		writel_relaxed(0x1, gic_dist_base_addr + GIC_DIST_CTRL);
}

bool gic_dist_disabled(void)
{
	return !(readl_relaxed(gic_dist_base_addr + GIC_DIST_CTRL) & 0x1);
}

void gic_timer_retrigger(void)
{
	u32 twd_int = readl_relaxed(twd_base + TWD_TIMER_INTSTAT);
	u32 gic_int = readl_relaxed(gic_dist_base_addr + GIC_DIST_PENDING_SET);
	u32 twd_ctrl = readl_relaxed(twd_base + TWD_TIMER_CONTROL);

	if (twd_int && !(gic_int & BIT(IRQ_LOCALTIMER))) {
		/*
		 * The local timer interrupt got lost while the distributor was
		 * disabled.  Ack the pending interrupt, and retrigger it.
		 */
		pr_warn("%s: lost localtimer interrupt\n", __func__);
		writel_relaxed(1, twd_base + TWD_TIMER_INTSTAT);
		if (!(twd_ctrl & TWD_TIMER_CONTROL_PERIODIC)) {
			writel_relaxed(1, twd_base + TWD_TIMER_COUNTER);
			twd_ctrl |= TWD_TIMER_CONTROL_ENABLE;
			writel_relaxed(twd_ctrl, twd_base + TWD_TIMER_CONTROL);
		}
	}
}

#ifdef CONFIG_CACHE_L2X0

void __iomem *omap4_get_l2cache_base(void)
{
	return l2cache_base;
}

static void omap4_l2c310_write_sec(unsigned long val, unsigned reg)
{
	unsigned smc_op;

	switch (reg) {
	case L2X0_CTRL:
		smc_op = OMAP4_MON_L2X0_CTRL_INDEX;
		break;

	case L2X0_AUX_CTRL:
		smc_op = OMAP4_MON_L2X0_AUXCTRL_INDEX;
		break;

	case L2X0_DEBUG_CTRL:
		smc_op = OMAP4_MON_L2X0_DBG_CTRL_INDEX;
		break;

	case L310_PREFETCH_CTRL:
		smc_op = OMAP4_MON_L2X0_PREFETCH_INDEX;
		break;

	default:
		WARN_ONCE(1, "OMAP L2C310: ignoring write to reg 0x%x\n", reg);
		return;
	}

	omap_smc1(smc_op, val);
}

int __init omap_l2_cache_init(void)
{
	u32 aux_ctrl;

	/* Static mapping, never released */
	l2cache_base = ioremap(OMAP44XX_L2CACHE_BASE, SZ_4K);
	if (WARN_ON(!l2cache_base))
		return -ENOMEM;

	/* 16-way associativity, parity disabled, way size - 64KB (es2.0 +) */
	aux_ctrl = L2C_AUX_CTRL_SHARED_OVERRIDE |
		   L310_AUX_CTRL_DATA_PREFETCH |
		   L310_AUX_CTRL_INSTR_PREFETCH;

	outer_cache.write_sec = omap4_l2c310_write_sec;
	if (of_have_populated_dt())
		l2x0_of_init(aux_ctrl, 0xcf9fffff);
	else
		l2x0_init(l2cache_base, aux_ctrl, 0xcf9fffff);

	return 0;
}
#endif

void __iomem *omap4_get_sar_ram_base(void)
{
	return sar_ram_base;
}

/*
 * SAR RAM used to save and restore the HW
 * context in low power modes
 */
static int __init omap4_sar_ram_init(void)
{
	unsigned long sar_base;

	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (cpu_is_omap44xx())
		sar_base = OMAP44XX_SAR_RAM_BASE;
	else if (soc_is_omap54xx())
		sar_base = OMAP54XX_SAR_RAM_BASE;
	else
		return -ENOMEM;

	/* Static mapping, never released */
	sar_ram_base = ioremap(sar_base, SZ_16K);
	if (WARN_ON(!sar_ram_base))
		return -ENOMEM;

	return 0;
}
omap_early_initcall(omap4_sar_ram_init);

void __init omap_gic_of_init(void)
{
	struct device_node *np;

	/* Extract GIC distributor and TWD bases for OMAP4460 ROM Errata WA */
	if (!cpu_is_omap446x())
		goto skip_errata_init;

	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	gic_dist_base_addr = of_iomap(np, 0);
	WARN_ON(!gic_dist_base_addr);

	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-twd-timer");
	twd_base = of_iomap(np, 0);
	WARN_ON(!twd_base);

skip_errata_init:
	omap_wakeupgen_init();
#ifdef CONFIG_IRQ_CROSSBAR
	irqcrossbar_init();
#endif
	irqchip_init();
}
