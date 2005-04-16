/*
 *  linux/arch/arm/mach-pxa/dma.c
 *
 *  PXA DMA registration and IRQ dispatching
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/dma.h>

#include <asm/arch/pxa-regs.h>

static struct dma_channel {
	char *name;
	void (*irq_handler)(int, void *, struct pt_regs *);
	void *data;
} dma_channels[PXA_DMA_CHANNELS];


int pxa_request_dma (char *name, pxa_dma_prio prio,
			 void (*irq_handler)(int, void *, struct pt_regs *),
		 	 void *data)
{
	unsigned long flags;
	int i, found = 0;

	/* basic sanity checks */
	if (!name || !irq_handler)
		return -EINVAL;

	local_irq_save(flags);

	/* try grabbing a DMA channel with the requested priority */
	for (i = prio; i < prio + PXA_DMA_NBCH(prio); i++) {
		if (!dma_channels[i].name) {
			found = 1;
			break;
		}
	}

	if (!found) {
		/* requested prio group is full, try hier priorities */
		for (i = prio-1; i >= 0; i--) {
			if (!dma_channels[i].name) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		DCSR(i) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
		dma_channels[i].name = name;
		dma_channels[i].irq_handler = irq_handler;
		dma_channels[i].data = data;
	} else {
		printk (KERN_WARNING "No more available DMA channels for %s\n", name);
		i = -ENODEV;
	}

	local_irq_restore(flags);
	return i;
}

void pxa_free_dma (int dma_ch)
{
	unsigned long flags;

	if (!dma_channels[dma_ch].name) {
		printk (KERN_CRIT
			"%s: trying to free channel %d which is already freed\n",
			__FUNCTION__, dma_ch);
		return;
	}

	local_irq_save(flags);
	DCSR(dma_ch) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
	dma_channels[dma_ch].name = NULL;
	local_irq_restore(flags);
}

static irqreturn_t dma_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int i, dint = DINT;

	for (i = 0; i < PXA_DMA_CHANNELS; i++) {
		if (dint & (1 << i)) {
			struct dma_channel *channel = &dma_channels[i];
			if (channel->name && channel->irq_handler) {
				channel->irq_handler(i, channel->data, regs);
			} else {
				/*
				 * IRQ for an unregistered DMA channel:
				 * let's clear the interrupts and disable it.
				 */
				printk (KERN_WARNING "spurious IRQ for DMA channel %d\n", i);
				DCSR(i) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
			}
		}
	}
	return IRQ_HANDLED;
}

static int __init pxa_dma_init (void)
{
	int ret;

	ret = request_irq (IRQ_DMA, dma_irq_handler, 0, "DMA", NULL);
	if (ret)
		printk (KERN_CRIT "Wow!  Can't register IRQ for DMA\n");
	return ret;
}

arch_initcall(pxa_dma_init);

EXPORT_SYMBOL(pxa_request_dma);
EXPORT_SYMBOL(pxa_free_dma);

