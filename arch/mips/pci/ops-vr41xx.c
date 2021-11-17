// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ops-vr41xx.c, PCI configuration routines for the PCIU of NEC VR4100 series.
 *
 *  Copyright (C) 2001-2003 MontaVista Software Inc.
 *    Author: Yoichi Yuasa <source@mvista.com>
 *  Copyright (C) 2004-2005  Yoichi Yuasa <yuasa@linux-mips.org>
 */
/*
 * Changes:
 *  MontaVista Software Inc. <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/pci.h>
#include <linux/types.h>

#include <asm/io.h>

#define PCICONFDREG	(void __iomem *)KSEG1ADDR(0x0f000c14)
#define PCICONFAREG	(void __iomem *)KSEG1ADDR(0x0f000c18)

static inline int set_pci_configuration_address(unsigned char number,
						unsigned int devfn, int where)
{
	if (number == 0) {
		/*
		 * Type 0 configuration
		 */
		if (PCI_SLOT(devfn) < 11 || where > 0xff)
			return -EINVAL;

		writel((1U << PCI_SLOT(devfn)) | (PCI_FUNC(devfn) << 8) |
		       (where & 0xfc), PCICONFAREG);
	} else {
		/*
		 * Type 1 configuration
		 */
		if (where > 0xff)
			return -EINVAL;

		writel(((uint32_t)number << 16) | ((devfn & 0xff) << 8) |
		       (where & 0xfc) | 1U, PCICONFAREG);
	}

	return 0;
}

static int pci_config_read(struct pci_bus *bus, unsigned int devfn, int where,
			   int size, uint32_t *val)
{
	uint32_t data;

	*val = 0xffffffffU;
	if (set_pci_configuration_address(bus->number, devfn, where) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data = readl(PCICONFDREG);

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
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int pci_config_write(struct pci_bus *bus, unsigned int devfn, int where,
			    int size, uint32_t val)
{
	uint32_t data;
	int shift;

	if (set_pci_configuration_address(bus->number, devfn, where) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data = readl(PCICONFDREG);

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
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	writel(data, PCICONFDREG);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops vr41xx_pci_ops = {
	.read	= pci_config_read,
	.write	= pci_config_write,
};
