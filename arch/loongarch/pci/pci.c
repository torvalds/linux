// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>
#include <asm/cacheflush.h>
#include <asm/loongson.h>

#define PCI_DEVICE_ID_LOONGSON_HOST     0x7a00
#define PCI_DEVICE_ID_LOONGSON_DC1      0x7a06
#define PCI_DEVICE_ID_LOONGSON_DC2      0x7a36

int raw_pci_read(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val)
{
	struct pci_bus *bus_tmp = pci_find_bus(domain, bus);

	if (bus_tmp)
		return bus_tmp->ops->read(bus_tmp, devfn, reg, len, val);
	return -EINVAL;
}

int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val)
{
	struct pci_bus *bus_tmp = pci_find_bus(domain, bus);

	if (bus_tmp)
		return bus_tmp->ops->write(bus_tmp, devfn, reg, len, val);
	return -EINVAL;
}

phys_addr_t mcfg_addr_init(int node)
{
	return (((u64)node << 44) | MCFG_EXT_PCICFG_BASE);
}

static int __init pcibios_init(void)
{
	unsigned int lsize;

	/*
	 * Set PCI cacheline size to that of the last level in the
	 * cache hierarchy.
	 */
	lsize = cpu_last_level_cache_line_size();

	BUG_ON(!lsize);

	pci_dfl_cache_line_size = lsize >> 2;

	pr_debug("PCI: pci_cache_line_size set to %d bytes\n", lsize);

	return 0;
}

subsys_initcall(pcibios_init);

int pcibios_device_add(struct pci_dev *dev)
{
	int id;
	struct irq_domain *dom;

	id = pci_domain_nr(dev->bus);
	dom = irq_find_matching_fwnode(get_pch_msi_handle(id), DOMAIN_BUS_PCI_MSI);
	dev_set_msi_domain(&dev->dev, dom);

	return 0;
}

int pcibios_alloc_irq(struct pci_dev *dev)
{
	if (acpi_disabled)
		return 0;
	if (pci_dev_msi_enabled(dev))
		return 0;
	return acpi_pci_irq_enable(dev);
}

static void pci_fixup_vgadev(struct pci_dev *pdev)
{
	struct pci_dev *devp = NULL;

	while ((devp = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, devp))) {
		if (devp->vendor != PCI_VENDOR_ID_LOONGSON) {
			vga_set_default_device(devp);
			dev_info(&pdev->dev,
				"Overriding boot device as %X:%X\n",
				devp->vendor, devp->device);
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC1, pci_fixup_vgadev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC2, pci_fixup_vgadev);
