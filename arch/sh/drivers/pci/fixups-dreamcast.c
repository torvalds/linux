/*
 * arch/sh/drivers/pci/fixups-dreamcast.c
 *
 * PCI fixups for the Sega Dreamcast
 *
 * Copyright (C) 2001, 2002  M. R. Brown
 * Copyright (C) 2002, 2003, 2006  Paul Mundt
 *
 * This file originally bore the message (with enclosed-$):
 *	Id: pci.c,v 1.3 2003/05/04 19:29:46 lethal Exp
 *	Dreamcast PCI: Supports SEGA Broadband Adaptor only.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <mach/pci.h>

static void __init gapspci_fixup_resources(struct pci_dev *dev)
{
	struct pci_channel *p = dev->sysdata;

	printk(KERN_NOTICE "PCI: Fixing up device %s\n", pci_name(dev));

	switch (dev->device) {
	case PCI_DEVICE_ID_SEGA_BBA:
		/*
		 * We also assume that dev->devfn == 0
		 */
		dev->resource[1].start	= p->io_resource->start  + 0x100;
		dev->resource[1].end	= dev->resource[1].start + 0x200 - 1;

		/*
		 * This is not a normal BAR, prevent any attempts to move
		 * the BAR, as this will result in a bus lock.
		 */
		dev->resource[1].flags |= IORESOURCE_PCI_FIXED;

		/*
		 * Redirect dma memory allocations to special memory window.
		 */
		BUG_ON(!dma_declare_coherent_memory(&dev->dev,
						GAPSPCI_DMA_BASE,
						GAPSPCI_DMA_BASE,
						GAPSPCI_DMA_SIZE,
						DMA_MEMORY_MAP |
						DMA_MEMORY_EXCLUSIVE));
		break;
	default:
		printk("PCI: Failed resource fixup\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, gapspci_fixup_resources);

int __init pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/*
	 * The interrupt routing semantics here are quite trivial.
	 *
	 * We basically only support one interrupt, so we only bother
	 * updating a device's interrupt line with this single shared
	 * interrupt. Keeps routing quite simple, doesn't it?
	 */
	return GAPSPCI_IRQ;
}
