// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Magnus Damm
 * Copyright (C) 2015 Glider bvba
 */

#define pr_fmt(fmt)	"board_staging: "  fmt

#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>

#include "board.h"

static struct device_node *irqc_node __initdata;
static unsigned int irqc_base __initdata;

static bool find_by_address(u64 base_address)
{
	struct device_node *dn = of_find_all_nodes(NULL);
	struct resource res;

	while (dn) {
		if (!of_address_to_resource(dn, 0, &res)) {
			if (res.start == base_address) {
				of_node_put(dn);
				return true;
			}
		}
		dn = of_find_all_nodes(dn);
	}

	return false;
}

bool __init board_staging_dt_node_available(const struct resource *resource,
					    unsigned int num_resources)
{
	unsigned int i;

	for (i = 0; i < num_resources; i++) {
		const struct resource *r = resource + i;

		if (resource_type(r) == IORESOURCE_MEM)
			if (find_by_address(r->start))
				return true; /* DT node available */
	}

	return false; /* Nothing found */
}

int __init board_staging_gic_setup_xlate(const char *gic_match,
					 unsigned int base)
{
	WARN_ON(irqc_node);

	irqc_node = of_find_compatible_node(NULL, NULL, gic_match);

	WARN_ON(!irqc_node);
	if (!irqc_node)
		return -ENOENT;

	irqc_base = base;
	return 0;
}

static void __init gic_fixup_resource(struct resource *res)
{
	struct of_phandle_args irq_data;
	unsigned int hwirq = res->start;
	unsigned int virq;

	if (resource_type(res) != IORESOURCE_IRQ || !irqc_node)
		return;

	irq_data.np = irqc_node;
	irq_data.args_count = 3;
	irq_data.args[0] = 0;
	irq_data.args[1] = hwirq - irqc_base;
	switch (res->flags &
		(IORESOURCE_IRQ_LOWEDGE | IORESOURCE_IRQ_HIGHEDGE |
		 IORESOURCE_IRQ_LOWLEVEL | IORESOURCE_IRQ_HIGHLEVEL)) {
	case IORESOURCE_IRQ_LOWEDGE:
		irq_data.args[2] = IRQ_TYPE_EDGE_FALLING;
		break;
	case IORESOURCE_IRQ_HIGHEDGE:
		irq_data.args[2] = IRQ_TYPE_EDGE_RISING;
		break;
	case IORESOURCE_IRQ_LOWLEVEL:
		irq_data.args[2] = IRQ_TYPE_LEVEL_LOW;
		break;
	case IORESOURCE_IRQ_HIGHLEVEL:
	default:
		irq_data.args[2] = IRQ_TYPE_LEVEL_HIGH;
		break;
	}

	virq = irq_create_of_mapping(&irq_data);
	if (WARN_ON(!virq))
		return;

	pr_debug("hwirq %u -> virq %u\n", hwirq, virq);
	res->start = virq;
}

void __init board_staging_gic_fixup_resources(struct resource *res,
					      unsigned int nres)
{
	unsigned int i;

	for (i = 0; i < nres; i++)
		gic_fixup_resource(&res[i]);
}

int __init board_staging_register_clock(const struct board_staging_clk *bsc)
{
	int error;

	pr_debug("Aliasing clock %s for con_id %s dev_id %s\n", bsc->clk,
		 bsc->con_id, bsc->dev_id);
	error = clk_add_alias(bsc->con_id, bsc->dev_id, bsc->clk, NULL);
	if (error)
		pr_err("Failed to alias clock %s (%d)\n", bsc->clk, error);

	return error;
}

#ifdef CONFIG_PM_GENERIC_DOMAINS_OF
static int board_staging_add_dev_domain(struct platform_device *pdev,
					const char *domain)
{
	struct device *dev = &pdev->dev;
	struct of_phandle_args pd_args;
	struct device_node *np;

	np = of_find_node_by_path(domain);
	if (!np) {
		pr_err("Cannot find domain node %s\n", domain);
		return -ENOENT;
	}

	pd_args.np = np;
	pd_args.args_count = 0;

	/* Initialization similar to device_pm_init_common() */
	spin_lock_init(&dev->power.lock);
	dev->power.early_init = true;

	return of_genpd_add_device(&pd_args, dev);
}
#else
static inline int board_staging_add_dev_domain(struct platform_device *pdev,
					       const char *domain)
{
	return 0;
}
#endif

int __init board_staging_register_device(const struct board_staging_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	unsigned int i;
	int error;

	pr_debug("Trying to register device %s\n", pdev->name);
	if (board_staging_dt_node_available(pdev->resource,
					    pdev->num_resources)) {
		pr_warn("Skipping %s, already in DT\n", pdev->name);
		return -EEXIST;
	}

	board_staging_gic_fixup_resources(pdev->resource, pdev->num_resources);

	for (i = 0; i < dev->nclocks; i++)
		board_staging_register_clock(&dev->clocks[i]);

	if (dev->domain)
		board_staging_add_dev_domain(pdev, dev->domain);

	error = platform_device_register(pdev);
	if (error) {
		pr_err("Failed to register device %s (%d)\n", pdev->name,
		       error);
		return error;
	}

	return error;
}

void __init board_staging_register_devices(const struct board_staging_dev *devs,
					   unsigned int ndevs)
{
	unsigned int i;

	for (i = 0; i < ndevs; i++)
		board_staging_register_device(&devs[i]);
}
