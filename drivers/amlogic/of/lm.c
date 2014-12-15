/*
 * 		drivers/amlogic/of/lm.c
 *
 *		Copyright (C) 2013 Victor Wan,  Amlogic,inc.
 *				<victor.wan@amlogic.com>
 *		
 *		ported from:
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    and		 Arnd Bergmann, IBM Corp.
 *    Merged from powerpc/kernel/of_platform.c and
 *    sparc{,64}/kernel/of_device.c by Stephen Rothwell
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <plat/lm.h>

const struct of_device_id of_lm_bus_match_table[] = {
	{ .compatible = "logicmodule-bus", },
	{} /* Empty terminated list */
};

/** Checks if the given "compat" string matches one of the strings in
 * the device's "compatible" property
 */
static int of_lm_device_is_compatible(const struct device_node *device,
		const char *compat)
{
	const char* cp;
	int cplen, l;

	cp = of_get_property(device, "lm-compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (of_compat_cmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}
/**
 * of_lm_match_node - Tell if an device_node has a matching of_match structure
 *	@matches:	array of of device match structures to search in
 *	@node:		the of device structure to match against
 *
 *	Low level utility function used by device matching.
 */
const struct of_device_id *of_lm_match_node(const struct of_device_id *matches,
					 const struct device_node *node)
{
	if (!matches)
		return NULL;

	while (matches->name[0] || matches->type[0] || matches->compatible[0]) {
		int match = 1;
		if (matches->name[0])
			match &= node->name
				&& !strcmp(matches->name, node->name);
		if (matches->type[0])
			match &= node->type
				&& !strcmp(matches->type, node->type);
		if (matches->compatible[0])
			match &= of_lm_device_is_compatible(node,
						matches->compatible);
		if (match)
			return matches;
		matches++;
	}
	return NULL;
}
EXPORT_SYMBOL(of_lm_match_node);
#if 0
static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

/**
 * of_find_device_by_node - Find the lm_device associated with a node
 * @np: Pointer to device tree node
 *
 * Returns platform_device pointer, or NULL if not found
 */
struct platform_device *of_find_lm_device_by_node(struct device_node *np)
{
	struct device *dev;

	dev = bus_find_device(&lm_bustype, NULL, np, of_dev_node_match);
	return dev ? to_lm_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_lm_device_by_node);
#endif
/**
 * of_lm_device_create - Alloc, initialize and register an of_device
 * @np: pointer to node to create device for
 * @bus_id: name to assign device
 * @parent: Linux device model parent device.
 *
 * Returns pointer to created lm device, or NULL if a device was not
 * registered.  Unavailable devices will not get registered.
 */
static struct lm_device *of_lm_device_create(struct device_node *node,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	struct lm_device *dev;
	const void *prop;
//	struct resource *res, temp_res;
	//int ret,id,irq;
	int ret,id;

	pr_debug("Creating of lm device %s\n", node->full_name);
	if (!of_device_is_available(node))
		return NULL;

	/* Allow the HW Peripheral ID to be overridden */
	prop = of_get_property(node, "lm-periph-id", NULL);
	if (prop)
		id = of_read_ulong(prop, 1);
	else
		id = -1; // root dev

	dev = kzalloc(sizeof(struct lm_device),GFP_KERNEL);
	if (!dev){
		printk(KERN_ERR "out of memory to alloc lm device\n");
		return NULL;
	}
/*
	prop = of_get_property(node, "irq", NULL);
	if (prop)
		irq = of_read_ulong(prop, 1);
	else{
		irq = 0;
	}
	printk(KERN_ERR "  --- irq: %d\n",irq);
*/	
	/* setup generic device info */
	dev->id = id;
//	dev->irq = irq;
	dev->dev.coherent_dma_mask = ~0;
	dev->dev.of_node = of_node_get(node);
	dev->dev.parent = parent;
	dev->dev.platform_data = platform_data;
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if(id >= 0)
		dev_set_name(&dev->dev,"lm%d", dev->id);
	else
		dev_set_name(&dev->dev,"lm-root");


	/* setup lm-specific device info */
	dev->dma_mask_room = DMA_BIT_MASK(32);


//	ret = of_address_to_resource(node, 0, &dev->resource);
//	if (ret)
//		goto err_free;

	ret = lm_device_register(dev);
	if (ret)
		goto err_free;

	return dev;

err_free:
	put_device(&dev->dev);
	return NULL;
}

EXPORT_SYMBOL(of_lm_device_create);

/**
 * of_devname_lookup() - Given a device node, lookup the preferred Linux name
 */
static const struct of_dev_auxdata *of_dev_lookup(const struct of_dev_auxdata *lookup,
				 struct device_node *np)
{
	struct resource res;

	if (!lookup)
		return NULL;

	for(; lookup->compatible != NULL; lookup++) {
		if (!of_device_is_compatible(np, lookup->compatible))
			continue;
		if (!of_address_to_resource(np, 0, &res))
			if (res.start != lookup->phys_addr)
				continue;
		pr_debug("%s: devname=%s\n", np->full_name, lookup->name);
		return lookup;
	}

	return NULL;
}

/**
 * of_lm_bus_create() - Create a device for a node and its children.
 * @bus: device node of the bus to instantiate
 * @matches: match table for bus nodes
 * @lookup: auxdata table for matching id and platform_data with device nodes
 * @parent: parent for new device, or NULL for top level.
 * @strict: require compatible property
 *
 * Creates a lm_device for the provided device_node, and optionally
 * recursively create devices for all the child nodes.
 */
static int of_lm_bus_create(struct device_node *bus,
				  const struct of_device_id *matches,
				  const struct of_dev_auxdata *lookup,
				  struct device *parent, bool strict)
{
	const struct of_dev_auxdata *auxdata;
	struct device_node *child;
	struct lm_device *dev;
	const char *bus_id = NULL;
	void *platform_data = NULL;
	int rc = 0;

	/* Make sure it has a compatible property */
	if (strict && (!of_get_property(bus, "lm-compatible", NULL))) {
		pr_debug("%s() - skipping %s, no compatible prop\n",
			 __func__, bus->full_name);
		return 0;
	}

	auxdata = of_dev_lookup(lookup, bus);
	if (auxdata) {
		bus_id = auxdata->name;
		platform_data = auxdata->platform_data;
	}

	dev = of_lm_device_create(bus, bus_id, platform_data, parent);
	if (!dev || !of_lm_match_node(matches, bus))
		return 0;

	if(dev->id != -1)
		parent = &dev->dev; 
	
	for_each_child_of_node(bus, child) {
		pr_debug("   create child: %s\n", child->full_name);
		rc = of_lm_bus_create(child, matches, lookup, parent, strict);
		if (rc) {
			of_node_put(child);
			break;
		}
	}
	return rc;
}

/**
 * of_lm_bus_probe() - Probe the device-tree for lm buses
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table for bus nodes
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Note that children of the provided root are not instantiated as devices
 * unless the specified root itself matches the bus list and is not NULL.
 */
int of_lm_bus_probe(struct device_node *root,
			  const struct of_device_id *matches,
			  struct device *parent)
{
	struct device_node *child;
	int rc = 0;

	root = root ? of_node_get(root) : of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("of_lm_bus_probe()\n");
	pr_debug(" starting at: %s\n", root->full_name);

	/* Do a self check of bus type, if there's a match, create children */
	if (of_match_node(matches, root)) {
		rc = of_lm_bus_create(root, matches, NULL, parent, false);
	} else for_each_child_of_node(root, child) {
		if (!of_match_node(matches, child))
			continue;
		rc = of_lm_bus_create(child, matches, NULL, parent, false);
		if (rc)
			break;
	}

	of_node_put(root);
	return rc;
}
EXPORT_SYMBOL(of_lm_bus_probe);

/**
 * of_lm_populate() - Populate lm_devices from device tree data
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table, NULL to use the default
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Similar to of_lm_bus_probe(), this function walks the device tree
 * and creates devices from nodes.  It differs in that it follows the modern
 * convention of requiring all device nodes to have a 'compatible' property,
 * and it is suitable for creating devices which are children of the root
 * node (of_lm_bus_probe will only create children of the root which
 * are selected by the @matches argument).
 *
 * New board support should be using this function instead of
 * of_lm_bus_probe().
 *
 * Returns 0 on success, < 0 on failure.
 */
int of_lm_populate(struct device_node *root,
			const struct of_device_id *matches,
			const struct of_dev_auxdata *lookup,
			struct device *parent)
{
	struct device_node *child;
	int rc = 0;

	root = root ? of_node_get(root) : of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	for_each_child_of_node(root, child) {
		rc = of_lm_bus_create(child, matches, lookup, parent, true);
		if (rc)
			break;
	}

	of_node_put(root);
	return rc;
}
EXPORT_SYMBOL_GPL(of_lm_populate);
