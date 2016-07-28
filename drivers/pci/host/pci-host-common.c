/*
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

static int gen_pci_parse_request_of_pci_ranges(struct device *dev,
		       struct list_head *resources, struct resource **bus_range)
{
	int err, res_valid = 0;
	struct device_node *np = dev->of_node;
	resource_size_t iobase;
	struct resource_entry *win;

	err = of_pci_get_host_bridge_resources(np, 0, 0xff, resources, &iobase);
	if (err)
		return err;

	resource_list_for_each_entry(win, resources) {
		struct resource *parent, *res = win->res;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			parent = &ioport_resource;
			err = pci_remap_iospace(res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);
				continue;
			}
			break;
		case IORESOURCE_MEM:
			parent = &iomem_resource;
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);
			break;
		case IORESOURCE_BUS:
			*bus_range = res;
		default:
			continue;
		}

		err = devm_request_resource(dev, parent, res);
		if (err)
			goto out_release_res;
	}

	if (!res_valid) {
		dev_err(dev, "non-prefetchable memory resource required\n");
		err = -EINVAL;
		goto out_release_res;
	}

	return 0;

out_release_res:
	return err;
}

static void gen_pci_unmap_cfg(void *ptr)
{
	pci_ecam_free((struct pci_config_window *)ptr);
}

static struct pci_config_window *gen_pci_init(struct device *dev,
		struct list_head *resources, struct pci_ecam_ops *ops)
{
	int err;
	struct resource cfgres;
	struct resource *bus_range = NULL;
	struct pci_config_window *cfg;

	/* Parse our PCI ranges and request their resources */
	err = gen_pci_parse_request_of_pci_ranges(dev, resources, &bus_range);
	if (err)
		goto err_out;

	err = of_address_to_resource(dev->of_node, 0, &cfgres);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		goto err_out;
	}

	cfg = pci_ecam_create(dev, &cfgres, bus_range, ops);
	if (IS_ERR(cfg)) {
		err = PTR_ERR(cfg);
		goto err_out;
	}

	err = devm_add_action(dev, gen_pci_unmap_cfg, cfg);
	if (err) {
		gen_pci_unmap_cfg(cfg);
		goto err_out;
	}
	return cfg;

err_out:
	pci_free_resource_list(resources);
	return ERR_PTR(err);
}

int pci_host_common_probe(struct platform_device *pdev,
			  struct pci_ecam_ops *ops)
{
	const char *type;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pci_bus *bus, *child;
	struct pci_config_window *cfg;
	struct list_head resources;

	type = of_get_property(np, "device_type", NULL);
	if (!type || strcmp(type, "pci")) {
		dev_err(dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

	of_pci_check_probe_only();

	/* Parse and map our Configuration Space windows */
	INIT_LIST_HEAD(&resources);
	cfg = gen_pci_init(dev, &resources, ops);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	/* Do not reassign resources if probe only */
	if (!pci_has_flag(PCI_PROBE_ONLY))
		pci_add_flags(PCI_REASSIGN_ALL_RSRC | PCI_REASSIGN_ALL_BUS);

	bus = pci_scan_root_bus(dev, cfg->busr.start, &ops->pci_ops, cfg,
				&resources);
	if (!bus) {
		dev_err(dev, "Scanning rootbus failed");
		return -ENODEV;
	}

	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);

	if (!pci_has_flag(PCI_PROBE_ONLY)) {
		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);

		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}

	pci_bus_add_devices(bus);
	return 0;
}

MODULE_DESCRIPTION("Generic PCI host driver common code");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");
