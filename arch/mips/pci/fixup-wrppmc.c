/*
 * fixup-wrppmc.c: PPMC board specific PCI fixup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006, Wind River Inc. Rongkai.zhan (rongkai.zhan@windriver.com)
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/gt64120.h>

/* PCI interrupt pins */
#define PCI_INTA		1
#define PCI_INTB		2
#define PCI_INTC		3
#define PCI_INTD		4

#define PCI_SLOT_MAXNR	32 /* Each PCI bus has 32 physical slots */

static char pci_irq_tab[PCI_SLOT_MAXNR][5] __initdata = {
	/* 0    INTA   INTB   INTC   INTD */
	[0] = {0, 0, 0, 0, 0},		/* Slot 0: GT64120 PCI bridge */
	[6] = {0, WRPPMC_PCI_INTA_IRQ, 0, 0, 0},
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return pci_irq_tab[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
