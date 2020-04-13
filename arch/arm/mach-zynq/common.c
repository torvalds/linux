// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contains common code that is intended to be used across
 * boards so that it's not replicated.
 *
 *  Copyright (C) 2011 Xilinx
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk/zynq.h>
#include <linux/clocksource.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/memblock.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/smp_scu.h>
#include <asm/system_info.h>
#include <asm/hardware/cache-l2x0.h>

#include "common.h"

#define ZYNQ_DEVCFG_MCTRL		0x80
#define ZYNQ_DEVCFG_PS_VERSION_SHIFT	28
#define ZYNQ_DEVCFG_PS_VERSION_MASK	0xF

void __iomem *zynq_scu_base;

/**
 * zynq_memory_init - Initialize special memory
 *
 * We need to stop things allocating the low memory as DMA can't work in
 * the 1st 512K of memory.
 */
static void __init zynq_memory_init(void)
{
	if (!__pa(PAGE_OFFSET))
		memblock_reserve(__pa(PAGE_OFFSET), 0x80000);
}

static struct platform_device zynq_cpuidle_device = {
	.name = "cpuidle-zynq",
};

/**
 * zynq_get_revision - Get Zynq silicon revision
 *
 * Return: Silicon version or -1 otherwise
 */
static int __init zynq_get_revision(void)
{
	struct device_node *np;
	void __iomem *zynq_devcfg_base;
	u32 revision;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynq-devcfg-1.0");
	if (!np) {
		pr_err("%s: no devcfg node found\n", __func__);
		return -1;
	}

	zynq_devcfg_base = of_iomap(np, 0);
	if (!zynq_devcfg_base) {
		pr_err("%s: Unable to map I/O memory\n", __func__);
		return -1;
	}

	revision = readl(zynq_devcfg_base + ZYNQ_DEVCFG_MCTRL);
	revision >>= ZYNQ_DEVCFG_PS_VERSION_SHIFT;
	revision &= ZYNQ_DEVCFG_PS_VERSION_MASK;

	iounmap(zynq_devcfg_base);

	return revision;
}

static void __init zynq_init_late(void)
{
	zynq_core_pm_init();
	zynq_pm_late_init();
}

/**
 * zynq_init_machine - System specific initialization, intended to be
 *		       called from board specific initialization.
 */
static void __init zynq_init_machine(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	struct device *parent = NULL;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		goto out;

	system_rev = zynq_get_revision();

	soc_dev_attr->family = kasprintf(GFP_KERNEL, "Xilinx Zynq");
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "0x%x", system_rev);
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "0x%x",
					 zynq_slcr_get_device_id());

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->family);
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr->soc_id);
		kfree(soc_dev_attr);
		goto out;
	}

	parent = soc_device_to_device(soc_dev);

out:
	/*
	 * Finished with the static registrations now; fill in the missing
	 * devices
	 */
	of_platform_default_populate(NULL, NULL, parent);

	platform_device_register(&zynq_cpuidle_device);
}

static void __init zynq_timer_init(void)
{
	zynq_clock_init();
	of_clk_init(NULL);
	timer_probe();
}

static struct map_desc zynq_cortex_a9_scu_map __initdata = {
	.length	= SZ_256,
	.type	= MT_DEVICE,
};

static void __init zynq_scu_map_io(void)
{
	unsigned long base;

	base = scu_a9_get_base();
	zynq_cortex_a9_scu_map.pfn = __phys_to_pfn(base);
	/* Expected address is in vmalloc area that's why simple assign here */
	zynq_cortex_a9_scu_map.virtual = base;
	iotable_init(&zynq_cortex_a9_scu_map, 1);
	zynq_scu_base = (void __iomem *)base;
	BUG_ON(!zynq_scu_base);
}

/**
 * zynq_map_io - Create memory mappings needed for early I/O.
 */
static void __init zynq_map_io(void)
{
	debug_ll_io_init();
	zynq_scu_map_io();
}

static void __init zynq_irq_init(void)
{
	zynq_early_slcr_init();
	irqchip_init();
}

static const char * const zynq_dt_match[] = {
	"xlnx,zynq-7000",
	NULL
};

DT_MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	/* 64KB way size, 8-way associativity, parity disabled */
	.l2c_aux_val    = 0x00400000,
	.l2c_aux_mask	= 0xffbfffff,
	.smp		= smp_ops(zynq_smp_ops),
	.map_io		= zynq_map_io,
	.init_irq	= zynq_irq_init,
	.init_machine	= zynq_init_machine,
	.init_late	= zynq_init_late,
	.init_time	= zynq_timer_init,
	.dt_compat	= zynq_dt_match,
	.reserve	= zynq_memory_init,
MACHINE_END
