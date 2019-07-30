// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * Copyright (C) 2010,2013, NVIDIA Corporation
 */

#include <linux/cpu_pm.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/syscore_ops.h>

#include "board.h"
#include "iomap.h"
#include "irq.h"

#define SGI_MASK 0xFFFF

#ifdef CONFIG_PM_SLEEP
static void __iomem *tegra_gic_cpu_base;
#endif

bool tegra_pending_sgi(void)
{
	u32 pending_set;
	void __iomem *distbase = IO_ADDRESS(TEGRA_ARM_INT_DIST_BASE);

	pending_set = readl_relaxed(distbase + GIC_DIST_PENDING_SET);

	if (pending_set & SGI_MASK)
		return true;

	return false;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_gic_notifier(struct notifier_block *self,
			      unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER:
		writel_relaxed(0x1E0, tegra_gic_cpu_base + GIC_CPU_CTRL);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block tegra_gic_notifier_block = {
	.notifier_call = tegra_gic_notifier,
};

static const struct of_device_id tegra114_dt_gic_match[] __initconst = {
	{ .compatible = "arm,cortex-a15-gic" },
	{ }
};

static void __init tegra114_gic_cpu_pm_registration(void)
{
	struct device_node *dn;

	dn = of_find_matching_node(NULL, tegra114_dt_gic_match);
	if (!dn)
		return;

	tegra_gic_cpu_base = of_iomap(dn, 1);

	cpu_pm_register_notifier(&tegra_gic_notifier_block);
}
#else
static void __init tegra114_gic_cpu_pm_registration(void) { }
#endif

static const struct of_device_id tegra_ictlr_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-ictlr" },
	{ .compatible = "nvidia,tegra30-ictlr" },
	{ }
};

void __init tegra_init_irq(void)
{
	if (WARN_ON(!of_find_matching_node(NULL, tegra_ictlr_match)))
		pr_warn("Outdated DT detected, suspend/resume will NOT work\n");

	tegra114_gic_cpu_pm_registration();
}
