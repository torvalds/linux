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
#include <linux/platform_device.h>
#include <linux/memblock.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/export.h>

#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/memblock.h>

#include <plat/sram.h>
#include <plat/omap-secure.h>
#include <plat/mmc.h>

#include "omap-wakeupgen.h"

#include "soc.h"
#include "common.h"
#include "hsmmc.h"
#include "omap4-sar-layout.h"

#ifdef CONFIG_CACHE_L2X0
static void __iomem *l2cache_base;
#endif

static void __iomem *sar_ram_base;

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
	dram_io_desc[0].type = MT_MEMORY_SO;
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

void __init gic_init_irq(void)
{
	void __iomem *omap_irq_base;
	void __iomem *gic_dist_base_addr;

	/* Static mapping, never released */
	gic_dist_base_addr = ioremap(OMAP44XX_GIC_DIST_BASE, SZ_4K);
	BUG_ON(!gic_dist_base_addr);

	/* Static mapping, never released */
	omap_irq_base = ioremap(OMAP44XX_GIC_CPU_BASE, SZ_512);
	BUG_ON(!omap_irq_base);

	omap_wakeupgen_init();

	gic_init(0, 29, gic_dist_base_addr, omap_irq_base);
}

#ifdef CONFIG_CACHE_L2X0

void __iomem *omap4_get_l2cache_base(void)
{
	return l2cache_base;
}

static void omap4_l2x0_disable(void)
{
	/* Disable PL310 L2 Cache controller */
	omap_smc1(0x102, 0x0);
}

static void omap4_l2x0_set_debug(unsigned long val)
{
	/* Program PL310 L2 Cache controller debug register */
	omap_smc1(0x100, val);
}

static int __init omap_l2_cache_init(void)
{
	u32 aux_ctrl = 0;

	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!cpu_is_omap44xx())
		return -ENODEV;

	/* Static mapping, never released */
	l2cache_base = ioremap(OMAP44XX_L2CACHE_BASE, SZ_4K);
	if (WARN_ON(!l2cache_base))
		return -ENOMEM;

	/*
	 * 16-way associativity, parity disabled
	 * Way size - 32KB (es1.0)
	 * Way size - 64KB (es2.0 +)
	 */
	aux_ctrl = ((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) |
			(0x1 << 25) |
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT));

	if (omap_rev() == OMAP4430_REV_ES1_0) {
		aux_ctrl |= 0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT;
	} else {
		aux_ctrl |= ((0x3 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) |
			(1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
			(1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT));
	}
	if (omap_rev() != OMAP4430_REV_ES1_0)
		omap_smc1(0x109, aux_ctrl);

	/* Enable PL310 L2 Cache controller */
	omap_smc1(0x102, 0x1);

	if (of_have_populated_dt())
		l2x0_of_init(aux_ctrl, L2X0_AUX_CTRL_MASK);
	else
		l2x0_init(l2cache_base, aux_ctrl, L2X0_AUX_CTRL_MASK);

	/*
	 * Override default outer_cache.disable with a OMAP4
	 * specific one
	*/
	outer_cache.disable = omap4_l2x0_disable;
	outer_cache.set_debug = omap4_l2x0_set_debug;

	return 0;
}
early_initcall(omap_l2_cache_init);
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
	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!cpu_is_omap44xx())
		return -ENOMEM;

	/* Static mapping, never released */
	sar_ram_base = ioremap(OMAP44XX_SAR_RAM_BASE, SZ_16K);
	if (WARN_ON(!sar_ram_base))
		return -ENOMEM;

	return 0;
}
early_initcall(omap4_sar_ram_init);

static struct of_device_id irq_match[] __initdata = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{ .compatible = "arm,cortex-a15-gic", .data = gic_of_init, },
	{ }
};

void __init omap_gic_of_init(void)
{
	omap_wakeupgen_init();
	of_irq_init(irq_match);
}

#if defined(CONFIG_MMC_OMAP_HS) || defined(CONFIG_MMC_OMAP_HS_MODULE)
static int omap4_twl6030_hsmmc_late_init(struct device *dev)
{
	int irq = 0;
	struct platform_device *pdev = container_of(dev,
				struct platform_device, dev);
	struct omap_mmc_platform_data *pdata = dev->platform_data;

	/* Setting MMC1 Card detect Irq */
	if (pdev->id == 0) {
		irq = twl6030_mmc_card_detect_config();
		if (irq < 0) {
			dev_err(dev, "%s: Error card detect config(%d)\n",
				__func__, irq);
			return irq;
		}
		pdata->slots[0].card_detect_irq = irq;
		pdata->slots[0].card_detect = twl6030_mmc_card_detect;
	}
	return 0;
}

static __init void omap4_twl6030_hsmmc_set_late_init(struct device *dev)
{
	struct omap_mmc_platform_data *pdata;

	/* dev can be null if CONFIG_MMC_OMAP_HS is not set */
	if (!dev) {
		pr_err("Failed %s\n", __func__);
		return;
	}
	pdata = dev->platform_data;
	pdata->init =	omap4_twl6030_hsmmc_late_init;
}

int __init omap4_twl6030_hsmmc_init(struct omap2_hsmmc_info *controllers)
{
	struct omap2_hsmmc_info *c;

	omap_hsmmc_init(controllers);
	for (c = controllers; c->mmc; c++) {
		/* pdev can be null if CONFIG_MMC_OMAP_HS is not set */
		if (!c->pdev)
			continue;
		omap4_twl6030_hsmmc_set_late_init(&c->pdev->dev);
	}

	return 0;
}
#else
int __init omap4_twl6030_hsmmc_init(struct omap2_hsmmc_info *controllers)
{
	return 0;
}
#endif
