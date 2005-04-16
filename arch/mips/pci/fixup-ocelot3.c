/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 Montavista Software Inc.
 * Author: Manish Lachwani (mlachwani@mvista.com)
 *
 * Looking at the schematics for the Ocelot-3 board, there are
 * two PCI busses and each bus has two PCI slots.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mipsregs.h>

/*
 * Do platform specific device initialization at
 * pci_enable_device() time
 */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int bus = dev->bus->number;

	if (bus == 0 && slot == 1)
		return 2;	/* PCI-X A */
	if (bus == 0 && slot == 2)
		return 3;	/* PCI-X B */
	if (bus == 1 && slot == 1)
		return 4;	/* PCI A */
	if (bus == 1 && slot == 2)
		return 5;	/* PCI B */

return 0;
	panic("Whooops in pcibios_map_irq");
}
