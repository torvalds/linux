/*
 *  Copyright (C) NEC Electronics Corporation 2004-2006
 *
 *  This file is based on the arch/mips/pci/ops-vr41xx.c
 *
 *	Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/debug.h>

#include <asm/emma/emma2rh.h>

#define RTABORT (0x1<<9)
#define RMABORT (0x1<<10)
#define EMMA2RH_PCI_SLOT_NUM 9	/* 0000:09.0 is final PCI device */

/*
 * access config space
 */

static int check_args(struct pci_bus *bus, u32 devfn, u32 * bus_num)
{
	/* check if the bus is top-level */
	if (bus->parent != NULL) {
		*bus_num = bus->number;
		db_assert(bus_num != NULL);
	} else
		*bus_num = 0;

	if (*bus_num == 0) {
		/* Type 0 */
		if (PCI_SLOT(devfn) >= 10)
			return PCIBIOS_DEVICE_NOT_FOUND;
	} else {
		/* Type 1 */
		if ((*bus_num >= 64) || (PCI_SLOT(devfn) >= 16))
			return PCIBIOS_DEVICE_NOT_FOUND;
	}
	return 0;
}

static inline int set_pci_configuration_address(unsigned char bus_num,
						unsigned int devfn, int where)
{
	u32 config_win0;

	emma2rh_out32(EMMA2RH_PCI_INT, ~RMABORT);
	if (bus_num == 0)
		/*
		 * Type 0 configuration
		 */
		config_win0 = (1 << (22 + PCI_SLOT(devfn))) | (5 << 9);
	else
		/*
		 * Type 1 configuration
		 */
		config_win0 = (bus_num << 26) | (PCI_SLOT(devfn) << 22) |
		    (1 << 15) | (5 << 9);

	emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, config_win0);

	return 0;
}

static int pci_config_read(struct pci_bus *bus, unsigned int devfn, int where,
			   int size, uint32_t * val)
{
	u32 bus_num;
	u32 base = KSEG1ADDR(EMMA2RH_PCI_CONFIG_BASE);
	u32 backup_win0;
	u32 data;

	*val = 0xffffffffU;

	if (check_args(bus, devfn, &bus_num) == PCIBIOS_DEVICE_NOT_FOUND)
		return PCIBIOS_DEVICE_NOT_FOUND;

	backup_win0 = emma2rh_in32(EMMA2RH_PCI_IWIN0_CTR);

	if (set_pci_configuration_address(bus_num, devfn, where) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data =
	    *(volatile u32 *)(base + (PCI_FUNC(devfn) << 8) +
			      (where & 0xfffffffc));

	switch (size) {
	case 1:
		*val = (data >> ((where & 3) << 3)) & 0xffU;
		break;
	case 2:
		*val = (data >> ((where & 2) << 3)) & 0xffffU;
		break;
	case 4:
		*val = data;
		break;
	default:
		emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, backup_win0);
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, backup_win0);

	if (emma2rh_in32(EMMA2RH_PCI_INT) & RMABORT)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_config_write(struct pci_bus *bus, unsigned int devfn, int where,
			    int size, u32 val)
{
	u32 bus_num;
	u32 base = KSEG1ADDR(EMMA2RH_PCI_CONFIG_BASE);
	u32 backup_win0;
	u32 data;
	int shift;

	if (check_args(bus, devfn, &bus_num) == PCIBIOS_DEVICE_NOT_FOUND)
		return PCIBIOS_DEVICE_NOT_FOUND;

	backup_win0 = emma2rh_in32(EMMA2RH_PCI_IWIN0_CTR);

	if (set_pci_configuration_address(bus_num, devfn, where) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* read modify write */
	data =
	    *(volatile u32 *)(base + (PCI_FUNC(devfn) << 8) +
			      (where & 0xfffffffc));

	switch (size) {
	case 1:
		shift = (where & 3) << 3;
		data &= ~(0xffU << shift);
		data |= ((val & 0xffU) << shift);
		break;
	case 2:
		shift = (where & 2) << 3;
		data &= ~(0xffffU << shift);
		data |= ((val & 0xffffU) << shift);
		break;
	case 4:
		data = val;
		break;
	default:
		emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, backup_win0);
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}
	*(volatile u32 *)(base + (PCI_FUNC(devfn) << 8) +
			  (where & 0xfffffffc)) = data;

	emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, backup_win0);
	if (emma2rh_in32(EMMA2RH_PCI_INT) & RMABORT)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops emma2rh_pci_ops = {
	.read = pci_config_read,
	.write = pci_config_write,
};
