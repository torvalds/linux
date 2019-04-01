// SPDX-License-Identifier: GPL-2.0
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

static void gapspci_fixup_resources(struct pci_dev *dev)
{
	struct pci_channel *p = dev->sysdata;
	struct resource res;
	struct pci_bus_region region;

	printk(KERN_NOTICE "PCI: Fixing up device %s\n", pci_name(dev));

	switch (dev->device) {
	case PCI_DEVICE_ID_SEGA_BBA:
		/*
		 * We also assume that dev->devfn == 0
		 */
		dev->resource[1].start	= p->resources[0].start  + 0x100;
		dev->resource[1].end	= dev->resource[1].start + 0x200 - 1;

		/*
		 * This is not a normal BAR, prevent any attempts to move
		 * the BAR, as this will result in a bus lock.
		 */
		dev->resource[1].flags |= IORESOURCE_PCI_FIXED;

		/*
		 * Redirect dma memory allocations to special memory window.
		 *
		 * If this GAPSPCI region were mapped by a BAR, the CPU
		 * phys_addr_t would be pci_resource_start(), and the bus
		 * address would be pci_bus_address(pci_resource_start()).
		 * But apparently there's no BAR mapping it, so we just
		 * "know" its CPU address is GAPSPCI_DMA_BASE.
		 */
		res.start = GAPSPCI_DMA_BASE;
		res.end = GAPSPCI_DMA_BASE + GAPSPCI_DMA_SIZE - 1;
		res.flags = IORESOURCE_MEM;
		pcibios_resource_to_bus(dev->bus, &region, &res);
		BUG_ON(dma_declare_coherent_memory(&dev->dev,
						res.start,
						region.start,
						resource_size(&res)));
		break;
	default:
		printk("PCI: Failed resource fixup\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, gapspci_fixup_resources);

int pcibios_map_platform_irq(const struct pci_dev *dev, u8 slot, u8 pin)
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
