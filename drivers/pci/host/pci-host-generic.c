/*
 * Simple, generic PCI host controller driver targetting firmware-initialised
 * systems and virtual machines (e.g. the PCI emulation provided by kvmtool).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>

#include "../ecam.h"

static struct pci_ecam_ops gen_pci_cfg_cam_bus_ops = {
	.bus_shift	= 16,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};

static const struct of_device_id gen_pci_of_match[] = {
	{ .compatible = "pci-host-cam-generic",
	  .data = &gen_pci_cfg_cam_bus_ops },

	{ .compatible = "pci-host-ecam-generic",
	  .data = &pci_generic_ecam_ops },

	{ },
};

MODULE_DEVICE_TABLE(of, gen_pci_of_match);

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
	},
	.probe = gen_pci_probe,
};
module_platform_driver(gen_pci_driver);

MODULE_DESCRIPTION("Generic PCI host driver");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");
