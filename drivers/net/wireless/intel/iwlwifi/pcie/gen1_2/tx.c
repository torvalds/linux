// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2003-2014, 2018-2021, 2023-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/dmapool.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/tcp.h>
#include <net/ip6_checksum.h>
#include <net/tso.h>

#include "fw/api/commands.h"
#include "fw/api/datapath.h"
#include "fw/api/debug.h"
#include "iwl-fh.h"
#include "iwl-debug.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-io.h"
#include "iwl-scd.h"
#include "iwl-op-mode.h"
#include "internal.h"
#include "fw/api/tx.h"
#include "fw/dbg.h"
#include "pcie/utils.h"

/*************** DMA-QUEUE-GENERAL-FUNCTIONS  *****
 * DMA services
 *
 * Theory of operation
 *
 * A Tx or Rx queue resides in host DRAM, and is comprised of a circular buffer
 * of buffer descriptors, each of which points to one or more data buffers for
 * the device to read from or fill.  Driver and device exchange status of each
 * queue via "read" and "write" pointers.  Driver keeps minimum of 2 empty
 * entries in each circular buffer, to protect against confusing empty and full
 * queue states.
 *
 * The device reads or writes the data in the queues via the device's several
 * DMA/FIFO channels.  Each queue is mapped to a single DMA channel.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 ***************************************************/


int iwl_pcie_alloc_dma_ptr(struct iwl_trans *trans,
			   struct iwl_dma_ptr *ptr, size_t size)
{
	if (WARN_ON(ptr->addr))
		return -EINVAL;

	ptr->addr = dma_alloc_coherent(trans->dev, size,
				       &ptr->dma, GFP_KERNEL);
	if (!ptr->addr)
		return -ENOMEM;
	ptr->size = size;
	return 0;
}

void iwl_pcie_free_dma_ptr(struct iwl_trans *trans, struct iwl_dma_ptr *ptr)
{
	if (unlikely(!ptr->addr))
		return;

	dma_free_coherent(trans->dev, ptr->size, ptr->addr, ptr->dma);
	memset(ptr, 0, sizeof(*ptr));
}

/*
 * iwl_pcie_txq_inc_wr_ptr - Send new write index to hardware
 */
static void iwl_pcie_txq_inc_wr_ptr(struct iwl_trans *trans,
				    struct iwl_txq *txq)
{
	u32 reg = 0;
	int txq_id = txq->id;

	lockdep_assert_held(&txq->lock);

	/*
	 * explicitly wake up the NIC if:
	 * 1. shadow registers aren't enabled
	 * 2. NIC is woken up for CMD regardless of shadow outside this function
	 * 3. there is a chance that the NIC is asleep
	 */
	if (!trans->mac_cfg->base->shadow_reg_enable &&
	    txq_id != trans->conf.cmd_queue &&
	    test_bit(STATUS_TPOWER_PMI, &trans->status)) {
		/*
		 * wake up nic if it's powered down ...
		 * uCode will wake up, and interrupt us again, so next
		 * time we'll skip this part.
		 */
		reg = iwl_read32(trans, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			IWL_DEBUG_INFO(trans, "Tx queue %d requesting wakeup, GP1 = 0x%x\n",
				       txq_id, reg);
			iwl_set_bit(trans, CSR_GP_CNTRL,
				    CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			txq->need_update = true;
			return;
		}
	}

	/*
	 * if not in power-save mode, uCode will never sleep when we're
	 * trying to tx (during RFKILL, we're not trying to tx).
	 */
	IWL_DEBUG_TX(trans, "Q:%d WR: 0x%x\n", txq_id, txq->write_ptr);
	if (!txq->block)
		iwl_write32(trans, HBUS_TARG_WRPTR,
			    txq->write_ptr | (txq_id << 8));
}

void iwl_pcie_txq_check_wrptrs(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	for (i = 0; i < trans->mac_cfg->base->num_of_queues; i++) {
		struct iwl_txq *txq = trans_pcie->txqs.txq[i];

		if (!test_bit(i, trans_pcie->txqs.queue_used))
			continue;

		spin_lock_bh(&txq->lock);
		if (txq->need_update) {
			iwl_pcie_txq_inc_wr_ptr(trans, txq);
			txq->need_update = false;
		}
		spin_unlock_bh(&txq->lock);
	}
}

static inline void iwl_pcie_gen1_tfd_set_tb(struct iwl_tfd *tfd,
					    u8 idx, dma_addr_t addr, u16 len)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];
	u16 hi_n_len = len << 4;

	put_unaligned_le32(addr, &tb->lo);
	hi_n_len |= iwl_get_dma_hi_addr(addr);

	tb->hi_n_len = cpu_to_le16(hi_n_len);

	tfd->num_tbs = idx + 1;
}

static inline u8 iwl_txq_gen1_tfd_get_num_tbs(struct iwl_tfd *tfd)
{
	return tfd->num_tbs & 0x1f;
}

static int iwl_pcie_txq_build_tfd(struct iwl_trans *trans, struct iwl_txq *txq,
				  dma_addr_t addr, u16 len, bool reset)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	void *tfd;
	u32 num_tbs;

	tfd = (u8 *)txq->tfds + trans_pcie->txqs.tfd.size * txq->write_ptr;

	if (reset)
		memset(tfd, 0, trans_pcie->txqs.tfd.size);

	num_tbs = iwl_txq_gen1_tfd_get_num_tbs(tfd);

	/* Each TFD can point to a maximum max_tbs Tx buffers */
	if (num_tbs >= trans_pcie->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Error can not send more than %d chunks\n",
			trans_pcie->txqs.tfd.max_tbs);
		return -EINVAL;
	}

	if (WARN(addr & ~IWL_TX_DMA_MASK,
		 "Unaligned address = %llx\n", (unsigned long long)addr))
		return -EINVAL;

	iwl_pcie_gen1_tfd_set_tb(tfd, num_tbs, addr, len);

	return num_tbs;
}

static void iwl_pcie_clear_cmd_in_flight(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans->mac_cfg->base->apmg_wake_up_wa)
		return;

	spin_lock(&trans_pcie->reg_lock);

	if (WARN_ON(!trans_pcie->cmd_hold_nic_awake)) {
		spin_unlock(&trans_pcie->reg_lock);
		return;
	}

	trans_pcie->cmd_hold_nic_awake = false;
	iwl_trans_clear_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	spin_unlock(&trans_pcie->reg_lock);
}

static void iwl_pcie_free_and_unmap_tso_page(struct iwl_trans *trans,
					     struct page *page)
{
	struct iwl_tso_page_info *info = IWL_TSO_PAGE_INFO(page_address(page));

	/* Decrease internal use count and unmap/free page if needed */
	if (refcount_dec_and_test(&info->use_count)) {
		dma_unmap_page(trans->dev, info->dma_addr, PAGE_SIZE,
			       DMA_TO_DEVICE);

		__free_page(page);
	}
}

void iwl_pcie_free_tso_pages(struct iwl_trans *trans, struct sk_buff *skb,
			     struct iwl_cmd_meta *cmd_meta)
{
	struct page **page_ptr;
	struct page *next;

	page_ptr = (void *)((u8 *)skb->cb + trans->conf.cb_data_offs);
	next = *page_ptr;
	*page_ptr = NULL;

	while (next) {
		struct iwl_tso_page_info *info;
		struct page *tmp = next;

		info = IWL_TSO_PAGE_INFO(page_address(next));
		next = info->next;

		/* Unmap the scatter gather list that is on the last page */
		if (!next && cmd_meta->sg_offset) {
			struct sg_table *sgt;

			sgt = (void *)((u8 *)page_address(tmp) +
				       cmd_meta->sg_offset);

			dma_unmap_sgtable(trans->dev, sgt, DMA_TO_DEVICE, 0);
		}

		iwl_pcie_free_and_unmap_tso_page(trans, tmp);
	}
}

static inline dma_addr_t
iwl_txq_gen1_tfd_tb_get_addr(struct iwl_tfd *tfd, u8 idx)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];
	dma_addr_t addr;
	dma_addr_t hi_len;

	addr = get_unaligned_le32(&tb->lo);

	if (sizeof(dma_addr_t) <= sizeof(u32))
		return addr;

	hi_len = le16_to_cpu(tb->hi_n_len) & 0xF;

	/*
	 * shift by 16 twice to avoid warnings on 32-bit
	 * (where this code never runs anyway due to the
	 * if statement above)
	 */
	return addr | ((hi_len << 16) << 16);
}

static void iwl_txq_set_tfd_invalid_gen1(struct iwl_trans *trans,
					 struct iwl_tfd *tfd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	tfd->num_tbs = 0;

	iwl_pcie_gen1_tfd_set_tb(tfd, 0, trans_pcie->invalid_tx_cmd.dma,
				 trans_pcie->invalid_tx_cmd.size);
}

static void iwl_txq_gen1_tfd_unmap(struct iwl_trans *trans,
				   struct iwl_cmd_meta *meta,
				   struct iwl_txq *txq, int index)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i, num_tbs;
	struct iwl_tfd *tfd = iwl_txq_get_tfd(trans, txq, index);

	/* Sanity check on number of chunks */
	num_tbs = iwl_txq_gen1_tfd_get_num_tbs(tfd);

	if (num_tbs > trans_pcie->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Too many chunks: %i\n", num_tbs);
		/* @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* TB1 is mapped directly, the rest is the TSO page and SG list. */
	if (meta->sg_offset)
		num_tbs = 2;

	/* first TB is never freed - it's the bidirectional DMA data */

	for (i = 1; i < num_tbs; i++) {
		if (meta->tbs & BIT(i))
			dma_unmap_page(trans->dev,
				       iwl_txq_gen1_tfd_tb_get_addr(tfd, i),
				       iwl_txq_gen1_tfd_tb_get_len(trans,
								   tfd, i),
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(trans->dev,
					 iwl_txq_gen1_tfd_tb_get_addr(tfd, i),
					 iwl_txq_gen1_tfd_tb_get_len(trans,
								     tfd, i),
					 DMA_TO_DEVICE);
	}

	meta->tbs = 0;

	iwl_txq_set_tfd_invalid_gen1(trans, tfd);
}

/**
 * iwl_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 * @trans: transport private data
 * @txq: tx queue
 * @read_ptr: the TXQ read_ptr to free
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
static void iwl_txq_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq,
			     int read_ptr)
{
	/* rd_ptr is bounded by TFD_QUEUE_SIZE_MAX and
	 * idx is bounded by n_window
	 */
	int idx = iwl_txq_get_cmd_index(txq, read_ptr);
	struct sk_buff *skb;

	lockdep_assert_held(&txq->reclaim_lock);

	if (!txq->entries)
		return;

	/* We have only q->n_window txq->entries, but we use
	 * TFD_QUEUE_SIZE_MAX tfds
	 */
	if (trans->mac_cfg->gen2)
		iwl_txq_gen2_tfd_unmap(trans, &txq->entries[idx].meta,
				       iwl_txq_get_tfd(trans, txq, read_ptr));
	else
		iwl_txq_gen1_tfd_unmap(trans, &txq->entries[idx].meta,
				       txq, read_ptr);

	/* free SKB */
	skb = txq->entries[idx].skb;

	/* Can be called from irqs-disabled context
	 * If skb is not NULL, it means that the whole queue is being
	 * freed and that the queue is not empty - free the skb
	 */
	if (skb) {
		iwl_op_mode_free_skb(trans->op_mode, skb);
		txq->entries[idx].skb = NULL;
	}
}

/*
 * iwl_pcie_txq_unmap -  Unmap any remaining DMA mappings and free skb's
 */
static void iwl_pcie_txq_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];

	if (!txq) {
		IWL_ERR(trans, "Trying to free a queue that wasn't allocated?\n");
		return;
	}

	spin_lock_bh(&txq->reclaim_lock);
	spin_lock(&txq->lock);
	while (txq->write_ptr != txq->read_ptr) {
		IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n",
				   txq_id, txq->read_ptr);

		if (txq_id != trans->conf.cmd_queue) {
			struct sk_buff *skb = txq->entries[txq->read_ptr].skb;
			struct iwl_cmd_meta *cmd_meta =
				&txq->entries[txq->read_ptr].meta;

			if (WARN_ON_ONCE(!skb))
				continue;

			iwl_pcie_free_tso_pages(trans, skb, cmd_meta);
		}
		iwl_txq_free_tfd(trans, txq, txq->read_ptr);
		txq->read_ptr = iwl_txq_inc_wrap(trans, txq->read_ptr);

		if (txq->read_ptr == txq->write_ptr &&
		    txq_id == trans->conf.cmd_queue)
			iwl_pcie_clear_cmd_in_flight(trans);
	}

	while (!skb_queue_empty(&txq->overflow_q)) {
		struct sk_buff *skb = __skb_dequeue(&txq->overflow_q);

		iwl_op_mode_free_skb(trans->op_mode, skb);
	}

	spin_unlock(&txq->lock);
	spin_unlock_bh(&txq->reclaim_lock);

	/* just in case - this queue may have been stopped */
	iwl_trans_pcie_wake_queue(trans, txq);
}

/*
 * iwl_pcie_txq_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
static void iwl_pcie_txq_free(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];
	struct device *dev = trans->dev;
	int i;

	if (WARN_ON(!txq))
		return;

	iwl_pcie_txq_unmap(trans, txq_id);

	/* De-alloc array of command/tx buffers */
	if (txq_id == trans->conf.cmd_queue)
		for (i = 0; i < txq->n_window; i++) {
			kfree_sensitive(txq->entries[i].cmd);
			kfree_sensitive(txq->entries[i].free_buf);
		}

	/* De-alloc circular buffer of TFDs */
	if (txq->tfds) {
		dma_free_coherent(dev,
				  trans_pcie->txqs.tfd.size *
				  trans->mac_cfg->base->max_tfd_queue_size,
				  txq->tfds, txq->dma_addr);
		txq->dma_addr = 0;
		txq->tfds = NULL;

		dma_free_coherent(dev,
				  sizeof(*txq->first_tb_bufs) * txq->n_window,
				  txq->first_tb_bufs, txq->first_tb_dma);
	}

	kfree(txq->entries);
	txq->entries = NULL;

	timer_delete_sync(&txq->stuck_timer);

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}

void iwl_pcie_tx_start(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int nq = trans->mac_cfg->base->num_of_queues;
	int chan;
	u32 reg_val;
	int clear_dwords = (SCD_TRANS_TBL_OFFSET_QUEUE(nq) -
				SCD_CONTEXT_MEM_LOWER_BOUND) / sizeof(u32);

	/* make sure all queue are not stopped/used */
	memset(trans_pcie->txqs.queue_stopped, 0,
	       sizeof(trans_pcie->txqs.queue_stopped));
	memset(trans_pcie->txqs.queue_used, 0,
	       sizeof(trans_pcie->txqs.queue_used));

	trans_pcie->scd_base_addr =
		iwl_read_prph(trans, SCD_SRAM_BASE_ADDR);

	/* reset context data, TX status and translation data */
	iwl_trans_write_mem(trans, trans_pcie->scd_base_addr +
				   SCD_CONTEXT_MEM_LOWER_BOUND,
			    NULL, clear_dwords);

	iwl_write_prph(trans, SCD_DRAM_BASE_ADDR,
		       trans_pcie->txqs.scd_bc_tbls.dma >> 10);

	/* The chain extension of the SCD doesn't work well. This feature is
	 * enabled by default by the HW, so we need to disable it manually.
	 */
	if (trans->mac_cfg->base->scd_chain_ext_wa)
		iwl_write_prph(trans, SCD_CHAINEXT_EN, 0);

	iwl_trans_ac_txq_enable(trans, trans->conf.cmd_queue,
				trans->conf.cmd_fifo,
				IWL_DEF_WD_TIMEOUT);

	/* Activate all Tx DMA/FIFO channels */
	iwl_scd_activate_fifos(trans);

	/* Enable DMA channel */
	for (chan = 0; chan < FH_TCSR_CHNL_NUM; chan++)
		iwl_write_direct32(trans, FH_TCSR_CHNL_TX_CONFIG_REG(chan),
				   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
				   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = iwl_read_direct32(trans, FH_TX_CHICKEN_BITS_REG);
	iwl_write_direct32(trans, FH_TX_CHICKEN_BITS_REG,
			   reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	/* Enable L1-Active */
	if (trans->mac_cfg->device_family < IWL_DEVICE_FAMILY_8000)
		iwl_clear_bits_prph(trans, APMG_PCIDEV_STT_REG,
				    APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
}

void iwl_trans_pcie_tx_reset(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int txq_id;

	/*
	 * we should never get here in gen2 trans mode return early to avoid
	 * having invalid accesses
	 */
	if (WARN_ON_ONCE(trans->mac_cfg->gen2))
		return;

	for (txq_id = 0; txq_id < trans->mac_cfg->base->num_of_queues;
	     txq_id++) {
		struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];
		if (trans->mac_cfg->gen2)
			iwl_write_direct64(trans,
					   FH_MEM_CBBC_QUEUE(trans, txq_id),
					   txq->dma_addr);
		else
			iwl_write_direct32(trans,
					   FH_MEM_CBBC_QUEUE(trans, txq_id),
					   txq->dma_addr >> 8);
		iwl_pcie_txq_unmap(trans, txq_id);
		txq->read_ptr = 0;
		txq->write_ptr = 0;
	}

	/* Tell NIC where to find the "keep warm" buffer */
	iwl_write_direct32(trans, FH_KW_MEM_ADDR_REG,
			   trans_pcie->kw.dma >> 4);

	/*
	 * Send 0 as the scd_base_addr since the device may have be reset
	 * while we were in WoWLAN in which case SCD_SRAM_BASE_ADDR will
	 * contain garbage.
	 */
	iwl_pcie_tx_start(trans);
}

static void iwl_pcie_tx_stop_fh(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ch, ret;
	u32 mask = 0;

	spin_lock_bh(&trans_pcie->irq_lock);

	if (!iwl_trans_grab_nic_access(trans))
		goto out;

	/* Stop each Tx DMA channel */
	for (ch = 0; ch < FH_TCSR_CHNL_NUM; ch++) {
		iwl_write32(trans, FH_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
		mask |= FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch);
	}

	/* Wait for DMA channels to be idle */
	ret = iwl_poll_bits(trans, FH_TSSR_TX_STATUS_REG, mask, 5000);
	if (ret)
		IWL_ERR(trans,
			"Failing on timeout while stopping DMA channel %d [0x%08x]\n",
			ch, iwl_read32(trans, FH_TSSR_TX_STATUS_REG));

	iwl_trans_release_nic_access(trans);

out:
	spin_unlock_bh(&trans_pcie->irq_lock);
}

/*
 * iwl_pcie_tx_stop - Stop all Tx DMA channels
 */
int iwl_pcie_tx_stop(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int txq_id;

	/* Turn off all Tx DMA fifos */
	iwl_scd_deactivate_fifos(trans);

	/* Turn off all Tx DMA channels */
	iwl_pcie_tx_stop_fh(trans);

	/*
	 * This function can be called before the op_mode disabled the
	 * queues. This happens when we have an rfkill interrupt.
	 * Since we stop Tx altogether - mark the queues as stopped.
	 */
	memset(trans_pcie->txqs.queue_stopped, 0,
	       sizeof(trans_pcie->txqs.queue_stopped));
	memset(trans_pcie->txqs.queue_used, 0,
	       sizeof(trans_pcie->txqs.queue_used));

	/* This can happen: start_hw, stop_device */
	if (!trans_pcie->txq_memory)
		return 0;

	/* Unmap DMA from host system and free skb's */
	for (txq_id = 0; txq_id < trans->mac_cfg->base->num_of_queues;
	     txq_id++)
		iwl_pcie_txq_unmap(trans, txq_id);

	return 0;
}

/*
 * iwl_trans_tx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void iwl_pcie_tx_free(struct iwl_trans *trans)
{
	int txq_id;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	memset(trans_pcie->txqs.queue_used, 0,
	       sizeof(trans_pcie->txqs.queue_used));

	/* Tx queues */
	if (trans_pcie->txq_memory) {
		for (txq_id = 0;
		     txq_id < trans->mac_cfg->base->num_of_queues;
		     txq_id++) {
			iwl_pcie_txq_free(trans, txq_id);
			trans_pcie->txqs.txq[txq_id] = NULL;
		}
	}

	kfree(trans_pcie->txq_memory);
	trans_pcie->txq_memory = NULL;

	iwl_pcie_free_dma_ptr(trans, &trans_pcie->kw);

	iwl_pcie_free_dma_ptr(trans, &trans_pcie->txqs.scd_bc_tbls);
}

void iwl_txq_log_scd_error(struct iwl_trans *trans, struct iwl_txq *txq)
{
	u32 txq_id = txq->id;
	u32 status;
	bool active;
	u8 fifo;

	if (trans->mac_cfg->gen2) {
		IWL_ERR(trans, "Queue %d is stuck %d %d\n", txq_id,
			txq->read_ptr, txq->write_ptr);
		/* TODO: access new SCD registers and dump them */
		return;
	}

	status = iwl_read_prph(trans, SCD_QUEUE_STATUS_BITS(txq_id));
	fifo = (status >> SCD_QUEUE_STTS_REG_POS_TXF) & 0x7;
	active = !!(status & BIT(SCD_QUEUE_STTS_REG_POS_ACTIVE));

	IWL_ERR(trans,
		"Queue %d is %sactive on fifo %d and stuck for %u ms. SW [%d, %d] HW [%d, %d] FH TRB=0x0%x\n",
		txq_id, active ? "" : "in", fifo,
		jiffies_to_msecs(txq->wd_timeout),
		txq->read_ptr, txq->write_ptr,
		iwl_read_prph(trans, SCD_QUEUE_RDPTR(txq_id)) &
			(trans->mac_cfg->base->max_tfd_queue_size - 1),
			iwl_read_prph(trans, SCD_QUEUE_WRPTR(txq_id)) &
			(trans->mac_cfg->base->max_tfd_queue_size - 1),
			iwl_read_direct32(trans, FH_TX_TRB_REG(fifo)));
}

static void iwl_txq_stuck_timer(struct timer_list *t)
{
	struct iwl_txq *txq = timer_container_of(txq, t, stuck_timer);
	struct iwl_trans *trans = txq->trans;

	spin_lock(&txq->lock);
	/* check if triggered erroneously */
	if (txq->read_ptr == txq->write_ptr) {
		spin_unlock(&txq->lock);
		return;
	}
	spin_unlock(&txq->lock);

	iwl_txq_log_scd_error(trans, txq);

	iwl_force_nmi(trans);
}

int iwl_pcie_txq_alloc(struct iwl_trans *trans, struct iwl_txq *txq,
		       int slots_num, bool cmd_queue)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	size_t num_entries = trans->mac_cfg->gen2 ?
		slots_num : trans->mac_cfg->base->max_tfd_queue_size;
	size_t tfd_sz;
	size_t tb0_buf_sz;
	int i;

	if (WARN_ONCE(slots_num <= 0, "Invalid slots num:%d\n", slots_num))
		return -EINVAL;

	if (WARN_ON(txq->entries || txq->tfds))
		return -EINVAL;

	tfd_sz = trans_pcie->txqs.tfd.size * num_entries;

	timer_setup(&txq->stuck_timer, iwl_txq_stuck_timer, 0);
	txq->trans = trans;

	txq->n_window = slots_num;

	txq->entries = kcalloc(slots_num,
			       sizeof(struct iwl_pcie_txq_entry),
			       GFP_KERNEL);

	if (!txq->entries)
		goto error;

	if (cmd_queue)
		for (i = 0; i < slots_num; i++) {
			txq->entries[i].cmd =
				kmalloc(sizeof(struct iwl_device_cmd),
					GFP_KERNEL);
			if (!txq->entries[i].cmd)
				goto error;
		}

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device
	 */
	txq->tfds = dma_alloc_coherent(trans->dev, tfd_sz,
				       &txq->dma_addr, GFP_KERNEL);
	if (!txq->tfds)
		goto error;

	BUILD_BUG_ON(sizeof(*txq->first_tb_bufs) != IWL_FIRST_TB_SIZE_ALIGN);

	tb0_buf_sz = sizeof(*txq->first_tb_bufs) * slots_num;

	txq->first_tb_bufs = dma_alloc_coherent(trans->dev, tb0_buf_sz,
						&txq->first_tb_dma,
						GFP_KERNEL);
	if (!txq->first_tb_bufs)
		goto err_free_tfds;

	for (i = 0; i < num_entries; i++) {
		void *tfd = iwl_txq_get_tfd(trans, txq, i);

		if (trans->mac_cfg->gen2)
			iwl_txq_set_tfd_invalid_gen2(trans, tfd);
		else
			iwl_txq_set_tfd_invalid_gen1(trans, tfd);
	}

	return 0;
err_free_tfds:
	dma_free_coherent(trans->dev, tfd_sz, txq->tfds, txq->dma_addr);
	txq->tfds = NULL;
error:
	if (txq->entries && cmd_queue)
		for (i = 0; i < slots_num; i++)
			kfree(txq->entries[i].cmd);
	kfree(txq->entries);
	txq->entries = NULL;

	return -ENOMEM;
}

#define BC_TABLE_SIZE	(sizeof(struct iwl_bc_tbl_entry) * TFD_QUEUE_BC_SIZE)

/*
 * iwl_pcie_tx_alloc - allocate TX context
 * Allocate all Tx DMA structures and initialize them
 */
static int iwl_pcie_tx_alloc(struct iwl_trans *trans)
{
	int ret;
	int txq_id, slots_num;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u16 bc_tbls_size = trans->mac_cfg->base->num_of_queues;

	if (WARN_ON(trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210))
		return -EINVAL;

	bc_tbls_size *= BC_TABLE_SIZE;

	/*It is not allowed to alloc twice, so warn when this happens.
	 * We cannot rely on the previous allocation, so free and fail */
	if (WARN_ON(trans_pcie->txq_memory)) {
		ret = -EINVAL;
		goto error;
	}

	ret = iwl_pcie_alloc_dma_ptr(trans, &trans_pcie->txqs.scd_bc_tbls,
				     bc_tbls_size);
	if (ret) {
		IWL_ERR(trans, "Scheduler BC Table allocation failed\n");
		goto error;
	}

	/* Alloc keep-warm buffer */
	ret = iwl_pcie_alloc_dma_ptr(trans, &trans_pcie->kw, IWL_KW_SIZE);
	if (ret) {
		IWL_ERR(trans, "Keep Warm allocation failed\n");
		goto error;
	}

	trans_pcie->txq_memory =
		kcalloc(trans->mac_cfg->base->num_of_queues,
			sizeof(struct iwl_txq), GFP_KERNEL);
	if (!trans_pcie->txq_memory) {
		IWL_ERR(trans, "Not enough memory for txq\n");
		ret = -ENOMEM;
		goto error;
	}

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < trans->mac_cfg->base->num_of_queues;
	     txq_id++) {
		bool cmd_queue = (txq_id == trans->conf.cmd_queue);

		if (cmd_queue)
			slots_num = max_t(u32, IWL_CMD_QUEUE_SIZE,
					  trans->mac_cfg->base->min_txq_size);
		else
			slots_num = max_t(u32, IWL_DEFAULT_QUEUE_SIZE,
					  trans->mac_cfg->base->min_ba_txq_size);
		trans_pcie->txqs.txq[txq_id] = &trans_pcie->txq_memory[txq_id];
		ret = iwl_pcie_txq_alloc(trans, trans_pcie->txqs.txq[txq_id],
					 slots_num, cmd_queue);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue alloc failed\n", txq_id);
			goto error;
		}
		trans_pcie->txqs.txq[txq_id]->id = txq_id;
	}

	return 0;

error:
	iwl_pcie_tx_free(trans);

	return ret;
}

/*
 * iwl_queue_init - Initialize queue's high/low-water and read/write indexes
 */
static int iwl_queue_init(struct iwl_txq *q, int slots_num)
{
	q->n_window = slots_num;

	/* slots_num must be power-of-two size, otherwise
	 * iwl_txq_get_cmd_index is broken.
	 */
	if (WARN_ON(!is_power_of_2(slots_num)))
		return -EINVAL;

	q->low_mark = q->n_window / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_window / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->write_ptr = 0;
	q->read_ptr = 0;

	return 0;
}

int iwl_txq_init(struct iwl_trans *trans, struct iwl_txq *txq,
		 int slots_num, bool cmd_queue)
{
	u32 tfd_queue_max_size =
		trans->mac_cfg->base->max_tfd_queue_size;
	int ret;

	txq->need_update = false;

	/* max_tfd_queue_size must be power-of-two size, otherwise
	 * iwl_txq_inc_wrap and iwl_txq_dec_wrap are broken.
	 */
	if (WARN_ONCE(tfd_queue_max_size & (tfd_queue_max_size - 1),
		      "Max tfd queue size must be a power of two, but is %d",
		      tfd_queue_max_size))
		return -EINVAL;

	/* Initialize queue's high/low-water marks, and head/tail indexes */
	ret = iwl_queue_init(txq, slots_num);
	if (ret)
		return ret;

	spin_lock_init(&txq->lock);
	spin_lock_init(&txq->reclaim_lock);

	if (cmd_queue) {
		static struct lock_class_key iwl_txq_cmd_queue_lock_class;

		lockdep_set_class(&txq->lock, &iwl_txq_cmd_queue_lock_class);
	}

	__skb_queue_head_init(&txq->overflow_q);

	return 0;
}

int iwl_pcie_tx_init(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;
	int txq_id, slots_num;
	bool alloc = false;

	if (!trans_pcie->txq_memory) {
		ret = iwl_pcie_tx_alloc(trans);
		if (ret)
			goto error;
		alloc = true;
	}

	spin_lock_bh(&trans_pcie->irq_lock);

	/* Turn off all Tx DMA fifos */
	iwl_scd_deactivate_fifos(trans);

	/* Tell NIC where to find the "keep warm" buffer */
	iwl_write_direct32(trans, FH_KW_MEM_ADDR_REG,
			   trans_pcie->kw.dma >> 4);

	spin_unlock_bh(&trans_pcie->irq_lock);

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < trans->mac_cfg->base->num_of_queues;
	     txq_id++) {
		bool cmd_queue = (txq_id == trans->conf.cmd_queue);

		if (cmd_queue)
			slots_num = max_t(u32, IWL_CMD_QUEUE_SIZE,
					  trans->mac_cfg->base->min_txq_size);
		else
			slots_num = max_t(u32, IWL_DEFAULT_QUEUE_SIZE,
					  trans->mac_cfg->base->min_ba_txq_size);
		ret = iwl_txq_init(trans, trans_pcie->txqs.txq[txq_id], slots_num,
				   cmd_queue);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue init failed\n", txq_id);
			goto error;
		}

		/*
		 * Tell nic where to find circular buffer of TFDs for a
		 * given Tx queue, and enable the DMA channel used for that
		 * queue.
		 * Circular buffer (TFD queue in DRAM) physical base address
		 */
		iwl_write_direct32(trans, FH_MEM_CBBC_QUEUE(trans, txq_id),
				   trans_pcie->txqs.txq[txq_id]->dma_addr >> 8);
	}

	iwl_set_bits_prph(trans, SCD_GP_CTRL, SCD_GP_CTRL_AUTO_ACTIVE_MODE);
	if (trans->mac_cfg->base->num_of_queues > 20)
		iwl_set_bits_prph(trans, SCD_GP_CTRL,
				  SCD_GP_CTRL_ENABLE_31_QUEUES);

	return 0;
error:
	/*Upon error, free only if we allocated something */
	if (alloc)
		iwl_pcie_tx_free(trans);
	return ret;
}

static int iwl_pcie_set_cmd_in_flight(struct iwl_trans *trans,
				      const struct iwl_host_cmd *cmd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	/* Make sure the NIC is still alive in the bus */
	if (test_bit(STATUS_TRANS_DEAD, &trans->status))
		return -ENODEV;

	if (!trans->mac_cfg->base->apmg_wake_up_wa)
		return 0;

	/*
	 * wake up the NIC to make sure that the firmware will see the host
	 * command - we will let the NIC sleep once all the host commands
	 * returned. This needs to be done only on NICs that have
	 * apmg_wake_up_wa set (see above.)
	 */
	if (!_iwl_trans_pcie_grab_nic_access(trans, false))
		return -EIO;

	/*
	 * In iwl_trans_grab_nic_access(), we've acquired the reg_lock.
	 * There, we also returned immediately if cmd_hold_nic_awake is
	 * already true, so it's OK to unconditionally set it to true.
	 */
	trans_pcie->cmd_hold_nic_awake = true;
	spin_unlock(&trans_pcie->reg_lock);

	return 0;
}

static void iwl_txq_progress(struct iwl_txq *txq)
{
	lockdep_assert_held(&txq->lock);

	if (!txq->wd_timeout)
		return;

	/*
	 * station is asleep and we send data - that must
	 * be uAPSD or PS-Poll. Don't rearm the timer.
	 */
	if (txq->frozen)
		return;

	/*
	 * if empty delete timer, otherwise move timer forward
	 * since we're making progress on this queue
	 */
	if (txq->read_ptr == txq->write_ptr)
		timer_delete(&txq->stuck_timer);
	else
		mod_timer(&txq->stuck_timer, jiffies + txq->wd_timeout);
}

static inline bool iwl_txq_used(const struct iwl_txq *q, int i,
				int read_ptr, int write_ptr)
{
	int index = iwl_txq_get_cmd_index(q, i);
	int r = iwl_txq_get_cmd_index(q, read_ptr);
	int w = iwl_txq_get_cmd_index(q, write_ptr);

	return w >= r ?
		(index >= r && index < w) :
		!(index < r && index >= w);
}

/*
 * iwl_pcie_cmdq_reclaim - Reclaim TX command queue entries already Tx'd
 *
 * When FW advances 'R' index, all entries between old and new 'R' index
 * need to be reclaimed. As result, some free space forms.  If there is
 * enough free space (> low mark), wake the stack that feeds us.
 */
static void iwl_pcie_cmdq_reclaim(struct iwl_trans *trans, int txq_id, int idx)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];
	int nfreed = 0;
	u16 r;

	lockdep_assert_held(&txq->lock);

	idx = iwl_txq_get_cmd_index(txq, idx);
	r = iwl_txq_get_cmd_index(txq, txq->read_ptr);

	if (idx >= trans->mac_cfg->base->max_tfd_queue_size ||
	    (!iwl_txq_used(txq, idx, txq->read_ptr, txq->write_ptr))) {
		WARN_ONCE(test_bit(txq_id, trans_pcie->txqs.queue_used),
			  "%s: Read index for DMA queue txq id (%d), index %d is out of range [0-%d] %d %d.\n",
			  __func__, txq_id, idx,
			  trans->mac_cfg->base->max_tfd_queue_size,
			  txq->write_ptr, txq->read_ptr);
		return;
	}

	for (idx = iwl_txq_inc_wrap(trans, idx); r != idx;
	     r = iwl_txq_inc_wrap(trans, r)) {
		txq->read_ptr = iwl_txq_inc_wrap(trans, txq->read_ptr);

		if (nfreed++ > 0) {
			IWL_ERR(trans, "HCMD skipped: index (%d) %d %d\n",
				idx, txq->write_ptr, r);
			iwl_force_nmi(trans);
		}
	}

	if (txq->read_ptr == txq->write_ptr)
		iwl_pcie_clear_cmd_in_flight(trans);

	iwl_txq_progress(txq);
}

static int iwl_pcie_txq_set_ratid_map(struct iwl_trans *trans, u16 ra_tid,
				 u16 txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr = trans_pcie->scd_base_addr +
			SCD_TRANS_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = iwl_trans_read_mem32(trans, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	iwl_trans_write_mem32(trans, tbl_dw_addr, tbl_dw);

	return 0;
}

/* Receiver address (actually, Rx station's index into station table),
 * combined with Traffic ID (QOS priority), in format used by Tx Scheduler */
#define BUILD_RAxTID(sta_id, tid)	(((sta_id) << 4) + (tid))

bool iwl_trans_pcie_txq_enable(struct iwl_trans *trans, int txq_id, u16 ssn,
			       const struct iwl_trans_txq_scd_cfg *cfg,
			       unsigned int wdg_timeout)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];
	int fifo = -1;
	bool scd_bug = false;

	if (test_and_set_bit(txq_id, trans_pcie->txqs.queue_used))
		WARN_ONCE(1, "queue %d already used - expect issues", txq_id);

	txq->wd_timeout = msecs_to_jiffies(wdg_timeout);

	if (cfg) {
		fifo = cfg->fifo;

		/* Disable the scheduler prior configuring the cmd queue */
		if (txq_id == trans->conf.cmd_queue &&
		    trans->conf.scd_set_active)
			iwl_scd_enable_set_active(trans, 0);

		/* Stop this Tx queue before configuring it */
		iwl_scd_txq_set_inactive(trans, txq_id);

		/* Set this queue as a chain-building queue unless it is CMD */
		if (txq_id != trans->conf.cmd_queue)
			iwl_scd_txq_set_chain(trans, txq_id);

		if (cfg->aggregate) {
			u16 ra_tid = BUILD_RAxTID(cfg->sta_id, cfg->tid);

			/* Map receiver-address / traffic-ID to this queue */
			iwl_pcie_txq_set_ratid_map(trans, ra_tid, txq_id);

			/* enable aggregations for the queue */
			iwl_scd_txq_enable_agg(trans, txq_id);
			txq->ampdu = true;
		} else {
			/*
			 * disable aggregations for the queue, this will also
			 * make the ra_tid mapping configuration irrelevant
			 * since it is now a non-AGG queue.
			 */
			iwl_scd_txq_disable_agg(trans, txq_id);

			ssn = txq->read_ptr;
		}
	} else {
		/*
		 * If we need to move the SCD write pointer by steps of
		 * 0x40, 0x80 or 0xc0, it gets stuck. Avoids this and let
		 * the op_mode know by returning true later.
		 * Do this only in case cfg is NULL since this trick can
		 * be done only if we have DQA enabled which is true for mvm
		 * only. And mvm never sets a cfg pointer.
		 * This is really ugly, but this is the easiest way out for
		 * this sad hardware issue.
		 * This bug has been fixed on devices 9000 and up.
		 */
		scd_bug = !trans->mac_cfg->mq_rx_supported &&
			!((ssn - txq->write_ptr) & 0x3f) &&
			(ssn != txq->write_ptr);
		if (scd_bug)
			ssn++;
	}

	/* Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	txq->read_ptr = (ssn & 0xff);
	txq->write_ptr = (ssn & 0xff);
	iwl_write_direct32(trans, HBUS_TARG_WRPTR,
			   (ssn & 0xff) | (txq_id << 8));

	if (cfg) {
		u8 frame_limit = cfg->frame_limit;

		iwl_write_prph(trans, SCD_QUEUE_RDPTR(txq_id), ssn);

		/* Set up Tx window size and frame limit for this queue */
		iwl_trans_write_mem32(trans, trans_pcie->scd_base_addr +
				SCD_CONTEXT_QUEUE_OFFSET(txq_id), 0);
		iwl_trans_write_mem32(trans,
			trans_pcie->scd_base_addr +
			SCD_CONTEXT_QUEUE_OFFSET(txq_id) + sizeof(u32),
			SCD_QUEUE_CTX_REG2_VAL(WIN_SIZE, frame_limit) |
			SCD_QUEUE_CTX_REG2_VAL(FRAME_LIMIT, frame_limit));

		/* Set up status area in SRAM, map to Tx DMA/FIFO, activate */
		iwl_write_prph(trans, SCD_QUEUE_STATUS_BITS(txq_id),
			       (1 << SCD_QUEUE_STTS_REG_POS_ACTIVE) |
			       (cfg->fifo << SCD_QUEUE_STTS_REG_POS_TXF) |
			       (1 << SCD_QUEUE_STTS_REG_POS_WSL) |
			       SCD_QUEUE_STTS_REG_MSK);

		/* enable the scheduler for this queue (only) */
		if (txq_id == trans->conf.cmd_queue &&
		    trans->conf.scd_set_active)
			iwl_scd_enable_set_active(trans, BIT(txq_id));

		IWL_DEBUG_TX_QUEUES(trans,
				    "Activate queue %d on FIFO %d WrPtr: %d\n",
				    txq_id, fifo, ssn & 0xff);
	} else {
		IWL_DEBUG_TX_QUEUES(trans,
				    "Activate queue %d WrPtr: %d\n",
				    txq_id, ssn & 0xff);
	}

	return scd_bug;
}

void iwl_trans_pcie_txq_set_shared_mode(struct iwl_trans *trans, u32 txq_id,
					bool shared_mode)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];

	txq->ampdu = !shared_mode;
}

void iwl_trans_pcie_txq_disable(struct iwl_trans *trans, int txq_id,
				bool configure_scd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 stts_addr = trans_pcie->scd_base_addr +
			SCD_TX_STTS_QUEUE_OFFSET(txq_id);
	static const u32 zero_val[4] = {};

	trans_pcie->txqs.txq[txq_id]->frozen_expiry_remainder = 0;
	trans_pcie->txqs.txq[txq_id]->frozen = false;

	/*
	 * Upon HW Rfkill - we stop the device, and then stop the queues
	 * in the op_mode. Just for the sake of the simplicity of the op_mode,
	 * allow the op_mode to call txq_disable after it already called
	 * stop_device.
	 */
	if (!test_and_clear_bit(txq_id, trans_pcie->txqs.queue_used)) {
		WARN_ONCE(test_bit(STATUS_DEVICE_ENABLED, &trans->status),
			  "queue %d not used", txq_id);
		return;
	}

	if (configure_scd) {
		iwl_scd_txq_set_inactive(trans, txq_id);

		iwl_trans_write_mem(trans, stts_addr, (const void *)zero_val,
				    ARRAY_SIZE(zero_val));
	}

	iwl_pcie_txq_unmap(trans, txq_id);
	trans_pcie->txqs.txq[txq_id]->ampdu = false;

	IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", txq_id);
}

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

static void iwl_trans_pcie_block_txq_ptrs(struct iwl_trans *trans, bool block)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	for (i = 0; i < trans->mac_cfg->base->num_of_queues; i++) {
		struct iwl_txq *txq = trans_pcie->txqs.txq[i];

		if (i == trans->conf.cmd_queue)
			continue;

		/* we skip the command queue (obviously) so it's OK to nest */
		spin_lock_nested(&txq->lock, 1);

		if (!block && !(WARN_ON_ONCE(!txq->block))) {
			txq->block--;
			if (!txq->block) {
				iwl_write32(trans, HBUS_TARG_WRPTR,
					    txq->write_ptr | (i << 8));
			}
		} else if (block) {
			txq->block++;
		}

		spin_unlock(&txq->lock);
	}
}

/*
 * iwl_pcie_enqueue_hcmd - enqueue a uCode command
 * @priv: device private data point
 * @cmd: a pointer to the ucode command structure
 *
 * The function returns < 0 values to indicate the operation
 * failed. On success, it returns the index (>= 0) of command in the
 * command queue.
 */
int iwl_pcie_enqueue_hcmd(struct iwl_trans *trans,
			  struct iwl_host_cmd *cmd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[trans->conf.cmd_queue];
	struct iwl_device_cmd *out_cmd;
	struct iwl_cmd_meta *out_meta;
	void *dup_buf = NULL;
	dma_addr_t phys_addr;
	int idx;
	u16 copy_size, cmd_size, tb0_size;
	bool had_nocopy = false;
	u8 group_id = iwl_cmd_groupid(cmd->id);
	int i, ret;
	u32 cmd_pos;
	const u8 *cmddata[IWL_MAX_CMD_TBS_PER_TFD];
	u16 cmdlen[IWL_MAX_CMD_TBS_PER_TFD];
	unsigned long flags;

	if (WARN(!trans->conf.wide_cmd_header &&
		 group_id > IWL_ALWAYS_LONG_GROUP,
		 "unsupported wide command %#x\n", cmd->id))
		return -EINVAL;

	if (group_id != 0) {
		copy_size = sizeof(struct iwl_cmd_header_wide);
		cmd_size = sizeof(struct iwl_cmd_header_wide);
	} else {
		copy_size = sizeof(struct iwl_cmd_header);
		cmd_size = sizeof(struct iwl_cmd_header);
	}

	/* need one for the header if the first is NOCOPY */
	BUILD_BUG_ON(IWL_MAX_CMD_TBS_PER_TFD > IWL_NUM_OF_TBS - 1);

	for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
		cmddata[i] = cmd->data[i];
		cmdlen[i] = cmd->len[i];

		if (!cmd->len[i])
			continue;

		/* need at least IWL_FIRST_TB_SIZE copied */
		if (copy_size < IWL_FIRST_TB_SIZE) {
			int copy = IWL_FIRST_TB_SIZE - copy_size;

			if (copy > cmdlen[i])
				copy = cmdlen[i];
			cmdlen[i] -= copy;
			cmddata[i] += copy;
			copy_size += copy;
		}

		if (cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY) {
			had_nocopy = true;
			if (WARN_ON(cmd->dataflags[i] & IWL_HCMD_DFL_DUP)) {
				idx = -EINVAL;
				goto free_dup_buf;
			}
		} else if (cmd->dataflags[i] & IWL_HCMD_DFL_DUP) {
			/*
			 * This is also a chunk that isn't copied
			 * to the static buffer so set had_nocopy.
			 */
			had_nocopy = true;

			/* only allowed once */
			if (WARN_ON(dup_buf)) {
				idx = -EINVAL;
				goto free_dup_buf;
			}

			dup_buf = kmemdup(cmddata[i], cmdlen[i],
					  GFP_ATOMIC);
			if (!dup_buf)
				return -ENOMEM;
		} else {
			/* NOCOPY must not be followed by normal! */
			if (WARN_ON(had_nocopy)) {
				idx = -EINVAL;
				goto free_dup_buf;
			}
			copy_size += cmdlen[i];
		}
		cmd_size += cmd->len[i];
	}

	/*
	 * If any of the command structures end up being larger than
	 * the TFD_MAX_PAYLOAD_SIZE and they aren't dynamically
	 * allocated into separate TFDs, then we will need to
	 * increase the size of the buffers.
	 */
	if (WARN(copy_size > TFD_MAX_PAYLOAD_SIZE,
		 "Command %s (%#x) is too large (%d bytes)\n",
		 iwl_get_cmd_string(trans, cmd->id),
		 cmd->id, copy_size)) {
		idx = -EINVAL;
		goto free_dup_buf;
	}

	spin_lock_irqsave(&txq->lock, flags);

	if (iwl_txq_space(trans, txq) < ((cmd->flags & CMD_ASYNC) ? 2 : 1)) {
		spin_unlock_irqrestore(&txq->lock, flags);

		IWL_ERR(trans, "No space in command queue\n");
		iwl_op_mode_nic_error(trans->op_mode,
				      IWL_ERR_TYPE_CMD_QUEUE_FULL);
		iwl_trans_schedule_reset(trans, IWL_ERR_TYPE_CMD_QUEUE_FULL);
		idx = -ENOSPC;
		goto free_dup_buf;
	}

	idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	out_cmd = txq->entries[idx].cmd;
	out_meta = &txq->entries[idx].meta;

	/* re-initialize, this also marks the SG list as unused */
	memset(out_meta, 0, sizeof(*out_meta));
	if (cmd->flags & CMD_WANT_SKB)
		out_meta->source = cmd;

	/* set up the header */
	if (group_id != 0) {
		out_cmd->hdr_wide.cmd = iwl_cmd_opcode(cmd->id);
		out_cmd->hdr_wide.group_id = group_id;
		out_cmd->hdr_wide.version = iwl_cmd_version(cmd->id);
		out_cmd->hdr_wide.length =
			cpu_to_le16(cmd_size -
				    sizeof(struct iwl_cmd_header_wide));
		out_cmd->hdr_wide.reserved = 0;
		out_cmd->hdr_wide.sequence =
			cpu_to_le16(QUEUE_TO_SEQ(trans->conf.cmd_queue) |
						 INDEX_TO_SEQ(txq->write_ptr));

		cmd_pos = sizeof(struct iwl_cmd_header_wide);
		copy_size = sizeof(struct iwl_cmd_header_wide);
	} else {
		out_cmd->hdr.cmd = iwl_cmd_opcode(cmd->id);
		out_cmd->hdr.sequence =
			cpu_to_le16(QUEUE_TO_SEQ(trans->conf.cmd_queue) |
						 INDEX_TO_SEQ(txq->write_ptr));
		out_cmd->hdr.group_id = 0;

		cmd_pos = sizeof(struct iwl_cmd_header);
		copy_size = sizeof(struct iwl_cmd_header);
	}

	/* and copy the data that needs to be copied */
	for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
		int copy;

		if (!cmd->len[i])
			continue;

		/* copy everything if not nocopy/dup */
		if (!(cmd->dataflags[i] & (IWL_HCMD_DFL_NOCOPY |
					   IWL_HCMD_DFL_DUP))) {
			copy = cmd->len[i];

			memcpy((u8 *)out_cmd + cmd_pos, cmd->data[i], copy);
			cmd_pos += copy;
			copy_size += copy;
			continue;
		}

		/*
		 * Otherwise we need at least IWL_FIRST_TB_SIZE copied
		 * in total (for bi-directional DMA), but copy up to what
		 * we can fit into the payload for debug dump purposes.
		 */
		copy = min_t(int, TFD_MAX_PAYLOAD_SIZE - cmd_pos, cmd->len[i]);

		memcpy((u8 *)out_cmd + cmd_pos, cmd->data[i], copy);
		cmd_pos += copy;

		/* However, treat copy_size the proper way, we need it below */
		if (copy_size < IWL_FIRST_TB_SIZE) {
			copy = IWL_FIRST_TB_SIZE - copy_size;

			if (copy > cmd->len[i])
				copy = cmd->len[i];
			copy_size += copy;
		}
	}

	IWL_DEBUG_HC(trans,
		     "Sending command %s (%.2x.%.2x), seq: 0x%04X, %d bytes at %d[%d]:%d\n",
		     iwl_get_cmd_string(trans, cmd->id),
		     group_id, out_cmd->hdr.cmd,
		     le16_to_cpu(out_cmd->hdr.sequence),
		     cmd_size, txq->write_ptr, idx, trans->conf.cmd_queue);

	/* start the TFD with the minimum copy bytes */
	tb0_size = min_t(int, copy_size, IWL_FIRST_TB_SIZE);
	memcpy(&txq->first_tb_bufs[idx], &out_cmd->hdr, tb0_size);
	iwl_pcie_txq_build_tfd(trans, txq,
			       iwl_txq_get_first_tb_dma(txq, idx),
			       tb0_size, true);

	/* map first command fragment, if any remains */
	if (copy_size > tb0_size) {
		phys_addr = dma_map_single(trans->dev,
					   ((u8 *)&out_cmd->hdr) + tb0_size,
					   copy_size - tb0_size,
					   DMA_TO_DEVICE);
		if (dma_mapping_error(trans->dev, phys_addr)) {
			iwl_txq_gen1_tfd_unmap(trans, out_meta, txq,
					       txq->write_ptr);
			idx = -ENOMEM;
			goto out;
		}

		iwl_pcie_txq_build_tfd(trans, txq, phys_addr,
				       copy_size - tb0_size, false);
	}

	/* map the remaining (adjusted) nocopy/dup fragments */
	for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
		void *data = (void *)(uintptr_t)cmddata[i];

		if (!cmdlen[i])
			continue;
		if (!(cmd->dataflags[i] & (IWL_HCMD_DFL_NOCOPY |
					   IWL_HCMD_DFL_DUP)))
			continue;
		if (cmd->dataflags[i] & IWL_HCMD_DFL_DUP)
			data = dup_buf;
		phys_addr = dma_map_single(trans->dev, data,
					   cmdlen[i], DMA_TO_DEVICE);
		if (dma_mapping_error(trans->dev, phys_addr)) {
			iwl_txq_gen1_tfd_unmap(trans, out_meta, txq,
					       txq->write_ptr);
			idx = -ENOMEM;
			goto out;
		}

		iwl_pcie_txq_build_tfd(trans, txq, phys_addr, cmdlen[i], false);
	}

	BUILD_BUG_ON(IWL_TFH_NUM_TBS > sizeof(out_meta->tbs) * BITS_PER_BYTE);
	out_meta->flags = cmd->flags;
	if (WARN_ON_ONCE(txq->entries[idx].free_buf))
		kfree_sensitive(txq->entries[idx].free_buf);
	txq->entries[idx].free_buf = dup_buf;

	trace_iwlwifi_dev_hcmd(trans->dev, cmd, cmd_size, &out_cmd->hdr_wide);

	/* start timer if queue currently empty */
	if (txq->read_ptr == txq->write_ptr && txq->wd_timeout)
		mod_timer(&txq->stuck_timer, jiffies + txq->wd_timeout);

	ret = iwl_pcie_set_cmd_in_flight(trans, cmd);
	if (ret < 0) {
		idx = ret;
		goto out;
	}

	if (cmd->flags & CMD_BLOCK_TXQS)
		iwl_trans_pcie_block_txq_ptrs(trans, true);

	/* Increment and update queue's write index */
	txq->write_ptr = iwl_txq_inc_wrap(trans, txq->write_ptr);
	iwl_pcie_txq_inc_wr_ptr(trans, txq);

 out:
	spin_unlock_irqrestore(&txq->lock, flags);
 free_dup_buf:
	if (idx < 0)
		kfree(dup_buf);
	return idx;
}

/*
 * iwl_pcie_hcmd_complete - Pull unused buffers off the queue and reclaim them
 * @rxb: Rx buffer to reclaim
 */
void iwl_pcie_hcmd_complete(struct iwl_trans *trans,
			    struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	u8 group_id;
	u32 cmd_id;
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	int cmd_index;
	struct iwl_device_cmd *cmd;
	struct iwl_cmd_meta *meta;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[trans->conf.cmd_queue];

	/* If a Tx command is being handled and it isn't in the actual
	 * command queue then there a command routing bug has been introduced
	 * in the queue management code. */
	if (IWL_FW_CHECK(trans, txq_id != trans->conf.cmd_queue,
			 "wrong command queue %d (should be %d), sequence 0x%X readp=%d writep=%d pkt=%*phN\n",
			 txq_id, trans->conf.cmd_queue, sequence, txq->read_ptr,
			 txq->write_ptr, 32, pkt))
		return;

	spin_lock_bh(&txq->lock);

	cmd_index = iwl_txq_get_cmd_index(txq, index);
	cmd = txq->entries[cmd_index].cmd;
	meta = &txq->entries[cmd_index].meta;
	group_id = cmd->hdr.group_id;
	cmd_id = WIDE_ID(group_id, cmd->hdr.cmd);

	if (trans->mac_cfg->gen2)
		iwl_txq_gen2_tfd_unmap(trans, meta,
				       iwl_txq_get_tfd(trans, txq, index));
	else
		iwl_txq_gen1_tfd_unmap(trans, meta, txq, index);

	/* Input error checking is done when commands are added to queue. */
	if (meta->flags & CMD_WANT_SKB) {
		struct page *p = rxb_steal_page(rxb);

		meta->source->resp_pkt = pkt;
		meta->source->_rx_page_addr = (unsigned long)page_address(p);
		meta->source->_rx_page_order = trans_pcie->rx_page_order;
	}

	if (meta->flags & CMD_BLOCK_TXQS)
		iwl_trans_pcie_block_txq_ptrs(trans, false);

	iwl_pcie_cmdq_reclaim(trans, txq_id, index);

	if (!(meta->flags & CMD_ASYNC)) {
		if (!test_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status)) {
			IWL_WARN(trans,
				 "HCMD_ACTIVE already clear for command %s\n",
				 iwl_get_cmd_string(trans, cmd_id));
		}
		clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
		IWL_DEBUG_INFO(trans, "Clearing HCMD_ACTIVE for command %s\n",
			       iwl_get_cmd_string(trans, cmd_id));
		wake_up(&trans_pcie->wait_command_queue);
	}

	meta->flags = 0;

	spin_unlock_bh(&txq->lock);
}

static int iwl_fill_data_tbs(struct iwl_trans *trans, struct sk_buff *skb,
			     struct iwl_txq *txq, u8 hdr_len,
			     struct iwl_cmd_meta *out_meta)
{
	u16 head_tb_len;
	int i;

	/*
	 * Set up TFD's third entry to point directly to remainder
	 * of skb's head, if any
	 */
	head_tb_len = skb_headlen(skb) - hdr_len;

	if (head_tb_len > 0) {
		dma_addr_t tb_phys = dma_map_single(trans->dev,
						    skb->data + hdr_len,
						    head_tb_len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
			return -EINVAL;
		trace_iwlwifi_dev_tx_tb(trans->dev, skb, skb->data + hdr_len,
					tb_phys, head_tb_len);
		iwl_pcie_txq_build_tfd(trans, txq, tb_phys, head_tb_len, false);
	}

	/* set up the remaining entries to point to the data */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		dma_addr_t tb_phys;
		int tb_idx;

		if (!skb_frag_size(frag))
			continue;

		tb_phys = skb_frag_dma_map(trans->dev, frag, 0,
					   skb_frag_size(frag), DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
			return -EINVAL;
		trace_iwlwifi_dev_tx_tb(trans->dev, skb, skb_frag_address(frag),
					tb_phys, skb_frag_size(frag));
		tb_idx = iwl_pcie_txq_build_tfd(trans, txq, tb_phys,
						skb_frag_size(frag), false);
		if (tb_idx < 0)
			return tb_idx;

		out_meta->tbs |= BIT(tb_idx);
	}

	return 0;
}

#ifdef CONFIG_INET
static void *iwl_pcie_get_page_hdr(struct iwl_trans *trans,
				   size_t len, struct sk_buff *skb)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tso_hdr_page *p = this_cpu_ptr(trans_pcie->txqs.tso_hdr_page);
	struct iwl_tso_page_info *info;
	struct page **page_ptr;
	dma_addr_t phys;
	void *ret;

	page_ptr = (void *)((u8 *)skb->cb + trans->conf.cb_data_offs);

	if (WARN_ON(*page_ptr))
		return NULL;

	if (!p->page)
		goto alloc;

	/*
	 * Check if there's enough room on this page
	 *
	 * Note that we put a page chaining pointer *last* in the
	 * page - we need it somewhere, and if it's there then we
	 * avoid DMA mapping the last bits of the page which may
	 * trigger the 32-bit boundary hardware bug.
	 *
	 * (see also get_workaround_page() in tx-gen2.c)
	 */
	if (((unsigned long)p->pos & ~PAGE_MASK) + len < IWL_TSO_PAGE_DATA_SIZE) {
		info = IWL_TSO_PAGE_INFO(page_address(p->page));
		goto out;
	}

	/* We don't have enough room on this page, get a new one. */
	iwl_pcie_free_and_unmap_tso_page(trans, p->page);

alloc:
	p->page = alloc_page(GFP_ATOMIC);
	if (!p->page)
		return NULL;
	p->pos = page_address(p->page);

	info = IWL_TSO_PAGE_INFO(page_address(p->page));

	/* set the chaining pointer to NULL */
	info->next = NULL;

	/* Create a DMA mapping for the page */
	phys = dma_map_page_attrs(trans->dev, p->page, 0, PAGE_SIZE,
				  DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	if (unlikely(dma_mapping_error(trans->dev, phys))) {
		__free_page(p->page);
		p->page = NULL;

		return NULL;
	}

	/* Store physical address and set use count */
	info->dma_addr = phys;
	refcount_set(&info->use_count, 1);
out:
	*page_ptr = p->page;
	/* Return an internal reference for the caller */
	refcount_inc(&info->use_count);
	ret = p->pos;
	p->pos += len;

	return ret;
}

/**
 * iwl_pcie_get_sgt_tb_phys - Find TB address in mapped SG list
 * @sgt: scatter gather table
 * @offset: Offset into the mapped memory (i.e. SKB payload data)
 * @len: Length of the area
 *
 * Find the DMA address that corresponds to the SKB payload data at the
 * position given by @offset.
 *
 * Returns: Address for TB entry
 */
dma_addr_t iwl_pcie_get_sgt_tb_phys(struct sg_table *sgt, unsigned int offset,
				    unsigned int len)
{
	struct scatterlist *sg;
	unsigned int sg_offset = 0;
	int i;

	/*
	 * Search the mapped DMA areas in the SG for the area that contains the
	 * data at offset with the given length.
	 */
	for_each_sgtable_dma_sg(sgt, sg, i) {
		if (offset >= sg_offset &&
		    offset + len <= sg_offset + sg_dma_len(sg))
			return sg_dma_address(sg) + offset - sg_offset;

		sg_offset += sg_dma_len(sg);
	}

	WARN_ON_ONCE(1);

	return DMA_MAPPING_ERROR;
}

/**
 * iwl_pcie_prep_tso - Prepare TSO page and SKB for sending
 * @trans: transport private data
 * @skb: the SKB to map
 * @cmd_meta: command meta to store the scatter list information for unmapping
 * @hdr: output argument for TSO headers
 * @hdr_room: requested length for TSO headers
 * @offset: offset into the data from which mapping should start
 *
 * Allocate space for a scatter gather list and TSO headers and map the SKB
 * using the scatter gather list. The SKB is unmapped again when the page is
 * free'ed again at the end of the operation.
 *
 * Returns: newly allocated and mapped scatter gather table with list
 */
struct sg_table *iwl_pcie_prep_tso(struct iwl_trans *trans, struct sk_buff *skb,
				   struct iwl_cmd_meta *cmd_meta,
				   u8 **hdr, unsigned int hdr_room,
				   unsigned int offset)
{
	struct sg_table *sgt;
	unsigned int n_segments = skb_shinfo(skb)->nr_frags + 1;
	int orig_nents;

	if (WARN_ON_ONCE(skb_has_frag_list(skb)))
		return NULL;

	*hdr = iwl_pcie_get_page_hdr(trans,
				     hdr_room + __alignof__(struct sg_table) +
				     sizeof(struct sg_table) +
				     n_segments * sizeof(struct scatterlist),
				     skb);
	if (!*hdr)
		return NULL;

	sgt = (void *)PTR_ALIGN(*hdr + hdr_room, __alignof__(struct sg_table));
	sgt->sgl = (void *)(sgt + 1);

	sg_init_table(sgt->sgl, n_segments);

	/* Only map the data, not the header (it is copied to the TSO page) */
	orig_nents = skb_to_sgvec(skb, sgt->sgl, offset, skb->len - offset);
	if (WARN_ON_ONCE(orig_nents <= 0))
		return NULL;

	sgt->orig_nents = orig_nents;

	/* And map the entire SKB */
	if (dma_map_sgtable(trans->dev, sgt, DMA_TO_DEVICE, 0) < 0)
		return NULL;

	/* Store non-zero (i.e. valid) offset for unmapping */
	cmd_meta->sg_offset = (unsigned long) sgt & ~PAGE_MASK;

	return sgt;
}

static int iwl_fill_data_tbs_amsdu(struct iwl_trans *trans, struct sk_buff *skb,
				   struct iwl_txq *txq, u8 hdr_len,
				   struct iwl_cmd_meta *out_meta,
				   struct iwl_device_tx_cmd *dev_cmd,
				   u16 tb1_len)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_cmd_v6 *tx_cmd = (void *)dev_cmd->payload;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int snap_ip_tcp_hdrlen, ip_hdrlen, total_len, hdr_room;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	unsigned int data_offset = 0;
	u16 length, iv_len, amsdu_pad;
	dma_addr_t start_hdr_phys;
	u8 *start_hdr, *pos_hdr;
	struct sg_table *sgt;
	struct tso_t tso;

	/* if the packet is protected, then it must be CCMP or GCMP */
	BUILD_BUG_ON(IEEE80211_CCMP_HDR_LEN != IEEE80211_GCMP_HDR_LEN);
	iv_len = ieee80211_has_protected(hdr->frame_control) ?
		IEEE80211_CCMP_HDR_LEN : 0;

	trace_iwlwifi_dev_tx(trans->dev, skb,
			     iwl_txq_get_tfd(trans, txq, txq->write_ptr),
			     trans_pcie->txqs.tfd.size,
			     &dev_cmd->hdr, IWL_FIRST_TB_SIZE + tb1_len, 0);

	ip_hdrlen = skb_network_header_len(skb);
	snap_ip_tcp_hdrlen = 8 + ip_hdrlen + tcp_hdrlen(skb);
	total_len = skb->len - snap_ip_tcp_hdrlen - hdr_len - iv_len;
	amsdu_pad = 0;

	/* total amount of header we may need for this A-MSDU */
	hdr_room = DIV_ROUND_UP(total_len, mss) *
		(3 + snap_ip_tcp_hdrlen + sizeof(struct ethhdr)) + iv_len;

	/* Our device supports 9 segments at most, it will fit in 1 page */
	sgt = iwl_pcie_prep_tso(trans, skb, out_meta, &start_hdr, hdr_room,
				snap_ip_tcp_hdrlen + hdr_len + iv_len);
	if (!sgt)
		return -ENOMEM;

	start_hdr_phys = iwl_pcie_get_tso_page_phys(start_hdr);
	pos_hdr = start_hdr;
	memcpy(pos_hdr, skb->data + hdr_len, iv_len);
	pos_hdr += iv_len;

	/*
	 * Pull the ieee80211 header + IV to be able to use TSO core,
	 * we will restore it for the tx_status flow.
	 */
	skb_pull(skb, hdr_len + iv_len);

	/*
	 * Remove the length of all the headers that we don't actually
	 * have in the MPDU by themselves, but that we duplicate into
	 * all the different MSDUs inside the A-MSDU.
	 */
	le16_add_cpu(&tx_cmd->params.len, -snap_ip_tcp_hdrlen);

	tso_start(skb, &tso);

	while (total_len) {
		/* this is the data left for this subframe */
		unsigned int data_left =
			min_t(unsigned int, mss, total_len);
		unsigned int hdr_tb_len;
		dma_addr_t hdr_tb_phys;
		u8 *subf_hdrs_start = pos_hdr;

		total_len -= data_left;

		memset(pos_hdr, 0, amsdu_pad);
		pos_hdr += amsdu_pad;
		amsdu_pad = (4 - (sizeof(struct ethhdr) + snap_ip_tcp_hdrlen +
				  data_left)) & 0x3;
		ether_addr_copy(pos_hdr, ieee80211_get_DA(hdr));
		pos_hdr += ETH_ALEN;
		ether_addr_copy(pos_hdr, ieee80211_get_SA(hdr));
		pos_hdr += ETH_ALEN;

		length = snap_ip_tcp_hdrlen + data_left;
		*((__be16 *)pos_hdr) = cpu_to_be16(length);
		pos_hdr += sizeof(length);

		/*
		 * This will copy the SNAP as well which will be considered
		 * as MAC header.
		 */
		tso_build_hdr(skb, pos_hdr, &tso, data_left, !total_len);

		pos_hdr += snap_ip_tcp_hdrlen;

		hdr_tb_len = pos_hdr - start_hdr;
		hdr_tb_phys = iwl_pcie_get_tso_page_phys(start_hdr);

		iwl_pcie_txq_build_tfd(trans, txq, hdr_tb_phys,
				       hdr_tb_len, false);
		trace_iwlwifi_dev_tx_tb(trans->dev, skb, start_hdr,
					hdr_tb_phys, hdr_tb_len);
		/* add this subframe's headers' length to the tx_cmd */
		le16_add_cpu(&tx_cmd->params.len, pos_hdr - subf_hdrs_start);

		/* prepare the start_hdr for the next subframe */
		start_hdr = pos_hdr;

		/* put the payload */
		while (data_left) {
			unsigned int size = min_t(unsigned int, tso.size,
						  data_left);
			dma_addr_t tb_phys;

			tb_phys = iwl_pcie_get_sgt_tb_phys(sgt, data_offset, size);
			/* Not a real mapping error, use direct comparison */
			if (unlikely(tb_phys == DMA_MAPPING_ERROR))
				return -EINVAL;

			iwl_pcie_txq_build_tfd(trans, txq, tb_phys,
					       size, false);
			trace_iwlwifi_dev_tx_tb(trans->dev, skb, tso.data,
						tb_phys, size);

			data_left -= size;
			data_offset += size;
			tso_build_data(skb, &tso, size);
		}
	}

	dma_sync_single_for_device(trans->dev, start_hdr_phys, hdr_room,
				   DMA_TO_DEVICE);

	/* re -add the WiFi header and IV */
	skb_push(skb, hdr_len + iv_len);

	return 0;
}
#else /* CONFIG_INET */
static int iwl_fill_data_tbs_amsdu(struct iwl_trans *trans, struct sk_buff *skb,
				   struct iwl_txq *txq, u8 hdr_len,
				   struct iwl_cmd_meta *out_meta,
				   struct iwl_device_tx_cmd *dev_cmd,
				   u16 tb1_len)
{
	/* No A-MSDU without CONFIG_INET */
	WARN_ON(1);

	return -1;
}
#endif /* CONFIG_INET */

#define IWL_TX_CRC_SIZE 4
#define IWL_TX_DELIMITER_SIZE 4

/*
 * iwl_txq_gen1_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void iwl_txq_gen1_update_byte_cnt_tbl(struct iwl_trans *trans,
					     struct iwl_txq *txq, u16 byte_cnt,
					     int num_tbs)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_bc_tbl_entry *scd_bc_tbl;
	int write_ptr = txq->write_ptr;
	int txq_id = txq->id;
	u8 sec_ctl = 0;
	u16 len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
	__le16 bc_ent;
	struct iwl_device_tx_cmd *dev_cmd = txq->entries[txq->write_ptr].cmd;
	struct iwl_tx_cmd_v6 *tx_cmd = (void *)dev_cmd->payload;
	u8 sta_id = tx_cmd->params.sta_id;

	scd_bc_tbl = trans_pcie->txqs.scd_bc_tbls.addr;

	sec_ctl = tx_cmd->params.sec_ctl;

	switch (sec_ctl & TX_CMD_SEC_MSK) {
	case TX_CMD_SEC_CCM:
		len += IEEE80211_CCMP_MIC_LEN;
		break;
	case TX_CMD_SEC_TKIP:
		len += IEEE80211_TKIP_ICV_LEN;
		break;
	case TX_CMD_SEC_WEP:
		len += IEEE80211_WEP_IV_LEN + IEEE80211_WEP_ICV_LEN;
		break;
	}

	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_7000 &&
	    trans->mac_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		len = DIV_ROUND_UP(len, 4);

	if (WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX))
		return;

	bc_ent = cpu_to_le16(len | (sta_id << 12));

	scd_bc_tbl[txq_id * TFD_QUEUE_BC_SIZE + write_ptr].tfd_offset = bc_ent;

	if (write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id * TFD_QUEUE_BC_SIZE + TFD_QUEUE_SIZE_MAX + write_ptr].tfd_offset =
			bc_ent;
}

int iwl_trans_pcie_tx(struct iwl_trans *trans, struct sk_buff *skb,
		      struct iwl_device_tx_cmd *dev_cmd, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct ieee80211_hdr *hdr;
	struct iwl_tx_cmd_v6 *tx_cmd = (struct iwl_tx_cmd_v6 *)dev_cmd->payload;
	struct iwl_cmd_meta *out_meta;
	struct iwl_txq *txq;
	dma_addr_t tb0_phys, tb1_phys, scratch_phys;
	void *tb1_addr;
	void *tfd;
	u16 len, tb1_len;
	bool wait_write_ptr;
	__le16 fc;
	u8 hdr_len;
	u16 wifi_seq;
	bool amsdu;

	txq = trans_pcie->txqs.txq[txq_id];

	if (WARN_ONCE(!test_bit(txq_id, trans_pcie->txqs.queue_used),
		      "TX on unused queue %d\n", txq_id))
		return -EINVAL;

	if (skb_is_nonlinear(skb) &&
	    skb_shinfo(skb)->nr_frags > IWL_TRANS_PCIE_MAX_FRAGS(trans_pcie) &&
	    __skb_linearize(skb))
		return -ENOMEM;

	/* mac80211 always puts the full header into the SKB's head,
	 * so there's no need to check if it's readable there
	 */
	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	hdr_len = ieee80211_hdrlen(fc);

	spin_lock(&txq->lock);

	if (iwl_txq_space(trans, txq) < txq->high_mark) {
		iwl_txq_stop(trans, txq);

		/* don't put the packet on the ring, if there is no room */
		if (unlikely(iwl_txq_space(trans, txq) < 3)) {
			struct iwl_device_tx_cmd **dev_cmd_ptr;

			dev_cmd_ptr = (void *)((u8 *)skb->cb +
					       trans->conf.cb_data_offs +
					       sizeof(void *));

			*dev_cmd_ptr = dev_cmd;
			__skb_queue_tail(&txq->overflow_q, skb);

			spin_unlock(&txq->lock);
			return 0;
		}
	}

	/* In AGG mode, the index in the ring must correspond to the WiFi
	 * sequence number. This is a HW requirements to help the SCD to parse
	 * the BA.
	 * Check here that the packets are in the right place on the ring.
	 */
	wifi_seq = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
	WARN_ONCE(txq->ampdu &&
		  (wifi_seq & 0xff) != txq->write_ptr,
		  "Q: %d WiFi Seq %d tfdNum %d",
		  txq_id, wifi_seq, txq->write_ptr);

	/* Set up driver data for this TFD */
	txq->entries[txq->write_ptr].skb = skb;
	txq->entries[txq->write_ptr].cmd = dev_cmd;

	dev_cmd->hdr.sequence =
		cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
			    INDEX_TO_SEQ(txq->write_ptr)));

	tb0_phys = iwl_txq_get_first_tb_dma(txq, txq->write_ptr);
	scratch_phys = tb0_phys + sizeof(struct iwl_cmd_header) +
		       offsetof(struct iwl_tx_cmd_v6_params, scratch);

	tx_cmd->params.dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->params.dram_msb_ptr = iwl_get_dma_hi_addr(scratch_phys);

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_meta = &txq->entries[txq->write_ptr].meta;
	memset(out_meta, 0, sizeof(*out_meta));

	/*
	 * The second TB (tb1) points to the remainder of the TX command
	 * and the 802.11 header - dword aligned size
	 * (This calculation modifies the TX command, so do it before the
	 * setup of the first TB)
	 */
	len = sizeof(struct iwl_tx_cmd_v6) + sizeof(struct iwl_cmd_header) +
	      hdr_len - IWL_FIRST_TB_SIZE;
	/* do not align A-MSDU to dword as the subframe header aligns it */
	amsdu = ieee80211_is_data_qos(fc) &&
		(*ieee80211_get_qos_ctl(hdr) &
		 IEEE80211_QOS_CTL_A_MSDU_PRESENT);
	if (!amsdu) {
		tb1_len = ALIGN(len, 4);
		/* Tell NIC about any 2-byte padding after MAC header */
		if (tb1_len != len)
			tx_cmd->params.tx_flags |= cpu_to_le32(TX_CMD_FLG_MH_PAD);
	} else {
		tb1_len = len;
	}

	/*
	 * The first TB points to bi-directional DMA data, we'll
	 * memcpy the data into it later.
	 */
	iwl_pcie_txq_build_tfd(trans, txq, tb0_phys,
			       IWL_FIRST_TB_SIZE, true);

	/* there must be data left over for TB1 or this code must be changed */
	BUILD_BUG_ON(sizeof(struct iwl_tx_cmd_v6) < IWL_FIRST_TB_SIZE);
	BUILD_BUG_ON(sizeof(struct iwl_cmd_header) +
		     offsetofend(struct iwl_tx_cmd_v6_params, scratch) >
		     IWL_FIRST_TB_SIZE);

	/* map the data for TB1 */
	tb1_addr = ((u8 *)&dev_cmd->hdr) + IWL_FIRST_TB_SIZE;
	tb1_phys = dma_map_single(trans->dev, tb1_addr, tb1_len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(trans->dev, tb1_phys)))
		goto out_err;
	iwl_pcie_txq_build_tfd(trans, txq, tb1_phys, tb1_len, false);

	trace_iwlwifi_dev_tx(trans->dev, skb,
			     iwl_txq_get_tfd(trans, txq, txq->write_ptr),
			     trans_pcie->txqs.tfd.size,
			     &dev_cmd->hdr, IWL_FIRST_TB_SIZE + tb1_len,
			     hdr_len);

	/*
	 * If gso_size wasn't set, don't give the frame "amsdu treatment"
	 * (adding subframes, etc.).
	 * This can happen in some testing flows when the amsdu was already
	 * pre-built, and we just need to send the resulting skb.
	 */
	if (amsdu && skb_shinfo(skb)->gso_size) {
		if (unlikely(iwl_fill_data_tbs_amsdu(trans, skb, txq, hdr_len,
						     out_meta, dev_cmd,
						     tb1_len)))
			goto out_err;
	} else {
		struct sk_buff *frag;

		if (unlikely(iwl_fill_data_tbs(trans, skb, txq, hdr_len,
					       out_meta)))
			goto out_err;

		skb_walk_frags(skb, frag) {
			if (unlikely(iwl_fill_data_tbs(trans, frag, txq, 0,
						       out_meta)))
				goto out_err;
		}
	}

	/* building the A-MSDU might have changed this data, so memcpy it now */
	memcpy(&txq->first_tb_bufs[txq->write_ptr], dev_cmd, IWL_FIRST_TB_SIZE);

	tfd = iwl_txq_get_tfd(trans, txq, txq->write_ptr);
	/* Set up entry for this TFD in Tx byte-count array */
	iwl_txq_gen1_update_byte_cnt_tbl(trans, txq, le16_to_cpu(tx_cmd->params.len),
					 iwl_txq_gen1_tfd_get_num_tbs(tfd));

	wait_write_ptr = ieee80211_has_morefrags(fc);

	/* start timer if queue currently empty */
	if (txq->read_ptr == txq->write_ptr && txq->wd_timeout) {
		/*
		 * If the TXQ is active, then set the timer, if not,
		 * set the timer in remainder so that the timer will
		 * be armed with the right value when the station will
		 * wake up.
		 */
		if (!txq->frozen)
			mod_timer(&txq->stuck_timer,
				  jiffies + txq->wd_timeout);
		else
			txq->frozen_expiry_remainder = txq->wd_timeout;
	}

	/* Tell device the write index *just past* this latest filled TFD */
	txq->write_ptr = iwl_txq_inc_wrap(trans, txq->write_ptr);
	if (!wait_write_ptr)
		iwl_pcie_txq_inc_wr_ptr(trans, txq);

	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually.
	 */
	spin_unlock(&txq->lock);
	return 0;
out_err:
	iwl_txq_gen1_tfd_unmap(trans, out_meta, txq, txq->write_ptr);
	spin_unlock(&txq->lock);
	return -1;
}

static void iwl_txq_gen1_inval_byte_cnt_tbl(struct iwl_trans *trans,
					    struct iwl_txq *txq,
					    int read_ptr)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_bc_tbl_entry *scd_bc_tbl = trans_pcie->txqs.scd_bc_tbls.addr;
	int txq_id = txq->id;
	u8 sta_id = 0;
	__le16 bc_ent;
	struct iwl_device_tx_cmd *dev_cmd = txq->entries[read_ptr].cmd;
	struct iwl_tx_cmd_v6 *tx_cmd = (void *)dev_cmd->payload;

	WARN_ON(read_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != trans->conf.cmd_queue)
		sta_id = tx_cmd->params.sta_id;

	bc_ent = cpu_to_le16(1 | (sta_id << 12));

	scd_bc_tbl[txq_id * TFD_QUEUE_BC_SIZE + read_ptr].tfd_offset = bc_ent;

	if (read_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id * TFD_QUEUE_BC_SIZE + TFD_QUEUE_SIZE_MAX + read_ptr].tfd_offset =
			bc_ent;
}

/* Frees buffers until index _not_ inclusive */
void iwl_pcie_reclaim(struct iwl_trans *trans, int txq_id, int ssn,
		      struct sk_buff_head *skbs, bool is_flush)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];
	int tfd_num, read_ptr, last_to_free;
	int txq_read_ptr, txq_write_ptr;

	/* This function is not meant to release cmd queue*/
	if (WARN_ON(txq_id == trans->conf.cmd_queue))
		return;

	if (WARN_ON(!txq))
		return;

	tfd_num = iwl_txq_get_cmd_index(txq, ssn);

	spin_lock_bh(&txq->reclaim_lock);

	spin_lock(&txq->lock);
	txq_read_ptr = txq->read_ptr;
	txq_write_ptr = txq->write_ptr;
	spin_unlock(&txq->lock);

	/* There is nothing to do if we are flushing an empty queue */
	if (is_flush && txq_write_ptr == txq_read_ptr)
		goto out;

	read_ptr = iwl_txq_get_cmd_index(txq, txq_read_ptr);

	if (!test_bit(txq_id, trans_pcie->txqs.queue_used)) {
		IWL_DEBUG_TX_QUEUES(trans, "Q %d inactive - ignoring idx %d\n",
				    txq_id, ssn);
		goto out;
	}

	if (read_ptr == tfd_num)
		goto out;

	IWL_DEBUG_TX_REPLY(trans, "[Q %d] %d (%d) -> %d (%d)\n",
			   txq_id, read_ptr, txq_read_ptr, tfd_num, ssn);

	/* Since we free until index _not_ inclusive, the one before index is
	 * the last we will free. This one must be used
	 */
	last_to_free = iwl_txq_dec_wrap(trans, tfd_num);

	if (!iwl_txq_used(txq, last_to_free, txq_read_ptr, txq_write_ptr)) {
		IWL_ERR(trans,
			"%s: Read index for txq id (%d), last_to_free %d is out of range [0-%d] %d %d.\n",
			__func__, txq_id, last_to_free,
			trans->mac_cfg->base->max_tfd_queue_size,
			txq_write_ptr, txq_read_ptr);

		iwl_op_mode_time_point(trans->op_mode,
				       IWL_FW_INI_TIME_POINT_FAKE_TX,
				       NULL);
		goto out;
	}

	if (WARN_ON(!skb_queue_empty(skbs)))
		goto out;

	for (;
	     read_ptr != tfd_num;
	     txq_read_ptr = iwl_txq_inc_wrap(trans, txq_read_ptr),
	     read_ptr = iwl_txq_get_cmd_index(txq, txq_read_ptr)) {
		struct iwl_cmd_meta *cmd_meta = &txq->entries[read_ptr].meta;
		struct sk_buff *skb = txq->entries[read_ptr].skb;

		if (WARN_ONCE(!skb, "no SKB at %d (%d) on queue %d\n",
			      read_ptr, txq_read_ptr, txq_id))
			continue;

		iwl_pcie_free_tso_pages(trans, skb, cmd_meta);

		__skb_queue_tail(skbs, skb);

		txq->entries[read_ptr].skb = NULL;

		if (!trans->mac_cfg->gen2)
			iwl_txq_gen1_inval_byte_cnt_tbl(trans, txq,
							txq_read_ptr);

		iwl_txq_free_tfd(trans, txq, txq_read_ptr);
	}

	spin_lock(&txq->lock);
	txq->read_ptr = txq_read_ptr;

	iwl_txq_progress(txq);

	if (iwl_txq_space(trans, txq) > txq->low_mark &&
	    test_bit(txq_id, trans_pcie->txqs.queue_stopped)) {
		struct sk_buff_head overflow_skbs;
		struct sk_buff *skb;

		__skb_queue_head_init(&overflow_skbs);
		skb_queue_splice_init(&txq->overflow_q,
				      is_flush ? skbs : &overflow_skbs);

		/*
		 * We are going to transmit from the overflow queue.
		 * Remember this state so that wait_for_txq_empty will know we
		 * are adding more packets to the TFD queue. It cannot rely on
		 * the state of &txq->overflow_q, as we just emptied it, but
		 * haven't TXed the content yet.
		 */
		txq->overflow_tx = true;

		/*
		 * This is tricky: we are in reclaim path and are holding
		 * reclaim_lock, so noone will try to access the txq data
		 * from that path. We stopped tx, so we can't have tx as well.
		 * Bottom line, we can unlock and re-lock later.
		 */
		spin_unlock(&txq->lock);

		while ((skb = __skb_dequeue(&overflow_skbs))) {
			struct iwl_device_tx_cmd *dev_cmd_ptr;

			dev_cmd_ptr = *(void **)((u8 *)skb->cb +
						 trans->conf.cb_data_offs +
						 sizeof(void *));

			/*
			 * Note that we can very well be overflowing again.
			 * In that case, iwl_txq_space will be small again
			 * and we won't wake mac80211's queue.
			 */
			iwl_trans_tx(trans, skb, dev_cmd_ptr, txq_id);
		}

		if (iwl_txq_space(trans, txq) > txq->low_mark)
			iwl_trans_pcie_wake_queue(trans, txq);

		spin_lock(&txq->lock);
		txq->overflow_tx = false;
	}

	spin_unlock(&txq->lock);
out:
	spin_unlock_bh(&txq->reclaim_lock);
}

/* Set wr_ptr of specific device and txq  */
void iwl_pcie_set_q_ptrs(struct iwl_trans *trans, int txq_id, int ptr)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];

	spin_lock_bh(&txq->lock);

	txq->write_ptr = ptr;
	txq->read_ptr = txq->write_ptr;

	spin_unlock_bh(&txq->lock);
}

void iwl_pcie_freeze_txq_timer(struct iwl_trans *trans,
			       unsigned long txqs, bool freeze)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int queue;

	for_each_set_bit(queue, &txqs, BITS_PER_LONG) {
		struct iwl_txq *txq = trans_pcie->txqs.txq[queue];
		unsigned long now;

		spin_lock_bh(&txq->lock);

		now = jiffies;

		if (txq->frozen == freeze)
			goto next_queue;

		IWL_DEBUG_TX_QUEUES(trans, "%s TXQ %d\n",
				    freeze ? "Freezing" : "Waking", queue);

		txq->frozen = freeze;

		if (txq->read_ptr == txq->write_ptr)
			goto next_queue;

		if (freeze) {
			if (unlikely(time_after(now,
						txq->stuck_timer.expires))) {
				/*
				 * The timer should have fired, maybe it is
				 * spinning right now on the lock.
				 */
				goto next_queue;
			}
			/* remember how long until the timer fires */
			txq->frozen_expiry_remainder =
				txq->stuck_timer.expires - now;
			timer_delete(&txq->stuck_timer);
			goto next_queue;
		}

		/*
		 * Wake a non-empty queue -> arm timer with the
		 * remainder before it froze
		 */
		mod_timer(&txq->stuck_timer,
			  now + txq->frozen_expiry_remainder);

next_queue:
		spin_unlock_bh(&txq->lock);
	}
}

#define HOST_COMPLETE_TIMEOUT	(2 * HZ)

static int iwl_trans_pcie_send_hcmd_sync(struct iwl_trans *trans,
					 struct iwl_host_cmd *cmd,
					 const char *cmd_str)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[trans->conf.cmd_queue];
	int cmd_idx;
	int ret;

	IWL_DEBUG_INFO(trans, "Attempting to send sync command %s\n", cmd_str);

	if (WARN(test_and_set_bit(STATUS_SYNC_HCMD_ACTIVE,
				  &trans->status),
		 "Command %s: a command is already active!\n", cmd_str))
		return -EIO;

	IWL_DEBUG_INFO(trans, "Setting HCMD_ACTIVE for command %s\n", cmd_str);

	if (trans->mac_cfg->gen2)
		cmd_idx = iwl_pcie_gen2_enqueue_hcmd(trans, cmd);
	else
		cmd_idx = iwl_pcie_enqueue_hcmd(trans, cmd);

	if (cmd_idx < 0) {
		ret = cmd_idx;
		clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
		IWL_ERR(trans, "Error sending %s: enqueue_hcmd failed: %d\n",
			cmd_str, ret);
		return ret;
	}

	ret = wait_event_timeout(trans_pcie->wait_command_queue,
				 !test_bit(STATUS_SYNC_HCMD_ACTIVE,
					   &trans->status),
				 HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		IWL_ERR(trans, "Error sending %s: time out after %dms.\n",
			cmd_str, jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

		IWL_ERR(trans, "Current CMD queue read_ptr %d write_ptr %d\n",
			txq->read_ptr, txq->write_ptr);

		clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
		IWL_DEBUG_INFO(trans, "Clearing HCMD_ACTIVE for command %s\n",
			       cmd_str);
		ret = -ETIMEDOUT;

		iwl_trans_pcie_sync_nmi(trans);
		goto cancel;
	}

	if (test_bit(STATUS_FW_ERROR, &trans->status)) {
		if (trans->suppress_cmd_error_once) {
			trans->suppress_cmd_error_once = false;
		} else {
			IWL_ERR(trans, "FW error in SYNC CMD %s\n", cmd_str);
			dump_stack();
		}
		ret = -EIO;
		goto cancel;
	}

	if (!(cmd->flags & CMD_SEND_IN_RFKILL) &&
	    test_bit(STATUS_RFKILL_OPMODE, &trans->status)) {
		IWL_DEBUG_RF_KILL(trans, "RFKILL in SYNC CMD... no rsp\n");
		ret = -ERFKILL;
		goto cancel;
	}

	if ((cmd->flags & CMD_WANT_SKB) && !cmd->resp_pkt) {
		IWL_ERR(trans, "Error: Response NULL in '%s'\n", cmd_str);
		ret = -EIO;
		goto cancel;
	}

	return 0;

cancel:
	if (cmd->flags & CMD_WANT_SKB) {
		/*
		 * Cancel the CMD_WANT_SKB flag for the cmd in the
		 * TX cmd queue. Otherwise in case the cmd comes
		 * in later, it will possibly set an invalid
		 * address (cmd->meta.source).
		 */
		txq->entries[cmd_idx].meta.flags &= ~CMD_WANT_SKB;
	}

	if (cmd->resp_pkt) {
		iwl_free_resp(cmd);
		cmd->resp_pkt = NULL;
	}

	return ret;
}

int iwl_trans_pcie_send_hcmd(struct iwl_trans *trans,
			     struct iwl_host_cmd *cmd)
{
	const char *cmd_str = iwl_get_cmd_string(trans, cmd->id);

	/* Make sure the NIC is still alive in the bus */
	if (test_bit(STATUS_TRANS_DEAD, &trans->status))
		return -ENODEV;

	if (!(cmd->flags & CMD_SEND_IN_RFKILL) &&
	    test_bit(STATUS_RFKILL_OPMODE, &trans->status)) {
		IWL_DEBUG_RF_KILL(trans, "Dropping CMD 0x%x: RF KILL\n",
				  cmd->id);
		return -ERFKILL;
	}

	if (cmd->flags & CMD_ASYNC) {
		int ret;

		IWL_DEBUG_INFO(trans, "Sending async command %s\n", cmd_str);

		/* An asynchronous command can not expect an SKB to be set. */
		if (WARN_ON(cmd->flags & CMD_WANT_SKB))
			return -EINVAL;

		if (trans->mac_cfg->gen2)
			ret = iwl_pcie_gen2_enqueue_hcmd(trans, cmd);
		else
			ret = iwl_pcie_enqueue_hcmd(trans, cmd);

		if (ret < 0) {
			IWL_ERR(trans,
				"Error sending %s: enqueue_hcmd failed: %d\n",
				iwl_get_cmd_string(trans, cmd->id), ret);
			return ret;
		}
		return 0;
	}

	return iwl_trans_pcie_send_hcmd_sync(trans, cmd, cmd_str);
}
