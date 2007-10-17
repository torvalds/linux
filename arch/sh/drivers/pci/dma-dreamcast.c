/*
 * arch/sh/drivers/pci/dma-dreamcast.c
 *
 * PCI DMA support for the Sega Dreamcast
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

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>

static int gapspci_dma_used = 0;

void *dreamcast_consistent_alloc(struct device *dev, size_t size,
				 dma_addr_t *dma_handle, gfp_t flag)
{
	unsigned long buf;

	if (dev && dev->bus != &pci_bus_type)
		return NULL;

	if (gapspci_dma_used + size > GAPSPCI_DMA_SIZE)
		return ERR_PTR(-EINVAL);

	buf = GAPSPCI_DMA_BASE + gapspci_dma_used;

	gapspci_dma_used = PAGE_ALIGN(gapspci_dma_used+size);

	*dma_handle = (dma_addr_t)buf;

	buf = P2SEGADDR(buf);

	/* Flush the dcache before we hand off the buffer */
	__flush_purge_region((void *)buf, size);

	return (void *)buf;
}

int dreamcast_consistent_free(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	if (dev && dev->bus != &pci_bus_type)
		return -EINVAL;

	/* XXX */
	gapspci_dma_used = 0;

	return 0;
}

