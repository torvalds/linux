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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "dma_v2.h"

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

/*
 * technically sources 1 and 2 do not require SED, but the op will have
 * at least 9 descriptors so that's irrelevant.
 */
static const u8 pq16_idx_to_sed[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,
				      1, 1, 1, 1, 1, 1, 1 };

static void ioat3_eh(struct ioat2_dma_chan *ioat);

static dma_addr_t xor_get_src(struct ioat_raw_descriptor *descs[2], int idx)
{
	struct ioat_raw_descriptor *raw = descs[xor_idx_to_desc >> idx & 1];

	return raw->field[xor_idx_to_field[idx]];
}

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

static int sed_get_pq16_pool_idx(int src_cnt)
{

	return pq16_idx_to_sed[src_cnt];
}

static bool is_jf_ioat(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_IOAT_JSF0:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF1:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF2:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF3:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF4:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF5:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF6:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF7:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF8:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF9:
		return true;
	default:
		return false;
	}
}

static bool is_snb_ioat(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_IOAT_SNB0:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB1:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB2:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB3:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB4:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB5:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB6:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB7:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB8:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB9:
		return true;
	default:
		return false;
	}
}

static bool is_ivb_ioat(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_IOAT_IVB0:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB1:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB2:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB3:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB4:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB5:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB6:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB7:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB8:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB9:
		return true;
	default:
		return false;
	}

}

static bool is_hsw_ioat(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_IOAT_HSW0:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW1:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW2:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW3:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW4:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW5:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW6:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW7:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW8:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW9:
		return true;
	default:
		return false;
	}

}

static bool is_xeon_cb32(struct pci_dev *pdev)
{
	return is_jf_ioat(pdev) || is_snb_ioat(pdev) || is_ivb_ioat(pdev) ||
		is_hsw_ioat(pdev);
}

static bool is_bwd_ioat(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_IOAT_BWD0:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD1:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD2:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD3:
		return true;
	default:
		return false;
	}
}

static bool is_bwd_noraid(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_IOAT_BWD2:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD3:
		return true;
	default:
		return false;
	}

}

static void pq16_set_src(struct ioat_raw_descriptor *desc[3],
			dma_addr_t addr, u32 offset, u8 coef, int idx)
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
ioat3_alloc_sed(struct ioatdma_device *device, unsigned int hw_pool)
{
	struct ioat_sed_ent *sed;
	gfp_t flags = __GFP_ZERO | GFP_ATOMIC;

	sed = kmem_cache_alloc(device->sed_pool, flags);
	if (!sed)
		return NULL;

	sed->hw_pool = hw_pool;
	sed->hw = dma_pool_alloc(device->sed_hw_pool[hw_pool],
				 flags, &sed->dma);
	if (!sed->hw) {
		kmem_cache_free(device->sed_pool, sed);
		return NULL;
	}

	return sed;
}

static void ioat3_free_sed(struct ioatdma_device *device, struct ioat_sed_ent *sed)
{
	if (!sed)
		return;

	dma_pool_free(device->sed_hw_pool[sed->hw_pool], sed->hw, sed->dma);
	kmem_cache_free(device->sed_pool, sed);
}

static void ioat3_dma_unmap(struct ioat2_dma_chan *ioat,
			    struct ioat_ring_ent *desc, int idx)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct pci_dev *pdev = chan->device->pdev;
	size_t len = desc->len;
	size_t offset = len - desc->hw->size;
	struct dma_async_tx_descriptor *tx = &desc->txd;
	enum dma_ctrl_flags flags = tx->flags;

	switch (desc->hw->ctl_f.op) {
	case IOAT_OP_COPY:
		if (!desc->hw->ctl_f.null) /* skip 'interrupt' ops */
			ioat_dma_unmap(chan, flags, len, desc->hw);
		break;
	case IOAT_OP_FILL: {
		struct ioat_fill_descriptor *hw = desc->fill;

		if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP))
			ioat_unmap(pdev, hw->dst_addr - offset, len,
				   PCI_DMA_FROMDEVICE, flags, 1);
		break;
	}
	case IOAT_OP_XOR_VAL:
	case IOAT_OP_XOR: {
		struct ioat_xor_descriptor *xor = desc->xor;
		struct ioat_ring_ent *ext;
		struct ioat_xor_ext_descriptor *xor_ex = NULL;
		int src_cnt = src_cnt_to_sw(xor->ctl_f.src_cnt);
		struct ioat_raw_descriptor *descs[2];
		int i;

		if (src_cnt > 5) {
			ext = ioat2_get_ring_ent(ioat, idx + 1);
			xor_ex = ext->xor_ex;
		}

		if (!(flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			descs[0] = (struct ioat_raw_descriptor *) xor;
			descs[1] = (struct ioat_raw_descriptor *) xor_ex;
			for (i = 0; i < src_cnt; i++) {
				dma_addr_t src = xor_get_src(descs, i);

				ioat_unmap(pdev, src - offset, len,
					   PCI_DMA_TODEVICE, flags, 0);
			}

			/* dest is a source in xor validate operations */
			if (xor->ctl_f.op == IOAT_OP_XOR_VAL) {
				ioat_unmap(pdev, xor->dst_addr - offset, len,
					   PCI_DMA_TODEVICE, flags, 1);
				break;
			}
		}

		if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP))
			ioat_unmap(pdev, xor->dst_addr - offset, len,
				   PCI_DMA_FROMDEVICE, flags, 1);
		break;
	}
	case IOAT_OP_PQ_VAL:
	case IOAT_OP_PQ: {
		struct ioat_pq_descriptor *pq = desc->pq;
		struct ioat_ring_ent *ext;
		struct ioat_pq_ext_descriptor *pq_ex = NULL;
		int src_cnt = src_cnt_to_sw(pq->ctl_f.src_cnt);
		struct ioat_raw_descriptor *descs[2];
		int i;

		if (src_cnt > 3) {
			ext = ioat2_get_ring_ent(ioat, idx + 1);
			pq_ex = ext->pq_ex;
		}

		/* in the 'continue' case don't unmap the dests as sources */
		if (dmaf_p_disabled_continue(flags))
			src_cnt--;
		else if (dmaf_continue(flags))
			src_cnt -= 3;

		if (!(flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			descs[0] = (struct ioat_raw_descriptor *) pq;
			descs[1] = (struct ioat_raw_descriptor *) pq_ex;
			for (i = 0; i < src_cnt; i++) {
				dma_addr_t src = pq_get_src(descs, i);

				ioat_unmap(pdev, src - offset, len,
					   PCI_DMA_TODEVICE, flags, 0);
			}

			/* the dests are sources in pq validate operations */
			if (pq->ctl_f.op == IOAT_OP_XOR_VAL) {
				if (!(flags & DMA_PREP_PQ_DISABLE_P))
					ioat_unmap(pdev, pq->p_addr - offset,
						   len, PCI_DMA_TODEVICE, flags, 0);
				if (!(flags & DMA_PREP_PQ_DISABLE_Q))
					ioat_unmap(pdev, pq->q_addr - offset,
						   len, PCI_DMA_TODEVICE, flags, 0);
				break;
			}
		}

		if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
			if (!(flags & DMA_PREP_PQ_DISABLE_P))
				ioat_unmap(pdev, pq->p_addr - offset, len,
					   PCI_DMA_BIDIRECTIONAL, flags, 1);
			if (!(flags & DMA_PREP_PQ_DISABLE_Q))
				ioat_unmap(pdev, pq->q_addr - offset, len,
					   PCI_DMA_BIDIRECTIONAL, flags, 1);
		}
		break;
	}
	case IOAT_OP_PQ_16S:
	case IOAT_OP_PQ_VAL_16S: {
		struct ioat_pq_descriptor *pq = desc->pq;
		int src_cnt = src16_cnt_to_sw(pq->ctl_f.src_cnt);
		struct ioat_raw_descriptor *descs[4];
		int i;

		/* in the 'continue' case don't unmap the dests as sources */
		if (dmaf_p_disabled_continue(flags))
			src_cnt--;
		else if (dmaf_continue(flags))
			src_cnt -= 3;

		if (!(flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			descs[0] = (struct ioat_raw_descriptor *)pq;
			descs[1] = (struct ioat_raw_descriptor *)(desc->sed->hw);
			descs[2] = (struct ioat_raw_descriptor *)(&desc->sed->hw->b[0]);
			for (i = 0; i < src_cnt; i++) {
				dma_addr_t src = pq16_get_src(descs, i);

				ioat_unmap(pdev, src - offset, len,
					   PCI_DMA_TODEVICE, flags, 0);
			}

			/* the dests are sources in pq validate operations */
			if (pq->ctl_f.op == IOAT_OP_XOR_VAL) {
				if (!(flags & DMA_PREP_PQ_DISABLE_P))
					ioat_unmap(pdev, pq->p_addr - offset,
						   len, PCI_DMA_TODEVICE,
						   flags, 0);
				if (!(flags & DMA_PREP_PQ_DISABLE_Q))
					ioat_unmap(pdev, pq->q_addr - offset,
						   len, PCI_DMA_TODEVICE,
						   flags, 0);
				break;
			}
		}

		if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
			if (!(flags & DMA_PREP_PQ_DISABLE_P))
				ioat_unmap(pdev, pq->p_addr - offset, len,
					   PCI_DMA_BIDIRECTIONAL, flags, 1);
			if (!(flags & DMA_PREP_PQ_DISABLE_Q))
				ioat_unmap(pdev, pq->q_addr - offset, len,
					   PCI_DMA_BIDIRECTIONAL, flags, 1);
		}
		break;
	}
	default:
		dev_err(&pdev->dev, "%s: unknown op type: %#x\n",
			__func__, desc->hw->ctl_f.op);
	}
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

static u64 ioat3_get_current_completion(struct ioat_chan_common *chan)
{
	u64 phys_complete;
	u64 completion;

	completion = *chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	return phys_complete;
}

static bool ioat3_cleanup_preamble(struct ioat_chan_common *chan,
				   u64 *phys_complete)
{
	*phys_complete = ioat3_get_current_completion(chan);
	if (*phys_complete == chan->last_completion)
		return false;

	clear_bit(IOAT_COMPLETION_ACK, &chan->state);
	mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

static void
desc_get_errstat(struct ioat2_dma_chan *ioat, struct ioat_ring_ent *desc)
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
static void __cleanup(struct ioat2_dma_chan *ioat, dma_addr_t phys_complete)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct ioatdma_device *device = chan->device;
	struct ioat_ring_ent *desc;
	bool seen_current = false;
	int idx = ioat->tail, i;
	u16 active;

	dev_dbg(to_dev(chan), "%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat->head, ioat->tail, ioat->issued);

	/*
	 * At restart of the channel, the completion address and the
	 * channel status will be 0 due to starting a new chain. Since
	 * it's new chain and the first descriptor "fails", there is
	 * nothing to clean up. We do not want to reap the entire submitted
	 * chain due to this 0 address value and then BUG.
	 */
	if (!phys_complete)
		return;

	active = ioat2_ring_active(ioat);
	for (i = 0; i < active && !seen_current; i++) {
		struct dma_async_tx_descriptor *tx;

		smp_read_barrier_depends();
		prefetch(ioat2_get_ring_ent(ioat, idx + i + 1));
		desc = ioat2_get_ring_ent(ioat, idx + i);
		dump_desc_dbg(ioat, desc);

		/* set err stat if we are using dwbes */
		if (device->cap & IOAT_CAP_DWBES)
			desc_get_errstat(ioat, desc);

		tx = &desc->txd;
		if (tx->cookie) {
			dma_cookie_complete(tx);
			ioat3_dma_unmap(ioat, desc, idx + i);
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
			ioat3_free_sed(device, desc->sed);
			desc->sed = NULL;
		}
	}
	smp_mb(); /* finish all descriptor reads before incrementing tail */
	ioat->tail = idx + i;
	BUG_ON(active && !seen_current); /* no active descs have written a completion? */
	chan->last_completion = phys_complete;

	if (active - i == 0) {
		dev_dbg(to_dev(chan), "%s: cancel completion timeout\n",
			__func__);
		clear_bit(IOAT_COMPLETION_PENDING, &chan->state);
		mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	}
	/* 5 microsecond delay per pending descriptor */
	writew(min((5 * (active - i)), IOAT_INTRDELAY_MASK),
	       chan->device->reg_base + IOAT_INTRDELAY_OFFSET);
}

static void ioat3_cleanup(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	u64 phys_complete;

	spin_lock_bh(&chan->cleanup_lock);

	if (ioat3_cleanup_preamble(chan, &phys_complete))
		__cleanup(ioat, phys_complete);

	if (is_ioat_halted(*chan->completion)) {
		u32 chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);

		if (chanerr & IOAT_CHANERR_HANDLE_MASK) {
			mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
			ioat3_eh(ioat);
		}
	}

	spin_unlock_bh(&chan->cleanup_lock);
}

static void ioat3_cleanup_event(unsigned long data)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan((void *) data);

	ioat3_cleanup(ioat);
	writew(IOAT_CHANCTRL_RUN, ioat->base.reg_base + IOAT_CHANCTRL_OFFSET);
}

static void ioat3_restart_channel(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	u64 phys_complete;

	ioat2_quiesce(chan, 0);
	if (ioat3_cleanup_preamble(chan, &phys_complete))
		__cleanup(ioat, phys_complete);

	__ioat2_restart_chan(ioat);
}

static void ioat3_eh(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct pci_dev *pdev = to_pdev(chan);
	struct ioat_dma_descriptor *hw;
	u64 phys_complete;
	struct ioat_ring_ent *desc;
	u32 err_handled = 0;
	u32 chanerr_int;
	u32 chanerr;

	/* cleanup so tail points to descriptor that caused the error */
	if (ioat3_cleanup_preamble(chan, &phys_complete))
		__cleanup(ioat, phys_complete);

	chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
	pci_read_config_dword(pdev, IOAT_PCI_CHANERR_INT_OFFSET, &chanerr_int);

	dev_dbg(to_dev(chan), "%s: error = %x:%x\n",
		__func__, chanerr, chanerr_int);

	desc = ioat2_get_ring_ent(ioat, ioat->tail);
	hw = desc->hw;
	dump_desc_dbg(ioat, desc);

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
		dev_err(to_dev(chan), "%s: fatal error (%x:%x)\n",
			__func__, chanerr, err_handled);
		BUG();
	}

	writel(chanerr, chan->reg_base + IOAT_CHANERR_OFFSET);
	pci_write_config_dword(pdev, IOAT_PCI_CHANERR_INT_OFFSET, chanerr_int);

	/* mark faulting descriptor as complete */
	*chan->completion = desc->txd.phys;

	spin_lock_bh(&ioat->prep_lock);
	ioat3_restart_channel(ioat);
	spin_unlock_bh(&ioat->prep_lock);
}

static void check_active(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;

	if (ioat2_ring_active(ioat)) {
		mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
		return;
	}

	if (test_and_clear_bit(IOAT_CHAN_ACTIVE, &chan->state))
		mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	else if (ioat->alloc_order > ioat_get_alloc_order()) {
		/* if the ring is idle, empty, and oversized try to step
		 * down the size
		 */
		reshape_ring(ioat, ioat->alloc_order - 1);

		/* keep shrinking until we get back to our minimum
		 * default size
		 */
		if (ioat->alloc_order > ioat_get_alloc_order())
			mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	}

}

static void ioat3_timer_event(unsigned long data)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan((void *) data);
	struct ioat_chan_common *chan = &ioat->base;
	dma_addr_t phys_complete;
	u64 status;

	status = ioat_chansts(chan);

	/* when halted due to errors check for channel
	 * programming errors before advancing the completion state
	 */
	if (is_ioat_halted(status)) {
		u32 chanerr;

		chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
		dev_err(to_dev(chan), "%s: Channel halted (%x)\n",
			__func__, chanerr);
		if (test_bit(IOAT_RUN, &chan->state))
			BUG_ON(is_ioat_bug(chanerr));
		else /* we never got off the ground */
			return;
	}

	/* if we haven't made progress and we have already
	 * acknowledged a pending completion once, then be more
	 * forceful with a restart
	 */
	spin_lock_bh(&chan->cleanup_lock);
	if (ioat_cleanup_preamble(chan, &phys_complete))
		__cleanup(ioat, phys_complete);
	else if (test_bit(IOAT_COMPLETION_ACK, &chan->state)) {
		spin_lock_bh(&ioat->prep_lock);
		ioat3_restart_channel(ioat);
		spin_unlock_bh(&ioat->prep_lock);
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	} else {
		set_bit(IOAT_COMPLETION_ACK, &chan->state);
		mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
	}


	if (ioat2_ring_active(ioat))
		mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
	else {
		spin_lock_bh(&ioat->prep_lock);
		check_active(ioat);
		spin_unlock_bh(&ioat->prep_lock);
	}
	spin_unlock_bh(&chan->cleanup_lock);
}

static enum dma_status
ioat3_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_SUCCESS)
		return ret;

	ioat3_cleanup(ioat);

	return dma_cookie_status(c, cookie, txstate);
}

static struct dma_async_tx_descriptor *
ioat3_prep_memset_lock(struct dma_chan *c, dma_addr_t dest, int value,
		       size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_ring_ent *desc;
	size_t total_len = len;
	struct ioat_fill_descriptor *fill;
	u64 src_data = (0x0101010101010101ULL) * (value & 0xff);
	int num_descs, idx, i;

	num_descs = ioat2_xferlen_to_descs(ioat, len);
	if (likely(num_descs) && ioat2_check_space_lock(ioat, num_descs) == 0)
		idx = ioat->head;
	else
		return NULL;
	i = 0;
	do {
		size_t xfer_size = min_t(size_t, len, 1 << ioat->xfercap_log);

		desc = ioat2_get_ring_ent(ioat, idx + i);
		fill = desc->fill;

		fill->size = xfer_size;
		fill->src_data = src_data;
		fill->dst_addr = dest;
		fill->ctl = 0;
		fill->ctl_f.op = IOAT_OP_FILL;

		len -= xfer_size;
		dest += xfer_size;
		dump_desc_dbg(ioat, desc);
	} while (++i < num_descs);

	desc->txd.flags = flags;
	desc->len = total_len;
	fill->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	fill->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	fill->ctl_f.compl_write = 1;
	dump_desc_dbg(ioat, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static struct dma_async_tx_descriptor *
__ioat3_prep_xor_lock(struct dma_chan *c, enum sum_check_flags *result,
		      dma_addr_t dest, dma_addr_t *src, unsigned int src_cnt,
		      size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
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

	num_descs = ioat2_xferlen_to_descs(ioat, len);
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
	if (likely(num_descs) && ioat2_check_space_lock(ioat, num_descs+1) == 0)
		idx = ioat->head;
	else
		return NULL;
	i = 0;
	do {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t, len, 1 << ioat->xfercap_log);
		int s;

		desc = ioat2_get_ring_ent(ioat, idx + i);
		xor = desc->xor;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor xor_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat2_get_ring_ent(ioat, idx + i + 1);
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
		dump_desc_dbg(ioat, desc);
	} while ((i += 1 + with_ext) < num_descs);

	/* last xor descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	xor->ctl_f.fence = !!(flags & DMA_PREP_FENCE);

	/* completion descriptor carries interrupt bit */
	compl_desc = ioat2_get_ring_ent(ioat, idx + i);
	compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
	hw = compl_desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;
	hw->size = NULL_DESC_BUFFER_SIZE;
	dump_desc_dbg(ioat, compl_desc);

	/* we leave the channel locked to ensure in order submission */
	return &compl_desc->txd;
}

static struct dma_async_tx_descriptor *
ioat3_prep_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
	       unsigned int src_cnt, size_t len, unsigned long flags)
{
	return __ioat3_prep_xor_lock(chan, NULL, dest, src, src_cnt, len, flags);
}

struct dma_async_tx_descriptor *
ioat3_prep_xor_val(struct dma_chan *chan, dma_addr_t *src,
		    unsigned int src_cnt, size_t len,
		    enum sum_check_flags *result, unsigned long flags)
{
	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*result = 0;

	return __ioat3_prep_xor_lock(chan, result, src[0], &src[1],
				     src_cnt - 1, len, flags);
}

static void
dump_pq_desc_dbg(struct ioat2_dma_chan *ioat, struct ioat_ring_ent *desc, struct ioat_ring_ent *ext)
{
	struct device *dev = to_dev(&ioat->base);
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

static void dump_pq16_desc_dbg(struct ioat2_dma_chan *ioat,
			       struct ioat_ring_ent *desc)
{
	struct device *dev = to_dev(&ioat->base);
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
__ioat3_prep_pq_lock(struct dma_chan *c, enum sum_check_flags *result,
		     const dma_addr_t *dst, const dma_addr_t *src,
		     unsigned int src_cnt, const unsigned char *scf,
		     size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioatdma_device *device = chan->device;
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
	int cb32 = (device->version < IOAT_VER_3_3) ? 1 : 0;

	dev_dbg(to_dev(chan), "%s\n", __func__);
	/* the engine requires at least two sources (we provide
	 * at least 1 implied source in the DMA_PREP_CONTINUE case)
	 */
	BUG_ON(src_cnt + dmaf_continue(flags) < 2);

	num_descs = ioat2_xferlen_to_descs(ioat, len);
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
	    ioat2_check_space_lock(ioat, num_descs + cb32) == 0)
		idx = ioat->head;
	else
		return NULL;
	i = 0;
	do {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t, len, 1 << ioat->xfercap_log);

		desc = ioat2_get_ring_ent(ioat, idx + i);
		pq = desc->pq;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor pq_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat2_get_ring_ent(ioat, idx + i + with_ext);
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
		if (device->cap & IOAT_CAP_DWBES)
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
	dump_pq_desc_dbg(ioat, desc, ext);

	if (!cb32) {
		pq->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
		pq->ctl_f.compl_write = 1;
		compl_desc = desc;
	} else {
		/* completion descriptor carries interrupt bit */
		compl_desc = ioat2_get_ring_ent(ioat, idx + i);
		compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
		hw = compl_desc->hw;
		hw->ctl = 0;
		hw->ctl_f.null = 1;
		hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
		hw->ctl_f.compl_write = 1;
		hw->size = NULL_DESC_BUFFER_SIZE;
		dump_desc_dbg(ioat, compl_desc);
	}


	/* we leave the channel locked to ensure in order submission */
	return &compl_desc->txd;
}

static struct dma_async_tx_descriptor *
__ioat3_prep_pq16_lock(struct dma_chan *c, enum sum_check_flags *result,
		       const dma_addr_t *dst, const dma_addr_t *src,
		       unsigned int src_cnt, const unsigned char *scf,
		       size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioatdma_device *device = chan->device;
	struct ioat_ring_ent *desc;
	size_t total_len = len;
	struct ioat_pq_descriptor *pq;
	u32 offset = 0;
	u8 op;
	int i, s, idx, num_descs;

	/* this function only handles src_cnt 9 - 16 */
	BUG_ON(src_cnt < 9);

	/* this function is only called with 9-16 sources */
	op = result ? IOAT_OP_PQ_VAL_16S : IOAT_OP_PQ_16S;

	dev_dbg(to_dev(chan), "%s\n", __func__);

	num_descs = ioat2_xferlen_to_descs(ioat, len);

	/*
	 * 16 source pq is only available on cb3.3 and has no completion
	 * write hw bug.
	 */
	if (num_descs && ioat2_check_space_lock(ioat, num_descs) == 0)
		idx = ioat->head;
	else
		return NULL;

	i = 0;

	do {
		struct ioat_raw_descriptor *descs[4];
		size_t xfer_size = min_t(size_t, len, 1 << ioat->xfercap_log);

		desc = ioat2_get_ring_ent(ioat, idx + i);
		pq = desc->pq;

		descs[0] = (struct ioat_raw_descriptor *) pq;

		desc->sed = ioat3_alloc_sed(device,
					    sed_get_pq16_pool_idx(src_cnt));
		if (!desc->sed) {
			dev_err(to_dev(chan),
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
		if (device->cap & IOAT_CAP_DWBES)
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

	dump_pq16_desc_dbg(ioat, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static struct dma_async_tx_descriptor *
ioat3_prep_pq(struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
	      unsigned int src_cnt, const unsigned char *scf, size_t len,
	      unsigned long flags)
{
	struct dma_device *dma = chan->device;

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

		return (src_cnt > 8) && (dma->max_pq > 8) ?
			__ioat3_prep_pq16_lock(chan, NULL, dst, single_source,
					       2, single_source_coef, len,
					       flags) :
			__ioat3_prep_pq_lock(chan, NULL, dst, single_source, 2,
					     single_source_coef, len, flags);

	} else {
		return (src_cnt > 8) && (dma->max_pq > 8) ?
			__ioat3_prep_pq16_lock(chan, NULL, dst, src, src_cnt,
					       scf, len, flags) :
			__ioat3_prep_pq_lock(chan, NULL, dst, src, src_cnt,
					     scf, len, flags);
	}
}

struct dma_async_tx_descriptor *
ioat3_prep_pq_val(struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
		  unsigned int src_cnt, const unsigned char *scf, size_t len,
		  enum sum_check_flags *pqres, unsigned long flags)
{
	struct dma_device *dma = chan->device;

	/* specify valid address for disabled result */
	if (flags & DMA_PREP_PQ_DISABLE_P)
		pq[0] = pq[1];
	if (flags & DMA_PREP_PQ_DISABLE_Q)
		pq[1] = pq[0];

	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*pqres = 0;

	return (src_cnt > 8) && (dma->max_pq > 8) ?
		__ioat3_prep_pq16_lock(chan, pqres, pq, src, src_cnt, scf, len,
				       flags) :
		__ioat3_prep_pq_lock(chan, pqres, pq, src, src_cnt, scf, len,
				     flags);
}

static struct dma_async_tx_descriptor *
ioat3_prep_pqxor(struct dma_chan *chan, dma_addr_t dst, dma_addr_t *src,
		 unsigned int src_cnt, size_t len, unsigned long flags)
{
	struct dma_device *dma = chan->device;
	unsigned char scf[src_cnt];
	dma_addr_t pq[2];

	memset(scf, 0, src_cnt);
	pq[0] = dst;
	flags |= DMA_PREP_PQ_DISABLE_Q;
	pq[1] = dst; /* specify valid address for disabled result */

	return (src_cnt > 8) && (dma->max_pq > 8) ?
		__ioat3_prep_pq16_lock(chan, NULL, pq, src, src_cnt, scf, len,
				       flags) :
		__ioat3_prep_pq_lock(chan, NULL, pq, src, src_cnt, scf, len,
				     flags);
}

struct dma_async_tx_descriptor *
ioat3_prep_pqxor_val(struct dma_chan *chan, dma_addr_t *src,
		     unsigned int src_cnt, size_t len,
		     enum sum_check_flags *result, unsigned long flags)
{
	struct dma_device *dma = chan->device;
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


	return (src_cnt > 8) && (dma->max_pq > 8) ?
		__ioat3_prep_pq16_lock(chan, result, pq, &src[1], src_cnt - 1,
				       scf, len, flags) :
		__ioat3_prep_pq_lock(chan, result, pq, &src[1], src_cnt - 1,
				     scf, len, flags);
}

static struct dma_async_tx_descriptor *
ioat3_prep_interrupt_lock(struct dma_chan *c, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;

	if (ioat2_check_space_lock(ioat, 1) == 0)
		desc = ioat2_get_ring_ent(ioat, ioat->head);
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

	dump_desc_dbg(ioat, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static void ioat3_dma_test_callback(void *dma_async_param)
{
	struct completion *cmp = dma_async_param;

	complete(cmp);
}

#define IOAT_NUM_SRC_TEST 6 /* must be <= 8 */
static int ioat_xor_val_self_test(struct ioatdma_device *device)
{
	int i, src_idx;
	struct page *dest;
	struct page *xor_srcs[IOAT_NUM_SRC_TEST];
	struct page *xor_val_srcs[IOAT_NUM_SRC_TEST + 1];
	dma_addr_t dma_srcs[IOAT_NUM_SRC_TEST + 1];
	dma_addr_t dma_addr, dest_dma;
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
	u8 cmp_byte = 0;
	u32 cmp_word;
	u32 xor_val_result;
	int err = 0;
	struct completion cmp;
	unsigned long tmo;
	struct device *dev = &device->pdev->dev;
	struct dma_device *dma = &device->common;
	u8 op = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!dma_has_cap(DMA_XOR, dma->cap_mask))
		return 0;

	for (src_idx = 0; src_idx < IOAT_NUM_SRC_TEST; src_idx++) {
		xor_srcs[src_idx] = alloc_page(GFP_KERNEL);
		if (!xor_srcs[src_idx]) {
			while (src_idx--)
				__free_page(xor_srcs[src_idx]);
			return -ENOMEM;
		}
	}

	dest = alloc_page(GFP_KERNEL);
	if (!dest) {
		while (src_idx--)
			__free_page(xor_srcs[src_idx]);
		return -ENOMEM;
	}

	/* Fill in src buffers */
	for (src_idx = 0; src_idx < IOAT_NUM_SRC_TEST; src_idx++) {
		u8 *ptr = page_address(xor_srcs[src_idx]);
		for (i = 0; i < PAGE_SIZE; i++)
			ptr[i] = (1 << src_idx);
	}

	for (src_idx = 0; src_idx < IOAT_NUM_SRC_TEST; src_idx++)
		cmp_byte ^= (u8) (1 << src_idx);

	cmp_word = (cmp_byte << 24) | (cmp_byte << 16) |
			(cmp_byte << 8) | cmp_byte;

	memset(page_address(dest), 0, PAGE_SIZE);

	dma_chan = container_of(dma->channels.next, struct dma_chan,
				device_node);
	if (dma->device_alloc_chan_resources(dma_chan) < 1) {
		err = -ENODEV;
		goto out;
	}

	/* test xor */
	op = IOAT_OP_XOR;

	dest_dma = dma_map_page(dev, dest, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < IOAT_NUM_SRC_TEST; i++)
		dma_srcs[i] = dma_map_page(dev, xor_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
	tx = dma->device_prep_dma_xor(dma_chan, dest_dma, dma_srcs,
				      IOAT_NUM_SRC_TEST, PAGE_SIZE,
				      DMA_PREP_INTERRUPT |
				      DMA_COMPL_SKIP_SRC_UNMAP |
				      DMA_COMPL_SKIP_DEST_UNMAP);

	if (!tx) {
		dev_err(dev, "Self-test xor prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat3_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test xor setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (dma->device_tx_status(dma_chan, cookie, NULL) != DMA_SUCCESS) {
		dev_err(dev, "Self-test xor timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	dma_unmap_page(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < IOAT_NUM_SRC_TEST; i++)
		dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE, DMA_TO_DEVICE);

	dma_sync_single_for_cpu(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < (PAGE_SIZE / sizeof(u32)); i++) {
		u32 *ptr = page_address(dest);
		if (ptr[i] != cmp_word) {
			dev_err(dev, "Self-test xor failed compare\n");
			err = -ENODEV;
			goto free_resources;
		}
	}
	dma_sync_single_for_device(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);

	/* skip validate if the capability is not present */
	if (!dma_has_cap(DMA_XOR_VAL, dma_chan->device->cap_mask))
		goto free_resources;

	op = IOAT_OP_XOR_VAL;

	/* validate the sources with the destintation page */
	for (i = 0; i < IOAT_NUM_SRC_TEST; i++)
		xor_val_srcs[i] = xor_srcs[i];
	xor_val_srcs[i] = dest;

	xor_val_result = 1;

	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
		dma_srcs[i] = dma_map_page(dev, xor_val_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
	tx = dma->device_prep_dma_xor_val(dma_chan, dma_srcs,
					  IOAT_NUM_SRC_TEST + 1, PAGE_SIZE,
					  &xor_val_result, DMA_PREP_INTERRUPT |
					  DMA_COMPL_SKIP_SRC_UNMAP |
					  DMA_COMPL_SKIP_DEST_UNMAP);
	if (!tx) {
		dev_err(dev, "Self-test zero prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat3_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test zero setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (dma->device_tx_status(dma_chan, cookie, NULL) != DMA_SUCCESS) {
		dev_err(dev, "Self-test validate timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
		dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE, DMA_TO_DEVICE);

	if (xor_val_result != 0) {
		dev_err(dev, "Self-test validate failed compare\n");
		err = -ENODEV;
		goto free_resources;
	}

	/* skip memset if the capability is not present */
	if (!dma_has_cap(DMA_MEMSET, dma_chan->device->cap_mask))
		goto free_resources;

	/* test memset */
	op = IOAT_OP_FILL;

	dma_addr = dma_map_page(dev, dest, 0,
			PAGE_SIZE, DMA_FROM_DEVICE);
	tx = dma->device_prep_dma_memset(dma_chan, dma_addr, 0, PAGE_SIZE,
					 DMA_PREP_INTERRUPT |
					 DMA_COMPL_SKIP_SRC_UNMAP |
					 DMA_COMPL_SKIP_DEST_UNMAP);
	if (!tx) {
		dev_err(dev, "Self-test memset prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat3_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test memset setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (dma->device_tx_status(dma_chan, cookie, NULL) != DMA_SUCCESS) {
		dev_err(dev, "Self-test memset timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);

	for (i = 0; i < PAGE_SIZE/sizeof(u32); i++) {
		u32 *ptr = page_address(dest);
		if (ptr[i]) {
			dev_err(dev, "Self-test memset failed compare\n");
			err = -ENODEV;
			goto free_resources;
		}
	}

	/* test for non-zero parity sum */
	op = IOAT_OP_XOR_VAL;

	xor_val_result = 0;
	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
		dma_srcs[i] = dma_map_page(dev, xor_val_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
	tx = dma->device_prep_dma_xor_val(dma_chan, dma_srcs,
					  IOAT_NUM_SRC_TEST + 1, PAGE_SIZE,
					  &xor_val_result, DMA_PREP_INTERRUPT |
					  DMA_COMPL_SKIP_SRC_UNMAP |
					  DMA_COMPL_SKIP_DEST_UNMAP);
	if (!tx) {
		dev_err(dev, "Self-test 2nd zero prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat3_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test  2nd zero setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (dma->device_tx_status(dma_chan, cookie, NULL) != DMA_SUCCESS) {
		dev_err(dev, "Self-test 2nd validate timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	if (xor_val_result != SUM_CHECK_P_RESULT) {
		dev_err(dev, "Self-test validate failed compare\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
		dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE, DMA_TO_DEVICE);

	goto free_resources;
dma_unmap:
	if (op == IOAT_OP_XOR) {
		dma_unmap_page(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);
		for (i = 0; i < IOAT_NUM_SRC_TEST; i++)
			dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE,
				       DMA_TO_DEVICE);
	} else if (op == IOAT_OP_XOR_VAL) {
		for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
			dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE,
				       DMA_TO_DEVICE);
	} else if (op == IOAT_OP_FILL)
		dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);
free_resources:
	dma->device_free_chan_resources(dma_chan);
out:
	src_idx = IOAT_NUM_SRC_TEST;
	while (src_idx--)
		__free_page(xor_srcs[src_idx]);
	__free_page(dest);
	return err;
}

static int ioat3_dma_self_test(struct ioatdma_device *device)
{
	int rc = ioat_dma_self_test(device);

	if (rc)
		return rc;

	rc = ioat_xor_val_self_test(device);
	if (rc)
		return rc;

	return 0;
}

static int ioat3_irq_reinit(struct ioatdma_device *device)
{
	int msixcnt = device->common.chancnt;
	struct pci_dev *pdev = device->pdev;
	int i;
	struct msix_entry *msix;
	struct ioat_chan_common *chan;
	int err = 0;

	switch (device->irq_mode) {
	case IOAT_MSIX:

		for (i = 0; i < msixcnt; i++) {
			msix = &device->msix_entries[i];
			chan = ioat_chan_by_index(device, i);
			devm_free_irq(&pdev->dev, msix->vector, chan);
		}

		pci_disable_msix(pdev);
		break;

	case IOAT_MSIX_SINGLE:
		msix = &device->msix_entries[0];
		chan = ioat_chan_by_index(device, 0);
		devm_free_irq(&pdev->dev, msix->vector, chan);
		pci_disable_msix(pdev);
		break;

	case IOAT_MSI:
		chan = ioat_chan_by_index(device, 0);
		devm_free_irq(&pdev->dev, pdev->irq, chan);
		pci_disable_msi(pdev);
		break;

	case IOAT_INTX:
		chan = ioat_chan_by_index(device, 0);
		devm_free_irq(&pdev->dev, pdev->irq, chan);
		break;

	default:
		return 0;
	}

	device->irq_mode = IOAT_NOIRQ;

	err = ioat_dma_setup_interrupts(device);

	return err;
}

static int ioat3_reset_hw(struct ioat_chan_common *chan)
{
	/* throw away whatever the channel was doing and get it
	 * initialized, with ioat3 specific workarounds
	 */
	struct ioatdma_device *device = chan->device;
	struct pci_dev *pdev = device->pdev;
	u32 chanerr;
	u16 dev_id;
	int err;

	ioat2_quiesce(chan, msecs_to_jiffies(100));

	chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
	writel(chanerr, chan->reg_base + IOAT_CHANERR_OFFSET);

	if (device->version < IOAT_VER_3_3) {
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

	err = ioat2_reset_sync(chan, msecs_to_jiffies(200));
	if (err) {
		dev_err(&pdev->dev, "Failed to reset!\n");
		return err;
	}

	if (device->irq_mode != IOAT_NOIRQ && is_bwd_ioat(pdev))
		err = ioat3_irq_reinit(device);

	return err;
}

static void ioat3_intr_quirk(struct ioatdma_device *device)
{
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	u32 errmask;

	dma = &device->common;

	/*
	 * if we have descriptor write back error status, we mask the
	 * error interrupts
	 */
	if (device->cap & IOAT_CAP_DWBES) {
		list_for_each_entry(c, &dma->channels, device_node) {
			chan = to_chan_common(c);
			errmask = readl(chan->reg_base +
					IOAT_CHANERR_MASK_OFFSET);
			errmask |= IOAT_CHANERR_XOR_P_OR_CRC_ERR |
				   IOAT_CHANERR_XOR_Q_ERR;
			writel(errmask, chan->reg_base +
					IOAT_CHANERR_MASK_OFFSET);
		}
	}
}

int ioat3_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	int dca_en = system_has_dca_enabled(pdev);
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	bool is_raid_device = false;
	int err;

	device->enumerate_channels = ioat2_enumerate_channels;
	device->reset_hw = ioat3_reset_hw;
	device->self_test = ioat3_dma_self_test;
	device->intr_quirk = ioat3_intr_quirk;
	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat2_dma_prep_memcpy_lock;
	dma->device_issue_pending = ioat2_issue_pending;
	dma->device_alloc_chan_resources = ioat2_alloc_chan_resources;
	dma->device_free_chan_resources = ioat2_free_chan_resources;

	if (is_xeon_cb32(pdev))
		dma->copy_align = 6;

	dma_cap_set(DMA_INTERRUPT, dma->cap_mask);
	dma->device_prep_dma_interrupt = ioat3_prep_interrupt_lock;

	device->cap = readl(device->reg_base + IOAT_DMA_CAP_OFFSET);

	if (is_bwd_noraid(pdev))
		device->cap &= ~(IOAT_CAP_XOR | IOAT_CAP_PQ | IOAT_CAP_RAID16SS);

	/* dca is incompatible with raid operations */
	if (dca_en && (device->cap & (IOAT_CAP_XOR|IOAT_CAP_PQ)))
		device->cap &= ~(IOAT_CAP_XOR|IOAT_CAP_PQ);

	if (device->cap & IOAT_CAP_XOR) {
		is_raid_device = true;
		dma->max_xor = 8;
		dma->xor_align = 6;

		dma_cap_set(DMA_XOR, dma->cap_mask);
		dma->device_prep_dma_xor = ioat3_prep_xor;

		dma_cap_set(DMA_XOR_VAL, dma->cap_mask);
		dma->device_prep_dma_xor_val = ioat3_prep_xor_val;
	}

	if (device->cap & IOAT_CAP_PQ) {
		is_raid_device = true;

		dma->device_prep_dma_pq = ioat3_prep_pq;
		dma->device_prep_dma_pq_val = ioat3_prep_pq_val;
		dma_cap_set(DMA_PQ, dma->cap_mask);
		dma_cap_set(DMA_PQ_VAL, dma->cap_mask);

		if (device->cap & IOAT_CAP_RAID16SS) {
			dma_set_maxpq(dma, 16, 0);
			dma->pq_align = 0;
		} else {
			dma_set_maxpq(dma, 8, 0);
			if (is_xeon_cb32(pdev))
				dma->pq_align = 6;
			else
				dma->pq_align = 0;
		}

		if (!(device->cap & IOAT_CAP_XOR)) {
			dma->device_prep_dma_xor = ioat3_prep_pqxor;
			dma->device_prep_dma_xor_val = ioat3_prep_pqxor_val;
			dma_cap_set(DMA_XOR, dma->cap_mask);
			dma_cap_set(DMA_XOR_VAL, dma->cap_mask);

			if (device->cap & IOAT_CAP_RAID16SS) {
				dma->max_xor = 16;
				dma->xor_align = 0;
			} else {
				dma->max_xor = 8;
				if (is_xeon_cb32(pdev))
					dma->xor_align = 6;
				else
					dma->xor_align = 0;
			}
		}
	}

	if (is_raid_device && (device->cap & IOAT_CAP_FILL_BLOCK)) {
		dma_cap_set(DMA_MEMSET, dma->cap_mask);
		dma->device_prep_dma_memset = ioat3_prep_memset_lock;
	}


	dma->device_tx_status = ioat3_tx_status;
	device->cleanup_fn = ioat3_cleanup_event;
	device->timer_fn = ioat3_timer_event;

	if (is_xeon_cb32(pdev)) {
		dma_cap_clear(DMA_XOR_VAL, dma->cap_mask);
		dma->device_prep_dma_xor_val = NULL;

		dma_cap_clear(DMA_PQ_VAL, dma->cap_mask);
		dma->device_prep_dma_pq_val = NULL;
	}

	/* starting with CB3.3 super extended descriptors are supported */
	if (device->cap & IOAT_CAP_RAID16SS) {
		char pool_name[14];
		int i;

		/* allocate sw descriptor pool for SED */
		device->sed_pool = kmem_cache_create("ioat_sed",
				sizeof(struct ioat_sed_ent), 0, 0, NULL);
		if (!device->sed_pool)
			return -ENOMEM;

		for (i = 0; i < MAX_SED_POOLS; i++) {
			snprintf(pool_name, 14, "ioat_hw%d_sed", i);

			/* allocate SED DMA pool */
			device->sed_hw_pool[i] = dma_pool_create(pool_name,
					&pdev->dev,
					SED_SIZE * (i + 1), 64, 0);
			if (!device->sed_hw_pool[i])
				goto sed_pool_cleanup;

		}
	}

	err = ioat_probe(device);
	if (err)
		return err;
	ioat_set_tcp_copy_break(262144);

	list_for_each_entry(c, &dma->channels, device_node) {
		chan = to_chan_common(c);
		writel(IOAT_DMA_DCA_ANY_CPU,
		       chan->reg_base + IOAT_DCACTRL_OFFSET);
	}

	err = ioat_register(device);
	if (err)
		return err;

	ioat_kobject_add(device, &ioat2_ktype);

	if (dca)
		device->dca = ioat3_dca_init(pdev, device->reg_base);

	return 0;

sed_pool_cleanup:
	if (device->sed_pool) {
		int i;
		kmem_cache_destroy(device->sed_pool);

		for (i = 0; i < MAX_SED_POOLS; i++)
			if (device->sed_hw_pool[i])
				dma_pool_destroy(device->sed_hw_pool[i]);
	}

	return -ENOMEM;
}

void ioat3_dma_remove(struct ioatdma_device *device)
{
	if (device->sed_pool) {
		int i;
		kmem_cache_destroy(device->sed_pool);

		for (i = 0; i < MAX_SED_POOLS; i++)
			if (device->sed_hw_pool[i])
				dma_pool_destroy(device->sed_hw_pool[i]);
	}
}
