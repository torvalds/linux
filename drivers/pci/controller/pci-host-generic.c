// SPDX-License-Identifier: GPL-2.0
/*
 * Simple, generic PCI host controller driver targeting firmware-initialised
 * systems and virtual machines (e.g. the PCI emulation provided by kvmtool).
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

static struct pci_ecam_ops gen_pci_cfg_cam_bus_ops = {
	.bus_shift	= 16,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};

static bool pci_dw_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct pci_config_window *cfg = bus->sysdata;

	/*
	 * The Synopsys DesignWare PCIe controller in ECAM mode will not filter
	 * type 0 config TLPs sent to devices 1 and up on its downstream port,
	 * resulting in devices appearing multiple times on bus 0 unless we
	 * filter out those accesses here.
	 */
	if (bus->number == cfg->busr.start && PCI_SLOT(devfn) > 0)
		return false;

	return true;
}

static void __iomem *pci_dw_ecam_map_bus(struct pci_bus *bus,
					 unsigned int devfn, int where)
{
	if (!pci_dw_valid_device(bus, devfn))
		return NULL;

	return pci_ecam_map_bus(bus, devfn, where);
}

static struct pci_ecam_ops pci_dw_ecam_bus_ops = {
	.bus_shift	= 20,
	.pci_ops	= {
		.map_bus	= pci_dw_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};

static const struct of_device_id gen_pci_of_match[] = {
	{ .compatible = "pci-host-cam-generic",
	  .data = &gen_pci_cfg_cam_bus_ops },

	{ .compatible = "pci-host-ecam-generic",
	  .data = &pci_generic_ecam_ops },

	{ .compatible = "marvell,armada8k-pcie-ecam",
	  .data = &pci_dw_ecam_bus_ops },

	{ .compatible = "socionext,synquacer-pcie-ecam",
	  .data = &pci_dw_ecam_bus_ops },

	{ .compatible = "snps,dw-pcie-ecam",
	  .data = &pci_dw_ecam_bus_ops },

	{ },
};

static int gen_pci_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct pci_ecam_ops *ops;

	of_id = of_match_node(gen_pci_of_match, pdev->dev.of_node);
	ops = (struct pci_ecam_ops *)of_id->data;

	return pci_host_common_probe(pdev, ops);
}

static struct platform_driver gen_pci_driver = {
	.driver = {
		.name = "pci-host-generic",
		.of_match_table = gen_pci_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = gen_pci_probe,
	.remove = pci_host_common_remove,
};
builtin_platform_driver(gen_pci_driver);
