/*
 * Core driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007-2008 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include "../dmaengine.h"
#include "internal.h"

/*
 * This supports the Synopsys "DesignWare AHB Central DMA Controller",
 * (DW_ahb_dmac) which is used with various AMBA 2.0 systems (not all
 * of which use ARM any more).  See the "Databook" from Synopsys for
 * information beyond what licensees probably provide.
 *
 * The driver has been tested with the Atmel AT32AP7000, which does not
 * support descriptor writeback.
 */

#define DWC_DEFAULT_CTLLO(_chan) ({				\
		struct dw_dma_chan *_dwc = to_dw_dma_chan(_chan);	\
		struct dma_slave_config	*_sconfig = &_dwc->dma_sconfig;	\
		bool _is_slave = is_slave_direction(_dwc->direction);	\
		u8 _smsize = _is_slave ? _sconfig->src_maxburst :	\
			DW_DMA_MSIZE_16;			\
		u8 _dmsize = _is_slave ? _sconfig->dst_maxburst :	\
			DW_DMA_MSIZE_16;			\
		u8 _dms = (_dwc->direction == DMA_MEM_TO_DEV) ?		\
			_dwc->dws.p_master : _dwc->dws.m_master;	\
		u8 _sms = (_dwc->direction == DMA_DEV_TO_MEM) ?		\
			_dwc->dws.p_master : _dwc->dws.m_master;	\
								\
		(DWC_CTLL_DST_MSIZE(_dmsize)			\
		 | DWC_CTLL_SRC_MSIZE(_smsize)			\
		 | DWC_CTLL_LLP_D_EN				\
		 | DWC_CTLL_LLP_S_EN				\
		 | DWC_CTLL_DMS(_dms)				\
		 | DWC_CTLL_SMS(_sms));				\
	})

/* The set of bus widths supported by the DMA controller */
#define DW_DMA_BUSWIDTHS			  \
	BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED)	| \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)		| \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES)		| \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)

/*----------------------------------------------------------------------*/

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static struct dw_desc *dwc_first_active(struct dw_dma_chan *dwc)
{
	return to_dw_desc(dwc->active_list.next);
}

static dma_cookie_t dwc_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct dw_desc		*desc = txd_to_dw_desc(tx);
	struct dw_dma_chan	*dwc = to_dw_dma_chan(tx->chan);
	dma_cookie_t		cookie;
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	cookie = dma_cookie_assign(tx);

	/*
	 * REVISIT: We should attempt to chain as many descriptors as
	 * possible, perhaps even appending to those already submitted
	 * for DMA. But this is hard to do in a race-free manner.
	 */

	list_add_tail(&desc->desc_node, &dwc->queue);
	spin_unlock_irqrestore(&dwc->lock, flags);
	dev_vdbg(chan2dev(tx->chan), "%s: queued %u\n",
		 __func__, desc->txd.cookie);

	return cookie;
}

static struct dw_desc *dwc_desc_get(struct dw_dma_chan *dwc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	struct dw_desc *desc;
	dma_addr_t phys;

	desc = dma_pool_zalloc(dw->desc_pool, GFP_ATOMIC, &phys);
	if (!desc)
		return NULL;

	dwc->descs_allocated++;
	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->txd, &dwc->chan);
	desc->txd.tx_submit = dwc_tx_submit;
	desc->txd.flags = DMA_CTRL_ACK;
	desc->txd.phys = phys;
	return desc;
}

static void dwc_desc_put(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	struct dw_desc *child, *_next;

	if (unlikely(!desc))
		return;

	list_for_each_entry_safe(child, _next, &desc->tx_list, desc_node) {
		list_del(&child->desc_node);
		dma_pool_free(dw->desc_pool, child, child->txd.phys);
		dwc->descs_allocated--;
	}

	dma_pool_free(dw->desc_pool, desc, desc->txd.phys);
	dwc->descs_allocated--;
}

static void dwc_initialize_chan_idma32(struct dw_dma_chan *dwc)
{
	u32 cfghi = 0;
	u32 cfglo = 0;

	/* Set default burst alignment */
	cfglo |= IDMA32C_CFGL_DST_BURST_ALIGN | IDMA32C_CFGL_SRC_BURST_ALIGN;

	/* Low 4 bits of the request lines */
	cfghi |= IDMA32C_CFGH_DST_PER(dwc->dws.dst_id & 0xf);
	cfghi |= IDMA32C_CFGH_SRC_PER(dwc->dws.src_id & 0xf);

	/* Request line extension (2 bits) */
	cfghi |= IDMA32C_CFGH_DST_PER_EXT(dwc->dws.dst_id >> 4 & 0x3);
	cfghi |= IDMA32C_CFGH_SRC_PER_EXT(dwc->dws.src_id >> 4 & 0x3);

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);
}

static void dwc_initialize_chan_dw(struct dw_dma_chan *dwc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	u32 cfghi = DWC_CFGH_FIFO_MODE;
	u32 cfglo = DWC_CFGL_CH_PRIOR(dwc->priority);
	bool hs_polarity = dwc->dws.hs_polarity;

	cfghi |= DWC_CFGH_DST_PER(dwc->dws.dst_id);
	cfghi |= DWC_CFGH_SRC_PER(dwc->dws.src_id);
	cfghi |= DWC_CFGH_PROTCTL(dw->pdata->protctl);

	/* Set polarity of handshake interface */
	cfglo |= hs_polarity ? DWC_CFGL_HS_DST_POL | DWC_CFGL_HS_SRC_POL : 0;

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);
}

static void dwc_initialize(struct dw_dma_chan *dwc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);

	if (test_bit(DW_DMA_IS_INITIALIZED, &dwc->flags))
		return;

	if (dw->pdata->is_idma32)
		dwc_initialize_chan_idma32(dwc);
	else
		dwc_initialize_chan_dw(dwc);

	/* Enable interrupts */
	channel_set_bit(dw, MASK.XFER, dwc->mask);
	channel_set_bit(dw, MASK.ERROR, dwc->mask);

	set_bit(DW_DMA_IS_INITIALIZED, &dwc->flags);
}

/*----------------------------------------------------------------------*/

static inline void dwc_dump_chan_regs(struct dw_dma_chan *dwc)
{
	dev_err(chan2dev(&dwc->chan),
		"  SAR: 0x%x DAR: 0x%x LLP: 0x%x CTL: 0x%x:%08x\n",
		channel_readl(dwc, SAR),
		channel_readl(dwc, DAR),
		channel_readl(dwc, LLP),
		channel_readl(dwc, CTL_HI),
		channel_readl(dwc, CTL_LO));
}

static inline void dwc_chan_disable(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	channel_clear_bit(dw, CH_EN, dwc->mask);
	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();
}

static u32 bytes2block(struct dw_dma_chan *dwc, size_t bytes,
			  unsigned int width, size_t *len)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	u32 block;

	/* Always in bytes for iDMA 32-bit */
	if (dw->pdata->is_idma32)
		width = 0;

	if ((bytes >> width) > dwc->block_size) {
		block = dwc->block_size;
		*len = block << width;
	} else {
		block = bytes >> width;
		*len = bytes;
	}

	return block;
}

static size_t block2bytes(struct dw_dma_chan *dwc, u32 block, u32 width)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);

	if (dw->pdata->is_idma32)
		return IDMA32C_CTLH_BLOCK_TS(block);

	return DWC_CTLH_BLOCK_TS(block) << width;
}

/*----------------------------------------------------------------------*/

/* Perform single block transfer */
static inline void dwc_do_single_block(struct dw_dma_chan *dwc,
				       struct dw_desc *desc)
{
	struct dw_dma	*dw = to_dw_dma(dwc->chan.device);
	u32		ctllo;

	/*
	 * Software emulation of LLP mode relies on interrupts to continue
	 * multi block transfer.
	 */
	ctllo = lli_read(desc, ctllo) | DWC_CTLL_INT_EN;

	channel_writel(dwc, SAR, lli_read(desc, sar));
	channel_writel(dwc, DAR, lli_read(desc, dar));
	channel_writel(dwc, CTL_LO, ctllo);
	channel_writel(dwc, CTL_HI, lli_read(desc, ctlhi));
	channel_set_bit(dw, CH_EN, dwc->mask);

	/* Move pointer to next descriptor */
	dwc->tx_node_active = dwc->tx_node_active->next;
}

/* Called with dwc->lock held and bh disabled */
static void dwc_dostart(struct dw_dma_chan *dwc, struct dw_desc *first)
{
	struct dw_dma	*dw = to_dw_dma(dwc->chan.device);
	u8		lms = DWC_LLP_LMS(dwc->dws.m_master);
	unsigned long	was_soft_llp;

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"%s: BUG: Attempted to start non-idle channel\n",
			__func__);
		dwc_dump_chan_regs(dwc);

		/* The tasklet will hopefully advance the queue... */
		return;
	}

	if (dwc->nollp) {
		was_soft_llp = test_and_set_bit(DW_DMA_IS_SOFT_LLP,
						&dwc->flags);
		if (was_soft_llp) {
			dev_err(chan2dev(&dwc->chan),
				"BUG: Attempted to start new LLP transfer inside ongoing one\n");
			return;
		}

		dwc_initialize(dwc);

		first->residue = first->total_len;
		dwc->tx_node_active = &first->tx_list;

		/* Submit first block */
		dwc_do_single_block(dwc, first);

		return;
	}

	dwc_initialize(dwc);

	channel_writel(dwc, LLP, first->txd.phys | lms);
	channel_writel(dwc, CTL_LO, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	channel_writel(dwc, CTL_HI, 0);
	channel_set_bit(dw, CH_EN, dwc->mask);
}

static void dwc_dostart_first_queued(struct dw_dma_chan *dwc)
{
	struct dw_desc *desc;

	if (list_empty(&dwc->queue))
		return;

	list_move(dwc->queue.next, &dwc->active_list);
	desc = dwc_first_active(dwc);
	dev_vdbg(chan2dev(&dwc->chan), "%s: started %u\n", __func__, desc->txd.cookie);
	dwc_dostart(dwc, desc);
}

/*----------------------------------------------------------------------*/

static void
dwc_descriptor_complete(struct dw_dma_chan *dwc, struct dw_desc *desc,
		bool callback_required)
{
	struct dma_async_tx_descriptor	*txd = &desc->txd;
	struct dw_desc			*child;
	unsigned long			flags;
	struct dmaengine_desc_callback	cb;

	dev_vdbg(chan2dev(&dwc->chan), "descriptor %u complete\n", txd->cookie);

	spin_lock_irqsave(&dwc->lock, flags);
	dma_cookie_complete(txd);
	if (callback_required)
		dmaengine_desc_get_callback(txd, &cb);
	else
		memset(&cb, 0, sizeof(cb));

	/* async_tx_ack */
	list_for_each_entry(child, &desc->tx_list, desc_node)
		async_tx_ack(&child->txd);
	async_tx_ack(&desc->txd);
	dwc_desc_put(dwc, desc);
	spin_unlock_irqrestore(&dwc->lock, flags);

	dmaengine_desc_callback_invoke(&cb, NULL);
}

static void dwc_complete_all(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	struct dw_desc *desc, *_desc;
	LIST_HEAD(list);
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"BUG: XFER bit set, but channel not idle!\n");

		/* Try to continue after resetting the channel... */
		dwc_chan_disable(dw, dwc);
	}

	/*
	 * Submit queued descriptors ASAP, i.e. before we go through
	 * the completed ones.
	 */
	list_splice_init(&dwc->active_list, &list);
	dwc_dostart_first_queued(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc, true);
}

/* Returns how many bytes were already received from source */
static inline u32 dwc_get_sent(struct dw_dma_chan *dwc)
{
	u32 ctlhi = channel_readl(dwc, CTL_HI);
	u32 ctllo = channel_readl(dwc, CTL_LO);

	return block2bytes(dwc, ctlhi, ctllo >> 4 & 7);
}

static void dwc_scan_descriptors(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	dma_addr_t llp;
	struct dw_desc *desc, *_desc;
	struct dw_desc *child;
	u32 status_xfer;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	llp = channel_readl(dwc, LLP);
	status_xfer = dma_readl(dw, RAW.XFER);

	if (status_xfer & dwc->mask) {
		/* Everything we've submitted is done */
		dma_writel(dw, CLEAR.XFER, dwc->mask);

		if (test_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags)) {
			struct list_head *head, *active = dwc->tx_node_active;

			/*
			 * We are inside first active descriptor.
			 * Otherwise something is really wrong.
			 */
			desc = dwc_first_active(dwc);

			head = &desc->tx_list;
			if (active != head) {
				/* Update residue to reflect last sent descriptor */
				if (active == head->next)
					desc->residue -= desc->len;
				else
					desc->residue -= to_dw_desc(active->prev)->len;

				child = to_dw_desc(active);

				/* Submit next block */
				dwc_do_single_block(dwc, child);

				spin_unlock_irqrestore(&dwc->lock, flags);
				return;
			}

			/* We are done here */
			clear_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags);
		}

		spin_unlock_irqrestore(&dwc->lock, flags);

		dwc_complete_all(dw, dwc);
		return;
	}

	if (list_empty(&dwc->active_list)) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		return;
	}

	if (test_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags)) {
		dev_vdbg(chan2dev(&dwc->chan), "%s: soft LLP mode\n", __func__);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return;
	}

	dev_vdbg(chan2dev(&dwc->chan), "%s: llp=%pad\n", __func__, &llp);

	list_for_each_entry_safe(desc, _desc, &dwc->active_list, desc_node) {
		/* Initial residue value */
		desc->residue = desc->total_len;

		/* Check first descriptors addr */
		if (desc->txd.phys == DWC_LLP_LOC(llp)) {
			spin_unlock_irqrestore(&dwc->lock, flags);
			return;
		}

		/* Check first descriptors llp */
		if (lli_read(desc, llp) == llp) {
			/* This one is currently in progress */
			desc->residue -= dwc_get_sent(dwc);
			spin_unlock_irqrestore(&dwc->lock, flags);
			return;
		}

		desc->residue -= desc->len;
		list_for_each_entry(child, &desc->tx_list, desc_node) {
			if (lli_read(child, llp) == llp) {
				/* Currently in progress */
				desc->residue -= dwc_get_sent(dwc);
				spin_unlock_irqrestore(&dwc->lock, flags);
				return;
			}
			desc->residue -= child->len;
		}

		/*
		 * No descriptors so far seem to be in progress, i.e.
		 * this one must be done.
		 */
		spin_unlock_irqrestore(&dwc->lock, flags);
		dwc_descriptor_complete(dwc, desc, true);
		spin_lock_irqsave(&dwc->lock, flags);
	}

	dev_err(chan2dev(&dwc->chan),
		"BUG: All descriptors done, but channel not idle!\n");

	/* Try to continue after resetting the channel... */
	dwc_chan_disable(dw, dwc);

	dwc_dostart_first_queued(dwc);
	spin_unlock_irqrestore(&dwc->lock, flags);
}

static inline void dwc_dump_lli(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	dev_crit(chan2dev(&dwc->chan), "  desc: s0x%x d0x%x l0x%x c0x%x:%x\n",
		 lli_read(desc, sar),
		 lli_read(desc, dar),
		 lli_read(desc, llp),
		 lli_read(desc, ctlhi),
		 lli_read(desc, ctllo));
}

static void dwc_handle_error(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	struct dw_desc *bad_desc;
	struct dw_desc *child;
	unsigned long flags;

	dwc_scan_descriptors(dw, dwc);

	spin_lock_irqsave(&dwc->lock, flags);

	/*
	 * The descriptor currently at the head of the active list is
	 * borked. Since we don't have any way to report errors, we'll
	 * just have to scream loudly and try to carry on.
	 */
	bad_desc = dwc_first_active(dwc);
	list_del_init(&bad_desc->desc_node);
	list_move(dwc->queue.next, dwc->active_list.prev);

	/* Clear the error flag and try to restart the controller */
	dma_writel(dw, CLEAR.ERROR, dwc->mask);
	if (!list_empty(&dwc->active_list))
		dwc_dostart(dwc, dwc_first_active(dwc));

	/*
	 * WARN may seem harsh, but since this only happens
	 * when someone submits a bad physical address in a
	 * descriptor, we should consider ourselves lucky that the
	 * controller flagged an error instead of scribbling over
	 * random memory locations.
	 */
	dev_WARN(chan2dev(&dwc->chan), "Bad descriptor submitted for DMA!\n"
				       "  cookie: %d\n", bad_desc->txd.cookie);
	dwc_dump_lli(dwc, bad_desc);
	list_for_each_entry(child, &bad_desc->tx_list, desc_node)
		dwc_dump_lli(dwc, child);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Pretend the descriptor completed successfully */
	dwc_descriptor_complete(dwc, bad_desc, true);
}

static void dw_dma_tasklet(unsigned long data)
{
	struct dw_dma *dw = (struct dw_dma *)data;
	struct dw_dma_chan *dwc;
	u32 status_xfer;
	u32 status_err;
	unsigned int i;

	status_xfer = dma_readl(dw, RAW.XFER);
	status_err = dma_readl(dw, RAW.ERROR);

	dev_vdbg(dw->dma.dev, "%s: status_err=%x\n", __func__, status_err);

	for (i = 0; i < dw->dma.chancnt; i++) {
		dwc = &dw->chan[i];
		if (test_bit(DW_DMA_IS_CYCLIC, &dwc->flags))
			dev_vdbg(dw->dma.dev, "Cyclic xfer is not implemented\n");
		else if (status_err & (1 << i))
			dwc_handle_error(dw, dwc);
		else if (status_xfer & (1 << i))
			dwc_scan_descriptors(dw, dwc);
	}

	/* Re-enable interrupts */
	channel_set_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_set_bit(dw, MASK.ERROR, dw->all_chan_mask);
}

static irqreturn_t dw_dma_interrupt(int irq, void *dev_id)
{
	struct dw_dma *dw = dev_id;
	u32 status;

	/* Check if we have any interrupt from the DMAC which is not in use */
	if (!dw->in_use)
		return IRQ_NONE;

	status = dma_readl(dw, STATUS_INT);
	dev_vdbg(dw->dma.dev, "%s: status=0x%x\n", __func__, status);

	/* Check if we have any interrupt from the DMAC */
	if (!status)
		return IRQ_NONE;

	/*
	 * Just disable the interrupts. We'll turn them back on in the
	 * softirq handler.
	 */
	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.BLOCK, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	status = dma_readl(dw, STATUS_INT);
	if (status) {
		dev_err(dw->dma.dev,
			"BUG: Unexpected interrupts pending: 0x%x\n",
			status);

		/* Try to recover */
		channel_clear_bit(dw, MASK.XFER, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.BLOCK, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.SRC_TRAN, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.DST_TRAN, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.ERROR, (1 << 8) - 1);
	}

	tasklet_schedule(&dw->tasklet);

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

static struct dma_async_tx_descriptor *
dwc_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc;
	struct dw_desc		*first;
	struct dw_desc		*prev;
	size_t			xfer_count;
	size_t			offset;
	u8			m_master = dwc->dws.m_master;
	unsigned int		src_width;
	unsigned int		dst_width;
	unsigned int		data_width = dw->pdata->data_width[m_master];
	u32			ctllo;
	u8			lms = DWC_LLP_LMS(m_master);

	dev_vdbg(chan2dev(chan),
			"%s: d%pad s%pad l0x%zx f0x%lx\n", __func__,
			&dest, &src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "%s: length is zero!\n", __func__);
		return NULL;
	}

	dwc->direction = DMA_MEM_TO_MEM;

	src_width = dst_width = __ffs(data_width | src | dest | len);

	ctllo = DWC_DEFAULT_CTLLO(chan)
			| DWC_CTLL_DST_WIDTH(dst_width)
			| DWC_CTLL_SRC_WIDTH(src_width)
			| DWC_CTLL_DST_INC
			| DWC_CTLL_SRC_INC
			| DWC_CTLL_FC_M2M;
	prev = first = NULL;

	for (offset = 0; offset < len; offset += xfer_count) {
		desc = dwc_desc_get(dwc);
		if (!desc)
			goto err_desc_get;

		lli_write(desc, sar, src + offset);
		lli_write(desc, dar, dest + offset);
		lli_write(desc, ctllo, ctllo);
		lli_write(desc, ctlhi, bytes2block(dwc, len - offset, src_width, &xfer_count));
		desc->len = xfer_count;

		if (!first) {
			first = desc;
		} else {
			lli_write(prev, llp, desc->txd.phys | lms);
			list_add_tail(&desc->desc_node, &first->tx_list);
		}
		prev = desc;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		lli_set(prev, ctllo, DWC_CTLL_INT_EN);

	prev->lli.llp = 0;
	lli_clear(prev, ctllo, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	first->txd.flags = flags;
	first->total_len = len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
}

static struct dma_async_tx_descriptor *
dwc_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dma_slave_config	*sconfig = &dwc->dma_sconfig;
	struct dw_desc		*prev;
	struct dw_desc		*first;
	u32			ctllo;
	u8			m_master = dwc->dws.m_master;
	u8			lms = DWC_LLP_LMS(m_master);
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		data_width = dw->pdata->data_width[m_master];
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	if (unlikely(!is_slave_direction(direction) || !sg_len))
		return NULL;

	dwc->direction = direction;

	prev = first = NULL;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		reg_width = __ffs(sconfig->dst_addr_width);
		reg = sconfig->dst_addr;
		ctllo = (DWC_DEFAULT_CTLLO(chan)
				| DWC_CTLL_DST_WIDTH(reg_width)
				| DWC_CTLL_DST_FIX
				| DWC_CTLL_SRC_INC);

		ctllo |= sconfig->device_fc ? DWC_CTLL_FC(DW_DMA_FC_P_M2P) :
			DWC_CTLL_FC(DW_DMA_FC_D_M2P);

		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, mem;
			size_t		dlen;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);

			mem_width = __ffs(data_width | mem | len);

slave_sg_todev_fill_desc:
			desc = dwc_desc_get(dwc);
			if (!desc)
				goto err_desc_get;

			lli_write(desc, sar, mem);
			lli_write(desc, dar, reg);
			lli_write(desc, ctlhi, bytes2block(dwc, len, mem_width, &dlen));
			lli_write(desc, ctllo, ctllo | DWC_CTLL_SRC_WIDTH(mem_width));
			desc->len = dlen;

			if (!first) {
				first = desc;
			} else {
				lli_write(prev, llp, desc->txd.phys | lms);
				list_add_tail(&desc->desc_node, &first->tx_list);
			}
			prev = desc;

			mem += dlen;
			len -= dlen;
			total_len += dlen;

			if (len)
				goto slave_sg_todev_fill_desc;
		}
		break;
	case DMA_DEV_TO_MEM:
		reg_width = __ffs(sconfig->src_addr_width);
		reg = sconfig->src_addr;
		ctllo = (DWC_DEFAULT_CTLLO(chan)
				| DWC_CTLL_SRC_WIDTH(reg_width)
				| DWC_CTLL_DST_INC
				| DWC_CTLL_SRC_FIX);

		ctllo |= sconfig->device_fc ? DWC_CTLL_FC(DW_DMA_FC_P_P2M) :
			DWC_CTLL_FC(DW_DMA_FC_D_P2M);

		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, mem;
			size_t		dlen;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);

slave_sg_fromdev_fill_desc:
			desc = dwc_desc_get(dwc);
			if (!desc)
				goto err_desc_get;

			lli_write(desc, sar, reg);
			lli_write(desc, dar, mem);
			lli_write(desc, ctlhi, bytes2block(dwc, len, reg_width, &dlen));
			mem_width = __ffs(data_width | mem | dlen);
			lli_write(desc, ctllo, ctllo | DWC_CTLL_DST_WIDTH(mem_width));
			desc->len = dlen;

			if (!first) {
				first = desc;
			} else {
				lli_write(prev, llp, desc->txd.phys | lms);
				list_add_tail(&desc->desc_node, &first->tx_list);
			}
			prev = desc;

			mem += dlen;
			len -= dlen;
			total_len += dlen;

			if (len)
				goto slave_sg_fromdev_fill_desc;
		}
		break;
	default:
		return NULL;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		lli_set(prev, ctllo, DWC_CTLL_INT_EN);

	prev->lli.llp = 0;
	lli_clear(prev, ctllo, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	first->total_len = total_len;

	return &first->txd;

err_desc_get:
	dev_err(chan2dev(chan),
		"not enough descriptors available. Direction %d\n", direction);
	dwc_desc_put(dwc, first);
	return NULL;
}

bool dw_dma_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	struct dw_dma_slave *dws = param;

	if (dws->dma_dev != chan->device->dev)
		return false;

	/* We have to copy data since dws can be temporary storage */
	memcpy(&dwc->dws, dws, sizeof(struct dw_dma_slave));

	return true;
}
EXPORT_SYMBOL_GPL(dw_dma_filter);

static int dwc_config(struct dma_chan *chan, struct dma_slave_config *sconfig)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	struct dma_slave_config *sc = &dwc->dma_sconfig;
	struct dw_dma *dw = to_dw_dma(chan->device);
	/*
	 * Fix sconfig's burst size according to dw_dmac. We need to convert
	 * them as:
	 * 1 -> 0, 4 -> 1, 8 -> 2, 16 -> 3.
	 *
	 * NOTE: burst size 2 is not supported by DesignWare controller.
	 *       iDMA 32-bit supports it.
	 */
	u32 s = dw->pdata->is_idma32 ? 1 : 2;

	/* Check if chan will be configured for slave transfers */
	if (!is_slave_direction(sconfig->direction))
		return -EINVAL;

	memcpy(&dwc->dma_sconfig, sconfig, sizeof(*sconfig));
	dwc->direction = sconfig->direction;

	sc->src_maxburst = sc->src_maxburst > 1 ? fls(sc->src_maxburst) - s : 0;
	sc->dst_maxburst = sc->dst_maxburst > 1 ? fls(sc->dst_maxburst) - s : 0;

	return 0;
}

static void dwc_chan_pause(struct dw_dma_chan *dwc, bool drain)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	unsigned int		count = 20;	/* timeout iterations */
	u32			cfglo;

	cfglo = channel_readl(dwc, CFG_LO);
	if (dw->pdata->is_idma32) {
		if (drain)
			cfglo |= IDMA32C_CFGL_CH_DRAIN;
		else
			cfglo &= ~IDMA32C_CFGL_CH_DRAIN;
	}
	channel_writel(dwc, CFG_LO, cfglo | DWC_CFGL_CH_SUSP);
	while (!(channel_readl(dwc, CFG_LO) & DWC_CFGL_FIFO_EMPTY) && count--)
		udelay(2);

	set_bit(DW_DMA_IS_PAUSED, &dwc->flags);
}

static int dwc_pause(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc_chan_pause(dwc, false);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static inline void dwc_chan_resume(struct dw_dma_chan *dwc)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	channel_writel(dwc, CFG_LO, cfglo & ~DWC_CFGL_CH_SUSP);

	clear_bit(DW_DMA_IS_PAUSED, &dwc->flags);
}

static int dwc_resume(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);

	if (test_bit(DW_DMA_IS_PAUSED, &dwc->flags))
		dwc_chan_resume(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc_terminate_all(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	unsigned long		flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&dwc->lock, flags);

	clear_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags);

	dwc_chan_pause(dwc, true);

	dwc_chan_disable(dw, dwc);

	dwc_chan_resume(dwc);

	/* active_list entries will end up before queued entries */
	list_splice_init(&dwc->queue, &list);
	list_splice_init(&dwc->active_list, &list);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Flush all pending and queued descriptors */
	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc, false);

	return 0;
}

static struct dw_desc *dwc_find_desc(struct dw_dma_chan *dwc, dma_cookie_t c)
{
	struct dw_desc *desc;

	list_for_each_entry(desc, &dwc->active_list, desc_node)
		if (desc->txd.cookie == c)
			return desc;

	return NULL;
}

static u32 dwc_get_residue(struct dw_dma_chan *dwc, dma_cookie_t cookie)
{
	struct dw_desc *desc;
	unsigned long flags;
	u32 residue;

	spin_lock_irqsave(&dwc->lock, flags);

	desc = dwc_find_desc(dwc, cookie);
	if (desc) {
		if (desc == dwc_first_active(dwc)) {
			residue = desc->residue;
			if (test_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags) && residue)
				residue -= dwc_get_sent(dwc);
		} else {
			residue = desc->total_len;
		}
	} else {
		residue = 0;
	}

	spin_unlock_irqrestore(&dwc->lock, flags);
	return residue;
}

static enum dma_status
dwc_tx_status(struct dma_chan *chan,
	      dma_cookie_t cookie,
	      struct dma_tx_state *txstate)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	enum dma_status		ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	dwc_scan_descriptors(to_dw_dma(chan->device), dwc);

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	dma_set_residue(txstate, dwc_get_residue(dwc, cookie));

	if (test_bit(DW_DMA_IS_PAUSED, &dwc->flags) && ret == DMA_IN_PROGRESS)
		return DMA_PAUSED;

	return ret;
}

static void dwc_issue_pending(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (list_empty(&dwc->active_list))
		dwc_dostart_first_queued(dwc);
	spin_unlock_irqrestore(&dwc->lock, flags);
}

/*----------------------------------------------------------------------*/

/*
 * Program FIFO size of channels.
 *
 * By default full FIFO (512 bytes) is assigned to channel 0. Here we
 * slice FIFO on equal parts between channels.
 */
static void idma32_fifo_partition(struct dw_dma *dw)
{
	u64 value = IDMA32C_FP_PSIZE_CH0(64) | IDMA32C_FP_PSIZE_CH1(64) |
		    IDMA32C_FP_UPDATE;
	u64 fifo_partition = 0;

	if (!dw->pdata->is_idma32)
		return;

	/* Fill FIFO_PARTITION low bits (Channels 0..1, 4..5) */
	fifo_partition |= value << 0;

	/* Fill FIFO_PARTITION high bits (Channels 2..3, 6..7) */
	fifo_partition |= value << 32;

	/* Program FIFO Partition registers - 64 bytes per channel */
	idma32_writeq(dw, FIFO_PARTITION1, fifo_partition);
	idma32_writeq(dw, FIFO_PARTITION0, fifo_partition);
}

static void dw_dma_off(struct dw_dma *dw)
{
	unsigned int i;

	dma_writel(dw, CFG, 0);

	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.BLOCK, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.SRC_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.DST_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	while (dma_readl(dw, CFG) & DW_CFG_DMA_EN)
		cpu_relax();

	for (i = 0; i < dw->dma.chancnt; i++)
		clear_bit(DW_DMA_IS_INITIALIZED, &dw->chan[i].flags);
}

static void dw_dma_on(struct dw_dma *dw)
{
	dma_writel(dw, CFG, DW_CFG_DMA_EN);
}

static int dwc_alloc_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_dbg(chan2dev(chan), "DMA channel not idle?\n");
		return -EIO;
	}

	dma_cookie_init(chan);

	/*
	 * NOTE: some controllers may have additional features that we
	 * need to initialize here, like "scatter-gather" (which
	 * doesn't mean what you think it means), and status writeback.
	 */

	/*
	 * We need controller-specific data to set up slave transfers.
	 */
	if (chan->private && !dw_dma_filter(chan, chan->private)) {
		dev_warn(chan2dev(chan), "Wrong controller-specific data\n");
		return -EINVAL;
	}

	/* Enable controller here if needed */
	if (!dw->in_use)
		dw_dma_on(dw);
	dw->in_use |= dwc->mask;

	return 0;
}

static void dwc_free_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	unsigned long		flags;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "%s: descs allocated=%u\n", __func__,
			dwc->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&dwc->active_list));
	BUG_ON(!list_empty(&dwc->queue));
	BUG_ON(dma_readl(to_dw_dma(chan->device), CH_EN) & dwc->mask);

	spin_lock_irqsave(&dwc->lock, flags);

	/* Clear custom channel configuration */
	memset(&dwc->dws, 0, sizeof(struct dw_dma_slave));

	clear_bit(DW_DMA_IS_INITIALIZED, &dwc->flags);

	/* Disable interrupts */
	channel_clear_bit(dw, MASK.XFER, dwc->mask);
	channel_clear_bit(dw, MASK.BLOCK, dwc->mask);
	channel_clear_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Disable controller in case it was a last user */
	dw->in_use &= ~dwc->mask;
	if (!dw->in_use)
		dw_dma_off(dw);

	dev_vdbg(chan2dev(chan), "%s: done\n", __func__);
}

int dw_dma_probe(struct dw_dma_chip *chip)
{
	struct dw_dma_platform_data *pdata;
	struct dw_dma		*dw;
	bool			autocfg = false;
	unsigned int		dw_params;
	unsigned int		i;
	int			err;

	dw = devm_kzalloc(chip->dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw->pdata = devm_kzalloc(chip->dev, sizeof(*dw->pdata), GFP_KERNEL);
	if (!dw->pdata)
		return -ENOMEM;

	dw->regs = chip->regs;
	chip->dw = dw;

	pm_runtime_get_sync(chip->dev);

	if (!chip->pdata) {
		dw_params = dma_readl(dw, DW_PARAMS);
		dev_dbg(chip->dev, "DW_PARAMS: 0x%08x\n", dw_params);

		autocfg = dw_params >> DW_PARAMS_EN & 1;
		if (!autocfg) {
			err = -EINVAL;
			goto err_pdata;
		}

		/* Reassign the platform data pointer */
		pdata = dw->pdata;

		/* Get hardware configuration parameters */
		pdata->nr_channels = (dw_params >> DW_PARAMS_NR_CHAN & 7) + 1;
		pdata->nr_masters = (dw_params >> DW_PARAMS_NR_MASTER & 3) + 1;
		for (i = 0; i < pdata->nr_masters; i++) {
			pdata->data_width[i] =
				4 << (dw_params >> DW_PARAMS_DATA_WIDTH(i) & 3);
		}
		pdata->block_size = dma_readl(dw, MAX_BLK_SIZE);

		/* Fill platform data with the default values */
		pdata->is_private = true;
		pdata->is_memcpy = true;
		pdata->chan_allocation_order = CHAN_ALLOCATION_ASCENDING;
		pdata->chan_priority = CHAN_PRIORITY_ASCENDING;
	} else if (chip->pdata->nr_channels > DW_DMA_MAX_NR_CHANNELS) {
		err = -EINVAL;
		goto err_pdata;
	} else {
		memcpy(dw->pdata, chip->pdata, sizeof(*dw->pdata));

		/* Reassign the platform data pointer */
		pdata = dw->pdata;
	}

	dw->chan = devm_kcalloc(chip->dev, pdata->nr_channels, sizeof(*dw->chan),
				GFP_KERNEL);
	if (!dw->chan) {
		err = -ENOMEM;
		goto err_pdata;
	}

	/* Calculate all channel mask before DMA setup */
	dw->all_chan_mask = (1 << pdata->nr_channels) - 1;

	/* Force dma off, just in case */
	dw_dma_off(dw);

	idma32_fifo_partition(dw);

	/* Device and instance ID for IRQ and DMA pool */
	if (pdata->is_idma32)
		snprintf(dw->name, sizeof(dw->name), "idma32:dmac%d", chip->id);
	else
		snprintf(dw->name, sizeof(dw->name), "dw:dmac%d", chip->id);

	/* Create a pool of consistent memory blocks for hardware descriptors */
	dw->desc_pool = dmam_pool_create(dw->name, chip->dev,
					 sizeof(struct dw_desc), 4, 0);
	if (!dw->desc_pool) {
		dev_err(chip->dev, "No memory for descriptors dma pool\n");
		err = -ENOMEM;
		goto err_pdata;
	}

	tasklet_init(&dw->tasklet, dw_dma_tasklet, (unsigned long)dw);

	err = request_irq(chip->irq, dw_dma_interrupt, IRQF_SHARED,
			  dw->name, dw);
	if (err)
		goto err_pdata;

	INIT_LIST_HEAD(&dw->dma.channels);
	for (i = 0; i < pdata->nr_channels; i++) {
		struct dw_dma_chan	*dwc = &dw->chan[i];

		dwc->chan.device = &dw->dma;
		dma_cookie_init(&dwc->chan);
		if (pdata->chan_allocation_order == CHAN_ALLOCATION_ASCENDING)
			list_add_tail(&dwc->chan.device_node,
					&dw->dma.channels);
		else
			list_add(&dwc->chan.device_node, &dw->dma.channels);

		/* 7 is highest priority & 0 is lowest. */
		if (pdata->chan_priority == CHAN_PRIORITY_ASCENDING)
			dwc->priority = pdata->nr_channels - i - 1;
		else
			dwc->priority = i;

		dwc->ch_regs = &__dw_regs(dw)->CHAN[i];
		spin_lock_init(&dwc->lock);
		dwc->mask = 1 << i;

		INIT_LIST_HEAD(&dwc->active_list);
		INIT_LIST_HEAD(&dwc->queue);

		channel_clear_bit(dw, CH_EN, dwc->mask);

		dwc->direction = DMA_TRANS_NONE;

		/* Hardware configuration */
		if (autocfg) {
			unsigned int r = DW_DMA_MAX_NR_CHANNELS - i - 1;
			void __iomem *addr = &__dw_regs(dw)->DWC_PARAMS[r];
			unsigned int dwc_params = readl(addr);

			dev_dbg(chip->dev, "DWC_PARAMS[%d]: 0x%08x\n", i,
					   dwc_params);

			/*
			 * Decode maximum block size for given channel. The
			 * stored 4 bit value represents blocks from 0x00 for 3
			 * up to 0x0a for 4095.
			 */
			dwc->block_size =
				(4 << ((pdata->block_size >> 4 * i) & 0xf)) - 1;
			dwc->nollp =
				(dwc_params >> DWC_PARAMS_MBLK_EN & 0x1) == 0;
		} else {
			dwc->block_size = pdata->block_size;
			dwc->nollp = !pdata->multi_block[i];
		}
	}

	/* Clear all interrupts on all channels. */
	dma_writel(dw, CLEAR.XFER, dw->all_chan_mask);
	dma_writel(dw, CLEAR.BLOCK, dw->all_chan_mask);
	dma_writel(dw, CLEAR.SRC_TRAN, dw->all_chan_mask);
	dma_writel(dw, CLEAR.DST_TRAN, dw->all_chan_mask);
	dma_writel(dw, CLEAR.ERROR, dw->all_chan_mask);

	/* Set capabilities */
	dma_cap_set(DMA_SLAVE, dw->dma.cap_mask);
	if (pdata->is_private)
		dma_cap_set(DMA_PRIVATE, dw->dma.cap_mask);
	if (pdata->is_memcpy)
		dma_cap_set(DMA_MEMCPY, dw->dma.cap_mask);

	dw->dma.dev = chip->dev;
	dw->dma.device_alloc_chan_resources = dwc_alloc_chan_resources;
	dw->dma.device_free_chan_resources = dwc_free_chan_resources;

	dw->dma.device_prep_dma_memcpy = dwc_prep_dma_memcpy;
	dw->dma.device_prep_slave_sg = dwc_prep_slave_sg;

	dw->dma.device_config = dwc_config;
	dw->dma.device_pause = dwc_pause;
	dw->dma.device_resume = dwc_resume;
	dw->dma.device_terminate_all = dwc_terminate_all;

	dw->dma.device_tx_status = dwc_tx_status;
	dw->dma.device_issue_pending = dwc_issue_pending;

	/* DMA capabilities */
	dw->dma.src_addr_widths = DW_DMA_BUSWIDTHS;
	dw->dma.dst_addr_widths = DW_DMA_BUSWIDTHS;
	dw->dma.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV) |
			     BIT(DMA_MEM_TO_MEM);
	dw->dma.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	err = dma_async_device_register(&dw->dma);
	if (err)
		goto err_dma_register;

	dev_info(chip->dev, "DesignWare DMA Controller, %d channels\n",
		 pdata->nr_channels);

	pm_runtime_put_sync_suspend(chip->dev);

	return 0;

err_dma_register:
	free_irq(chip->irq, dw);
err_pdata:
	pm_runtime_put_sync_suspend(chip->dev);
	return err;
}
EXPORT_SYMBOL_GPL(dw_dma_probe);

int dw_dma_remove(struct dw_dma_chip *chip)
{
	struct dw_dma		*dw = chip->dw;
	struct dw_dma_chan	*dwc, *_dwc;

	pm_runtime_get_sync(chip->dev);

	dw_dma_off(dw);
	dma_async_device_unregister(&dw->dma);

	free_irq(chip->irq, dw);
	tasklet_kill(&dw->tasklet);

	list_for_each_entry_safe(dwc, _dwc, &dw->dma.channels,
			chan.device_node) {
		list_del(&dwc->chan.device_node);
		channel_clear_bit(dw, CH_EN, dwc->mask);
	}

	pm_runtime_put_sync_suspend(chip->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_dma_remove);

int dw_dma_disable(struct dw_dma_chip *chip)
{
	struct dw_dma *dw = chip->dw;

	dw_dma_off(dw);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_dma_disable);

int dw_dma_enable(struct dw_dma_chip *chip)
{
	struct dw_dma *dw = chip->dw;

	idma32_fifo_partition(dw);

	dw_dma_on(dw);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_dma_enable);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare DMA Controller core driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_AUTHOR("Viresh Kumar <vireshk@kernel.org>");
