/*
 * leon_pci.c: LEON Host PCI support
 *
 * Copyright (C) 2011 Aeroflex Gaisler AB, Daniel Hellstrom
 *
 * Code is partially derived from pcic.c
 */

#include <linux/of_device.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/export.h>
#include <asm/leon.h>
#include <asm/leon_pci.h>

/* The LEON architecture does not rely on a BIOS or bootloader to setup
 * PCI for us. The Linux generic routines are used to setup resources,
 * reset values of configuration-space register settings are preserved.
 *
 * PCI Memory and Prefetchable Memory is direct-mapped. However I/O Space is
 * accessed through a Window which is translated to low 64KB in PCI space, the
 * first 4KB is not used so 60KB is available.
 */
void leon_pci_init(struct platform_device *ofdev, struct leon_pci_info *info)
{
	LIST_HEAD(resources);
	struct pci_bus *root_bus;

	pci_add_resource_offset(&resources, &info->io_space,
				info->io_space.start - 0x1000);
	pci_add_resource(&resources, &info->mem_space);
	info->busn.flags = IORESOURCE_BUS;
	pci_add_resource(&resources, &info->busn);

	root_bus = pci_scan_root_bus(&ofdev->dev, 0, info->ops, info,
				     &resources);
	if (!root_bus) {
		pci_free_resource_list(&resources);
		return;
	}

	/* Setup IRQs of all devices using custom routines */
	pci_fixup_irqs(pci_common_swizzle, info->map_irq);

	/* Assign devices with resources */
	pci_assign_unassigned_resources();
	pci_bus_add_devices(root_bus);
}

void pcibios_fixup_bus(struct pci_bus *pbus)
{
	struct pci_dev *dev;
	int i, has_io, has_mem;
	u16 cmd;

	list_for_each_entry(dev, &pbus->devices, bus_list) {
		/*
		 * We can not rely on that the bootloader has enabled I/O
		 * or memory access to PCI devices. Instead we enable it here
		 * if the device has BARs of respective type.
		 */
		has_io = has_mem = 0;
		for (i = 0; i < PCI_ROM_RESOURCE; i++) {
			unsigned long f = dev->resource[i].flags;
			if (f & IORESOURCE_IO)
				has_io = 1;
			else if (f & IORESOURCE_MEM)
				has_mem = 1;
		}
		/* ROM BARs are mapped into 32-bit memory space */
		if (dev->resource[PCI_ROM_RESOURCE].end != 0) {
			dev->resource[PCI_ROM_RESOURCE].flags |=
							IORESOURCE_ROM_ENABLE;
			has_mem = 1;
		}
		pci_bus_read_config_word(pbus, dev->devfn, PCI_COMMAND, &cmd);
		if (has_io && !(cmd & PCI_COMMAND_IO)) {
#ifdef CONFIG_PCI_DEBUG
			printk(KERN_INFO "LEONPCI: Enabling I/O for dev %s\n",
					 pci_name(dev));
#endif
			cmd |= PCI_COMMAND_IO;
			pci_bus_write_config_word(pbus, dev->devfn, PCI_COMMAND,
									cmd);
		}
		if (has_mem && !(cmd & PCI_COMMAND_MEMORY)) {
#ifdef CONFIG_PCI_DEBUG
			printk(KERN_INFO "LEONPCI: Enabling MEMORY for dev"
					 "%s\n", pci_name(dev));
#endif
			cmd |= PCI_COMMAND_MEMORY;
			pci_bus_write_config_word(pbus, dev->devfn, PCI_COMMAND,
									cmd);
		}
	}
}
