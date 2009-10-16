/*
 * fuloong2e specific PCI support.
 *
 * Copyright (C) 1999, 2000, 2004  MIPS Technologies, Inc.
 *	All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin <wuzj@lemote.com>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <loongson.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#define CFG_SPACE_REG(offset) \
	(void *)CKSEG1ADDR(LOONGSON_PCICFG_BASE | (offset))
#define ID_SEL_BEGIN 11
#define MAX_DEV_NUM (31 - ID_SEL_BEGIN)


static int loongson_pcibios_config_access(unsigned char access_type,
				      struct pci_bus *bus,
				      unsigned int devfn, int where,
				      u32 *data)
{
	u32 busnum = bus->number;
	u32 addr, type;
	u32 dummy;
	void *addrp;
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);
	int reg = where & ~3;

	if (busnum == 0) {
		/* Type 0 configuration for onboard PCI bus */
		if (device > MAX_DEV_NUM)
			return -1;

		addr = (1 << (device + ID_SEL_BEGIN)) | (function << 8) | reg;
		type = 0;
	} else {
		/* Type 1 configuration for offboard PCI bus */
		addr = (busnum << 16) | (device << 11) | (function << 8) | reg;
		type = 0x10000;
	}

	/* Clear aborts */
	LOONGSON_PCICMD |= LOONGSON_PCICMD_MABORT_CLR | \
				LOONGSON_PCICMD_MTABORT_CLR;

	LOONGSON_PCIMAP_CFG = (addr >> 16) | type;

	/* Flush Bonito register block */
	dummy = LOONGSON_PCIMAP_CFG;
	mmiowb();

	addrp = CFG_SPACE_REG(addr & 0xffff);
	if (access_type == PCI_ACCESS_WRITE)
		writel(cpu_to_le32(*data), addrp);
	else
		*data = le32_to_cpu(readl(addrp));

	/* Detect Master/Target abort */
	if (LOONGSON_PCICMD & (LOONGSON_PCICMD_MABORT_CLR |
			     LOONGSON_PCICMD_MTABORT_CLR)) {
		/* Error occurred */

		/* Clear bits */
		LOONGSON_PCICMD |= (LOONGSON_PCICMD_MABORT_CLR |
				  LOONGSON_PCICMD_MTABORT_CLR);

		return -1;
	}

	return 0;

}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int loongson_pcibios_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 *val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (loongson_pcibios_config_access(PCI_ACCESS_READ, bus, devfn, where,
				       &data))
		return -1;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int loongson_pcibios_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		data = val;
	else {
		if (loongson_pcibios_config_access(PCI_ACCESS_READ, bus, devfn,
					where, &data))
			return -1;

		if (size == 1)
			data = (data & ~(0xff << ((where & 3) << 3))) |
				(val << ((where & 3) << 3));
		else if (size == 2)
			data = (data & ~(0xffff << ((where & 3) << 3))) |
				(val << ((where & 3) << 3));
	}

	if (loongson_pcibios_config_access(PCI_ACCESS_WRITE, bus, devfn, where,
				       &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops loongson_pci_ops = {
	.read = loongson_pcibios_read,
	.write = loongson_pcibios_write
};
