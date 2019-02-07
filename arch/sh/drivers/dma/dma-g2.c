// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/drivers/dma/dma-g2.c
 *
 * G2 bus DMA support
 *
 * Copyright (C) 2003 - 2006  Paul Mundt
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <asm/cacheflush.h>
#include <mach/sysasic.h>
#include <mach/dma.h>
#include <asm/dma.h>

struct g2_channel {
	unsigned long g2_addr;		/* G2 bus address */
	unsigned long root_addr;	/* Root bus (SH-4) address */
	unsigned long size;		/* Size (in bytes), 32-byte aligned */
	unsigned long direction;	/* Transfer direction */
	unsigned long ctrl;		/* Transfer control */
	unsigned long chan_enable;	/* Channel enable */
	unsigned long xfer_enable;	/* Transfer enable */
	unsigned long xfer_stat;	/* Transfer status */
} __attribute__ ((aligned(32)));

struct g2_status {
	unsigned long g2_addr;
	unsigned long root_addr;
	unsigned long size;
	unsigned long status;
} __attribute__ ((aligned(16)));

struct g2_dma_info {
	struct g2_channel channel[G2_NR_DMA_CHANNELS];
	unsigned long pad1[G2_NR_DMA_CHANNELS];
	unsigned long wait_state;
	unsigned long pad2[10];
	unsigned long magic;
	struct g2_status status[G2_NR_DMA_CHANNELS];
} __attribute__ ((aligned(256)));

static volatile struct g2_dma_info *g2_dma = (volatile struct g2_dma_info *)0xa05f7800;

#define g2_bytes_remaining(i) \
	((g2_dma->channel[i].size - \
	  g2_dma->status[i].size) & 0x0fffffff)

static irqreturn_t g2_dma_interrupt(int irq, void *dev_id)
{
	int i;

	for (i = 0; i < G2_NR_DMA_CHANNELS; i++) {
		if (g2_dma->status[i].status & 0x20000000) {
			unsigned int bytes = g2_bytes_remaining(i);

			if (likely(bytes == 0)) {
				struct dma_info *info = dev_id;
				struct dma_channel *chan = info->channels + i;

				wake_up(&chan->wait_queue);

				return IRQ_HANDLED;
			}
		}
	}

	return IRQ_NONE;
}

static int g2_enable_dma(struct dma_channel *chan)
{
	unsigned int chan_nr = chan->chan;

	g2_dma->channel[chan_nr].chan_enable = 1;
	g2_dma->channel[chan_nr].xfer_enable = 1;

	return 0;
}

static int g2_disable_dma(struct dma_channel *chan)
{
	unsigned int chan_nr = chan->chan;

	g2_dma->channel[chan_nr].chan_enable = 0;
	g2_dma->channel[chan_nr].xfer_enable = 0;

	return 0;
}

static int g2_xfer_dma(struct dma_channel *chan)
{
	unsigned int chan_nr = chan->chan;

	if (chan->sar & 31) {
		printk("g2dma: unaligned source 0x%lx\n", chan->sar);
		return -EINVAL;
	}

	if (chan->dar & 31) {
		printk("g2dma: unaligned dest 0x%lx\n", chan->dar);
		return -EINVAL;
	}

	/* Align the count */
	if (chan->count & 31)
		chan->count = (chan->count + (32 - 1)) & ~(32 - 1);

	/* Fixup destination */
	chan->dar += 0xa0800000;

	/* Fixup direction */
	chan->mode = !chan->mode;

	flush_icache_range((unsigned long)chan->sar, chan->count);

	g2_disable_dma(chan);

	g2_dma->channel[chan_nr].g2_addr   = chan->dar & 0x1fffffe0;
	g2_dma->channel[chan_nr].root_addr = chan->sar & 0x1fffffe0;
	g2_dma->channel[chan_nr].size	   = (chan->count & ~31) | 0x80000000;
	g2_dma->channel[chan_nr].direction = chan->mode;

	/*
	 * bit 0 - ???
	 * bit 1 - if set, generate a hardware event on transfer completion
	 * bit 2 - ??? something to do with suspend?
	 */
	g2_dma->channel[chan_nr].ctrl	= 5; /* ?? */

	g2_enable_dma(chan);

	/* debug cruft */
	pr_debug("count, sar, dar, mode, ctrl, chan, xfer: %ld, 0x%08lx, "
		 "0x%08lx, %ld, %ld, %ld, %ld\n",
		 g2_dma->channel[chan_nr].size,
		 g2_dma->channel[chan_nr].root_addr,
		 g2_dma->channel[chan_nr].g2_addr,
		 g2_dma->channel[chan_nr].direction,
		 g2_dma->channel[chan_nr].ctrl,
		 g2_dma->channel[chan_nr].chan_enable,
		 g2_dma->channel[chan_nr].xfer_enable);

	return 0;
}

static int g2_get_residue(struct dma_channel *chan)
{
	return g2_bytes_remaining(chan->chan);
}

static struct dma_ops g2_dma_ops = {
	.xfer		= g2_xfer_dma,
	.get_residue	= g2_get_residue,
};

static struct dma_info g2_dma_info = {
	.name		= "g2_dmac",
	.nr_channels	= 4,
	.ops		= &g2_dma_ops,
	.flags		= DMAC_CHANNELS_TEI_CAPABLE,
};

static int __init g2_dma_init(void)
{
	int ret;

	ret = request_irq(HW_EVENT_G2_DMA, g2_dma_interrupt, 0,
			  "g2 DMA handler", &g2_dma_info);
	if (unlikely(ret))
		return -EINVAL;

	/* Magic */
	g2_dma->wait_state	= 27;
	g2_dma->magic		= 0x4659404f;

	ret = register_dmac(&g2_dma_info);
	if (unlikely(ret != 0))
		free_irq(HW_EVENT_G2_DMA, &g2_dma_info);

	return ret;
}

static void __exit g2_dma_exit(void)
{
	free_irq(HW_EVENT_G2_DMA, &g2_dma_info);
	unregister_dmac(&g2_dma_info);
}

subsys_initcall(g2_dma_init);
module_exit(g2_dma_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("G2 bus DMA driver");
MODULE_LICENSE("GPL v2");
