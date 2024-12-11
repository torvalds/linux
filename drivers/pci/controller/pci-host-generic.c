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
#include <linux/module.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

static const struct pci_ecam_ops gen_pci_cfg_cam_bus_ops = {
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

static const struct pci_ecam_ops pci_dw_ecam_bus_ops = {
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
MODULE_DEVICE_TABLE(of, gen_pci_of_match);

static struct platform_driver gen_pci_driver = {
	.driver = {
		.name = "pci-host-generic",
		.of_match_table = gen_pci_of_match,
	},
	.probe = pci_host_common_probe,
	.remove = pci_host_common_remove,
};
module_platform_driver(gen_pci_driver);

MODULE_DESCRIPTION("Generic PCI host controller driver");
MODULE_LICENSE("GPL v2");
