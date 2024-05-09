// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zynq power management
 *
 *  Copyright (C) 2012 - 2014 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "common.h"

/* register offsets */
#define DDRC_CTRL_REG1_OFFS		0x60
#define DDRC_DRAM_PARAM_REG3_OFFS	0x20

/* bitfields */
#define DDRC_CLOCKSTOP_MASK	BIT(23)
#define DDRC_SELFREFRESH_MASK	BIT(12)

static void __iomem *ddrc_base;

/**
 * zynq_pm_ioremap() - Create IO mappings
 * @comp:	DT compatible string
 * Return: Pointer to the mapped memory or NULL.
 *
 * Remap the memory region for a compatible DT node.
 */
static void __iomem *zynq_pm_ioremap(const char *comp)
{
	struct device_node *np;
	void __iomem *base = NULL;

	np = of_find_compatible_node(NULL, NULL, comp);
	if (np) {
		base = of_iomap(np, 0);
		of_node_put(np);
	} else {
		pr_warn("%s: no compatible node found for '%s'\n", __func__,
				comp);
	}

	return base;
}

/**
 * zynq_pm_late_init() - Power management init
 *
 * Initialization of power management related features and infrastructure.
 */
void __init zynq_pm_late_init(void)
{
	u32 reg;

	ddrc_base = zynq_pm_ioremap("xlnx,zynq-ddrc-a05");
	if (!ddrc_base) {
		pr_warn("%s: Unable to map DDRC IO memory.\n", __func__);
	} else {
		/*
		 * Enable DDRC clock stop feature. The HW takes care of
		 * entering/exiting the correct mode depending
		 * on activity state.
		 */
		reg = readl(ddrc_base + DDRC_DRAM_PARAM_REG3_OFFS);
		reg |= DDRC_CLOCKSTOP_MASK;
		writel(reg, ddrc_base + DDRC_DRAM_PARAM_REG3_OFFS);
	}
}
