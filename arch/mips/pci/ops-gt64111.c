/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2002 by Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/pci.h>
#include <asm/io.h>
#include <asm/gt64120.h>

#include <asm/mach-cobalt/cobalt.h>

/*
 * Device 31 on the GT64111 is used to generate PCI special
 * cycles, so we shouldn't expected to find a device there ...
 */
static inline int pci_range_ck(struct pci_bus *bus, unsigned int devfn)
{
	if (bus->number == 0 && PCI_SLOT(devfn) < 31)
		return 0;

	return -1;
}

static int gt64111_pci_read_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 * val)
{
	if (pci_range_ck(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 4:
		PCI_CFG_SET(devfn, where);
		*val = GT_READ(GT_PCI0_CFGDATA_OFS);
		return PCIBIOS_SUCCESSFUL;

	case 2:
		PCI_CFG_SET(devfn, (where & ~0x3));
		*val = GT_READ(GT_PCI0_CFGDATA_OFS)
		    >> ((where & 3) * 8);
		return PCIBIOS_SUCCESSFUL;

	case 1:
		PCI_CFG_SET(devfn, (where & ~0x3));
		*val = GT_READ(GT_PCI0_CFGDATA_OFS)
		    >> ((where & 3) * 8);
		return PCIBIOS_SUCCESSFUL;
	}

	return PCIBIOS_BAD_REGISTER_NUMBER;
}

static int gt64111_pci_write_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 val)
{
	u32 tmp;

	if (pci_range_ck(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 4:
		PCI_CFG_SET(devfn, where);
		GT_WRITE(GT_PCI0_CFGDATA_OFS, val);

		return PCIBIOS_SUCCESSFUL;

	case 2:
		PCI_CFG_SET(devfn, (where & ~0x3));
		tmp = GT_READ(GT_PCI0_CFGDATA_OFS);
		tmp &= ~(0xffff << ((where & 0x3) * 8));
		tmp |= (val << ((where & 0x3) * 8));
		GT_WRITE(GT_PCI0_CFGDATA_OFS, tmp);

		return PCIBIOS_SUCCESSFUL;

	case 1:
		PCI_CFG_SET(devfn, (where & ~0x3));
		tmp = GT_READ(GT_PCI0_CFGDATA_OFS);
		tmp &= ~(0xff << ((where & 0x3) * 8));
		tmp |= (val << ((where & 0x3) * 8));
		GT_WRITE(GT_PCI0_CFGDATA_OFS, tmp);

		return PCIBIOS_SUCCESSFUL;
	}

	return PCIBIOS_BAD_REGISTER_NUMBER;
}

struct pci_ops gt64111_pci_ops = {
	.read = gt64111_pci_read_config,
	.write = gt64111_pci_write_config,
};
