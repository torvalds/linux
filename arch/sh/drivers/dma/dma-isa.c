/*
 * arch/sh/drivers/dma/dma-isa.c
 *
 * Generic ISA DMA wrapper for SH DMA API
 *
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/dma.h>

/*
 * This implements a small wrapper set to make code using the old ISA DMA API
 * work with the SH DMA API. Since most of the work in the new API happens
 * at ops->xfer() time, we simply use the various set_dma_xxx() routines to
 * fill in per-channel info, and then hand hand this off to ops->xfer() at
 * enable_dma() time.
 *
 * For channels that are doing on-demand data transfer via cascading, the
 * channel itself will still need to be configured through the new API. As
 * such, this code is meant for only the simplest of tasks (and shouldn't be
 * used in any new drivers at all).
 *
 * It should also be noted that various functions here are labelled as
 * being deprecated. This is due to the fact that the ops->xfer() method is
 * the preferred way of doing things (as well as just grabbing the spinlock
 * directly). As such, any users of this interface will be warned rather
 * loudly.
 */

unsigned long __deprecated claim_dma_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dma_spin_lock, flags);

	return flags;
}
EXPORT_SYMBOL(claim_dma_lock);

void __deprecated release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}
EXPORT_SYMBOL(release_dma_lock);

void __deprecated disable_dma(unsigned int chan)
{
	/* Nothing */
}
EXPORT_SYMBOL(disable_dma);

void __deprecated enable_dma(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	info->ops->xfer(channel);
}
EXPORT_SYMBOL(enable_dma);

void clear_dma_ff(unsigned int chan)
{
	/* Nothing */
}
EXPORT_SYMBOL(clear_dma_ff);

void set_dma_mode(unsigned int chan, char mode)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	channel->mode = mode;
}
EXPORT_SYMBOL(set_dma_mode);

void set_dma_addr(unsigned int chan, unsigned int addr)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	/*
	 * Single address mode is the only thing supported through
	 * this interface.
	 */
	if ((channel->mode & DMA_MODE_MASK) == DMA_MODE_READ) {
		channel->sar = addr;
	} else {
		channel->dar = addr;
	}
}
EXPORT_SYMBOL(set_dma_addr);

void set_dma_count(unsigned int chan, unsigned int count)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	channel->count = count;
}
EXPORT_SYMBOL(set_dma_count);

