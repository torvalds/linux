/*
 * Renesas SuperH DMA Engine support
 *
 * base is drivers/dma/flsdma.c
 *
 * Copyright (C) 2009 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 * Copyright (C) 2009 Renesas Solutions, Inc. All rights reserved.
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * - DMA of SuperH does not have Hardware DMA chain mode.
 * - MAX DMA size is 16MB.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <cpu/dma.h>
#include <asm/dma-sh.h>
#include "shdma.h"

/* DMA descriptor control */
enum sh_dmae_desc_status {
	DESC_IDLE,
	DESC_PREPARED,
	DESC_SUBMITTED,
	DESC_COMPLETED,	/* completed, have to call callback */
	DESC_WAITING,	/* callback called, waiting for ack / re-submit */
};

#define NR_DESCS_PER_CHANNEL 32
/*
 * Define the default configuration for dual address memory-memory transfer.
 * The 0x400 value represents auto-request, external->external.
 *
 * And this driver set 4byte burst mode.
 * If you want to change mode, you need to change RS_DEFAULT of value.
 * (ex 1byte burst mode -> (RS_DUAL & ~TS_32)
 */
#define RS_DEFAULT  (RS_DUAL)

/* A bitmask with bits enough for enum sh_dmae_slave_chan_id */
static unsigned long sh_dmae_slave_used[BITS_TO_LONGS(SHDMA_SLAVE_NUMBER)];

static void sh_dmae_chan_ld_cleanup(struct sh_dmae_chan *sh_chan, bool all);

static void sh_dmae_writel(struct sh_dmae_chan *sh_dc, u32 data, u32 reg)
{
	__raw_writel(data, sh_dc->base + reg / sizeof(u32));
}

static u32 sh_dmae_readl(struct sh_dmae_chan *sh_dc, u32 reg)
{
	return __raw_readl(sh_dc->base + reg / sizeof(u32));
}

static u16 dmaor_read(struct sh_dmae_device *shdev)
{
	return __raw_readw(shdev->chan_reg + DMAOR / sizeof(u32));
}

static void dmaor_write(struct sh_dmae_device *shdev, u16 data)
{
	__raw_writew(data, shdev->chan_reg + DMAOR / sizeof(u32));
}

/*
 * Reset DMA controller
 *
 * SH7780 has two DMAOR register
 */
static void sh_dmae_ctl_stop(struct sh_dmae_device *shdev)
{
	unsigned short dmaor = dmaor_read(shdev);

	dmaor_write(shdev, dmaor & ~(DMAOR_NMIF | DMAOR_AE | DMAOR_DME));
}

static int sh_dmae_rst(struct sh_dmae_device *shdev)
{
	unsigned short dmaor;

	sh_dmae_ctl_stop(shdev);
	dmaor = dmaor_read(shdev) | DMAOR_INIT;

	dmaor_write(shdev, dmaor);
	if (dmaor_read(shdev) & (DMAOR_AE | DMAOR_NMIF)) {
		pr_warning("dma-sh: Can't initialize DMAOR.\n");
		return -EINVAL;
	}
	return 0;
}

static bool dmae_is_busy(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);

	if ((chcr & (CHCR_DE | CHCR_TE)) == CHCR_DE)
		return true; /* working */

	return false; /* waiting */
}

static unsigned int ts_shift[] = TS_SHIFT;
static inline unsigned int calc_xmit_shift(u32 chcr)
{
	int cnt = ((chcr & CHCR_TS_LOW_MASK) >> CHCR_TS_LOW_SHIFT) |
		((chcr & CHCR_TS_HIGH_MASK) >> CHCR_TS_HIGH_SHIFT);

	return ts_shift[cnt];
}

static void dmae_set_reg(struct sh_dmae_chan *sh_chan, struct sh_dmae_regs *hw)
{
	sh_dmae_writel(sh_chan, hw->sar, SAR);
	sh_dmae_writel(sh_chan, hw->dar, DAR);
	sh_dmae_writel(sh_chan, hw->tcr >> sh_chan->xmit_shift, TCR);
}

static void dmae_start(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);

	chcr |= CHCR_DE | CHCR_IE;
	sh_dmae_writel(sh_chan, chcr & ~CHCR_TE, CHCR);
}

static void dmae_halt(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);

	chcr &= ~(CHCR_DE | CHCR_TE | CHCR_IE);
	sh_dmae_writel(sh_chan, chcr, CHCR);
}

static void dmae_init(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = RS_DEFAULT; /* default is DUAL mode */
	sh_chan->xmit_shift = calc_xmit_shift(chcr);
	sh_dmae_writel(sh_chan, chcr, CHCR);
}

static int dmae_set_chcr(struct sh_dmae_chan *sh_chan, u32 val)
{
	/* When DMA was working, can not set data to CHCR */
	if (dmae_is_busy(sh_chan))
		return -EBUSY;

	sh_chan->xmit_shift = calc_xmit_shift(val);
	sh_dmae_writel(sh_chan, val, CHCR);

	return 0;
}

static int dmae_set_dmars(struct sh_dmae_chan *sh_chan, u16 val)
{
	struct sh_dmae_device *shdev = container_of(sh_chan->common.device,
						struct sh_dmae_device, common);
	struct sh_dmae_pdata *pdata = shdev->pdata;
	struct sh_dmae_channel *chan_pdata = &pdata->channel[sh_chan->id];
	u16 __iomem *addr = shdev->dmars + chan_pdata->dmars / sizeof(u16);
	int shift = chan_pdata->dmars_bit;

	if (dmae_is_busy(sh_chan))
		return -EBUSY;

	__raw_writew((__raw_readw(addr) & (0xff00 >> shift)) | (val << shift),
		     addr);

	return 0;
}

static dma_cookie_t sh_dmae_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct sh_desc *desc = tx_to_sh_desc(tx), *chunk, *last = desc, *c;
	struct sh_dmae_chan *sh_chan = to_sh_chan(tx->chan);
	dma_async_tx_callback callback = tx->callback;
	dma_cookie_t cookie;

	spin_lock_bh(&sh_chan->desc_lock);

	cookie = sh_chan->common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;

	sh_chan->common.cookie = cookie;
	tx->cookie = cookie;

	/* Mark all chunks of this descriptor as submitted, move to the queue */
	list_for_each_entry_safe(chunk, c, desc->node.prev, node) {
		/*
		 * All chunks are on the global ld_free, so, we have to find
		 * the end of the chain ourselves
		 */
		if (chunk != desc && (chunk->mark == DESC_IDLE ||
				      chunk->async_tx.cookie > 0 ||
				      chunk->async_tx.cookie == -EBUSY ||
				      &chunk->node == &sh_chan->ld_free))
			break;
		chunk->mark = DESC_SUBMITTED;
		/* Callback goes to the last chunk */
		chunk->async_tx.callback = NULL;
		chunk->cookie = cookie;
		list_move_tail(&chunk->node, &sh_chan->ld_queue);
		last = chunk;
	}

	last->async_tx.callback = callback;
	last->async_tx.callback_param = tx->callback_param;

	dev_dbg(sh_chan->dev, "submit #%d@%p on %d: %x[%d] -> %x\n",
		tx->cookie, &last->async_tx, sh_chan->id,
		desc->hw.sar, desc->hw.tcr, desc->hw.dar);

	spin_unlock_bh(&sh_chan->desc_lock);

	return cookie;
}

/* Called with desc_lock held */
static struct sh_desc *sh_dmae_get_desc(struct sh_dmae_chan *sh_chan)
{
	struct sh_desc *desc;

	list_for_each_entry(desc, &sh_chan->ld_free, node)
		if (desc->mark != DESC_PREPARED) {
			BUG_ON(desc->mark != DESC_IDLE);
			list_del(&desc->node);
			return desc;
		}

	return NULL;
}

static struct sh_dmae_slave_config *sh_dmae_find_slave(
	struct sh_dmae_chan *sh_chan, enum sh_dmae_slave_chan_id slave_id)
{
	struct dma_device *dma_dev = sh_chan->common.device;
	struct sh_dmae_device *shdev = container_of(dma_dev,
					struct sh_dmae_device, common);
	struct sh_dmae_pdata *pdata = shdev->pdata;
	int i;

	if ((unsigned)slave_id >= SHDMA_SLAVE_NUMBER)
		return NULL;

	for (i = 0; i < pdata->slave_num; i++)
		if (pdata->slave[i].slave_id == slave_id)
			return pdata->slave + i;

	return NULL;
}

static int sh_dmae_alloc_chan_resources(struct dma_chan *chan)
{
	struct sh_dmae_chan *sh_chan = to_sh_chan(chan);
	struct sh_desc *desc;
	struct sh_dmae_slave *param = chan->private;

	/*
	 * This relies on the guarantee from dmaengine that alloc_chan_resources
	 * never runs concurrently with itself or free_chan_resources.
	 */
	if (param) {
		struct sh_dmae_slave_config *cfg;

		cfg = sh_dmae_find_slave(sh_chan, param->slave_id);
		if (!cfg)
			return -EINVAL;

		if (test_and_set_bit(param->slave_id, sh_dmae_slave_used))
			return -EBUSY;

		param->config = cfg;

		dmae_set_dmars(sh_chan, cfg->mid_rid);
		dmae_set_chcr(sh_chan, cfg->chcr);
	} else {
		if ((sh_dmae_readl(sh_chan, CHCR) & 0x700) != 0x400)
			dmae_set_chcr(sh_chan, RS_DEFAULT);
	}

	spin_lock_bh(&sh_chan->desc_lock);
	while (sh_chan->descs_allocated < NR_DESCS_PER_CHANNEL) {
		spin_unlock_bh(&sh_chan->desc_lock);
		desc = kzalloc(sizeof(struct sh_desc), GFP_KERNEL);
		if (!desc) {
			spin_lock_bh(&sh_chan->desc_lock);
			break;
		}
		dma_async_tx_descriptor_init(&desc->async_tx,
					&sh_chan->common);
		desc->async_tx.tx_submit = sh_dmae_tx_submit;
		desc->mark = DESC_IDLE;

		spin_lock_bh(&sh_chan->desc_lock);
		list_add(&desc->node, &sh_chan->ld_free);
		sh_chan->descs_allocated++;
	}
	spin_unlock_bh(&sh_chan->desc_lock);

	return sh_chan->descs_allocated;
}

/*
 * sh_dma_free_chan_resources - Free all resources of the channel.
 */
static void sh_dmae_free_chan_resources(struct dma_chan *chan)
{
	struct sh_dmae_chan *sh_chan = to_sh_chan(chan);
	struct sh_desc *desc, *_desc;
	LIST_HEAD(list);

	dmae_halt(sh_chan);

	/* Prepared and not submitted descriptors can still be on the queue */
	if (!list_empty(&sh_chan->ld_queue))
		sh_dmae_chan_ld_cleanup(sh_chan, true);

	if (chan->private) {
		/* The caller is holding dma_list_mutex */
		struct sh_dmae_slave *param = chan->private;
		clear_bit(param->slave_id, sh_dmae_slave_used);
	}

	spin_lock_bh(&sh_chan->desc_lock);

	list_splice_init(&sh_chan->ld_free, &list);
	sh_chan->descs_allocated = 0;

	spin_unlock_bh(&sh_chan->desc_lock);

	list_for_each_entry_safe(desc, _desc, &list, node)
		kfree(desc);
}

/**
 * sh_dmae_add_desc - get, set up and return one transfer descriptor
 * @sh_chan:	DMA channel
 * @flags:	DMA transfer flags
 * @dest:	destination DMA address, incremented when direction equals
 *		DMA_FROM_DEVICE or DMA_BIDIRECTIONAL
 * @src:	source DMA address, incremented when direction equals
 *		DMA_TO_DEVICE or DMA_BIDIRECTIONAL
 * @len:	DMA transfer length
 * @first:	if NULL, set to the current descriptor and cookie set to -EBUSY
 * @direction:	needed for slave DMA to decide which address to keep constant,
 *		equals DMA_BIDIRECTIONAL for MEMCPY
 * Returns 0 or an error
 * Locks: called with desc_lock held
 */
static struct sh_desc *sh_dmae_add_desc(struct sh_dmae_chan *sh_chan,
	unsigned long flags, dma_addr_t *dest, dma_addr_t *src, size_t *len,
	struct sh_desc **first, enum dma_data_direction direction)
{
	struct sh_desc *new;
	size_t copy_size;

	if (!*len)
		return NULL;

	/* Allocate the link descriptor from the free list */
	new = sh_dmae_get_desc(sh_chan);
	if (!new) {
		dev_err(sh_chan->dev, "No free link descriptor available\n");
		return NULL;
	}

	copy_size = min(*len, (size_t)SH_DMA_TCR_MAX + 1);

	new->hw.sar = *src;
	new->hw.dar = *dest;
	new->hw.tcr = copy_size;

	if (!*first) {
		/* First desc */
		new->async_tx.cookie = -EBUSY;
		*first = new;
	} else {
		/* Other desc - invisible to the user */
		new->async_tx.cookie = -EINVAL;
	}

	dev_dbg(sh_chan->dev,
		"chaining (%u/%u)@%x -> %x with %p, cookie %d, shift %d\n",
		copy_size, *len, *src, *dest, &new->async_tx,
		new->async_tx.cookie, sh_chan->xmit_shift);

	new->mark = DESC_PREPARED;
	new->async_tx.flags = flags;
	new->direction = direction;

	*len -= copy_size;
	if (direction == DMA_BIDIRECTIONAL || direction == DMA_TO_DEVICE)
		*src += copy_size;
	if (direction == DMA_BIDIRECTIONAL || direction == DMA_FROM_DEVICE)
		*dest += copy_size;

	return new;
}

/*
 * sh_dmae_prep_sg - prepare transfer descriptors from an SG list
 *
 * Common routine for public (MEMCPY) and slave DMA. The MEMCPY case is also
 * converted to scatter-gather to guarantee consistent locking and a correct
 * list manipulation. For slave DMA direction carries the usual meaning, and,
 * logically, the SG list is RAM and the addr variable contains slave address,
 * e.g., the FIFO I/O register. For MEMCPY direction equals DMA_BIDIRECTIONAL
 * and the SG list contains only one element and points at the source buffer.
 */
static struct dma_async_tx_descriptor *sh_dmae_prep_sg(struct sh_dmae_chan *sh_chan,
	struct scatterlist *sgl, unsigned int sg_len, dma_addr_t *addr,
	enum dma_data_direction direction, unsigned long flags)
{
	struct scatterlist *sg;
	struct sh_desc *first = NULL, *new = NULL /* compiler... */;
	LIST_HEAD(tx_list);
	int chunks = 0;
	int i;

	if (!sg_len)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i)
		chunks += (sg_dma_len(sg) + SH_DMA_TCR_MAX) /
			(SH_DMA_TCR_MAX + 1);

	/* Have to lock the whole loop to protect against concurrent release */
	spin_lock_bh(&sh_chan->desc_lock);

	/*
	 * Chaining:
	 * first descriptor is what user is dealing with in all API calls, its
	 *	cookie is at first set to -EBUSY, at tx-submit to a positive
	 *	number
	 * if more than one chunk is needed further chunks have cookie = -EINVAL
	 * the last chunk, if not equal to the first, has cookie = -ENOSPC
	 * all chunks are linked onto the tx_list head with their .node heads
	 *	only during this function, then they are immediately spliced
	 *	back onto the free list in form of a chain
	 */
	for_each_sg(sgl, sg, sg_len, i) {
		dma_addr_t sg_addr = sg_dma_address(sg);
		size_t len = sg_dma_len(sg);

		if (!len)
			goto err_get_desc;

		do {
			dev_dbg(sh_chan->dev, "Add SG #%d@%p[%d], dma %llx\n",
				i, sg, len, (unsigned long long)sg_addr);

			if (direction == DMA_FROM_DEVICE)
				new = sh_dmae_add_desc(sh_chan, flags,
						&sg_addr, addr, &len, &first,
						direction);
			else
				new = sh_dmae_add_desc(sh_chan, flags,
						addr, &sg_addr, &len, &first,
						direction);
			if (!new)
				goto err_get_desc;

			new->chunks = chunks--;
			list_add_tail(&new->node, &tx_list);
		} while (len);
	}

	if (new != first)
		new->async_tx.cookie = -ENOSPC;

	/* Put them back on the free list, so, they don't get lost */
	list_splice_tail(&tx_list, &sh_chan->ld_free);

	spin_unlock_bh(&sh_chan->desc_lock);

	return &first->async_tx;

err_get_desc:
	list_for_each_entry(new, &tx_list, node)
		new->mark = DESC_IDLE;
	list_splice(&tx_list, &sh_chan->ld_free);

	spin_unlock_bh(&sh_chan->desc_lock);

	return NULL;
}

static struct dma_async_tx_descriptor *sh_dmae_prep_memcpy(
	struct dma_chan *chan, dma_addr_t dma_dest, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct sh_dmae_chan *sh_chan;
	struct scatterlist sg;

	if (!chan || !len)
		return NULL;

	chan->private = NULL;

	sh_chan = to_sh_chan(chan);

	sg_init_table(&sg, 1);
	sg_set_page(&sg, pfn_to_page(PFN_DOWN(dma_src)), len,
		    offset_in_page(dma_src));
	sg_dma_address(&sg) = dma_src;
	sg_dma_len(&sg) = len;

	return sh_dmae_prep_sg(sh_chan, &sg, 1, &dma_dest, DMA_BIDIRECTIONAL,
			       flags);
}

static struct dma_async_tx_descriptor *sh_dmae_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_data_direction direction, unsigned long flags)
{
	struct sh_dmae_slave *param;
	struct sh_dmae_chan *sh_chan;

	if (!chan)
		return NULL;

	sh_chan = to_sh_chan(chan);
	param = chan->private;

	/* Someone calling slave DMA on a public channel? */
	if (!param || !sg_len) {
		dev_warn(sh_chan->dev, "%s: bad parameter: %p, %d, %d\n",
			 __func__, param, sg_len, param ? param->slave_id : -1);
		return NULL;
	}

	/*
	 * if (param != NULL), this is a successfully requested slave channel,
	 * therefore param->config != NULL too.
	 */
	return sh_dmae_prep_sg(sh_chan, sgl, sg_len, &param->config->addr,
			       direction, flags);
}

static void sh_dmae_terminate_all(struct dma_chan *chan)
{
	struct sh_dmae_chan *sh_chan = to_sh_chan(chan);

	if (!chan)
		return;

	sh_dmae_chan_ld_cleanup(sh_chan, true);
}

static dma_async_tx_callback __ld_cleanup(struct sh_dmae_chan *sh_chan, bool all)
{
	struct sh_desc *desc, *_desc;
	/* Is the "exposed" head of a chain acked? */
	bool head_acked = false;
	dma_cookie_t cookie = 0;
	dma_async_tx_callback callback = NULL;
	void *param = NULL;

	spin_lock_bh(&sh_chan->desc_lock);
	list_for_each_entry_safe(desc, _desc, &sh_chan->ld_queue, node) {
		struct dma_async_tx_descriptor *tx = &desc->async_tx;

		BUG_ON(tx->cookie > 0 && tx->cookie != desc->cookie);
		BUG_ON(desc->mark != DESC_SUBMITTED &&
		       desc->mark != DESC_COMPLETED &&
		       desc->mark != DESC_WAITING);

		/*
		 * queue is ordered, and we use this loop to (1) clean up all
		 * completed descriptors, and to (2) update descriptor flags of
		 * any chunks in a (partially) completed chain
		 */
		if (!all && desc->mark == DESC_SUBMITTED &&
		    desc->cookie != cookie)
			break;

		if (tx->cookie > 0)
			cookie = tx->cookie;

		if (desc->mark == DESC_COMPLETED && desc->chunks == 1) {
			if (sh_chan->completed_cookie != desc->cookie - 1)
				dev_dbg(sh_chan->dev,
					"Completing cookie %d, expected %d\n",
					desc->cookie,
					sh_chan->completed_cookie + 1);
			sh_chan->completed_cookie = desc->cookie;
		}

		/* Call callback on the last chunk */
		if (desc->mark == DESC_COMPLETED && tx->callback) {
			desc->mark = DESC_WAITING;
			callback = tx->callback;
			param = tx->callback_param;
			dev_dbg(sh_chan->dev, "descriptor #%d@%p on %d callback\n",
				tx->cookie, tx, sh_chan->id);
			BUG_ON(desc->chunks != 1);
			break;
		}

		if (tx->cookie > 0 || tx->cookie == -EBUSY) {
			if (desc->mark == DESC_COMPLETED) {
				BUG_ON(tx->cookie < 0);
				desc->mark = DESC_WAITING;
			}
			head_acked = async_tx_test_ack(tx);
		} else {
			switch (desc->mark) {
			case DESC_COMPLETED:
				desc->mark = DESC_WAITING;
				/* Fall through */
			case DESC_WAITING:
				if (head_acked)
					async_tx_ack(&desc->async_tx);
			}
		}

		dev_dbg(sh_chan->dev, "descriptor %p #%d completed.\n",
			tx, tx->cookie);

		if (((desc->mark == DESC_COMPLETED ||
		      desc->mark == DESC_WAITING) &&
		     async_tx_test_ack(&desc->async_tx)) || all) {
			/* Remove from ld_queue list */
			desc->mark = DESC_IDLE;
			list_move(&desc->node, &sh_chan->ld_free);
		}
	}
	spin_unlock_bh(&sh_chan->desc_lock);

	if (callback)
		callback(param);

	return callback;
}

/*
 * sh_chan_ld_cleanup - Clean up link descriptors
 *
 * This function cleans up the ld_queue of DMA channel.
 */
static void sh_dmae_chan_ld_cleanup(struct sh_dmae_chan *sh_chan, bool all)
{
	while (__ld_cleanup(sh_chan, all))
		;
}

static void sh_chan_xfer_ld_queue(struct sh_dmae_chan *sh_chan)
{
	struct sh_desc *desc;

	spin_lock_bh(&sh_chan->desc_lock);
	/* DMA work check */
	if (dmae_is_busy(sh_chan)) {
		spin_unlock_bh(&sh_chan->desc_lock);
		return;
	}

	/* Find the first not transferred desciptor */
	list_for_each_entry(desc, &sh_chan->ld_queue, node)
		if (desc->mark == DESC_SUBMITTED) {
			/* Get the ld start address from ld_queue */
			dmae_set_reg(sh_chan, &desc->hw);
			dmae_start(sh_chan);
			break;
		}

	spin_unlock_bh(&sh_chan->desc_lock);
}

static void sh_dmae_memcpy_issue_pending(struct dma_chan *chan)
{
	struct sh_dmae_chan *sh_chan = to_sh_chan(chan);
	sh_chan_xfer_ld_queue(sh_chan);
}

static enum dma_status sh_dmae_is_complete(struct dma_chan *chan,
					dma_cookie_t cookie,
					dma_cookie_t *done,
					dma_cookie_t *used)
{
	struct sh_dmae_chan *sh_chan = to_sh_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status status;

	sh_dmae_chan_ld_cleanup(sh_chan, false);

	last_used = chan->cookie;
	last_complete = sh_chan->completed_cookie;
	BUG_ON(last_complete < 0);

	if (done)
		*done = last_complete;

	if (used)
		*used = last_used;

	spin_lock_bh(&sh_chan->desc_lock);

	status = dma_async_is_complete(cookie, last_complete, last_used);

	/*
	 * If we don't find cookie on the queue, it has been aborted and we have
	 * to report error
	 */
	if (status != DMA_SUCCESS) {
		struct sh_desc *desc;
		status = DMA_ERROR;
		list_for_each_entry(desc, &sh_chan->ld_queue, node)
			if (desc->cookie == cookie) {
				status = DMA_IN_PROGRESS;
				break;
			}
	}

	spin_unlock_bh(&sh_chan->desc_lock);

	return status;
}

static irqreturn_t sh_dmae_interrupt(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;
	struct sh_dmae_chan *sh_chan = (struct sh_dmae_chan *)data;
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);

	if (chcr & CHCR_TE) {
		/* DMA stop */
		dmae_halt(sh_chan);

		ret = IRQ_HANDLED;
		tasklet_schedule(&sh_chan->tasklet);
	}

	return ret;
}

#if defined(CONFIG_CPU_SH4)
static irqreturn_t sh_dmae_err(int irq, void *data)
{
	struct sh_dmae_device *shdev = (struct sh_dmae_device *)data;
	int i;

	/* halt the dma controller */
	sh_dmae_ctl_stop(shdev);

	/* We cannot detect, which channel caused the error, have to reset all */
	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		struct sh_dmae_chan *sh_chan = shdev->chan[i];
		if (sh_chan) {
			struct sh_desc *desc;
			/* Stop the channel */
			dmae_halt(sh_chan);
			/* Complete all  */
			list_for_each_entry(desc, &sh_chan->ld_queue, node) {
				struct dma_async_tx_descriptor *tx = &desc->async_tx;
				desc->mark = DESC_IDLE;
				if (tx->callback)
					tx->callback(tx->callback_param);
			}
			list_splice_init(&sh_chan->ld_queue, &sh_chan->ld_free);
		}
	}
	sh_dmae_rst(shdev);

	return IRQ_HANDLED;
}
#endif

static void dmae_do_tasklet(unsigned long data)
{
	struct sh_dmae_chan *sh_chan = (struct sh_dmae_chan *)data;
	struct sh_desc *desc;
	u32 sar_buf = sh_dmae_readl(sh_chan, SAR);
	u32 dar_buf = sh_dmae_readl(sh_chan, DAR);

	spin_lock(&sh_chan->desc_lock);
	list_for_each_entry(desc, &sh_chan->ld_queue, node) {
		if (desc->mark == DESC_SUBMITTED &&
		    ((desc->direction == DMA_FROM_DEVICE &&
		      (desc->hw.dar + desc->hw.tcr) == dar_buf) ||
		     (desc->hw.sar + desc->hw.tcr) == sar_buf)) {
			dev_dbg(sh_chan->dev, "done #%d@%p dst %u\n",
				desc->async_tx.cookie, &desc->async_tx,
				desc->hw.dar);
			desc->mark = DESC_COMPLETED;
			break;
		}
	}
	spin_unlock(&sh_chan->desc_lock);

	/* Next desc */
	sh_chan_xfer_ld_queue(sh_chan);
	sh_dmae_chan_ld_cleanup(sh_chan, false);
}

static int __devinit sh_dmae_chan_probe(struct sh_dmae_device *shdev, int id,
					int irq, unsigned long flags)
{
	int err;
	struct sh_dmae_channel *chan_pdata = &shdev->pdata->channel[id];
	struct platform_device *pdev = to_platform_device(shdev->common.dev);
	struct sh_dmae_chan *new_sh_chan;

	/* alloc channel */
	new_sh_chan = kzalloc(sizeof(struct sh_dmae_chan), GFP_KERNEL);
	if (!new_sh_chan) {
		dev_err(shdev->common.dev,
			"No free memory for allocating dma channels!\n");
		return -ENOMEM;
	}

	new_sh_chan->dev = shdev->common.dev;
	new_sh_chan->id = id;
	new_sh_chan->irq = irq;
	new_sh_chan->base = shdev->chan_reg + chan_pdata->offset / sizeof(u32);

	/* Init DMA tasklet */
	tasklet_init(&new_sh_chan->tasklet, dmae_do_tasklet,
			(unsigned long)new_sh_chan);

	/* Init the channel */
	dmae_init(new_sh_chan);

	spin_lock_init(&new_sh_chan->desc_lock);

	/* Init descripter manage list */
	INIT_LIST_HEAD(&new_sh_chan->ld_queue);
	INIT_LIST_HEAD(&new_sh_chan->ld_free);

	/* copy struct dma_device */
	new_sh_chan->common.device = &shdev->common;

	/* Add the channel to DMA device channel list */
	list_add_tail(&new_sh_chan->common.device_node,
			&shdev->common.channels);
	shdev->common.chancnt++;

	if (pdev->id >= 0)
		snprintf(new_sh_chan->dev_id, sizeof(new_sh_chan->dev_id),
			 "sh-dmae%d.%d", pdev->id, new_sh_chan->id);
	else
		snprintf(new_sh_chan->dev_id, sizeof(new_sh_chan->dev_id),
			 "sh-dma%d", new_sh_chan->id);

	/* set up channel irq */
	err = request_irq(irq, &sh_dmae_interrupt, flags,
			  new_sh_chan->dev_id, new_sh_chan);
	if (err) {
		dev_err(shdev->common.dev, "DMA channel %d request_irq error "
			"with return %d\n", id, err);
		goto err_no_irq;
	}

	shdev->chan[id] = new_sh_chan;
	return 0;

err_no_irq:
	/* remove from dmaengine device node */
	list_del(&new_sh_chan->common.device_node);
	kfree(new_sh_chan);
	return err;
}

static void sh_dmae_chan_remove(struct sh_dmae_device *shdev)
{
	int i;

	for (i = shdev->common.chancnt - 1 ; i >= 0 ; i--) {
		if (shdev->chan[i]) {
			struct sh_dmae_chan *sh_chan = shdev->chan[i];

			free_irq(sh_chan->irq, sh_chan);

			list_del(&sh_chan->common.device_node);
			kfree(sh_chan);
			shdev->chan[i] = NULL;
		}
	}
	shdev->common.chancnt = 0;
}

static int __init sh_dmae_probe(struct platform_device *pdev)
{
	struct sh_dmae_pdata *pdata = pdev->dev.platform_data;
	unsigned long irqflags = IRQF_DISABLED,
		chan_flag[MAX_DMA_CHANNELS] = {};
	int errirq, chan_irq[MAX_DMA_CHANNELS];
	int err, i, irq_cnt = 0, irqres = 0;
	struct sh_dmae_device *shdev;
	struct resource *chan, *dmars, *errirq_res, *chanirq_res;

	/* get platform data */
	if (!pdata || !pdata->channel_num)
		return -ENODEV;

	chan = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* DMARS area is optional, if absent, this controller cannot do slave DMA */
	dmars = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	/*
	 * IRQ resources:
	 * 1. there always must be at least one IRQ IO-resource. On SH4 it is
	 *    the error IRQ, in which case it is the only IRQ in this resource:
	 *    start == end. If it is the only IRQ resource, all channels also
	 *    use the same IRQ.
	 * 2. DMA channel IRQ resources can be specified one per resource or in
	 *    ranges (start != end)
	 * 3. iff all events (channels and, optionally, error) on this
	 *    controller use the same IRQ, only one IRQ resource can be
	 *    specified, otherwise there must be one IRQ per channel, even if
	 *    some of them are equal
	 * 4. if all IRQs on this controller are equal or if some specific IRQs
	 *    specify IORESOURCE_IRQ_SHAREABLE in their resources, they will be
	 *    requested with the IRQF_SHARED flag
	 */
	errirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!chan || !errirq_res)
		return -ENODEV;

	if (!request_mem_region(chan->start, resource_size(chan), pdev->name)) {
		dev_err(&pdev->dev, "DMAC register region already claimed\n");
		return -EBUSY;
	}

	if (dmars && !request_mem_region(dmars->start, resource_size(dmars), pdev->name)) {
		dev_err(&pdev->dev, "DMAC DMARS region already claimed\n");
		err = -EBUSY;
		goto ermrdmars;
	}

	err = -ENOMEM;
	shdev = kzalloc(sizeof(struct sh_dmae_device), GFP_KERNEL);
	if (!shdev) {
		dev_err(&pdev->dev, "Not enough memory\n");
		goto ealloc;
	}

	shdev->chan_reg = ioremap(chan->start, resource_size(chan));
	if (!shdev->chan_reg)
		goto emapchan;
	if (dmars) {
		shdev->dmars = ioremap(dmars->start, resource_size(dmars));
		if (!shdev->dmars)
			goto emapdmars;
	}

	/* platform data */
	shdev->pdata = pdata;

	/* reset dma controller */
	err = sh_dmae_rst(shdev);
	if (err)
		goto rst_err;

	INIT_LIST_HEAD(&shdev->common.channels);

	dma_cap_set(DMA_MEMCPY, shdev->common.cap_mask);
	if (dmars)
		dma_cap_set(DMA_SLAVE, shdev->common.cap_mask);

	shdev->common.device_alloc_chan_resources
		= sh_dmae_alloc_chan_resources;
	shdev->common.device_free_chan_resources = sh_dmae_free_chan_resources;
	shdev->common.device_prep_dma_memcpy = sh_dmae_prep_memcpy;
	shdev->common.device_is_tx_complete = sh_dmae_is_complete;
	shdev->common.device_issue_pending = sh_dmae_memcpy_issue_pending;

	/* Compulsory for DMA_SLAVE fields */
	shdev->common.device_prep_slave_sg = sh_dmae_prep_slave_sg;
	shdev->common.device_terminate_all = sh_dmae_terminate_all;

	shdev->common.dev = &pdev->dev;
	/* Default transfer size of 32 bytes requires 32-byte alignment */
	shdev->common.copy_align = 5;

#if defined(CONFIG_CPU_SH4)
	chanirq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);

	if (!chanirq_res)
		chanirq_res = errirq_res;
	else
		irqres++;

	if (chanirq_res == errirq_res ||
	    (errirq_res->flags & IORESOURCE_BITS) == IORESOURCE_IRQ_SHAREABLE)
		irqflags = IRQF_SHARED;

	errirq = errirq_res->start;

	err = request_irq(errirq, sh_dmae_err, irqflags,
			  "DMAC Address Error", shdev);
	if (err) {
		dev_err(&pdev->dev,
			"DMA failed requesting irq #%d, error %d\n",
			errirq, err);
		goto eirq_err;
	}

#else
	chanirq_res = errirq_res;
#endif /* CONFIG_CPU_SH4 */

	if (chanirq_res->start == chanirq_res->end &&
	    !platform_get_resource(pdev, IORESOURCE_IRQ, 1)) {
		/* Special case - all multiplexed */
		for (; irq_cnt < pdata->channel_num; irq_cnt++) {
			chan_irq[irq_cnt] = chanirq_res->start;
			chan_flag[irq_cnt] = IRQF_SHARED;
		}
	} else {
		do {
			for (i = chanirq_res->start; i <= chanirq_res->end; i++) {
				if ((errirq_res->flags & IORESOURCE_BITS) ==
				    IORESOURCE_IRQ_SHAREABLE)
					chan_flag[irq_cnt] = IRQF_SHARED;
				else
					chan_flag[irq_cnt] = IRQF_DISABLED;
				dev_dbg(&pdev->dev,
					"Found IRQ %d for channel %d\n",
					i, irq_cnt);
				chan_irq[irq_cnt++] = i;
			}
			chanirq_res = platform_get_resource(pdev,
						IORESOURCE_IRQ, ++irqres);
		} while (irq_cnt < pdata->channel_num && chanirq_res);
	}

	if (irq_cnt < pdata->channel_num)
		goto eirqres;

	/* Create DMA Channel */
	for (i = 0; i < pdata->channel_num; i++) {
		err = sh_dmae_chan_probe(shdev, i, chan_irq[i], chan_flag[i]);
		if (err)
			goto chan_probe_err;
	}

	platform_set_drvdata(pdev, shdev);
	dma_async_device_register(&shdev->common);

	return err;

chan_probe_err:
	sh_dmae_chan_remove(shdev);
eirqres:
#if defined(CONFIG_CPU_SH4)
	free_irq(errirq, shdev);
eirq_err:
#endif
rst_err:
	if (dmars)
		iounmap(shdev->dmars);
emapdmars:
	iounmap(shdev->chan_reg);
emapchan:
	kfree(shdev);
ealloc:
	if (dmars)
		release_mem_region(dmars->start, resource_size(dmars));
ermrdmars:
	release_mem_region(chan->start, resource_size(chan));

	return err;
}

static int __exit sh_dmae_remove(struct platform_device *pdev)
{
	struct sh_dmae_device *shdev = platform_get_drvdata(pdev);
	struct resource *res;
	int errirq = platform_get_irq(pdev, 0);

	dma_async_device_unregister(&shdev->common);

	if (errirq > 0)
		free_irq(errirq, shdev);

	/* channel data remove */
	sh_dmae_chan_remove(shdev);

	if (shdev->dmars)
		iounmap(shdev->dmars);
	iounmap(shdev->chan_reg);

	kfree(shdev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res)
		release_mem_region(res->start, resource_size(res));

	return 0;
}

static void sh_dmae_shutdown(struct platform_device *pdev)
{
	struct sh_dmae_device *shdev = platform_get_drvdata(pdev);
	sh_dmae_ctl_stop(shdev);
}

static struct platform_driver sh_dmae_driver = {
	.remove		= __exit_p(sh_dmae_remove),
	.shutdown	= sh_dmae_shutdown,
	.driver = {
		.name	= "sh-dma-engine",
	},
};

static int __init sh_dmae_init(void)
{
	return platform_driver_probe(&sh_dmae_driver, sh_dmae_probe);
}
module_init(sh_dmae_init);

static void __exit sh_dmae_exit(void)
{
	platform_driver_unregister(&sh_dmae_driver);
}
module_exit(sh_dmae_exit);

MODULE_AUTHOR("Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>");
MODULE_DESCRIPTION("Renesas SH DMA Engine driver");
MODULE_LICENSE("GPL");
