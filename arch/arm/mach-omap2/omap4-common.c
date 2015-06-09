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
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/genalloc.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/memblock.h>
#include <asm/smp_twd.h>

#include "omap-wakeupgen.h"
#include "soc.h"
#include "iomap.h"
#include "common.h"
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

void omap4_l2c310_write_sec(unsigned long val, unsigned reg)
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

	case L310_POWER_CTRL:
		pr_info_once("OMAP L2C310: ROM does not support power control setting\n");
		return;

	default:
		WARN_ONCE(1, "OMAP L2C310: ignoring write to reg 0x%x\n", reg);
		return;
	}

	omap_smc1(smc_op, val);
}

int __init omap_l2_cache_init(void)
{
	/* Static mapping, never released */
	l2cache_base = ioremap(OMAP44XX_L2CACHE_BASE, SZ_4K);
	if (WARN_ON(!l2cache_base))
		return -ENOMEM;
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

static const struct of_device_id intc_match[] = {
	{ .compatible = "ti,omap4-wugen-mpu", },
	{ .compatible = "ti,omap5-wugen-mpu", },
	{ },
};

static struct device_node *intc_node;

unsigned int omap4_xlate_irq(unsigned int hwirq)
{
	struct of_phandle_args irq_data;
	unsigned int irq;

	if (!intc_node)
		intc_node = of_find_matching_node(NULL, intc_match);

	if (WARN_ON(!intc_node))
		return hwirq;

	irq_data.np = intc_node;
	irq_data.args_count = 3;
	irq_data.args[0] = 0;
	irq_data.args[1] = hwirq - OMAP44XX_IRQ_GIC_START;
	irq_data.args[2] = IRQ_TYPE_LEVEL_HIGH;

	irq = irq_create_of_mapping(&irq_data);
	if (WARN_ON(!irq))
		irq = hwirq;

	return irq;
}

void __init omap_gic_of_init(void)
{
	struct device_node *np;

	intc_node = of_find_matching_node(NULL, intc_match);
	if (WARN_ON(!intc_node)) {
		pr_err("No WUGEN found in DT, system will misbehave.\n");
		pr_err("UPDATE YOUR DEVICE TREE!\n");
	}

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
	irqchip_init();
}
