// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fixup-tb0287.c, The TANBAC TB0287 specific PCI fixups.
 *
 *  Copyright (C) 2005	Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/tb0287.h>

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	unsigned char bus;
	int irq = -1;

	bus = dev->bus->number;
	if (bus == 0) {
		switch (slot) {
		case 16:
			irq = TB0287_SM501_IRQ;
			break;
		case 17:
			irq = TB0287_SIL680A_IRQ;
			break;
		default:
			break;
		}
	} else if (bus == 1) {
		switch (PCI_SLOT(dev->devfn)) {
		case 0:
			irq = TB0287_PCI_SLOT_IRQ;
			break;
		case 2:
		case 3:
			irq = TB0287_RTL8110_IRQ;
			break;
		default:
			break;
		}
	} else if (bus > 1) {
		irq = TB0287_PCI_SLOT_IRQ;
	}

	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
