/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    and		 Arnd Bergmann, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#undef DEBUG

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <asm/errno.h>
#include <asm/topology.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/atomic.h>

/*
 * The list of OF IDs below is used for matching bus types in the
 * system whose devices are to be exposed as of_platform_devices.
 *
 * This is the default list valid for most platforms. This file provides
 * functions who can take an explicit list if necessary though
 *
 * The search is always performed recursively looking for children of
 * the provided device_node and recursively if such a children matches
 * a bus type in the list
 */

static const struct of_device_id of_default_bus_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .type = "spider", },
	{ .type = "axon", },
	{ .type = "plb5", },
	{ .type = "plb4", },
	{ .type = "opb", },
	{ .type = "ebc", },
	{},
};

struct bus_type of_platform_bus_type = {
       .uevent	= of_device_uevent,
};
EXPORT_SYMBOL(of_platform_bus_type);

static int __init of_bus_driver_init(void)
{
	return of_bus_type_init(&of_platform_bus_type, "of_platform");
}

postcore_initcall(of_bus_driver_init);

struct of_device* of_platform_device_create(struct device_node *np,
					    const char *bus_id,
					    struct device *parent)
{
	struct of_device *dev;

	dev = of_device_alloc(np, bus_id, parent);
	if (!dev)
		return NULL;

	dev->dma_mask = 0xffffffffUL;
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	dev->dev.bus = &of_platform_bus_type;

	/* We do not fill the DMA ops for platform devices by default.
	 * This is currently the responsibility of the platform code
	 * to do such, possibly using a device notifier
	 */

	if (of_device_register(dev) != 0) {
		of_device_free(dev);
		return NULL;
	}

	return dev;
}
EXPORT_SYMBOL(of_platform_device_create);



/**
 * of_platform_bus_create - Create an OF device for a bus node and all its
 * children. Optionally recursively instanciate matching busses.
 * @bus: device node of the bus to instanciate
 * @matches: match table, NULL to use the default, OF_NO_DEEP_PROBE to
 * disallow recursive creation of child busses
 */
static int of_platform_bus_create(const struct device_node *bus,
				  const struct of_device_id *matches,
				  struct device *parent)
{
	struct device_node *child;
	struct of_device *dev;
	int rc = 0;

	for_each_child_of_node(bus, child) {
		pr_debug("   create child: %s\n", child->full_name);
		dev = of_platform_device_create(child, NULL, parent);
		if (dev == NULL)
			rc = -ENOMEM;
		else if (!of_match_node(matches, child))
			continue;
		if (rc == 0) {
			pr_debug("   and sub busses\n");
			rc = of_platform_bus_create(child, matches, &dev->dev);
		} if (rc) {
			of_node_put(child);
			break;
		}
	}
	return rc;
}

/**
 * of_platform_bus_probe - Probe the device-tree for platform busses
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table, NULL to use the default
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Note that children of the provided root are not instanciated as devices
 * unless the specified root itself matches the bus list and is not NULL.
 */

int of_platform_bus_probe(struct device_node *root,
			  const struct of_device_id *matches,
			  struct device *parent)
{
	struct device_node *child;
	struct of_device *dev;
	int rc = 0;

	if (matches == NULL)
		matches = of_default_bus_ids;
	if (matches == OF_NO_DEEP_PROBE)
		return -EINVAL;
	if (root == NULL)
		root = of_find_node_by_path("/");
	else
		of_node_get(root);

	pr_debug("of_platform_bus_probe()\n");
	pr_debug(" starting at: %s\n", root->full_name);

	/* Do a self check of bus type, if there's a match, create
	 * children
	 */
	if (of_match_node(matches, root)) {
		pr_debug(" root match, create all sub devices\n");
		dev = of_platform_device_create(root, NULL, parent);
		if (dev == NULL) {
			rc = -ENOMEM;
			goto bail;
		}
		pr_debug(" create all sub busses\n");
		rc = of_platform_bus_create(root, matches, &dev->dev);
		goto bail;
	}
	for_each_child_of_node(root, child) {
		if (!of_match_node(matches, child))
			continue;

		pr_debug("  match: %s\n", child->full_name);
		dev = of_platform_device_create(child, NULL, parent);
		if (dev == NULL)
			rc = -ENOMEM;
		else
			rc = of_platform_bus_create(child, matches, &dev->dev);
		if (rc) {
			of_node_put(child);
			break;
		}
	}
 bail:
	of_node_put(root);
	return rc;
}
EXPORT_SYMBOL(of_platform_bus_probe);

static int of_dev_node_match(struct device *dev, void *data)
{
	return to_of_device(dev)->node == data;
}

struct of_device *of_find_device_by_node(struct device_node *np)
{
	struct device *dev;

	dev = bus_find_device(&of_platform_bus_type,
			      NULL, np, of_dev_node_match);
	if (dev)
		return to_of_device(dev);
	return NULL;
}
EXPORT_SYMBOL(of_find_device_by_node);

static int of_dev_phandle_match(struct device *dev, void *data)
{
	phandle *ph = data;
	return to_of_device(dev)->node->phandle == *ph;
}

struct of_device *of_find_device_by_phandle(phandle ph)
{
	struct device *dev;

	dev = bus_find_device(&of_platform_bus_type,
			      NULL, &ph, of_dev_phandle_match);
	if (dev)
		return to_of_device(dev);
	return NULL;
}
EXPORT_SYMBOL(of_find_device_by_phandle);


#ifdef CONFIG_PPC_OF_PLATFORM_PCI

/* The probing of PCI controllers from of_platform is currently
 * 64 bits only, mostly due to gratuitous differences between
 * the 32 and 64 bits PCI code on PowerPC and the 32 bits one
 * lacking some bits needed here.
 */

static int __devinit of_pci_phb_probe(struct of_device *dev,
				      const struct of_device_id *match)
{
	struct pci_controller *phb;

	/* Check if we can do that ... */
	if (ppc_md.pci_setup_phb == NULL)
		return -ENODEV;

	printk(KERN_INFO "Setting up PCI bus %s\n", dev->node->full_name);

	/* Alloc and setup PHB data structure */
	phb = pcibios_alloc_controller(dev->node);
	if (!phb)
		return -ENODEV;

	/* Setup parent in sysfs */
	phb->parent = &dev->dev;

	/* Setup the PHB using arch provided callback */
	if (ppc_md.pci_setup_phb(phb)) {
		pcibios_free_controller(phb);
		return -ENODEV;
	}

	/* Process "ranges" property */
	pci_process_bridge_OF_ranges(phb, dev->node, 0);

	/* Init pci_dn data structures */
	pci_devs_phb_init_dynamic(phb);

	/* Register devices with EEH */
#ifdef CONFIG_EEH
	if (dev->node->child)
		eeh_add_device_tree_early(dev->node);
#endif /* CONFIG_EEH */

	/* Scan the bus */
	pcibios_scan_phb(phb, dev->node);
	if (phb->bus == NULL)
		return -ENXIO;

	/* Claim resources. This might need some rework as well depending
	 * wether we are doing probe-only or not, like assigning unassigned
	 * resources etc...
	 */
	pcibios_claim_one_bus(phb->bus);

	/* Finish EEH setup */
#ifdef CONFIG_EEH
	eeh_add_device_tree_late(phb->bus);
#endif

	/* Add probed PCI devices to the device model */
	pci_bus_add_devices(phb->bus);

	return 0;
}

static struct of_device_id of_pci_phb_ids[] = {
	{ .type = "pci", },
	{ .type = "pcix", },
	{ .type = "pcie", },
	{ .type = "pciex", },
	{ .type = "ht", },
	{}
};

static struct of_platform_driver of_pci_phb_driver = {
	.match_table = of_pci_phb_ids,
	.probe = of_pci_phb_probe,
	.driver = {
		.name = "of-pci",
	},
};

static __init int of_pci_phb_init(void)
{
	return of_register_platform_driver(&of_pci_phb_driver);
}

device_initcall(of_pci_phb_init);

#endif /* CONFIG_PPC_OF_PLATFORM_PCI */
