/*
 * Code borrowed from powerpc/kernel/pci-common.c
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2014 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include <asm/pci-bridge.h>

/*
 * Called after each bus is probed, but before its children are examined
 */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	/* nothing to do, expected to be removed in the future */
}

/*
 * We don't have to worry about legacy ISA devices, so nothing to do here
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	return res->start;
}

/*
 * Try to assign the IRQ number from DT when adding a new device
 */
int pcibios_add_device(struct pci_dev *dev)
{
	dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);

	return 0;
}


#ifdef CONFIG_PCI_DOMAINS_GENERIC
static bool dt_domain_found = false;

void pci_bus_assign_domain_nr(struct pci_bus *bus, struct device *parent)
{
	int domain = of_get_pci_domain_nr(parent->of_node);

	if (domain >= 0) {
		dt_domain_found = true;
	} else if (dt_domain_found == true) {
		dev_err(parent, "Node %s is missing \"linux,pci-domain\" property in DT\n",
			parent->of_node->full_name);
		return;
	} else {
		domain = pci_get_new_domain_nr();
	}

	bus->domain_nr = domain;
}
#endif
