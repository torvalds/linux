// SPDX-License-Identifier: GPL-2.0
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
	struct pci_host_bridge *bridge;
	int ret;

	bridge = pci_alloc_host_bridge(0);
	if (!bridge)
		return;

	pci_add_resource_offset(&resources, &info->io_space,
				info->io_space.start - 0x1000);
	pci_add_resource(&resources, &info->mem_space);
	info->busn.flags = IORESOURCE_BUS;
	pci_add_resource(&resources, &info->busn);

	list_splice_init(&resources, &bridge->windows);
	bridge->dev.parent = &ofdev->dev;
	bridge->sysdata = info;
	bridge->busnr = 0;
	bridge->ops = info->ops;
	bridge->swizzle_irq = pci_common_swizzle;
	bridge->map_irq = info->map_irq;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret) {
		pci_free_host_bridge(bridge);
		return;
	}

	root_bus = bridge->bus;

	/* Assign devices with resources */
	pci_assign_unassigned_resources();
	pci_bus_add_devices(root_bus);
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, oldcmd;
	int i;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	oldcmd = cmd;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];

		/* Only set up the requested stuff */
		if (!(mask & (1<<i)))
			continue;

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != oldcmd) {
		pci_info(dev, "enabling device (%04x -> %04x)\n", oldcmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}
