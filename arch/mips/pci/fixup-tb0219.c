// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fixup-tb0219.c, The TANBAC TB0219 specific PCI fixups.
 *
 *  Copyright (C) 2003	Megasolution Inc. <matsu@megasolution.jp>
 *  Copyright (C) 2004-2005  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/tb0219.h>

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	switch (slot) {
	case 12:
		irq = TB0219_PCI_SLOT1_IRQ;
		break;
	case 13:
		irq = TB0219_PCI_SLOT2_IRQ;
		break;
	case 14:
		irq = TB0219_PCI_SLOT3_IRQ;
		break;
	default:
		break;
	}

	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
