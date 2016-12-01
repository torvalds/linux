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
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>
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

/*
 * Try to assign the IRQ number when probing a new device
 */
int pcibios_alloc_irq(struct pci_dev *dev)
{
	if (acpi_disabled)
		dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);
#ifdef CONFIG_ACPI
	else
		return acpi_pci_irq_enable(dev);
#endif

	return 0;
}

/*
 * raw_pci_read/write - Platform-specific PCI config space access.
 */
int raw_pci_read(unsigned int domain, unsigned int bus,
		  unsigned int devfn, int reg, int len, u32 *val)
{
	struct pci_bus *b = pci_find_bus(domain, bus);

	if (!b)
		return PCIBIOS_DEVICE_NOT_FOUND;
	return b->ops->read(b, devfn, reg, len, val);
}

int raw_pci_write(unsigned int domain, unsigned int bus,
		unsigned int devfn, int reg, int len, u32 val)
{
	struct pci_bus *b = pci_find_bus(domain, bus);

	if (!b)
		return PCIBIOS_DEVICE_NOT_FOUND;
	return b->ops->write(b, devfn, reg, len, val);
}

#ifdef CONFIG_NUMA

int pcibus_to_node(struct pci_bus *bus)
{
	return dev_to_node(&bus->dev);
}
EXPORT_SYMBOL(pcibus_to_node);

#endif

#ifdef CONFIG_ACPI

struct acpi_pci_generic_root_info {
	struct acpi_pci_root_info	common;
	struct pci_config_window	*cfg;	/* config space mapping */
};

int acpi_pci_bus_find_domain_nr(struct pci_bus *bus)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct acpi_device *adev = to_acpi_device(cfg->parent);
	struct acpi_pci_root *root = acpi_driver_data(adev);

	return root->segment;
}

int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	if (!acpi_disabled) {
		struct pci_config_window *cfg = bridge->bus->sysdata;
		struct acpi_device *adev = to_acpi_device(cfg->parent);
		ACPI_COMPANION_SET(&bridge->dev, adev);
	}

	return 0;
}

/*
 * Lookup the bus range for the domain in MCFG, and set up config space
 * mapping.
 */
static struct pci_config_window *
pci_acpi_setup_ecam_mapping(struct acpi_pci_root *root)
{
	struct device *dev = &root->device->dev;
	struct resource *bus_res = &root->secondary;
	u16 seg = root->segment;
	struct pci_config_window *cfg;
	struct resource cfgres;
	unsigned int bsz;

	/* Use address from _CBA if present, otherwise lookup MCFG */
	if (!root->mcfg_addr)
		root->mcfg_addr = pci_mcfg_lookup(seg, bus_res);

	if (!root->mcfg_addr) {
		dev_err(dev, "%04x:%pR ECAM region not found\n", seg, bus_res);
		return NULL;
	}

	bsz = 1 << pci_generic_ecam_ops.bus_shift;
	cfgres.start = root->mcfg_addr + bus_res->start * bsz;
	cfgres.end = cfgres.start + resource_size(bus_res) * bsz - 1;
	cfgres.flags = IORESOURCE_MEM;
	cfg = pci_ecam_create(dev, &cfgres, bus_res, &pci_generic_ecam_ops);
	if (IS_ERR(cfg)) {
		dev_err(dev, "%04x:%pR error %ld mapping ECAM\n", seg, bus_res,
			PTR_ERR(cfg));
		return NULL;
	}

	return cfg;
}

/* release_info: free resources allocated by init_info */
static void pci_acpi_generic_release_info(struct acpi_pci_root_info *ci)
{
	struct acpi_pci_generic_root_info *ri;

	ri = container_of(ci, struct acpi_pci_generic_root_info, common);
	pci_ecam_free(ri->cfg);
	kfree(ri);
}

static struct acpi_pci_root_ops acpi_pci_root_ops = {
	.release_info = pci_acpi_generic_release_info,
};

/* Interface called from ACPI code to setup PCI host controller */
struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	int node = acpi_get_node(root->device->handle);
	struct acpi_pci_generic_root_info *ri;
	struct pci_bus *bus, *child;

	ri = kzalloc_node(sizeof(*ri), GFP_KERNEL, node);
	if (!ri)
		return NULL;

	ri->cfg = pci_acpi_setup_ecam_mapping(root);
	if (!ri->cfg) {
		kfree(ri);
		return NULL;
	}

	acpi_pci_root_ops.pci_ops = &ri->cfg->ops->pci_ops;
	bus = acpi_pci_root_create(root, &acpi_pci_root_ops, &ri->common,
				   ri->cfg);
	if (!bus)
		return NULL;

	pci_bus_size_bridges(bus);
	pci_bus_assign_resources(bus);

	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	return bus;
}

void pcibios_add_bus(struct pci_bus *bus)
{
	acpi_pci_add_bus(bus);
}

void pcibios_remove_bus(struct pci_bus *bus)
{
	acpi_pci_remove_bus(bus);
}

#endif
