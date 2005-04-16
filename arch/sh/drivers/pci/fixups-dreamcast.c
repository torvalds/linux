/*
 * arch/sh/pci/fixups-dreamcast.c
 *
 * PCI fixups for the Sega Dreamcast
 *
 * Copyright (C) 2001, 2002  M. R. Brown
 * Copyright (C) 2002, 2003  Paul Mundt
 *
 * This file originally bore the message (with enclosed-$):
 *	Id: pci.c,v 1.3 2003/05/04 19:29:46 lethal Exp
 *	Dreamcast PCI: Supports SEGA Broadband Adaptor only.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>

static void __init gapspci_fixup_resources(struct pci_dev *dev)
{
	struct pci_channel *p = board_pci_channels;

	printk(KERN_NOTICE "PCI: Fixing up device %s\n", pci_name(dev));

	switch (dev->device) {
	case PCI_DEVICE_ID_SEGA_BBA:
		/*
		 * We also assume that dev->devfn == 0
		 */
		dev->resource[1].start	= p->io_resource->start  + 0x100;
		dev->resource[1].end	= dev->resource[1].start + 0x200 - 1;
		break;
	default:
		printk("PCI: Failed resource fixup\n");
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, gapspci_fixup_resources);

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	/* 
	 * We don't have any sub bus to fix up, and this is a rather
	 * stupid place to put general device fixups. Don't do it.
	 * Use the pcibios_fixups table or suffer the consequences.
	 */
}

void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev = 0;

	for_each_pci_dev(dev) {
		/*
		 * The interrupt routing semantics here are quite trivial.
		 *
		 * We basically only support one interrupt, so we only bother
		 * updating a device's interrupt line with this single shared
		 * interrupt. Keeps routing quite simple, doesn't it?
		 */
		printk(KERN_NOTICE "PCI: Fixing up IRQ routing for device %s\n",
		       pci_name(dev));

		dev->irq = GAPSPCI_IRQ;

		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
}

