/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/marvell.h>

static int mv_read_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 * val)
{
	struct mv_pci_controller *mvbc = bus->sysdata;
	unsigned long address_reg, data_reg;
	u32 address;

	address_reg = mvbc->config_addr;
	data_reg = mvbc->config_vreg;

	/* Accessing device 31 crashes those Marvells.  Since years.
	   Will they ever make sane controllers ... */
	if (PCI_SLOT(devfn) == 31)
		return PCIBIOS_DEVICE_NOT_FOUND;

	address = (bus->number << 16) | (devfn << 8) |
	          (where & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	MV_WRITE(address_reg, address);

	switch (size) {
	case 1:
		*val = MV_READ_8(data_reg + (where & 0x3));
		break;

	case 2:
		*val = MV_READ_16(data_reg + (where & 0x3));
		break;

	case 4:
		*val = MV_READ(data_reg);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int mv_write_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 val)
{
	struct mv_pci_controller *mvbc = bus->sysdata;
	unsigned long address_reg, data_reg;
	u32 address;

	address_reg = mvbc->config_addr;
	data_reg = mvbc->config_vreg;

	/* Accessing device 31 crashes those Marvells.  Since years.
	   Will they ever make sane controllers ... */
	if (PCI_SLOT(devfn) == 31)
		return PCIBIOS_DEVICE_NOT_FOUND;

	address = (bus->number << 16) | (devfn << 8) |
	          (where & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	MV_WRITE(address_reg, address);

	switch (size) {
	case 1:
		MV_WRITE_8(data_reg + (where & 0x3), val);
		break;

	case 2:
		MV_WRITE_16(data_reg + (where & 0x3), val);
		break;

	case 4:
		MV_WRITE(data_reg, val);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops mv_pci_ops = {
	.read	= mv_read_config,
	.write	= mv_write_config
};
