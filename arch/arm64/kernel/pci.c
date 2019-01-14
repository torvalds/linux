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

#ifdef CONFIG_ACPI
/*
 * Try to assign the IRQ number when probing a new device
 */
int pcibios_alloc_irq(struct pci_dev *dev)
{
	if (!acpi_disabled)
		acpi_pci_irq_enable(dev);

	return 0;
}
#endif

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
		struct device *bus_dev = &bridge->bus->dev;

		ACPI_COMPANION_SET(&bridge->dev, adev);
		set_dev_node(bus_dev, acpi_get_node(acpi_device_handle(adev)));
	}

	return 0;
}

static int pci_acpi_root_prepare_resources(struct acpi_pci_root_info *ci)
{
	struct resource_entry *entry, *tmp;
	int status;

	status = acpi_pci_probe_root_resources(ci);
	resource_list_for_each_entry_safe(entry, tmp, &ci->resources) {
		if (!(entry->res->flags & IORESOURCE_WINDOW))
			resource_list_destroy_entry(entry);
	}
	return status;
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
	struct pci_ecam_ops *ecam_ops;
	struct resource cfgres;
	struct acpi_device *adev;
	struct pci_config_window *cfg;
	int ret;

	ret = pci_mcfg_lookup(root, &cfgres, &ecam_ops);
	if (ret) {
		dev_err(dev, "%04x:%pR ECAM region not found\n", seg, bus_res);
		return NULL;
	}

	adev = acpi_resource_consumer(&cfgres);
	if (adev)
		dev_info(dev, "ECAM area %pR reserved by %s\n", &cfgres,
			 dev_name(&adev->dev));
	else
		dev_warn(dev, FW_BUG "ECAM area %pR not reserved in ACPI namespace\n",
			 &cfgres);

	cfg = pci_ecam_create(dev, &cfgres, bus_res, ecam_ops);
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
	kfree(ci->ops);
	kfree(ri);
}

/* Interface called from ACPI code to setup PCI host controller */
struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	struct acpi_pci_generic_root_info *ri;
	struct pci_bus *bus, *child;
	struct acpi_pci_root_ops *root_ops;

	ri = kzalloc(sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return NULL;

	root_ops = kzalloc(sizeof(*root_ops), GFP_KERNEL);
	if (!root_ops) {
		kfree(ri);
		return NULL;
	}

	ri->cfg = pci_acpi_setup_ecam_mapping(root);
	if (!ri->cfg) {
		kfree(ri);
		kfree(root_ops);
		return NULL;
	}

	root_ops->release_info = pci_acpi_generic_release_info;
	root_ops->prepare_resources = pci_acpi_root_prepare_resources;
	root_ops->pci_ops = &ri->cfg->ops->pci_ops;
	bus = acpi_pci_root_create(root, root_ops, &ri->common, ri->cfg);
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
