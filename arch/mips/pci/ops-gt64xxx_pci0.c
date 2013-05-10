/*
 * Copyright (C) 1999, 2000, 2004  MIPS Technologies, Inc.
 *	All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>

#include <asm/gt64120.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

/*
 *  PCI configuration cycle AD bus definition
 */
/* Type 0 */
#define PCI_CFG_TYPE0_REG_SHF           0
#define PCI_CFG_TYPE0_FUNC_SHF          8

/* Type 1 */
#define PCI_CFG_TYPE1_REG_SHF           0
#define PCI_CFG_TYPE1_FUNC_SHF          8
#define PCI_CFG_TYPE1_DEV_SHF           11
#define PCI_CFG_TYPE1_BUS_SHF           16

static int gt64xxx_pci0_pcibios_config_access(unsigned char access_type,
		struct pci_bus *bus, unsigned int devfn, int where, u32 * data)
{
	unsigned char busnum = bus->number;
	u32 intr;

	if ((busnum == 0) && (devfn >= PCI_DEVFN(31, 0)))
		return -1;	/* Because of a bug in the galileo (for slot 31). */

	/* Clear cause register bits */
	GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
	                             GT_INTRCAUSE_TARABORT0_BIT));

	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (busnum << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (devfn << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((where / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);

	if (access_type == PCI_ACCESS_WRITE) {
		if (busnum == 0 && PCI_SLOT(devfn) == 0) {
			/*
			 * The Galileo system controller is acting
			 * differently than other devices.
			 */
			GT_WRITE(GT_PCI0_CFGDATA_OFS, *data);
		} else
			__GT_WRITE(GT_PCI0_CFGDATA_OFS, *data);
	} else {
		if (busnum == 0 && PCI_SLOT(devfn) == 0) {
			/*
			 * The Galileo system controller is acting
			 * differently than other devices.
			 */
			*data = GT_READ(GT_PCI0_CFGDATA_OFS);
		} else
			*data = __GT_READ(GT_PCI0_CFGDATA_OFS);
	}

	/* Check for master or target abort */
	intr = GT_READ(GT_INTRCAUSE_OFS);

	if (intr & (GT_INTRCAUSE_MASABORT0_BIT | GT_INTRCAUSE_TARABORT0_BIT)) {
		/* Error occurred */

		/* Clear bits */
		GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
		                             GT_INTRCAUSE_TARABORT0_BIT));

		return -1;
	}

	return 0;
}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int gt64xxx_pci0_pcibios_read(struct pci_bus *bus, unsigned int devfn,
		int where, int size, u32 * val)
{
	u32 data = 0;

	if (gt64xxx_pci0_pcibios_config_access(PCI_ACCESS_READ, bus, devfn,
	                                       where, &data))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int gt64xxx_pci0_pcibios_write(struct pci_bus *bus, unsigned int devfn,
		int where, int size, u32 val)
{
	u32 data = 0;

	if (size == 4)
		data = val;
	else {
		if (gt64xxx_pci0_pcibios_config_access(PCI_ACCESS_READ, bus,
		                                       devfn, where, &data))
			return PCIBIOS_DEVICE_NOT_FOUND;

		if (size == 1)
			data = (data & ~(0xff << ((where & 3) << 3))) |
				(val << ((where & 3) << 3));
		else if (size == 2)
			data = (data & ~(0xffff << ((where & 3) << 3))) |
				(val << ((where & 3) << 3));
	}

	if (gt64xxx_pci0_pcibios_config_access(PCI_ACCESS_WRITE, bus, devfn,
	                                       where, &data))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops gt64xxx_pci0_ops = {
	.read	= gt64xxx_pci0_pcibios_read,
	.write	= gt64xxx_pci0_pcibios_write
};
