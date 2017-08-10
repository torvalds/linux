/*
 * Generic PCI host driver common code
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
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

static int gen_pci_parse_request_of_pci_ranges(struct device *dev,
		       struct list_head *resources, struct resource **bus_range)
{
	int err, res_valid = 0;
	struct device_node *np = dev->of_node;
	resource_size_t iobase;
	struct resource_entry *win, *tmp;

	err = of_pci_get_host_bridge_resources(np, 0, 0xff, resources, &iobase);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, resources);
	if (err)
		return err;

	resource_list_for_each_entry_safe(win, tmp, resources) {
		struct resource *res = win->res;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			err = pci_remap_iospace(res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);
				resource_list_destroy_entry(win);
			}
			break;
		case IORESOURCE_MEM:
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);
			break;
		case IORESOURCE_BUS:
			*bus_range = res;
			break;
		}
	}

	if (res_valid)
		return 0;

	dev_err(dev, "non-prefetchable memory resource required\n");
	return -EINVAL;
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
	struct pci_host_bridge *bridge;
	struct pci_config_window *cfg;
	struct list_head resources;
	int ret;

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

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

	list_splice_init(&resources, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = cfg;
	bridge->busnr = cfg->busr.start;
	bridge->ops = &ops->pci_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret < 0) {
		dev_err(dev, "Scanning root bridge failed");
		return ret;
	}

	bus = bridge->bus;

	/*
	 * We insert PCI resources into the iomem_resource and
	 * ioport_resource trees in either pci_bus_claim_resources()
	 * or pci_bus_assign_resources().
	 */
	if (pci_has_flag(PCI_PROBE_ONLY)) {
		pci_bus_claim_resources(bus);
	} else {
		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);

		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}

	pci_bus_add_devices(bus);
	return 0;
}
