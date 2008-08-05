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
#include <mach/hardware.h>
#include <asm/dma.h>

#include <mach/pxa-regs.h>

struct dma_channel {
	char *name;
	pxa_dma_prio prio;
	void (*irq_handler)(int, void *);
	void *data;
};

static struct dma_channel *dma_channels;
static int num_dma_channels;

int pxa_request_dma (char *name, pxa_dma_prio prio,
			 void (*irq_handler)(int, void *),
		 	 void *data)
{
	unsigned long flags;
	int i, found = 0;

	/* basic sanity checks */
	if (!name || !irq_handler)
		return -EINVAL;

	local_irq_save(flags);

	do {
		/* try grabbing a DMA channel with the requested priority */
		for (i = 0; i < num_dma_channels; i++) {
			if ((dma_channels[i].prio == prio) &&
			    !dma_channels[i].name) {
				found = 1;
				break;
			}
		}
		/* if requested prio group is full, try a hier priority */
	} while (!found && prio--);

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
			__func__, dma_ch);
		return;
	}

	local_irq_save(flags);
	DCSR(dma_ch) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
	dma_channels[dma_ch].name = NULL;
	local_irq_restore(flags);
}

static irqreturn_t dma_irq_handler(int irq, void *dev_id)
{
	int i, dint = DINT;

	for (i = 0; i < num_dma_channels; i++) {
		if (dint & (1 << i)) {
			struct dma_channel *channel = &dma_channels[i];
			if (channel->name && channel->irq_handler) {
				channel->irq_handler(i, channel->data);
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

int __init pxa_init_dma(int num_ch)
{
	int i, ret;

	dma_channels = kzalloc(sizeof(struct dma_channel) * num_ch, GFP_KERNEL);
	if (dma_channels == NULL)
		return -ENOMEM;

	ret = request_irq(IRQ_DMA, dma_irq_handler, IRQF_DISABLED, "DMA", NULL);
	if (ret) {
		printk (KERN_CRIT "Wow!  Can't register IRQ for DMA\n");
		kfree(dma_channels);
		return ret;
	}

	/* dma channel priorities on pxa2xx processors:
	 * ch 0 - 3,  16 - 19  <--> (0) DMA_PRIO_HIGH
	 * ch 4 - 7,  20 - 23  <--> (1) DMA_PRIO_MEDIUM
	 * ch 8 - 15, 24 - 31  <--> (2) DMA_PRIO_LOW
	 */
	for (i = 0; i < num_ch; i++)
		dma_channels[i].prio = min((i & 0xf) >> 2, DMA_PRIO_LOW);

	num_dma_channels = num_ch;
	return 0;
}

EXPORT_SYMBOL(pxa_request_dma);
EXPORT_SYMBOL(pxa_free_dma);
