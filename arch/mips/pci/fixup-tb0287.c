/*
 *  fixup-tb0287.c, The TANBAC TB0287 specific PCI fixups.
 *
 *  Copyright (C) 2005  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
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
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/tb0287.h>

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
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
