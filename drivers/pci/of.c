/*
 * PCI <-> OF mapping helpers
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include "pci.h"

void pci_set_of_node(struct pci_dev *dev)
{
	if (!dev->bus->dev.of_node)
		return;
	dev->dev.of_node = of_pci_find_child_device(dev->bus->dev.of_node,
						    dev->devfn);
}

void pci_release_of_node(struct pci_dev *dev)
{
	of_node_put(dev->dev.of_node);
	dev->dev.of_node = NULL;
}

void pci_set_bus_of_node(struct pci_bus *bus)
{
	if (bus->self == NULL)
		bus->dev.of_node = pcibios_get_phb_of_node(bus);
	else
		bus->dev.of_node = of_node_get(bus->self->dev.of_node);
}

void pci_release_bus_of_node(struct pci_bus *bus)
{
	of_node_put(bus->dev.of_node);
	bus->dev.of_node = NULL;
}

struct device_node * __weak pcibios_get_phb_of_node(struct pci_bus *bus)
{
	/* This should only be called for PHBs */
	if (WARN_ON(bus->self || bus->parent))
		return NULL;

	/* Look for a node pointer in either the intermediary device we
	 * create above the root bus or it's own parent. Normally only
	 * the later is populated.
	 */
	if (bus->bridge->of_node)
		return of_node_get(bus->bridge->of_node);
	if (bus->bridge->parent->of_node)
		return of_node_get(bus->bridge->parent->of_node);
	return NULL;
}
