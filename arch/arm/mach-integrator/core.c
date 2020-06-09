// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-integrator/core.c
 *
 *  Copyright (C) 2000-2003 Deep Blue Solutions Ltd
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/io.h>
#include <linux/stat.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pgtable.h>

#include <asm/mach-types.h>
#include <asm/mach/time.h>

#include "hardware.h"
#include "cm.h"
#include "common.h"

static DEFINE_RAW_SPINLOCK(cm_lock);
static void __iomem *cm_base;

/**
 * cm_get - get the value from the CM_CTRL register
 */
u32 cm_get(void)
{
	return readl(cm_base + INTEGRATOR_HDR_CTRL_OFFSET);
}

/**
 * cm_control - update the CM_CTRL register.
 * @mask: bits to change
 * @set: bits to set
 */
void cm_control(u32 mask, u32 set)
{
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&cm_lock, flags);
	val = readl(cm_base + INTEGRATOR_HDR_CTRL_OFFSET) & ~mask;
	writel(val | set, cm_base + INTEGRATOR_HDR_CTRL_OFFSET);
	raw_spin_unlock_irqrestore(&cm_lock, flags);
}

void cm_clear_irqs(void)
{
	/* disable core module IRQs */
	writel(0xffffffffU, cm_base + INTEGRATOR_HDR_IC_OFFSET +
		IRQ_ENABLE_CLEAR);
}

static const struct of_device_id cm_match[] = {
	{ .compatible = "arm,core-module-integrator"},
	{ },
};

void cm_init(void)
{
	struct device_node *cm = of_find_matching_node(NULL, cm_match);

	if (!cm) {
		pr_crit("no core module node found in device tree\n");
		return;
	}
	cm_base = of_iomap(cm, 0);
	if (!cm_base) {
		pr_crit("could not remap core module\n");
		return;
	}
	cm_clear_irqs();
}

/*
 * We need to stop things allocating the low memory; ideally we need a
 * better implementation of GFP_DMA which does not assume that DMA-able
 * memory starts at zero.
 */
void __init integrator_reserve(void)
{
	memblock_reserve(PHYS_OFFSET, __pa(swapper_pg_dir) - PHYS_OFFSET);
}
