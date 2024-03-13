/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <bcm63xx_cpu.h>

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return bcm63xx_get_irq_number(IRQ_PCI);
}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
