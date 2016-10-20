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

#ifdef CONFIG_OMAP_INTERCONNECT_BARRIER

/* Used to implement memory barrier on DRAM path */
#define OMAP4_DRAM_BARRIER_VA			0xfe600000

static void __iomem *dram_sync, *sram_sync;
static phys_addr_t dram_sync_paddr;
static u32 dram_sync_size;

/*
 * The OMAP4 bus structure contains asynchronous bridges which can buffer
 * data writes from the MPU. These asynchronous bridges can be found on
 * paths between the MPU to EMIF, and the MPU to L3 interconnects.
 *
 * We need to be careful about re-ordering which can happen as a result
 * of different accesses being performed via different paths, and
 * therefore different asynchronous bridges.
 */

/*
 * OMAP4 interconnect barrier which is called for each mb() and wmb().
 * This is to ensure that normal paths to DRAM (normal memory, cacheable
 * accesses) are properly synchronised with writes to DMA coherent memory
 * (normal memory, uncacheable) and device writes.
 *
 * The mb() and wmb() barriers only operate only on the MPU->MA->EMIF
 * path, as we need to ensure that data is visible to other system
 * masters prior to writes to those system masters being seen.
 *
 * Note: the SRAM path is not synchronised via mb() and wmb().
 */
static void omap4_mb(void)
{
	if (dram_sync)
		writel_relaxed(0, dram_sync);
}

/*
 * OMAP4 Errata i688 - asynchronous bridge corruption when entering WFI.
 *
 * If a data is stalled inside asynchronous bridge because of back
 * pressure, it may be accepted multiple times, creating pointer
 * misalignment that will corrupt next transfers on that data path until
 * next reset of the system. No recovery procedure once the issue is hit,
 * the path remains consistently broken.
 *
 * Async bridges can be found on paths between MPU to EMIF and MPU to L3
 * interconnects.
 *
 * This situation can happen only when the idle is initiated by a Master
 * Request Disconnection (which is trigged by software when executing WFI
 * on the CPU).
 *
 * The work-around for this errata needs all the initiators connected
 * through an async bridge to ensure that data path is properly drained
 * before issuing WFI. This condition will be met if one Strongly ordered
 * access is performed to the target right before executing the WFI.
 *
 * In MPU case, L3 T2ASYNC FIFO and DDR T2ASYNC FIFO needs to be drained.
 * IO barrier ensure that there is no synchronisation loss on initiators
 * operating on both interconnect port simultaneously.
 *
 * This is a stronger version of the OMAP4 memory barrier below, and
 * operates on both the MPU->MA->EMIF path but also the MPU->OCP path
 * as well, and is necessary prior to executing a WFI.
 */
void omap_interconnect_sync(void)
{
	if (dram_sync && sram_sync) {
		writel_relaxed(readl_relaxed(dram_sync), dram_sync);
		writel_relaxed(readl_relaxed(sram_sync), sram_sync);
		isb();
	}
}

static int __init omap4_sram_init(void)
{
	struct device_node *np;
	struct gen_pool *sram_pool;

	np = of_find_compatible_node(NULL, NULL, "ti,omap4-mpu");
	if (!np)
		pr_warn("%s:Unable to allocate sram needed to handle errata I688\n",
			__func__);
	sram_pool = of_gen_pool_get(np, "sram", 0);
	if (!sram_pool)
		pr_warn("%s:Unable to get sram pool needed to handle errata I688\n",
			__func__);
	else
		sram_sync = (void *)gen_pool_alloc(sram_pool, PAGE_SIZE);

	return 0;
}
omap_arch_initcall(omap4_sram_init);

/* Steal one page physical memory for barrier implementation */
void __init omap_barrier_reserve_memblock(void)
{
	dram_sync_size = ALIGN(PAGE_SIZE, SZ_1M);
	dram_sync_paddr = arm_memblock_steal(dram_sync_size, SZ_1M);
}

void __init omap_barriers_init(void)
{
	struct map_desc dram_io_desc[1];

	dram_io_desc[0].virtual = OMAP4_DRAM_BARRIER_VA;
	dram_io_desc[0].pfn = __phys_to_pfn(dram_sync_paddr);
	dram_io_desc[0].length = dram_sync_size;
	dram_io_desc[0].type = MT_MEMORY_RW_SO;
	iotable_init(dram_io_desc, ARRAY_SIZE(dram_io_desc));
	dram_sync = (void __iomem *) dram_io_desc[0].virtual;

	pr_info("OMAP4: Map %pa to %p for dram barrier\n",
		&dram_sync_paddr, dram_sync);

	soc_mb = omap4_mb;
}

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
 * SAR RAM used to save and restore the HW context in low power modes.
 * Note that we need to initialize this very early for kexec. See
 * omap4_mpuss_early_init().
 */
void __init omap4_sar_ram_init(void)
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
		return;

	/* Static mapping, never released */
	sar_ram_base = ioremap(sar_base, SZ_16K);
	if (WARN_ON(!sar_ram_base))
		return;
}

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
