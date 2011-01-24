/*
 * Read address ranges from a Broadcom CNB20LE Host Bridge
 *
 * Copyright (c) 2010 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <asm/pci_x86.h>

#include "bus_numa.h"

static void __devinit cnb20le_res(struct pci_dev *dev)
{
	struct pci_root_info *info;
	struct resource res;
	u16 word1, word2;
	u8 fbus, lbus;
	int i;

#ifdef CONFIG_ACPI
	/*
	 * We should get host bridge information from ACPI unless the BIOS
	 * doesn't support it.
	 */
	if (acpi_os_get_root_pointer())
		return;
#endif

	info = &pci_root_info[pci_root_num];
	pci_root_num++;

	/* read the PCI bus numbers */
	pci_read_config_byte(dev, 0x44, &fbus);
	pci_read_config_byte(dev, 0x45, &lbus);
	info->bus_min = fbus;
	info->bus_max = lbus;

	/*
	 * Add the legacy IDE ports on bus 0
	 *
	 * These do not exist anywhere in the bridge registers, AFAICT. I do
	 * not have the datasheet, so this is the best I can do.
	 */
	if (fbus == 0) {
		update_res(info, 0x01f0, 0x01f7, IORESOURCE_IO, 0);
		update_res(info, 0x03f6, 0x03f6, IORESOURCE_IO, 0);
		update_res(info, 0x0170, 0x0177, IORESOURCE_IO, 0);
		update_res(info, 0x0376, 0x0376, IORESOURCE_IO, 0);
		update_res(info, 0xffa0, 0xffaf, IORESOURCE_IO, 0);
	}

	/* read the non-prefetchable memory window */
	pci_read_config_word(dev, 0xc0, &word1);
	pci_read_config_word(dev, 0xc2, &word2);
	if (word1 != word2) {
		res.start = (word1 << 16) | 0x0000;
		res.end   = (word2 << 16) | 0xffff;
		res.flags = IORESOURCE_MEM;
		update_res(info, res.start, res.end, res.flags, 0);
	}

	/* read the prefetchable memory window */
	pci_read_config_word(dev, 0xc4, &word1);
	pci_read_config_word(dev, 0xc6, &word2);
	if (word1 != word2) {
		res.start = (word1 << 16) | 0x0000;
		res.end   = (word2 << 16) | 0xffff;
		res.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;
		update_res(info, res.start, res.end, res.flags, 0);
	}

	/* read the IO port window */
	pci_read_config_word(dev, 0xd0, &word1);
	pci_read_config_word(dev, 0xd2, &word2);
	if (word1 != word2) {
		res.start = word1;
		res.end   = word2;
		res.flags = IORESOURCE_IO;
		update_res(info, res.start, res.end, res.flags, 0);
	}

	/* print information about this host bridge */
	res.start = fbus;
	res.end   = lbus;
	res.flags = IORESOURCE_BUS;
	dev_info(&dev->dev, "CNB20LE PCI Host Bridge (domain %04x %pR)\n",
			    pci_domain_nr(dev->bus), &res);

	for (i = 0; i < info->res_num; i++)
		dev_info(&dev->dev, "host bridge window %pR\n", &info->res[i]);
}

DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_LE,
			cnb20le_res);

