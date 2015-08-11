/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2004 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * BSD LICENSE
 *
 * Copyright(c) 2004-2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Support routines for v3+ hardware
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/prefetch.h>
#include "../dmaengine.h"
#include "registers.h"
#include "hw.h"
#include "dma.h"

/* ioat hardware assumes at least two sources for raid operations */
#define src_cnt_to_sw(x) ((x) + 2)
#define src_cnt_to_hw(x) ((x) - 2)
#define ndest_to_sw(x) ((x) + 1)
#define ndest_to_hw(x) ((x) - 1)
#define src16_cnt_to_sw(x) ((x) + 9)
#define src16_cnt_to_hw(x) ((x) - 9)

/* provide a lookup table for setting the source address in the base or
 * extended descriptor of an xor or pq descriptor
 */
static const u8 xor_idx_to_desc = 0xe0;
static const u8 xor_idx_to_field[] = { 1, 4, 5, 6, 7, 0, 1, 2 };
static const u8 pq_idx_to_desc = 0xf8;
static const u8 pq16_idx_to_desc[] = { 0, 0, 1, 1, 1, 1, 1, 1, 1,
				       2, 2, 2, 2, 2, 2, 2 };
static const u8 pq_idx_to_field[] = { 1, 4, 5, 0, 1, 2, 4, 5 };
static const u8 pq16_idx_to_field[] = { 1, 4, 1, 2, 3, 4, 5, 6, 7,
					0, 1, 2, 3, 4, 5, 6 };

static void ioat3_eh(struct ioatdma_chan *ioat_chan);

static void xor_set_src(struct ioat_raw_descriptor *descs[2],
			dma_addr_t addr, u32 offset, int idx)
{
	struct ioat_raw_descriptor *raw = descs[xor_idx_to_desc >> idx & 1];

	raw->field[xor_idx_to_field[idx]] = addr + offset;
}

static dma_addr_t pq_get_src(struct ioat_raw_descriptor *descs[2], int idx)
{
	struct ioat_raw_descriptor *raw = descs[pq_idx_to_desc >> idx & 1];

	return raw->field[pq_idx_to_field[idx]];
}

static dma_addr_t pq16_get_src(struct ioat_raw_descriptor *desc[3], int idx)
{
	struct ioat_raw_descriptor *raw = desc[pq16_idx_to_desc[idx]];

	return raw->field[pq16_idx_to_field[idx]];
}

static void pq_set_src(struct ioat_raw_descriptor *descs[2],
		       dma_addr_t addr, u32 offset, u8 coef, int idx)
{
	struct ioat_pq_descriptor *pq = (struct ioat_pq_descriptor *) descs[0];
	struct ioat_raw_descriptor *raw = descs[pq_idx_to_desc >> idx & 1];

	raw->field[pq_idx_to_field[idx]] = addr + offset;
	pq->coef[idx] = coef;
}

static void pq16_set_src(struct ioat_raw_descriptor *desc[3],
			dma_addr_t addr, u32 offset, u8 coef, unsigned idx)
{
	struct ioat_pq_descriptor *pq = (struct ioat_pq_descriptor *)desc[0];
	struct ioat_pq16a_descriptor *pq16 =
		(struct ioat_pq16a_descriptor *)desc[1];
	struct ioat_raw_descriptor *raw = desc[pq16_idx_to_desc[idx]];

	raw->field[pq16_idx_to_field[idx]] = addr + offset;

	if (idx < 8)
		pq->coef[idx] = coef;
	else
		pq16->coef[idx - 8] = coef;
}

static struct ioat_sed_ent *
ioat3_alloc_sed(struct ioatdma_device *ioat_dma, unsigned int hw_pool)
{
	struct ioat_sed_ent *sed;
	gfp_t flags = __GFP_ZERO | GFP_ATOMIC;

	sed = kmem_cache_alloc(ioat_sed_cache, flags);
	if (!sed)
		return NULL;

	sed->hw_pool = hw_pool;
	sed->hw = dma_pool_alloc(ioat_dma->sed_hw_pool[hw_pool],
				 flags, &sed->dma);
	if (!sed->hw) {
		kmem_cache_free(ioat_sed_cache, sed);
		return NULL;
	}

	return sed;
}

static void
ioat3_free_sed(struct ioatdma_device *ioat_dma, struct ioat_sed_ent *sed)
{
	if (!sed)
		return;

	dma_pool_free(ioat_dma->sed_hw_pool[sed->hw_pool], sed->hw, sed->dma);
	kmem_cache_free(ioat_sed_cache, sed);
}

static bool desc_has_ext(struct ioat_ring_ent *desc)
{
	struct ioat_dma_descriptor *hw = desc->hw;

	if (hw->ctl_f.op == IOAT_OP_XOR ||
	    hw->ctl_f.op == IOAT_OP_XOR_VAL) {
		struct ioat_xor_descriptor *xor = desc->xor;

		if (src_cnt_to_sw(xor->ctl_f.src_cnt) > 5)
			return true;
	} else if (hw->ctl_f.op == IOAT_OP_PQ ||
		   hw->ctl_f.op == IOAT_OP_PQ_VAL) {
		struct ioat_pq_descriptor *pq = desc->pq;

		if (src_cnt_to_sw(pq->ctl_f.src_cnt) > 3)
			return true;
	}

	return false;
}

static u64 ioat3_get_current_completion(struct ioatdma_chan *ioat_chan)
{
	u64 phys_complete;
	u64 completion;

	completion = *ioat_chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(ioat_chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	return phys_complete;
}

static bool ioat3_cleanup_preamble(struct ioatdma_chan *ioat_chan,
				   u64 *phys_complete)
{
	*phys_complete = ioat3_get_current_completion(ioat_chan);
	if (*phys_complete == ioat_chan->last_completion)
		return false;

	clear_bit(IOAT_COMPLETION_ACK, &ioat_chan->state);
	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

static void
desc_get_errstat(struct ioatdma_chan *ioat_chan, struct ioat_ring_ent *desc)
{
	struct ioat_dma_descriptor *hw = desc->hw;

	switch (hw->ctl_f.op) {
	case IOAT_OP_PQ_VAL:
	case IOAT_OP_PQ_VAL_16S:
	{
		struct ioat_pq_descriptor *pq = desc->pq;

		/* check if there's error written */
		if (!pq->dwbes_f.wbes)
			return;

		/* need to set a chanerr var for checking to clear later */

		if (pq->dwbes_f.p_val_err)
			*desc->result |= SUM_CHECK_P_RESULT;

		if (pq->dwbes_f.q_val_err)
			*desc->result |= SUM_CHECK_Q_RESULT;

		return;
	}
	default:
		return;
	}
}

/**
 * __cleanup - reclaim used descriptors
 * @ioat: channel (ring) to clean
 *
 * The difference from the dma_v2.c __cleanup() is that this routine
 * handles extended descriptors and dma-unmapping raid operations.
 */
static void __cleanup(struct ioatdma_chan *ioat_chan, dma_addr_t phys_complete)
{
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	bool seen_current = false;
	int idx = ioat_chan->tail, i;
	u16 active;

	dev_dbg(to_dev(ioat_chan), "%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail, ioat_chan->issued);

	/*
	 * At restart of the channel, the completion address and the
	 * channel status will be 0 due to starting a new chain. Since
	 * it's new chain and the first descriptor "fails", there is
	 * nothing to clean up. We do not want to reap the entire submitted
	 * chain due to this 0 address value and then BUG.
	 */
	if (!phys_complete)
		return;

	active = ioat_ring_active(ioat_chan);
	for (i = 0; i < active && !seen_current; i++) {
		struct dma_async_tx_descriptor *tx;

		smp_read_barrier_depends();
		prefetch(ioat_get_ring_ent(ioat_chan, idx + i + 1));
		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		dump_desc_dbg(ioat_chan, desc);

		/* set err stat if we are using dwbes */
		if (ioat_dma->cap & IOAT_CAP_DWBES)
			desc_get_errstat(ioat_chan, desc);

		tx = &desc->txd;
		if (tx->cookie) {
			dma_cookie_complete(tx);
			dma_descriptor_unmap(tx);
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}

		if (tx->phys == phys_complete)
			seen_current = true;

		/* skip extended descriptors */
		if (desc_has_ext(desc)) {
			BUG_ON(i + 1 >= active);
			i++;
		}

		/* cleanup super extended descriptors */
		if (desc->sed) {
			ioat3_free_sed(ioat_dma, desc->sed);
			desc->sed = NULL;
		}
	}
	smp_mb(); /* finish all descriptor reads before incrementing tail */
	ioat_chan->tail = idx + i;
	BUG_ON(active && !seen_current); /* no active descs have written a completion? */
	ioat_chan->last_completion = phys_complete;

	if (active - i == 0) {
		dev_dbg(to_dev(ioat_chan), "%s: cancel completion timeout\n",
			__func__);
		clear_bit(IOAT_COMPLETION_PENDING, &ioat_chan->state);
		mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
	}
	/* 5 microsecond delay per pending descriptor */
	writew(min((5 * (active - i)), IOAT_INTRDELAY_MASK),
	       ioat_chan->ioat_dma->reg_base + IOAT_INTRDELAY_OFFSET);
}

static void ioat3_cleanup(struct ioatdma_chan *ioat_chan)
{
	u64 phys_complete;

	spin_lock_bh(&ioat_chan->cleanup_lock);

	if (ioat3_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);

	if (is_ioat_halted(*ioat_chan->completion)) {
		u32 chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

		if (chanerr & IOAT_CHANERR_HANDLE_MASK) {
			mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
			ioat3_eh(ioat_chan);
		}
	}

	spin_unlock_bh(&ioat_chan->cleanup_lock);
}

void ioat_cleanup_event(unsigned long data)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan((void *)data);

	ioat3_cleanup(ioat_chan);
	if (!test_bit(IOAT_RUN, &ioat_chan->state))
		return;
	writew(IOAT_CHANCTRL_RUN, ioat_chan->reg_base + IOAT_CHANCTRL_OFFSET);
}

static void ioat3_restart_channel(struct ioatdma_chan *ioat_chan)
{
	u64 phys_complete;

	ioat_quiesce(ioat_chan, 0);
	if (ioat3_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);

	__ioat_restart_chan(ioat_chan);
}

static void ioat3_eh(struct ioatdma_chan *ioat_chan)
{
	struct pci_dev *pdev = to_pdev(ioat_chan);
	struct ioat_dma_descriptor *hw;
	struct dma_async_tx_descriptor *tx;
	u64 phys_complete;
	struct ioat_ring_ent *desc;
	u32 err_handled = 0;
	u32 chanerr_int;
	u32 chanerr;

	/* cleanup so tail points to descriptor that caused the error */
	if (ioat3_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);

	chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
	pci_read_config_dword(pdev, IOAT_PCI_CHANERR_INT_OFFSET, &chanerr_int);

	dev_dbg(to_dev(ioat_chan), "%s: error = %x:%x\n",
		__func__, chanerr, chanerr_int);

	desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail);
	hw = desc->hw;
	dump_desc_dbg(ioat_chan, desc);

	switch (hw->ctl_f.op) {
	case IOAT_OP_XOR_VAL:
		if (chanerr & IOAT_CHANERR_XOR_P_OR_CRC_ERR) {
			*desc->result |= SUM_CHECK_P_RESULT;
			err_handled |= IOAT_CHANERR_XOR_P_OR_CRC_ERR;
		}
		break;
	case IOAT_OP_PQ_VAL:
	case IOAT_OP_PQ_VAL_16S:
		if (chanerr & IOAT_CHANERR_XOR_P_OR_CRC_ERR) {
			*desc->result |= SUM_CHECK_P_RESULT;
			err_handled |= IOAT_CHANERR_XOR_P_OR_CRC_ERR;
		}
		if (chanerr & IOAT_CHANERR_XOR_Q_ERR) {
			*desc->result |= SUM_CHECK_Q_RESULT;
			err_handled |= IOAT_CHANERR_XOR_Q_ERR;
		}
		break;
	}

	/* fault on unhandled error or spurious halt */
	if (chanerr ^ err_handled || chanerr == 0) {
		dev_err(to_dev(ioat_chan), "%s: fatal error (%x:%x)\n",
			__func__, chanerr, err_handled);
		BUG();
	} else { /* cleanup the faulty descriptor */
		tx = &desc->txd;
		if (tx->cookie) {
			dma_cookie_complete(tx);
			dma_descriptor_unmap(tx);
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}
	}

	writel(chanerr, ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
	pci_write_config_dword(pdev, IOAT_PCI_CHANERR_INT_OFFSET, chanerr_int);

	/* mark faulting descriptor as complete */
	*ioat_chan->completion = desc->txd.phys;

	spin_lock_bh(&ioat_chan->prep_lock);
	ioat3_restart_channel(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);
}

static void check_active(struct ioatdma_chan *ioat_chan)
{
	if (ioat_ring_active(ioat_chan)) {
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
		return;
	}

	if (test_and_clear_bit(IOAT_CHAN_ACTIVE, &ioat_chan->state))
		mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
	else if (ioat_chan->alloc_order > ioat_get_alloc_order()) {
		/* if the ring is idle, empty, and oversized try to step
		 * down the size
		 */
		reshape_ring(ioat_chan, ioat_chan->alloc_order - 1);

		/* keep shrinking until we get back to our minimum
		 * default size
		 */
		if (ioat_chan->alloc_order > ioat_get_alloc_order())
			mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
	}

}

void ioat_timer_event(unsigned long data)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan((void *)data);
	dma_addr_t phys_complete;
	u64 status;

	status = ioat_chansts(ioat_chan);

	/* when halted due to errors check for channel
	 * programming errors before advancing the completion state
	 */
	if (is_ioat_halted(status)) {
		u32 chanerr;

		chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
		dev_err(to_dev(ioat_chan), "%s: Channel halted (%x)\n",
			__func__, chanerr);
		if (test_bit(IOAT_RUN, &ioat_chan->state))
			BUG_ON(is_ioat_bug(chanerr));
		else /* we never got off the ground */
			return;
	}

	/* if we haven't made progress and we have already
	 * acknowledged a pending completion once, then be more
	 * forceful with a restart
	 */
	spin_lock_bh(&ioat_chan->cleanup_lock);
	if (ioat_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);
	else if (test_bit(IOAT_COMPLETION_ACK, &ioat_chan->state)) {
		spin_lock_bh(&ioat_chan->prep_lock);
		ioat3_restart_channel(ioat_chan);
		spin_unlock_bh(&ioat_chan->prep_lock);
		spin_unlock_bh(&ioat_chan->cleanup_lock);
		return;
	} else {
		set_bit(IOAT_COMPLETION_ACK, &ioat_chan->state);
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
	}


	if (ioat_ring_active(ioat_chan))
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
	else {
		spin_lock_bh(&ioat_chan->prep_lock);
		check_active(ioat_chan);
		spin_unlock_bh(&ioat_chan->prep_lock);
	}
	spin_unlock_bh(&ioat_chan->cleanup_lock);
}

enum dma_status
ioat_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	ioat3_cleanup(ioat_chan);

	return dma_cookie_status(c, cookie, txstate);
}

static struct dma_async_tx_descriptor *
__ioat_prep_xor_lock(struct dma_chan *c, enum sum_check_flags *result,
		      dma_addr_t dest, dma_addr_t *src, unsigned int src_cnt,
		      size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent *compl_desc;
	struct ioat_ring_ent *desc;
	struct ioat_ring_ent *ext;
	size_t total_len = len;
	struct ioat_xor_descriptor *xor;
	struct ioat_xor_ext_descriptor *xor_ex = NULL;
	struct ioat_dma_descriptor *hw;
	int num_descs, with_ext, idx, i;
	u32 offset = 0;
	u8 op = result ? IOAT_OP_XOR_VAL : IOAT_OP_XOR;

	BUG_ON(src_cnt < 2);

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);
	/* we need 2x the number of descriptors to cover greater than 5
	 * sources
	 */
	if (src_cnt > 5) {
		with_ext = 1;
		num_descs *= 2;
	} else
		with_ext = 0;

	/* completion writes from the raid engine may pass completion
	 * writes from the legacy engine, so we need one extra null
	 * (legacy) descriptor to ensure all completion writes arrive in
	 * order.
	 */
	if (likely(num_descs) &&
	    ioat_check_space_lock(ioat_chan, num_descs+1) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t,
					 len, 1 << ioat_chan->xfercap_log);
		int s;

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		xor = desc->xor;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor xor_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat_get_ring_ent(ioat_chan, idx + i + 1);
		xor_ex = ext->xor_ex;

		descs[0] = (struct ioat_raw_descriptor *) xor;
		descs[1] = (struct ioat_raw_descriptor *) xor_ex;
		for (s = 0; s < src_cnt; s++)
			xor_set_src(descs, src[s], offset, s);
		xor->size = xfer_size;
		xor->dst_addr = dest + offset;
		xor->ctl = 0;
		xor->ctl_f.op = op;
		xor->ctl_f.src_cnt = src_cnt_to_hw(src_cnt);

		len -= xfer_size;
		offset += xfer_size;
		dump_desc_dbg(ioat_chan, desc);
	} while ((i += 1 + with_ext) < num_descs);

	/* last xor descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	xor->ctl_f.fence = !!(flags & DMA_PREP_FENCE);

	/* completion descriptor carries interrupt bit */
	compl_desc = ioat_get_ring_ent(ioat_chan, idx + i);
	compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
	hw = compl_desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;
	hw->size = NULL_DESC_BUFFER_SIZE;
	dump_desc_dbg(ioat_chan, compl_desc);

	/* we leave the channel locked to ensure in order submission */
	return &compl_desc->txd;
}

struct dma_async_tx_descriptor *
ioat_prep_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
	       unsigned int src_cnt, size_t len, unsigned long flags)
{
	return __ioat_prep_xor_lock(chan, NULL, dest, src, src_cnt, len, flags);
}

struct dma_async_tx_descriptor *
ioat_prep_xor_val(struct dma_chan *chan, dma_addr_t *src,
		    unsigned int src_cnt, size_t len,
		    enum sum_check_flags *result, unsigned long flags)
{
	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*result = 0;

	return __ioat_prep_xor_lock(chan, result, src[0], &src[1],
				     src_cnt - 1, len, flags);
}

static void
dump_pq_desc_dbg(struct ioatdma_chan *ioat_chan, struct ioat_ring_ent *desc,
		 struct ioat_ring_ent *ext)
{
	struct device *dev = to_dev(ioat_chan);
	struct ioat_pq_descriptor *pq = desc->pq;
	struct ioat_pq_ext_descriptor *pq_ex = ext ? ext->pq_ex : NULL;
	struct ioat_raw_descriptor *descs[] = { (void *) pq, (void *) pq_ex };
	int src_cnt = src_cnt_to_sw(pq->ctl_f.src_cnt);
	int i;

	dev_dbg(dev, "desc[%d]: (%#llx->%#llx) flags: %#x"
		" sz: %#10.8x ctl: %#x (op: %#x int: %d compl: %d pq: '%s%s'"
		" src_cnt: %d)\n",
		desc_id(desc), (unsigned long long) desc->txd.phys,
		(unsigned long long) (pq_ex ? pq_ex->next : pq->next),
		desc->txd.flags, pq->size, pq->ctl, pq->ctl_f.op, pq->ctl_f.int_en,
		pq->ctl_f.compl_write,
		pq->ctl_f.p_disable ? "" : "p", pq->ctl_f.q_disable ? "" : "q",
		pq->ctl_f.src_cnt);
	for (i = 0; i < src_cnt; i++)
		dev_dbg(dev, "\tsrc[%d]: %#llx coef: %#x\n", i,
			(unsigned long long) pq_get_src(descs, i), pq->coef[i]);
	dev_dbg(dev, "\tP: %#llx\n", pq->p_addr);
	dev_dbg(dev, "\tQ: %#llx\n", pq->q_addr);
	dev_dbg(dev, "\tNEXT: %#llx\n", pq->next);
}

static void dump_pq16_desc_dbg(struct ioatdma_chan *ioat_chan,
			       struct ioat_ring_ent *desc)
{
	struct device *dev = to_dev(ioat_chan);
	struct ioat_pq_descriptor *pq = desc->pq;
	struct ioat_raw_descriptor *descs[] = { (void *)pq,
						(void *)pq,
						(void *)pq };
	int src_cnt = src16_cnt_to_sw(pq->ctl_f.src_cnt);
	int i;

	if (desc->sed) {
		descs[1] = (void *)desc->sed->hw;
		descs[2] = (void *)desc->sed->hw + 64;
	}

	dev_dbg(dev, "desc[%d]: (%#llx->%#llx) flags: %#x"
		" sz: %#x ctl: %#x (op: %#x int: %d compl: %d pq: '%s%s'"
		" src_cnt: %d)\n",
		desc_id(desc), (unsigned long long) desc->txd.phys,
		(unsigned long long) pq->next,
		desc->txd.flags, pq->size, pq->ctl,
		pq->ctl_f.op, pq->ctl_f.int_en,
		pq->ctl_f.compl_write,
		pq->ctl_f.p_disable ? "" : "p", pq->ctl_f.q_disable ? "" : "q",
		pq->ctl_f.src_cnt);
	for (i = 0; i < src_cnt; i++) {
		dev_dbg(dev, "\tsrc[%d]: %#llx coef: %#x\n", i,
			(unsigned long long) pq16_get_src(descs, i),
			pq->coef[i]);
	}
	dev_dbg(dev, "\tP: %#llx\n", pq->p_addr);
	dev_dbg(dev, "\tQ: %#llx\n", pq->q_addr);
}

static struct dma_async_tx_descriptor *
__ioat_prep_pq_lock(struct dma_chan *c, enum sum_check_flags *result,
		     const dma_addr_t *dst, const dma_addr_t *src,
		     unsigned int src_cnt, const unsigned char *scf,
		     size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *compl_desc;
	struct ioat_ring_ent *desc;
	struct ioat_ring_ent *ext;
	size_t total_len = len;
	struct ioat_pq_descriptor *pq;
	struct ioat_pq_ext_descriptor *pq_ex = NULL;
	struct ioat_dma_descriptor *hw;
	u32 offset = 0;
	u8 op = result ? IOAT_OP_PQ_VAL : IOAT_OP_PQ;
	int i, s, idx, with_ext, num_descs;
	int cb32 = (ioat_dma->version < IOAT_VER_3_3) ? 1 : 0;

	dev_dbg(to_dev(ioat_chan), "%s\n", __func__);
	/* the engine requires at least two sources (we provide
	 * at least 1 implied source in the DMA_PREP_CONTINUE case)
	 */
	BUG_ON(src_cnt + dmaf_continue(flags) < 2);

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);
	/* we need 2x the number of descriptors to cover greater than 3
	 * sources (we need 1 extra source in the q-only continuation
	 * case and 3 extra sources in the p+q continuation case.
	 */
	if (src_cnt + dmaf_p_disabled_continue(flags) > 3 ||
	    (dmaf_continue(flags) && !dmaf_p_disabled_continue(flags))) {
		with_ext = 1;
		num_descs *= 2;
	} else
		with_ext = 0;

	/* completion writes from the raid engine may pass completion
	 * writes from the legacy engine, so we need one extra null
	 * (legacy) descriptor to ensure all completion writes arrive in
	 * order.
	 */
	if (likely(num_descs) &&
	    ioat_check_space_lock(ioat_chan, num_descs + cb32) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t, len,
					 1 << ioat_chan->xfercap_log);

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		pq = desc->pq;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor pq_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat_get_ring_ent(ioat_chan, idx + i + with_ext);
		pq_ex = ext->pq_ex;

		descs[0] = (struct ioat_raw_descriptor *) pq;
		descs[1] = (struct ioat_raw_descriptor *) pq_ex;

		for (s = 0; s < src_cnt; s++)
			pq_set_src(descs, src[s], offset, scf[s], s);

		/* see the comment for dma_maxpq in include/linux/dmaengine.h */
		if (dmaf_p_disabled_continue(flags))
			pq_set_src(descs, dst[1], offset, 1, s++);
		else if (dmaf_continue(flags)) {
			pq_set_src(descs, dst[0], offset, 0, s++);
			pq_set_src(descs, dst[1], offset, 1, s++);
			pq_set_src(descs, dst[1], offset, 0, s++);
		}
		pq->size = xfer_size;
		pq->p_addr = dst[0] + offset;
		pq->q_addr = dst[1] + offset;
		pq->ctl = 0;
		pq->ctl_f.op = op;
		/* we turn on descriptor write back error status */
		if (ioat_dma->cap & IOAT_CAP_DWBES)
			pq->ctl_f.wb_en = result ? 1 : 0;
		pq->ctl_f.src_cnt = src_cnt_to_hw(s);
		pq->ctl_f.p_disable = !!(flags & DMA_PREP_PQ_DISABLE_P);
		pq->ctl_f.q_disable = !!(flags & DMA_PREP_PQ_DISABLE_Q);

		len -= xfer_size;
		offset += xfer_size;
	} while ((i += 1 + with_ext) < num_descs);

	/* last pq descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	pq->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	dump_pq_desc_dbg(ioat_chan, desc, ext);

	if (!cb32) {
		pq->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
		pq->ctl_f.compl_write = 1;
		compl_desc = desc;
	} else {
		/* completion descriptor carries interrupt bit */
		compl_desc = ioat_get_ring_ent(ioat_chan, idx + i);
		compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
		hw = compl_desc->hw;
		hw->ctl = 0;
		hw->ctl_f.null = 1;
		hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
		hw->ctl_f.compl_write = 1;
		hw->size = NULL_DESC_BUFFER_SIZE;
		dump_desc_dbg(ioat_chan, compl_desc);
	}


	/* we leave the channel locked to ensure in order submission */
	return &compl_desc->txd;
}

static struct dma_async_tx_descriptor *
__ioat_prep_pq16_lock(struct dma_chan *c, enum sum_check_flags *result,
		       const dma_addr_t *dst, const dma_addr_t *src,
		       unsigned int src_cnt, const unsigned char *scf,
		       size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	size_t total_len = len;
	struct ioat_pq_descriptor *pq;
	u32 offset = 0;
	u8 op;
	int i, s, idx, num_descs;

	/* this function is only called with 9-16 sources */
	op = result ? IOAT_OP_PQ_VAL_16S : IOAT_OP_PQ_16S;

	dev_dbg(to_dev(ioat_chan), "%s\n", __func__);

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);

	/*
	 * 16 source pq is only available on cb3.3 and has no completion
	 * write hw bug.
	 */
	if (num_descs && ioat_check_space_lock(ioat_chan, num_descs) == 0)
		idx = ioat_chan->head;
	else
		return NULL;

	i = 0;

	do {
		struct ioat_raw_descriptor *descs[4];
		size_t xfer_size = min_t(size_t, len,
					 1 << ioat_chan->xfercap_log);

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		pq = desc->pq;

		descs[0] = (struct ioat_raw_descriptor *) pq;

		desc->sed = ioat3_alloc_sed(ioat_dma, (src_cnt-2) >> 3);
		if (!desc->sed) {
			dev_err(to_dev(ioat_chan),
				"%s: no free sed entries\n", __func__);
			return NULL;
		}

		pq->sed_addr = desc->sed->dma;
		desc->sed->parent = desc;

		descs[1] = (struct ioat_raw_descriptor *)desc->sed->hw;
		descs[2] = (void *)descs[1] + 64;

		for (s = 0; s < src_cnt; s++)
			pq16_set_src(descs, src[s], offset, scf[s], s);

		/* see the comment for dma_maxpq in include/linux/dmaengine.h */
		if (dmaf_p_disabled_continue(flags))
			pq16_set_src(descs, dst[1], offset, 1, s++);
		else if (dmaf_continue(flags)) {
			pq16_set_src(descs, dst[0], offset, 0, s++);
			pq16_set_src(descs, dst[1], offset, 1, s++);
			pq16_set_src(descs, dst[1], offset, 0, s++);
		}

		pq->size = xfer_size;
		pq->p_addr = dst[0] + offset;
		pq->q_addr = dst[1] + offset;
		pq->ctl = 0;
		pq->ctl_f.op = op;
		pq->ctl_f.src_cnt = src16_cnt_to_hw(s);
		/* we turn on descriptor write back error status */
		if (ioat_dma->cap & IOAT_CAP_DWBES)
			pq->ctl_f.wb_en = result ? 1 : 0;
		pq->ctl_f.p_disable = !!(flags & DMA_PREP_PQ_DISABLE_P);
		pq->ctl_f.q_disable = !!(flags & DMA_PREP_PQ_DISABLE_Q);

		len -= xfer_size;
		offset += xfer_size;
	} while (++i < num_descs);

	/* last pq descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	pq->ctl_f.fence = !!(flags & DMA_PREP_FENCE);

	/* with cb3.3 we should be able to do completion w/o a null desc */
	pq->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	pq->ctl_f.compl_write = 1;

	dump_pq16_desc_dbg(ioat_chan, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static int src_cnt_flags(unsigned int src_cnt, unsigned long flags)
{
	if (dmaf_p_disabled_continue(flags))
		return src_cnt + 1;
	else if (dmaf_continue(flags))
		return src_cnt + 3;
	else
		return src_cnt;
}

struct dma_async_tx_descriptor *
ioat_prep_pq(struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
	      unsigned int src_cnt, const unsigned char *scf, size_t len,
	      unsigned long flags)
{
	/* specify valid address for disabled result */
	if (flags & DMA_PREP_PQ_DISABLE_P)
		dst[0] = dst[1];
	if (flags & DMA_PREP_PQ_DISABLE_Q)
		dst[1] = dst[0];

	/* handle the single source multiply case from the raid6
	 * recovery path
	 */
	if ((flags & DMA_PREP_PQ_DISABLE_P) && src_cnt == 1) {
		dma_addr_t single_source[2];
		unsigned char single_source_coef[2];

		BUG_ON(flags & DMA_PREP_PQ_DISABLE_Q);
		single_source[0] = src[0];
		single_source[1] = src[0];
		single_source_coef[0] = scf[0];
		single_source_coef[1] = 0;

		return src_cnt_flags(src_cnt, flags) > 8 ?
			__ioat_prep_pq16_lock(chan, NULL, dst, single_source,
					       2, single_source_coef, len,
					       flags) :
			__ioat_prep_pq_lock(chan, NULL, dst, single_source, 2,
					     single_source_coef, len, flags);

	} else {
		return src_cnt_flags(src_cnt, flags) > 8 ?
			__ioat_prep_pq16_lock(chan, NULL, dst, src, src_cnt,
					       scf, len, flags) :
			__ioat_prep_pq_lock(chan, NULL, dst, src, src_cnt,
					     scf, len, flags);
	}
}

struct dma_async_tx_descriptor *
ioat_prep_pq_val(struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
		  unsigned int src_cnt, const unsigned char *scf, size_t len,
		  enum sum_check_flags *pqres, unsigned long flags)
{
	/* specify valid address for disabled result */
	if (flags & DMA_PREP_PQ_DISABLE_P)
		pq[0] = pq[1];
	if (flags & DMA_PREP_PQ_DISABLE_Q)
		pq[1] = pq[0];

	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*pqres = 0;

	return src_cnt_flags(src_cnt, flags) > 8 ?
		__ioat_prep_pq16_lock(chan, pqres, pq, src, src_cnt, scf, len,
				       flags) :
		__ioat_prep_pq_lock(chan, pqres, pq, src, src_cnt, scf, len,
				     flags);
}

struct dma_async_tx_descriptor *
ioat_prep_pqxor(struct dma_chan *chan, dma_addr_t dst, dma_addr_t *src,
		 unsigned int src_cnt, size_t len, unsigned long flags)
{
	unsigned char scf[src_cnt];
	dma_addr_t pq[2];

	memset(scf, 0, src_cnt);
	pq[0] = dst;
	flags |= DMA_PREP_PQ_DISABLE_Q;
	pq[1] = dst; /* specify valid address for disabled result */

	return src_cnt_flags(src_cnt, flags) > 8 ?
		__ioat_prep_pq16_lock(chan, NULL, pq, src, src_cnt, scf, len,
				       flags) :
		__ioat_prep_pq_lock(chan, NULL, pq, src, src_cnt, scf, len,
				     flags);
}

struct dma_async_tx_descriptor *
ioat_prep_pqxor_val(struct dma_chan *chan, dma_addr_t *src,
		     unsigned int src_cnt, size_t len,
		     enum sum_check_flags *result, unsigned long flags)
{
	unsigned char scf[src_cnt];
	dma_addr_t pq[2];

	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*result = 0;

	memset(scf, 0, src_cnt);
	pq[0] = src[0];
	flags |= DMA_PREP_PQ_DISABLE_Q;
	pq[1] = pq[0]; /* specify valid address for disabled result */

	return src_cnt_flags(src_cnt, flags) > 8 ?
		__ioat_prep_pq16_lock(chan, result, pq, &src[1], src_cnt - 1,
				       scf, len, flags) :
		__ioat_prep_pq_lock(chan, result, pq, &src[1], src_cnt - 1,
				     scf, len, flags);
}

struct dma_async_tx_descriptor *
ioat_prep_interrupt_lock(struct dma_chan *c, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;

	if (ioat_check_space_lock(ioat_chan, 1) == 0)
		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->head);
	else
		return NULL;

	hw = desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = 1;
	hw->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	hw->ctl_f.compl_write = 1;
	hw->size = NULL_DESC_BUFFER_SIZE;
	hw->src_addr = 0;
	hw->dst_addr = 0;

	desc->txd.flags = flags;
	desc->len = 1;

	dump_desc_dbg(ioat_chan, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static int ioat3_irq_reinit(struct ioatdma_device *ioat_dma)
{
	struct pci_dev *pdev = ioat_dma->pdev;
	int irq = pdev->irq, i;

	if (!is_bwd_ioat(pdev))
		return 0;

	switch (ioat_dma->irq_mode) {
	case IOAT_MSIX:
		for (i = 0; i < ioat_dma->dma_dev.chancnt; i++) {
			struct msix_entry *msix = &ioat_dma->msix_entries[i];
			struct ioatdma_chan *ioat_chan;

			ioat_chan = ioat_chan_by_index(ioat_dma, i);
			devm_free_irq(&pdev->dev, msix->vector, ioat_chan);
		}

		pci_disable_msix(pdev);
		break;
	case IOAT_MSI:
		pci_disable_msi(pdev);
		/* fall through */
	case IOAT_INTX:
		devm_free_irq(&pdev->dev, irq, ioat_dma);
		break;
	default:
		return 0;
	}
	ioat_dma->irq_mode = IOAT_NOIRQ;

	return ioat_dma_setup_interrupts(ioat_dma);
}

int ioat_reset_hw(struct ioatdma_chan *ioat_chan)
{
	/* throw away whatever the channel was doing and get it
	 * initialized, with ioat3 specific workarounds
	 */
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct pci_dev *pdev = ioat_dma->pdev;
	u32 chanerr;
	u16 dev_id;
	int err;

	ioat_quiesce(ioat_chan, msecs_to_jiffies(100));

	chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
	writel(chanerr, ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

	if (ioat_dma->version < IOAT_VER_3_3) {
		/* clear any pending errors */
		err = pci_read_config_dword(pdev,
				IOAT_PCI_CHANERR_INT_OFFSET, &chanerr);
		if (err) {
			dev_err(&pdev->dev,
				"channel error register unreachable\n");
			return err;
		}
		pci_write_config_dword(pdev,
				IOAT_PCI_CHANERR_INT_OFFSET, chanerr);

		/* Clear DMAUNCERRSTS Cfg-Reg Parity Error status bit
		 * (workaround for spurious config parity error after restart)
		 */
		pci_read_config_word(pdev, IOAT_PCI_DEVICE_ID_OFFSET, &dev_id);
		if (dev_id == PCI_DEVICE_ID_INTEL_IOAT_TBG0) {
			pci_write_config_dword(pdev,
					       IOAT_PCI_DMAUNCERRSTS_OFFSET,
					       0x10);
		}
	}

	err = ioat_reset_sync(ioat_chan, msecs_to_jiffies(200));
	if (!err)
		err = ioat3_irq_reinit(ioat_dma);

	if (err)
		dev_err(&pdev->dev, "Failed to reset: %d\n", err);

	return err;
}
