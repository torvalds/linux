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

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/slab.h>

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

/**
 * pcibios_enable_device - Enable I/O and memory.
 * @dev: PCI device to be enabled
 * @mask: bitmask of BARs to enable
 */
int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	if (pci_has_flag(PCI_PROBE_ONLY))
		return 0;

	return pci_enable_resources(dev, mask);
}

/*
 * Try to assign the IRQ number from DT when adding a new device
 */
int pcibios_add_device(struct pci_dev *dev)
{
	dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);

	return 0;
}

/*
 * raw_pci_read/write - Platform-specific PCI config space access.
 */
int raw_pci_read(unsigned int domain, unsigned int bus,
		  unsigned int devfn, int reg, int len, u32 *val)
{
	return -ENXIO;
}

int raw_pci_write(unsigned int domain, unsigned int bus,
		unsigned int devfn, int reg, int len, u32 val)
{
	return -ENXIO;
}

#ifdef CONFIG_NUMA

int pcibus_to_node(struct pci_bus *bus)
{
	return dev_to_node(&bus->dev);
}
EXPORT_SYMBOL(pcibus_to_node);

#endif

#ifdef CONFIG_ACPI

int acpi_pci_bus_find_domain_nr(struct pci_bus *bus)
{
	return 0;
}

/* Root bridge scanning */
struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	/* TODO: Should be revisited when implementing PCI on ACPI */
	return NULL;
}
#endif
