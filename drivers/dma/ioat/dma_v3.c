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

static void ioat3_eh(struct ioatdma_chan *ioat_chan);

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

static void
ioat3_free_sed(struct ioatdma_device *ioat_dma, struct ioat_sed_ent *sed)
{
	if (!sed)
		return;

	dma_pool_free(ioat_dma->sed_hw_pool[sed->hw_pool], sed->hw, sed->dma);
	kmem_cache_free(ioat_sed_cache, sed);
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
