/*
 * linux/arch/unicore32/kernel/dma.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/dma.h>

struct dma_channel {
	char *name;
	puv3_dma_prio prio;
	void (*irq_handler)(int, void *);
	void (*err_handler)(int, void *);
	void *data;
};

static struct dma_channel dma_channels[MAX_DMA_CHANNELS];

int puv3_request_dma(char *name, puv3_dma_prio prio,
			 void (*irq_handler)(int, void *),
			 void (*err_handler)(int, void *),
			 void *data)
{
	unsigned long flags;
	int i, found = 0;

	/* basic sanity checks */
	if (!name)
		return -EINVAL;

	local_irq_save(flags);

	do {
		/* try grabbing a DMA channel with the requested priority */
		for (i = 0; i < MAX_DMA_CHANNELS; i++) {
			if ((dma_channels[i].prio == prio) &&
			    !dma_channels[i].name) {
				found = 1;
				break;
			}
		}
		/* if requested prio group is full, try a hier priority */
	} while (!found && prio--);

	if (found) {
		dma_channels[i].name = name;
		dma_channels[i].irq_handler = irq_handler;
		dma_channels[i].err_handler = err_handler;
		dma_channels[i].data = data;
	} else {
		printk(KERN_WARNING "No more available DMA channels for %s\n",
				name);
		i = -ENODEV;
	}

	local_irq_restore(flags);
	return i;
}
EXPORT_SYMBOL(puv3_request_dma);

void puv3_free_dma(int dma_ch)
{
	unsigned long flags;

	if (!dma_channels[dma_ch].name) {
		printk(KERN_CRIT
			"%s: trying to free channel %d which is already freed\n",
			__func__, dma_ch);
		return;
	}

	local_irq_save(flags);
	dma_channels[dma_ch].name = NULL;
	dma_channels[dma_ch].err_handler = NULL;
	local_irq_restore(flags);
}
EXPORT_SYMBOL(puv3_free_dma);

static irqreturn_t dma_irq_handler(int irq, void *dev_id)
{
	int i, dint;

	dint = readl(DMAC_ITCSR);
	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		if (dint & DMAC_CHANNEL(i)) {
			struct dma_channel *channel = &dma_channels[i];

			/* Clear TC interrupt of channel i */
			writel(DMAC_CHANNEL(i), DMAC_ITCCR);
			writel(0, DMAC_ITCCR);

			if (channel->name && channel->irq_handler) {
				channel->irq_handler(i, channel->data);
			} else {
				/*
				 * IRQ for an unregistered DMA channel:
				 * let's clear the interrupts and disable it.
				 */
				printk(KERN_WARNING "spurious IRQ for"
						" DMA channel %d\n", i);
			}
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t dma_err_handler(int irq, void *dev_id)
{
	int i, dint;

	dint = readl(DMAC_IESR);
	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		if (dint & DMAC_CHANNEL(i)) {
			struct dma_channel *channel = &dma_channels[i];

			/* Clear Err interrupt of channel i */
			writel(DMAC_CHANNEL(i), DMAC_IECR);
			writel(0, DMAC_IECR);

			if (channel->name && channel->err_handler) {
				channel->err_handler(i, channel->data);
			} else {
				/*
				 * IRQ for an unregistered DMA channel:
				 * let's clear the interrupts and disable it.
				 */
				printk(KERN_WARNING "spurious IRQ for"
						" DMA channel %d\n", i);
			}
		}
	}
	return IRQ_HANDLED;
}

int __init puv3_init_dma(void)
{
	int i, ret;

	/* dma channel priorities on v8 processors:
	 * ch 0 - 1  <--> (0) DMA_PRIO_HIGH
	 * ch 2 - 3  <--> (1) DMA_PRIO_MEDIUM
	 * ch 4 - 5  <--> (2) DMA_PRIO_LOW
	 */
	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		puv3_stop_dma(i);
		dma_channels[i].name = NULL;
		dma_channels[i].prio = min((i & 0x7) >> 1, DMA_PRIO_LOW);
	}

	ret = request_irq(IRQ_DMA, dma_irq_handler, 0, "DMA", NULL);
	if (ret) {
		printk(KERN_CRIT "Can't register IRQ for DMA\n");
		return ret;
	}

	ret = request_irq(IRQ_DMAERR, dma_err_handler, 0, "DMAERR", NULL);
	if (ret) {
		printk(KERN_CRIT "Can't register IRQ for DMAERR\n");
		free_irq(IRQ_DMA, "DMA");
		return ret;
	}

	return 0;
}

postcore_initcall(puv3_init_dma);
