/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SNI specific PCI support for RM200/RM300.
 *
 * Copyright (C) 1997 - 2000, 2003 Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/sni.h>

/*
 * It seems that on the RM200 only lower 3 bits of the 5 bit PCI device
 * address are decoded.  We therefore manually have to reject attempts at
 * reading outside this range.  Being on the paranoid side we only do this
 * test for bus 0 and hope forwarding and decoding work properly for any
 * subordinated busses.
 *
 * ASIC PCI only supports type 1 config cycles.
 */
static int set_config_address(unsigned int busno, unsigned int devfn, int reg)
{
	if ((devfn > 255) || (reg > 255))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (busno == 0 && devfn >= PCI_DEVFN(8, 0))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*(volatile u32 *)PCIMT_CONFIG_ADDRESS =
		 ((busno    & 0xff) << 16) |
	         ((devfn    & 0xff) <<  8) |
	          (reg      & 0xfc);

	return PCIBIOS_SUCCESSFUL;
}

static int pcimt_read(struct pci_bus *bus, unsigned int devfn, int reg,
		      int size, u32 * val)
{
	int res;

	if ((res = set_config_address(bus->number, devfn, reg)))
		return res;

	switch (size) {
	case 1:
		*val = *(volatile  u8 *) (PCIMT_CONFIG_DATA + (reg & 3));
		break;
	case 2:
		*val = *(volatile u16 *) (PCIMT_CONFIG_DATA + (reg & 2));
		break;
	case 4:
		*val = *(volatile u32 *) PCIMT_CONFIG_DATA;
		break;
	}

	return 0;
}

static int pcimt_write(struct pci_bus *bus, unsigned int devfn, int reg,
		       int size, u32 val)
{
	int res;

	if ((res = set_config_address(bus->number, devfn, reg)))
		return res;

	switch (size) {
	case 1:
		*(volatile  u8 *) (PCIMT_CONFIG_DATA + (reg & 3)) = val;
		break;
	case 2:
		*(volatile u16 *) (PCIMT_CONFIG_DATA + (reg & 2)) = val;
		break;
	case 4:
		*(volatile u32 *) PCIMT_CONFIG_DATA = val;
		break;
	}

	return 0;
}

struct pci_ops sni_pci_ops = {
	.read = pcimt_read,
	.write = pcimt_write,
};
