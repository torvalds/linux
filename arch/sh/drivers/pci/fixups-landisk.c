/*
 * arch/sh/drivers/pci/fixups-landisk.c
 *
 * PCI initialization for the I-O DATA Device, Inc. LANDISK board
 *
 * Copyright (C) 2006 kogiidena
 * Copyright (C) 2010 Nobuhiro Iwamatsu
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "pci-sh4.h"

#define PCIMCR_MRSET_OFF	0xBFFFFFFF
#define PCIMCR_RFSH_OFF		0xFFFFFFFB

int pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	/*
	 * slot0: pin1-4 = irq5,6,7,8
	 * slot1: pin1-4 = irq6,7,8,5
	 * slot2: pin1-4 = irq7,8,5,6
	 * slot3: pin1-4 = irq8,5,6,7
	 */
	int irq = ((slot + pin - 1) & 0x3) + 5;

	if ((slot | (pin - 1)) > 0x3) {
		printk(KERN_WARNING "PCI: Bad IRQ mapping request for slot %d pin %c\n",
		       slot, pin - 1 + 'A');
		return -1;
	}
	return irq;
}

int pci_fixup_pcic(struct pci_channel *chan)
{
	unsigned long bcr1, mcr;

	bcr1 = __raw_readl(SH7751_BCR1);
	bcr1 |= 0x40080000;	/* Enable Bit 19 BREQEN, set PCIC to slave */
	pci_write_reg(chan, bcr1, SH4_PCIBCR1);

	mcr = __raw_readl(SH7751_MCR);
	mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
	pci_write_reg(chan, mcr, SH4_PCIMCR);

	pci_write_reg(chan, 0x0c000000, SH7751_PCICONF5);
	pci_write_reg(chan, 0xd0000000, SH7751_PCICONF6);
	pci_write_reg(chan, 0x0c000000, SH4_PCILAR0);
	pci_write_reg(chan, 0x00000000, SH4_PCILAR1);

	return 0;
}
