// SPDX-License-Identifier: GPL-2.0
/*
 * Common library for PCI host controller drivers
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

#include "pci-host-common.h"

static void gen_pci_unmap_cfg(void *ptr)
{
	pci_ecam_free((struct pci_config_window *)ptr);
}

struct pci_config_window *pci_host_common_ecam_create(struct device *dev,
		struct pci_host_bridge *bridge, const struct pci_ecam_ops *ops)
{
	int err;
	struct resource cfgres;
	struct resource_entry *bus;
	struct pci_config_window *cfg;

	err = of_address_to_resource(dev->of_node, 0, &cfgres);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return ERR_PTR(err);
	}

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return ERR_PTR(-ENODEV);

	cfg = pci_ecam_create(dev, &cfgres, bus->res, ops);
	if (IS_ERR(cfg))
		return cfg;

	err = devm_add_action_or_reset(dev, gen_pci_unmap_cfg, cfg);
	if (err)
		return ERR_PTR(err);

	return cfg;
}
EXPORT_SYMBOL_GPL(pci_host_common_ecam_create);

int pci_host_common_init(struct platform_device *pdev,
			 const struct pci_ecam_ops *ops)
{
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *bridge;
	struct pci_config_window *cfg;

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	of_pci_check_probe_only();

	platform_set_drvdata(pdev, bridge);

	/* Parse and map our Configuration Space windows */
	cfg = pci_host_common_ecam_create(dev, bridge, ops);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	bridge->sysdata = cfg;
	bridge->ops = (struct pci_ops *)&ops->pci_ops;
	bridge->enable_device = ops->enable_device;
	bridge->disable_device = ops->disable_device;
	bridge->msi_domain = true;

	return pci_host_probe(bridge);
}
EXPORT_SYMBOL_GPL(pci_host_common_init);

int pci_host_common_probe(struct platform_device *pdev)
{
	const struct pci_ecam_ops *ops;

	ops = of_device_get_match_data(&pdev->dev);
	if (!ops)
		return -ENODEV;

	return pci_host_common_init(pdev, ops);
}
EXPORT_SYMBOL_GPL(pci_host_common_probe);

void pci_host_common_remove(struct platform_device *pdev)
{
	struct pci_host_bridge *bridge = platform_get_drvdata(pdev);

	pci_lock_rescan_remove();
	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);
	pci_unlock_rescan_remove();
}
EXPORT_SYMBOL_GPL(pci_host_common_remove);

MODULE_DESCRIPTION("Common library for PCI host controller drivers");
MODULE_LICENSE("GPL v2");
