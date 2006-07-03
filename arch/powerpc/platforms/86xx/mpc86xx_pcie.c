/*
 * Support for indirect PCI bridges.
 *
 * Copyright (C) 1998 Gabriel Paubert.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * "Temporary" MPC8548 Errata file -
 * The standard indirect_pci code should work with future silicon versions.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#include "mpc86xx.h"

#define PCI_CFG_OUT out_be32

/* ERRATA PCI-Ex 14 PCIE Controller timeout */
#define PCIE_FIX		out_be32(hose->cfg_addr+0x4, 0x0400ffff)


static int
indirect_read_config_pcie(struct pci_bus *bus, unsigned int devfn, int offset,
		     int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile void __iomem *cfg_data;
	u32 temp;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	/* Possible artifact of CDCpp50937 needs further investigation */
	if (devfn != 0x0 && bus->number == 0xff)
		return PCIBIOS_DEVICE_NOT_FOUND;

	PCIE_FIX;
	if (bus->number == 0xff) {
		PCI_CFG_OUT(hose->cfg_addr,
			    (0x80000000 | ((offset & 0xf00) << 16) |
			     ((bus->number - hose->bus_offset) << 16)
			     | (devfn << 8) | ((offset & 0xfc) )));
	} else {
		PCI_CFG_OUT(hose->cfg_addr,
			    (0x80000001 | ((offset & 0xf00) << 16) |
			     ((bus->number - hose->bus_offset) << 16)
			     | (devfn << 8) | ((offset & 0xfc) )));
	}

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	/* ERRATA PCI-Ex 12 - Configuration Address/Data Alignment */
	cfg_data = hose->cfg_data;
	PCIE_FIX;
	temp = in_le32(cfg_data);
	switch (len) {
	case 1:
		*val = (temp >> (((offset & 3))*8)) & 0xff;
		break;
	case 2:
		*val = (temp >> (((offset & 3))*8)) & 0xffff;
		break;
	default:
		*val = temp;
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
indirect_write_config_pcie(struct pci_bus *bus, unsigned int devfn, int offset,
		      int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile void __iomem *cfg_data;
	u32 temp;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	/* Possible artifact of CDCpp50937 needs further investigation */
	if (devfn != 0x0 && bus->number == 0xff)
		return PCIBIOS_DEVICE_NOT_FOUND;

	PCIE_FIX;
	if (bus->number == 0xff) {
		PCI_CFG_OUT(hose->cfg_addr,
			    (0x80000000 | ((offset & 0xf00) << 16) |
			     ((bus->number - hose->bus_offset) << 16)
			     | (devfn << 8) | ((offset & 0xfc) )));
	} else {
		PCI_CFG_OUT(hose->cfg_addr,
			    (0x80000001 | ((offset & 0xf00) << 16) |
			     ((bus->number - hose->bus_offset) << 16)
			     | (devfn << 8) | ((offset & 0xfc) )));
        }

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	/* ERRATA PCI-Ex 12 - Configuration Address/Data Alignment */
	cfg_data = hose->cfg_data;
	switch (len) {
	case 1:
		PCIE_FIX;
		temp = in_le32(cfg_data);
		temp = (temp & ~(0xff << ((offset & 3) * 8))) |
			(val << ((offset & 3) * 8));
		PCIE_FIX;
		out_le32(cfg_data, temp);
		break;
	case 2:
		PCIE_FIX;
		temp = in_le32(cfg_data);
		temp = (temp & ~(0xffff << ((offset & 3) * 8)));
		temp |= (val << ((offset & 3) * 8)) ;
		PCIE_FIX;
		out_le32(cfg_data, temp);
		break;
	default:
		PCIE_FIX;
		out_le32(cfg_data, val);
		break;
	}
	PCIE_FIX;
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops indirect_pcie_ops = {
	indirect_read_config_pcie,
	indirect_write_config_pcie
};

void __init
setup_indirect_pcie_nomap(struct pci_controller* hose, void __iomem * cfg_addr,
	void __iomem * cfg_data)
{
	hose->cfg_addr = cfg_addr;
	hose->cfg_data = cfg_data;
	hose->ops = &indirect_pcie_ops;
}

void __init
setup_indirect_pcie(struct pci_controller* hose, u32 cfg_addr, u32 cfg_data)
{
	unsigned long base = cfg_addr & PAGE_MASK;
	void __iomem *mbase, *addr, *data;

	mbase = ioremap(base, PAGE_SIZE);
	addr = mbase + (cfg_addr & ~PAGE_MASK);
	if ((cfg_data & PAGE_MASK) != base)
		mbase = ioremap(cfg_data & PAGE_MASK, PAGE_SIZE);
	data = mbase + (cfg_data & ~PAGE_MASK);
	setup_indirect_pcie_nomap(hose, addr, data);
}
