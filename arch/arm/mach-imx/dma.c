/*
 *  linux/arch/arm/mach-imx/dma.c
 *
 *  imx DMA registration and IRQ dispatching
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  03/03/2004 Sascha Hauer <sascha@saschahauer.de>
 *             initial version heavily inspired by
 *             linux/arch/arm/mach-pxa/dma.c
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

static struct dma_channel {
	char *name;
	void (*irq_handler) (int, void *, struct pt_regs *);
	void (*err_handler) (int, void *, struct pt_regs *);
	void *data;
} dma_channels[11];

/* set err_handler to NULL to have the standard info-only error handler */
int
imx_request_dma(char *name, imx_dma_prio prio,
		void (*irq_handler) (int, void *, struct pt_regs *),
		void (*err_handler) (int, void *, struct pt_regs *), void *data)
{
	unsigned long flags;
	int i, found = 0;

	/* basic sanity checks */
	if (!name || !irq_handler)
		return -EINVAL;

	local_irq_save(flags);

	/* try grabbing a DMA channel with the requested priority */
	for (i = prio; i < prio + (prio == DMA_PRIO_LOW) ? 8 : 4; i++) {
		if (!dma_channels[i].name) {
			found = 1;
			break;
		}
	}

	if (!found) {
		/* requested prio group is full, try hier priorities */
		for (i = prio - 1; i >= 0; i--) {
			if (!dma_channels[i].name) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		DIMR &= ~(1 << i);
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

void
imx_free_dma(int dma_ch)
{
	unsigned long flags;

	if (!dma_channels[dma_ch].name) {
		printk(KERN_CRIT
		       "%s: trying to free channel %d which is already freed\n",
		       __FUNCTION__, dma_ch);
		return;
	}

	local_irq_save(flags);
	DIMR &= ~(1 << dma_ch);
	dma_channels[dma_ch].name = NULL;
	local_irq_restore(flags);
}

static irqreturn_t
dma_err_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int i, disr = DISR;
	struct dma_channel *channel;
	unsigned int err_mask = DBTOSR | DRTOSR | DSESR | DBOSR;

	DISR = disr;
	for (i = 0; i < 11; i++) {
		channel = &dma_channels[i];

		if ( (err_mask & 1<<i) && channel->name && channel->err_handler) {
			channel->err_handler(i, channel->data, regs);
			continue;
		}

		if (DBTOSR & (1 << i)) {
			printk(KERN_WARNING
			       "Burst timeout on channel %d (%s)\n",
			       i, channel->name);
			DBTOSR |= (1 << i);
		}
		if (DRTOSR & (1 << i)) {
			printk(KERN_WARNING
			       "Request timeout on channel %d (%s)\n",
			       i, channel->name);
			DRTOSR |= (1 << i);
		}
		if (DSESR & (1 << i)) {
			printk(KERN_WARNING
			       "Transfer timeout on channel %d (%s)\n",
			       i, channel->name);
			DSESR |= (1 << i);
		}
		if (DBOSR & (1 << i)) {
			printk(KERN_WARNING
			       "Buffer overflow timeout on channel %d (%s)\n",
			       i, channel->name);
			DBOSR |= (1 << i);
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t
dma_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int i, disr = DISR;

	DISR = disr;
	for (i = 0; i < 11; i++) {
		if (disr & (1 << i)) {
			struct dma_channel *channel = &dma_channels[i];
			if (channel->name && channel->irq_handler) {
				channel->irq_handler(i, channel->data, regs);
			} else {
				/*
				 * IRQ for an unregistered DMA channel:
				 * let's clear the interrupts and disable it.
				 */
				printk(KERN_WARNING
				       "spurious IRQ for DMA channel %d\n", i);
			}
		}
	}
	return IRQ_HANDLED;
}

static int __init
imx_dma_init(void)
{
	int ret;

	/* reset DMA module */
	DCR = DCR_DRST;

	ret = request_irq(DMA_INT, dma_irq_handler, 0, "DMA", NULL);
	if (ret) {
		printk(KERN_CRIT "Wow!  Can't register IRQ for DMA\n");
		return ret;
	}

	ret = request_irq(DMA_ERR, dma_err_handler, 0, "DMA", NULL);
	if (ret) {
		printk(KERN_CRIT "Wow!  Can't register ERRIRQ for DMA\n");
		free_irq(DMA_INT, NULL);
	}

	/* enable DMA module */
	DCR = DCR_DEN;

	/* clear all interrupts */
	DISR = 0x3ff;

	/* enable interrupts */
	DIMR = 0;

	return ret;
}

arch_initcall(imx_dma_init);

EXPORT_SYMBOL(imx_request_dma);
EXPORT_SYMBOL(imx_free_dma);
