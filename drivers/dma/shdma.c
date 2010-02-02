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

static void sh_dmae_chan_ld_cleanup(struct sh_dmae_chan *sh_chan, bool all);

#define SH_DMAC_CHAN_BASE(id) (dma_base_addr[id])
static void sh_dmae_writel(struct sh_dmae_chan *sh_dc, u32 data, u32 reg)
{
	ctrl_outl(data, (SH_DMAC_CHAN_BASE(sh_dc->id) + reg));
}

static u32 sh_dmae_readl(struct sh_dmae_chan *sh_dc, u32 reg)
{
	return ctrl_inl((SH_DMAC_CHAN_BASE(sh_dc->id) + reg));
}

static void dmae_init(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = RS_DEFAULT; /* default is DUAL mode */
	sh_dmae_writel(sh_chan, chcr, CHCR);
}

/*
 * Reset DMA controller
 *
 * SH7780 has two DMAOR register
 */
static void sh_dmae_ctl_stop(int id)
{
	unsigned short dmaor = dmaor_read_reg(id);

	dmaor &= ~(DMAOR_NMIF | DMAOR_AE);
	dmaor_write_reg(id, dmaor);
}

static int sh_dmae_rst(int id)
{
	unsigned short dmaor;

	sh_dmae_ctl_stop(id);
	dmaor = dmaor_read_reg(id) | DMAOR_INIT;

	dmaor_write_reg(id, dmaor);
	if (dmaor_read_reg(id) & (DMAOR_AE | DMAOR_NMIF)) {
		pr_warning(KERN_ERR "dma-sh: Can't initialize DMAOR.\n");
		return -EINVAL;
	}
	return 0;
}

static int dmae_is_busy(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);
	if (chcr & CHCR_DE) {
		if (!(chcr & CHCR_TE))
			return -EBUSY; /* working */
	}
	return 0; /* waiting */
}

static inline unsigned int calc_xmit_shift(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);
	return ts_shift[(chcr & CHCR_TS_MASK) >> CHCR_TS_SHIFT];
}

static void dmae_set_reg(struct sh_dmae_chan *sh_chan, struct sh_dmae_regs *hw)
{
	sh_dmae_writel(sh_chan, hw->sar, SAR);
	sh_dmae_writel(sh_chan, hw->dar, DAR);
	sh_dmae_writel(sh_chan, hw->tcr >> calc_xmit_shift(sh_chan), TCR);
}

static void dmae_start(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);

	chcr |= CHCR_DE | CHCR_IE;
	sh_dmae_writel(sh_chan, chcr, CHCR);
}

static void dmae_halt(struct sh_dmae_chan *sh_chan)
{
	u32 chcr = sh_dmae_readl(sh_chan, CHCR);

	chcr &= ~(CHCR_DE | CHCR_TE | CHCR_IE);
	sh_dmae_writel(sh_chan, chcr, CHCR);
}

static int dmae_set_chcr(struct sh_dmae_chan *sh_chan, u32 val)
{
	int ret = dmae_is_busy(sh_chan);
	/* When DMA was working, can not set data to CHCR */
	if (ret)
		return ret;

	sh_dmae_writel(sh_chan, val, CHCR);
	return 0;
}

#define DMARS1_ADDR	0x04
#define DMARS2_ADDR	0x08
#define DMARS_SHIFT 8
#define DMARS_CHAN_MSK 0x01
static int dmae_set_dmars(struct sh_dmae_chan *sh_chan, u16 val)
{
	u32 addr;
	int shift = 0;
	int ret = dmae_is_busy(sh_chan);
	if (ret)
		return ret;

	if (sh_chan->id & DMARS_CHAN_MSK)
		shift = DMARS_SHIFT;

	switch (sh_chan->id) {
	/* DMARS0 */
	case 0:
	case 1:
		addr = SH_DMARS_BASE;
		break;
	/* DMARS1 */
	case 2:
	case 3:
		addr = (SH_DMARS_BASE + DMARS1_ADDR);
		break;
	/* DMARS2 */
	case 4:
	case 5:
		addr = (SH_DMARS_BASE + DMARS2_ADDR);
		break;
	default:
		return -EINVAL;
	}

	ctrl_outw((val << shift) |
		(ctrl_inw(addr) & (shift ? 0xFF00 : 0x00FF)),
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

static int sh_dmae_alloc_chan_resources(struct dma_chan *chan)
{
	struct sh_dmae_chan *sh_chan = to_sh_chan(chan);
	struct sh_desc *desc;

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

	/* Prepared and not submitted descriptors can still be on the queue */
	if (!list_empty(&sh_chan->ld_queue))
		sh_dmae_chan_ld_cleanup(sh_chan, true);

	spin_lock_bh(&sh_chan->desc_lock);

	list_splice_init(&sh_chan->ld_free, &list);
	sh_chan->descs_allocated = 0;

	spin_unlock_bh(&sh_chan->desc_lock);

	list_for_each_entry_safe(desc, _desc, &list, node)
		kfree(desc);
}

static struct dma_async_tx_descriptor *sh_dmae_prep_memcpy(
	struct dma_chan *chan, dma_addr_t dma_dest, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct sh_dmae_chan *sh_chan;
	struct sh_desc *first = NULL, *prev = NULL, *new;
	size_t copy_size;
	LIST_HEAD(tx_list);
	int chunks = (len + SH_DMA_TCR_MAX) / (SH_DMA_TCR_MAX + 1);

	if (!chan)
		return NULL;

	if (!len)
		return NULL;

	sh_chan = to_sh_chan(chan);

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
	do {
		/* Allocate the link descriptor from the free list */
		new = sh_dmae_get_desc(sh_chan);
		if (!new) {
			dev_err(sh_chan->dev,
				"No free memory for link descriptor\n");
			list_for_each_entry(new, &tx_list, node)
				new->mark = DESC_IDLE;
			list_splice(&tx_list, &sh_chan->ld_free);
			spin_unlock_bh(&sh_chan->desc_lock);
			return NULL;
		}

		copy_size = min(len, (size_t)SH_DMA_TCR_MAX + 1);

		new->hw.sar = dma_src;
		new->hw.dar = dma_dest;
		new->hw.tcr = copy_size;
		if (!first) {
			/* First desc */
			new->async_tx.cookie = -EBUSY;
			first = new;
		} else {
			/* Other desc - invisible to the user */
			new->async_tx.cookie = -EINVAL;
		}

		dev_dbg(sh_chan->dev,
			"chaining %u of %u with %p, dst %x, cookie %d\n",
			copy_size, len, &new->async_tx, dma_dest,
			new->async_tx.cookie);

		new->mark = DESC_PREPARED;
		new->async_tx.flags = flags;
		new->chunks = chunks--;

		prev = new;
		len -= copy_size;
		dma_src += copy_size;
		dma_dest += copy_size;
		/* Insert the link descriptor to the LD ring */
		list_add_tail(&new->node, &tx_list);
	} while (len);

	if (new != first)
		new->async_tx.cookie = -ENOSPC;

	/* Put them back on the free list, so, they don't get lost */
	list_splice_tail(&tx_list, &sh_chan->ld_free);

	spin_unlock_bh(&sh_chan->desc_lock);

	return &first->async_tx;
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
			BUG_ON(sh_chan->completed_cookie != desc->cookie - 1);
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
	struct sh_desc *sd;

	spin_lock_bh(&sh_chan->desc_lock);
	/* DMA work check */
	if (dmae_is_busy(sh_chan)) {
		spin_unlock_bh(&sh_chan->desc_lock);
		return;
	}

	/* Find the first un-transfer desciptor */
	list_for_each_entry(sd, &sh_chan->ld_queue, node)
		if (sd->mark == DESC_SUBMITTED) {
			/* Get the ld start address from ld_queue */
			dmae_set_reg(sh_chan, &sd->hw);
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

	sh_dmae_chan_ld_cleanup(sh_chan, false);

	last_used = chan->cookie;
	last_complete = sh_chan->completed_cookie;
	BUG_ON(last_complete < 0);

	if (done)
		*done = last_complete;

	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
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
	int err = 0;
	struct sh_dmae_device *shdev = (struct sh_dmae_device *)data;

	/* IRQ Multi */
	if (shdev->pdata.mode & SHDMA_MIX_IRQ) {
		int cnt = 0;
		switch (irq) {
#if defined(DMTE6_IRQ) && defined(DMAE1_IRQ)
		case DMTE6_IRQ:
			cnt++;
#endif
		case DMTE0_IRQ:
			if (dmaor_read_reg(cnt) & (DMAOR_NMIF | DMAOR_AE)) {
				disable_irq(irq);
				return IRQ_HANDLED;
			}
		default:
			return IRQ_NONE;
		}
	} else {
		/* reset dma controller */
		err = sh_dmae_rst(0);
		if (err)
			return err;
#ifdef SH_DMAC_BASE1
		if (shdev->pdata.mode & SHDMA_DMAOR1) {
			err = sh_dmae_rst(1);
			if (err)
				return err;
		}
#endif
		disable_irq(irq);
		return IRQ_HANDLED;
	}
}
#endif

static void dmae_do_tasklet(unsigned long data)
{
	struct sh_dmae_chan *sh_chan = (struct sh_dmae_chan *)data;
	struct sh_desc *desc;
	u32 sar_buf = sh_dmae_readl(sh_chan, SAR);

	spin_lock(&sh_chan->desc_lock);
	list_for_each_entry(desc, &sh_chan->ld_queue, node) {
		if ((desc->hw.sar + desc->hw.tcr) == sar_buf &&
		    desc->mark == DESC_SUBMITTED) {
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

static unsigned int get_dmae_irq(unsigned int id)
{
	unsigned int irq = 0;
	if (id < ARRAY_SIZE(dmte_irq_map))
		irq = dmte_irq_map[id];
	return irq;
}

static int __devinit sh_dmae_chan_probe(struct sh_dmae_device *shdev, int id)
{
	int err;
	unsigned int irq = get_dmae_irq(id);
	unsigned long irqflags = IRQF_DISABLED;
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

	if (shdev->pdata.mode & SHDMA_MIX_IRQ) {
		irqflags = IRQF_SHARED;
#if defined(DMTE6_IRQ)
		if (irq >= DMTE6_IRQ)
			irq = DMTE6_IRQ;
		else
#endif
			irq = DMTE0_IRQ;
	}

	snprintf(new_sh_chan->dev_id, sizeof(new_sh_chan->dev_id),
			"sh-dmae%d", new_sh_chan->id);

	/* set up channel irq */
	err = request_irq(irq, &sh_dmae_interrupt, irqflags,
			  new_sh_chan->dev_id, new_sh_chan);
	if (err) {
		dev_err(shdev->common.dev, "DMA channel %d request_irq error "
			"with return %d\n", id, err);
		goto err_no_irq;
	}

	/* CHCR register control function */
	new_sh_chan->set_chcr = dmae_set_chcr;
	/* DMARS register control function */
	new_sh_chan->set_dmars = dmae_set_dmars;

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
			struct sh_dmae_chan *shchan = shdev->chan[i];
			if (!(shdev->pdata.mode & SHDMA_MIX_IRQ))
				free_irq(dmte_irq_map[i], shchan);

			list_del(&shchan->common.device_node);
			kfree(shchan);
			shdev->chan[i] = NULL;
		}
	}
	shdev->common.chancnt = 0;
}

static int __init sh_dmae_probe(struct platform_device *pdev)
{
	int err = 0, cnt, ecnt;
	unsigned long irqflags = IRQF_DISABLED;
#if defined(CONFIG_CPU_SH4)
	int eirq[] = { DMAE0_IRQ,
#if defined(DMAE1_IRQ)
			DMAE1_IRQ
#endif
		};
#endif
	struct sh_dmae_device *shdev;

	/* get platform data */
	if (!pdev->dev.platform_data)
		return -ENODEV;

	shdev = kzalloc(sizeof(struct sh_dmae_device), GFP_KERNEL);
	if (!shdev) {
		dev_err(&pdev->dev, "No enough memory\n");
		return -ENOMEM;
	}

	/* platform data */
	memcpy(&shdev->pdata, pdev->dev.platform_data,
			sizeof(struct sh_dmae_pdata));

	/* reset dma controller */
	err = sh_dmae_rst(0);
	if (err)
		goto rst_err;

	/* SH7780/85/23 has DMAOR1 */
	if (shdev->pdata.mode & SHDMA_DMAOR1) {
		err = sh_dmae_rst(1);
		if (err)
			goto rst_err;
	}

	INIT_LIST_HEAD(&shdev->common.channels);

	dma_cap_set(DMA_MEMCPY, shdev->common.cap_mask);
	shdev->common.device_alloc_chan_resources
		= sh_dmae_alloc_chan_resources;
	shdev->common.device_free_chan_resources = sh_dmae_free_chan_resources;
	shdev->common.device_prep_dma_memcpy = sh_dmae_prep_memcpy;
	shdev->common.device_is_tx_complete = sh_dmae_is_complete;
	shdev->common.device_issue_pending = sh_dmae_memcpy_issue_pending;
	shdev->common.dev = &pdev->dev;
	/* Default transfer size of 32 bytes requires 32-byte alignment */
	shdev->common.copy_align = 5;

#if defined(CONFIG_CPU_SH4)
	/* Non Mix IRQ mode SH7722/SH7730 etc... */
	if (shdev->pdata.mode & SHDMA_MIX_IRQ) {
		irqflags = IRQF_SHARED;
		eirq[0] = DMTE0_IRQ;
#if defined(DMTE6_IRQ) && defined(DMAE1_IRQ)
		eirq[1] = DMTE6_IRQ;
#endif
	}

	for (ecnt = 0 ; ecnt < ARRAY_SIZE(eirq); ecnt++) {
		err = request_irq(eirq[ecnt], sh_dmae_err, irqflags,
				  "DMAC Address Error", shdev);
		if (err) {
			dev_err(&pdev->dev, "DMA device request_irq"
				"error (irq %d) with return %d\n",
				eirq[ecnt], err);
			goto eirq_err;
		}
	}
#endif /* CONFIG_CPU_SH4 */

	/* Create DMA Channel */
	for (cnt = 0 ; cnt < MAX_DMA_CHANNELS ; cnt++) {
		err = sh_dmae_chan_probe(shdev, cnt);
		if (err)
			goto chan_probe_err;
	}

	platform_set_drvdata(pdev, shdev);
	dma_async_device_register(&shdev->common);

	return err;

chan_probe_err:
	sh_dmae_chan_remove(shdev);

eirq_err:
	for (ecnt-- ; ecnt >= 0; ecnt--)
		free_irq(eirq[ecnt], shdev);

rst_err:
	kfree(shdev);

	return err;
}

static int __exit sh_dmae_remove(struct platform_device *pdev)
{
	struct sh_dmae_device *shdev = platform_get_drvdata(pdev);

	dma_async_device_unregister(&shdev->common);

	if (shdev->pdata.mode & SHDMA_MIX_IRQ) {
		free_irq(DMTE0_IRQ, shdev);
#if defined(DMTE6_IRQ)
		free_irq(DMTE6_IRQ, shdev);
#endif
	}

	/* channel data remove */
	sh_dmae_chan_remove(shdev);

	if (!(shdev->pdata.mode & SHDMA_MIX_IRQ)) {
		free_irq(DMAE0_IRQ, shdev);
#if defined(DMAE1_IRQ)
		free_irq(DMAE1_IRQ, shdev);
#endif
	}
	kfree(shdev);

	return 0;
}

static void sh_dmae_shutdown(struct platform_device *pdev)
{
	struct sh_dmae_device *shdev = platform_get_drvdata(pdev);
	sh_dmae_ctl_stop(0);
	if (shdev->pdata.mode & SHDMA_DMAOR1)
		sh_dmae_ctl_stop(1);
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
