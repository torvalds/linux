/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Marvell MV64340 interrupt fixup code.
 *
 * Marvell wants an NDA for their docs so this was written without
 * documentation.  You've been warned.
 *
 * Copyright (C) 2004 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/mipsregs.h>

/*
 * WARNING: Example of how _NOT_ to do it.
 */
int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int bus = dev->bus->number;

	if (bus == 0 && slot == 1)
		return 3;	/* PCI-X A */
	if (bus == 0 && slot == 2)
		return 4;	/* PCI-X B */
	if (bus == 1 && slot == 1)
		return 5;	/* PCI A */
	if (bus == 1 && slot == 2)
		return 6;	/* PCI B */

return 0;
	panic("Whooops in pcibios_map_irq");
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
