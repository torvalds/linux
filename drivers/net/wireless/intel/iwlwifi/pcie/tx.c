// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2003-2014, 2018-2021, 2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <net/ip6_checksum.h>
#include <net/tso.h>

#include "iwl-debug.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-io.h"
#include "iwl-scd.h"
#include "iwl-op-mode.h"
#include "internal.h"
#include "fw/api/tx.h"

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
	if (!trans->trans_cfg->base_params->shadow_reg_enable &&
	    txq_id != trans->txqs.cmd.q_id &&
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
	int i;

	for (i = 0; i < trans->trans_cfg->base_params->num_of_queues; i++) {
		struct iwl_txq *txq = trans->txqs.txq[i];

		if (!test_bit(i, trans->txqs.queue_used))
			continue;

		spin_lock_bh(&txq->lock);
		if (txq->need_update) {
			iwl_pcie_txq_inc_wr_ptr(trans, txq);
			txq->need_update = false;
		}
		spin_unlock_bh(&txq->lock);
	}
}

static int iwl_pcie_txq_build_tfd(struct iwl_trans *trans, struct iwl_txq *txq,
				  dma_addr_t addr, u16 len, bool reset)
{
	void *tfd;
	u32 num_tbs;

	tfd = (u8 *)txq->tfds + trans->txqs.tfd.size * txq->write_ptr;

	if (reset)
		memset(tfd, 0, trans->txqs.tfd.size);

	num_tbs = iwl_txq_gen1_tfd_get_num_tbs(trans, tfd);

	/* Each TFD can point to a maximum max_tbs Tx buffers */
	if (num_tbs >= trans->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Error can not send more than %d chunks\n",
			trans->txqs.tfd.max_tbs);
		return -EINVAL;
	}

	if (WARN(addr & ~IWL_TX_DMA_MASK,
		 "Unaligned address = %llx\n", (unsigned long long)addr))
		return -EINVAL;

	iwl_pcie_gen1_tfd_set_tb(trans, tfd, num_tbs, addr, len);

	return num_tbs;
}

static void iwl_pcie_clear_cmd_in_flight(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans->trans_cfg->base_params->apmg_wake_up_wa)
		return;

	spin_lock(&trans_pcie->reg_lock);

	if (WARN_ON(!trans_pcie->cmd_hold_nic_awake)) {
		spin_unlock(&trans_pcie->reg_lock);
		return;
	}

	trans_pcie->cmd_hold_nic_awake = false;
	__iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	spin_unlock(&trans_pcie->reg_lock);
}

/*
 * iwl_pcie_txq_unmap -  Unmap any remaining DMA mappings and free skb's
 */
static void iwl_pcie_txq_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_txq *txq = trans->txqs.txq[txq_id];

	if (!txq) {
		IWL_ERR(trans, "Trying to free a queue that wasn't allocated?\n");
		return;
	}

	spin_lock_bh(&txq->lock);
	while (txq->write_ptr != txq->read_ptr) {
		IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n",
				   txq_id, txq->read_ptr);

		if (txq_id != trans->txqs.cmd.q_id) {
			struct sk_buff *skb = txq->entries[txq->read_ptr].skb;

			if (WARN_ON_ONCE(!skb))
				continue;

			iwl_txq_free_tso_page(trans, skb);
		}
		iwl_txq_free_tfd(trans, txq);
		txq->read_ptr = iwl_txq_inc_wrap(trans, txq->read_ptr);

		if (txq->read_ptr == txq->write_ptr &&
		    txq_id == trans->txqs.cmd.q_id)
			iwl_pcie_clear_cmd_in_flight(trans);
	}

	while (!skb_queue_empty(&txq->overflow_q)) {
		struct sk_buff *skb = __skb_dequeue(&txq->overflow_q);

		iwl_op_mode_free_skb(trans->op_mode, skb);
	}

	spin_unlock_bh(&txq->lock);

	/* just in case - this queue may have been stopped */
	iwl_wake_queue(trans, txq);
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
	struct iwl_txq *txq = trans->txqs.txq[txq_id];
	struct device *dev = trans->dev;
	int i;

	if (WARN_ON(!txq))
		return;

	iwl_pcie_txq_unmap(trans, txq_id);

	/* De-alloc array of command/tx buffers */
	if (txq_id == trans->txqs.cmd.q_id)
		for (i = 0; i < txq->n_window; i++) {
			kfree_sensitive(txq->entries[i].cmd);
			kfree_sensitive(txq->entries[i].free_buf);
		}

	/* De-alloc circular buffer of TFDs */
	if (txq->tfds) {
		dma_free_coherent(dev,
				  trans->txqs.tfd.size *
				  trans->trans_cfg->base_params->max_tfd_queue_size,
				  txq->tfds, txq->dma_addr);
		txq->dma_addr = 0;
		txq->tfds = NULL;

		dma_free_coherent(dev,
				  sizeof(*txq->first_tb_bufs) * txq->n_window,
				  txq->first_tb_bufs, txq->first_tb_dma);
	}

	kfree(txq->entries);
	txq->entries = NULL;

	del_timer_sync(&txq->stuck_timer);

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}

void iwl_pcie_tx_start(struct iwl_trans *trans, u32 scd_base_addr)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int nq = trans->trans_cfg->base_params->num_of_queues;
	int chan;
	u32 reg_val;
	int clear_dwords = (SCD_TRANS_TBL_OFFSET_QUEUE(nq) -
				SCD_CONTEXT_MEM_LOWER_BOUND) / sizeof(u32);

	/* make sure all queue are not stopped/used */
	memset(trans->txqs.queue_stopped, 0,
	       sizeof(trans->txqs.queue_stopped));
	memset(trans->txqs.queue_used, 0, sizeof(trans->txqs.queue_used));

	trans_pcie->scd_base_addr =
		iwl_read_prph(trans, SCD_SRAM_BASE_ADDR);

	WARN_ON(scd_base_addr != 0 &&
		scd_base_addr != trans_pcie->scd_base_addr);

	/* reset context data, TX status and translation data */
	iwl_trans_write_mem(trans, trans_pcie->scd_base_addr +
				   SCD_CONTEXT_MEM_LOWER_BOUND,
			    NULL, clear_dwords);

	iwl_write_prph(trans, SCD_DRAM_BASE_ADDR,
		       trans->txqs.scd_bc_tbls.dma >> 10);

	/* The chain extension of the SCD doesn't work well. This feature is
	 * enabled by default by the HW, so we need to disable it manually.
	 */
	if (trans->trans_cfg->base_params->scd_chain_ext_wa)
		iwl_write_prph(trans, SCD_CHAINEXT_EN, 0);

	iwl_trans_ac_txq_enable(trans, trans->txqs.cmd.q_id,
				trans->txqs.cmd.fifo,
				trans->txqs.cmd.wdg_timeout);

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
	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_8000)
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
	if (WARN_ON_ONCE(trans->trans_cfg->gen2))
		return;

	for (txq_id = 0; txq_id < trans->trans_cfg->base_params->num_of_queues;
	     txq_id++) {
		struct iwl_txq *txq = trans->txqs.txq[txq_id];
		if (trans->trans_cfg->gen2)
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
	iwl_pcie_tx_start(trans, 0);
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
	ret = iwl_poll_bit(trans, FH_TSSR_TX_STATUS_REG, mask, mask, 5000);
	if (ret < 0)
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
	memset(trans->txqs.queue_stopped, 0,
	       sizeof(trans->txqs.queue_stopped));
	memset(trans->txqs.queue_used, 0, sizeof(trans->txqs.queue_used));

	/* This can happen: start_hw, stop_device */
	if (!trans_pcie->txq_memory)
		return 0;

	/* Unmap DMA from host system and free skb's */
	for (txq_id = 0; txq_id < trans->trans_cfg->base_params->num_of_queues;
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

	memset(trans->txqs.queue_used, 0, sizeof(trans->txqs.queue_used));

	/* Tx queues */
	if (trans_pcie->txq_memory) {
		for (txq_id = 0;
		     txq_id < trans->trans_cfg->base_params->num_of_queues;
		     txq_id++) {
			iwl_pcie_txq_free(trans, txq_id);
			trans->txqs.txq[txq_id] = NULL;
		}
	}

	kfree(trans_pcie->txq_memory);
	trans_pcie->txq_memory = NULL;

	iwl_pcie_free_dma_ptr(trans, &trans_pcie->kw);

	iwl_pcie_free_dma_ptr(trans, &trans->txqs.scd_bc_tbls);
}

/*
 * iwl_pcie_tx_alloc - allocate TX context
 * Allocate all Tx DMA structures and initialize them
 */
static int iwl_pcie_tx_alloc(struct iwl_trans *trans)
{
	int ret;
	int txq_id, slots_num;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u16 bc_tbls_size = trans->trans_cfg->base_params->num_of_queues;

	if (WARN_ON(trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210))
		return -EINVAL;

	bc_tbls_size *= sizeof(struct iwlagn_scd_bc_tbl);

	/*It is not allowed to alloc twice, so warn when this happens.
	 * We cannot rely on the previous allocation, so free and fail */
	if (WARN_ON(trans_pcie->txq_memory)) {
		ret = -EINVAL;
		goto error;
	}

	ret = iwl_pcie_alloc_dma_ptr(trans, &trans->txqs.scd_bc_tbls,
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
		kcalloc(trans->trans_cfg->base_params->num_of_queues,
			sizeof(struct iwl_txq), GFP_KERNEL);
	if (!trans_pcie->txq_memory) {
		IWL_ERR(trans, "Not enough memory for txq\n");
		ret = -ENOMEM;
		goto error;
	}

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < trans->trans_cfg->base_params->num_of_queues;
	     txq_id++) {
		bool cmd_queue = (txq_id == trans->txqs.cmd.q_id);

		if (cmd_queue)
			slots_num = max_t(u32, IWL_CMD_QUEUE_SIZE,
					  trans->cfg->min_txq_size);
		else
			slots_num = max_t(u32, IWL_DEFAULT_QUEUE_SIZE,
					  trans->cfg->min_ba_txq_size);
		trans->txqs.txq[txq_id] = &trans_pcie->txq_memory[txq_id];
		ret = iwl_txq_alloc(trans, trans->txqs.txq[txq_id], slots_num,
				    cmd_queue);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue alloc failed\n", txq_id);
			goto error;
		}
		trans->txqs.txq[txq_id]->id = txq_id;
	}

	return 0;

error:
	iwl_pcie_tx_free(trans);

	return ret;
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
	for (txq_id = 0; txq_id < trans->trans_cfg->base_params->num_of_queues;
	     txq_id++) {
		bool cmd_queue = (txq_id == trans->txqs.cmd.q_id);

		if (cmd_queue)
			slots_num = max_t(u32, IWL_CMD_QUEUE_SIZE,
					  trans->cfg->min_txq_size);
		else
			slots_num = max_t(u32, IWL_DEFAULT_QUEUE_SIZE,
					  trans->cfg->min_ba_txq_size);
		ret = iwl_txq_init(trans, trans->txqs.txq[txq_id], slots_num,
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
				   trans->txqs.txq[txq_id]->dma_addr >> 8);
	}

	iwl_set_bits_prph(trans, SCD_GP_CTRL, SCD_GP_CTRL_AUTO_ACTIVE_MODE);
	if (trans->trans_cfg->base_params->num_of_queues > 20)
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

	if (!trans->trans_cfg->base_params->apmg_wake_up_wa)
		return 0;

	/*
	 * wake up the NIC to make sure that the firmware will see the host
	 * command - we will let the NIC sleep once all the host commands
	 * returned. This needs to be done only on NICs that have
	 * apmg_wake_up_wa set (see above.)
	 */
	if (!_iwl_trans_pcie_grab_nic_access(trans))
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

/*
 * iwl_pcie_cmdq_reclaim - Reclaim TX command queue entries already Tx'd
 *
 * When FW advances 'R' index, all entries between old and new 'R' index
 * need to be reclaimed. As result, some free space forms.  If there is
 * enough free space (> low mark), wake the stack that feeds us.
 */
static void iwl_pcie_cmdq_reclaim(struct iwl_trans *trans, int txq_id, int idx)
{
	struct iwl_txq *txq = trans->txqs.txq[txq_id];
	int nfreed = 0;
	u16 r;

	lockdep_assert_held(&txq->lock);

	idx = iwl_txq_get_cmd_index(txq, idx);
	r = iwl_txq_get_cmd_index(txq, txq->read_ptr);

	if (idx >= trans->trans_cfg->base_params->max_tfd_queue_size ||
	    (!iwl_txq_used(txq, idx))) {
		WARN_ONCE(test_bit(txq_id, trans->txqs.queue_used),
			  "%s: Read index for DMA queue txq id (%d), index %d is out of range [0-%d] %d %d.\n",
			  __func__, txq_id, idx,
			  trans->trans_cfg->base_params->max_tfd_queue_size,
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
	struct iwl_txq *txq = trans->txqs.txq[txq_id];
	int fifo = -1;
	bool scd_bug = false;

	if (test_and_set_bit(txq_id, trans->txqs.queue_used))
		WARN_ONCE(1, "queue %d already used - expect issues", txq_id);

	txq->wd_timeout = msecs_to_jiffies(wdg_timeout);

	if (cfg) {
		fifo = cfg->fifo;

		/* Disable the scheduler prior configuring the cmd queue */
		if (txq_id == trans->txqs.cmd.q_id &&
		    trans_pcie->scd_set_active)
			iwl_scd_enable_set_active(trans, 0);

		/* Stop this Tx queue before configuring it */
		iwl_scd_txq_set_inactive(trans, txq_id);

		/* Set this queue as a chain-building queue unless it is CMD */
		if (txq_id != trans->txqs.cmd.q_id)
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
		scd_bug = !trans->trans_cfg->mq_rx_supported &&
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
		if (txq_id == trans->txqs.cmd.q_id &&
		    trans_pcie->scd_set_active)
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
	struct iwl_txq *txq = trans->txqs.txq[txq_id];

	txq->ampdu = !shared_mode;
}

void iwl_trans_pcie_txq_disable(struct iwl_trans *trans, int txq_id,
				bool configure_scd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 stts_addr = trans_pcie->scd_base_addr +
			SCD_TX_STTS_QUEUE_OFFSET(txq_id);
	static const u32 zero_val[4] = {};

	trans->txqs.txq[txq_id]->frozen_expiry_remainder = 0;
	trans->txqs.txq[txq_id]->frozen = false;

	/*
	 * Upon HW Rfkill - we stop the device, and then stop the queues
	 * in the op_mode. Just for the sake of the simplicity of the op_mode,
	 * allow the op_mode to call txq_disable after it already called
	 * stop_device.
	 */
	if (!test_and_clear_bit(txq_id, trans->txqs.queue_used)) {
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
	trans->txqs.txq[txq_id]->ampdu = false;

	IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", txq_id);
}

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

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
	struct iwl_txq *txq = trans->txqs.txq[trans->txqs.cmd.q_id];
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

	if (WARN(!trans->wide_cmd_header &&
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
		iwl_op_mode_cmd_queue_full(trans->op_mode);
		idx = -ENOSPC;
		goto free_dup_buf;
	}

	idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	out_cmd = txq->entries[idx].cmd;
	out_meta = &txq->entries[idx].meta;

	memset(out_meta, 0, sizeof(*out_meta));	/* re-initialize to NULL */
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
			cpu_to_le16(QUEUE_TO_SEQ(trans->txqs.cmd.q_id) |
						 INDEX_TO_SEQ(txq->write_ptr));

		cmd_pos = sizeof(struct iwl_cmd_header_wide);
		copy_size = sizeof(struct iwl_cmd_header_wide);
	} else {
		out_cmd->hdr.cmd = iwl_cmd_opcode(cmd->id);
		out_cmd->hdr.sequence =
			cpu_to_le16(QUEUE_TO_SEQ(trans->txqs.cmd.q_id) |
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
		     cmd_size, txq->write_ptr, idx, trans->txqs.cmd.q_id);

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
	struct iwl_txq *txq = trans->txqs.txq[trans->txqs.cmd.q_id];

	/* If a Tx command is being handled and it isn't in the actual
	 * command queue then there a command routing bug has been introduced
	 * in the queue management code. */
	if (WARN(txq_id != trans->txqs.cmd.q_id,
		 "wrong command queue %d (should be %d), sequence 0x%X readp=%d writep=%d\n",
		 txq_id, trans->txqs.cmd.q_id, sequence, txq->read_ptr,
		 txq->write_ptr)) {
		iwl_print_hex_error(trans, pkt, 32);
		return;
	}

	spin_lock_bh(&txq->lock);

	cmd_index = iwl_txq_get_cmd_index(txq, index);
	cmd = txq->entries[cmd_index].cmd;
	meta = &txq->entries[cmd_index].meta;
	group_id = cmd->hdr.group_id;
	cmd_id = WIDE_ID(group_id, cmd->hdr.cmd);

	if (trans->trans_cfg->gen2)
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

	if (meta->flags & CMD_WANT_ASYNC_CALLBACK)
		iwl_op_mode_async_cb(trans->op_mode, cmd);

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
		wake_up(&trans->wait_command_queue);
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
static int iwl_fill_data_tbs_amsdu(struct iwl_trans *trans, struct sk_buff *skb,
				   struct iwl_txq *txq, u8 hdr_len,
				   struct iwl_cmd_meta *out_meta,
				   struct iwl_device_tx_cmd *dev_cmd,
				   u16 tb1_len)
{
	struct iwl_tx_cmd *tx_cmd = (void *)dev_cmd->payload;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int snap_ip_tcp_hdrlen, ip_hdrlen, total_len, hdr_room;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	u16 length, iv_len, amsdu_pad;
	u8 *start_hdr;
	struct iwl_tso_hdr_page *hdr_page;
	struct tso_t tso;

	/* if the packet is protected, then it must be CCMP or GCMP */
	BUILD_BUG_ON(IEEE80211_CCMP_HDR_LEN != IEEE80211_GCMP_HDR_LEN);
	iv_len = ieee80211_has_protected(hdr->frame_control) ?
		IEEE80211_CCMP_HDR_LEN : 0;

	trace_iwlwifi_dev_tx(trans->dev, skb,
			     iwl_txq_get_tfd(trans, txq, txq->write_ptr),
			     trans->txqs.tfd.size,
			     &dev_cmd->hdr, IWL_FIRST_TB_SIZE + tb1_len, 0);

	ip_hdrlen = skb_transport_header(skb) - skb_network_header(skb);
	snap_ip_tcp_hdrlen = 8 + ip_hdrlen + tcp_hdrlen(skb);
	total_len = skb->len - snap_ip_tcp_hdrlen - hdr_len - iv_len;
	amsdu_pad = 0;

	/* total amount of header we may need for this A-MSDU */
	hdr_room = DIV_ROUND_UP(total_len, mss) *
		(3 + snap_ip_tcp_hdrlen + sizeof(struct ethhdr)) + iv_len;

	/* Our device supports 9 segments at most, it will fit in 1 page */
	hdr_page = get_page_hdr(trans, hdr_room, skb);
	if (!hdr_page)
		return -ENOMEM;

	start_hdr = hdr_page->pos;
	memcpy(hdr_page->pos, skb->data + hdr_len, iv_len);
	hdr_page->pos += iv_len;

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
	le16_add_cpu(&tx_cmd->len, -snap_ip_tcp_hdrlen);

	tso_start(skb, &tso);

	while (total_len) {
		/* this is the data left for this subframe */
		unsigned int data_left =
			min_t(unsigned int, mss, total_len);
		unsigned int hdr_tb_len;
		dma_addr_t hdr_tb_phys;
		u8 *subf_hdrs_start = hdr_page->pos;

		total_len -= data_left;

		memset(hdr_page->pos, 0, amsdu_pad);
		hdr_page->pos += amsdu_pad;
		amsdu_pad = (4 - (sizeof(struct ethhdr) + snap_ip_tcp_hdrlen +
				  data_left)) & 0x3;
		ether_addr_copy(hdr_page->pos, ieee80211_get_DA(hdr));
		hdr_page->pos += ETH_ALEN;
		ether_addr_copy(hdr_page->pos, ieee80211_get_SA(hdr));
		hdr_page->pos += ETH_ALEN;

		length = snap_ip_tcp_hdrlen + data_left;
		*((__be16 *)hdr_page->pos) = cpu_to_be16(length);
		hdr_page->pos += sizeof(length);

		/*
		 * This will copy the SNAP as well which will be considered
		 * as MAC header.
		 */
		tso_build_hdr(skb, hdr_page->pos, &tso, data_left, !total_len);

		hdr_page->pos += snap_ip_tcp_hdrlen;

		hdr_tb_len = hdr_page->pos - start_hdr;
		hdr_tb_phys = dma_map_single(trans->dev, start_hdr,
					     hdr_tb_len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(trans->dev, hdr_tb_phys)))
			return -EINVAL;
		iwl_pcie_txq_build_tfd(trans, txq, hdr_tb_phys,
				       hdr_tb_len, false);
		trace_iwlwifi_dev_tx_tb(trans->dev, skb, start_hdr,
					hdr_tb_phys, hdr_tb_len);
		/* add this subframe's headers' length to the tx_cmd */
		le16_add_cpu(&tx_cmd->len, hdr_page->pos - subf_hdrs_start);

		/* prepare the start_hdr for the next subframe */
		start_hdr = hdr_page->pos;

		/* put the payload */
		while (data_left) {
			unsigned int size = min_t(unsigned int, tso.size,
						  data_left);
			dma_addr_t tb_phys;

			tb_phys = dma_map_single(trans->dev, tso.data,
						 size, DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
				return -EINVAL;

			iwl_pcie_txq_build_tfd(trans, txq, tb_phys,
					       size, false);
			trace_iwlwifi_dev_tx_tb(trans->dev, skb, tso.data,
						tb_phys, size);

			data_left -= size;
			tso_build_data(skb, &tso, size);
		}
	}

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

int iwl_trans_pcie_tx(struct iwl_trans *trans, struct sk_buff *skb,
		      struct iwl_device_tx_cmd *dev_cmd, int txq_id)
{
	struct ieee80211_hdr *hdr;
	struct iwl_tx_cmd *tx_cmd = (struct iwl_tx_cmd *)dev_cmd->payload;
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

	txq = trans->txqs.txq[txq_id];

	if (WARN_ONCE(!test_bit(txq_id, trans->txqs.queue_used),
		      "TX on unused queue %d\n", txq_id))
		return -EINVAL;

	if (skb_is_nonlinear(skb) &&
	    skb_shinfo(skb)->nr_frags > IWL_TRANS_MAX_FRAGS(trans) &&
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
					       trans->txqs.dev_cmd_offs);

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
		       offsetof(struct iwl_tx_cmd, scratch);

	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = iwl_get_dma_hi_addr(scratch_phys);

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_meta = &txq->entries[txq->write_ptr].meta;
	out_meta->flags = 0;

	/*
	 * The second TB (tb1) points to the remainder of the TX command
	 * and the 802.11 header - dword aligned size
	 * (This calculation modifies the TX command, so do it before the
	 * setup of the first TB)
	 */
	len = sizeof(struct iwl_tx_cmd) + sizeof(struct iwl_cmd_header) +
	      hdr_len - IWL_FIRST_TB_SIZE;
	/* do not align A-MSDU to dword as the subframe header aligns it */
	amsdu = ieee80211_is_data_qos(fc) &&
		(*ieee80211_get_qos_ctl(hdr) &
		 IEEE80211_QOS_CTL_A_MSDU_PRESENT);
	if (!amsdu) {
		tb1_len = ALIGN(len, 4);
		/* Tell NIC about any 2-byte padding after MAC header */
		if (tb1_len != len)
			tx_cmd->tx_flags |= cpu_to_le32(TX_CMD_FLG_MH_PAD);
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
	BUILD_BUG_ON(sizeof(struct iwl_tx_cmd) < IWL_FIRST_TB_SIZE);
	BUILD_BUG_ON(sizeof(struct iwl_cmd_header) +
		     offsetofend(struct iwl_tx_cmd, scratch) >
		     IWL_FIRST_TB_SIZE);

	/* map the data for TB1 */
	tb1_addr = ((u8 *)&dev_cmd->hdr) + IWL_FIRST_TB_SIZE;
	tb1_phys = dma_map_single(trans->dev, tb1_addr, tb1_len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(trans->dev, tb1_phys)))
		goto out_err;
	iwl_pcie_txq_build_tfd(trans, txq, tb1_phys, tb1_len, false);

	trace_iwlwifi_dev_tx(trans->dev, skb,
			     iwl_txq_get_tfd(trans, txq, txq->write_ptr),
			     trans->txqs.tfd.size,
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
	iwl_txq_gen1_update_byte_cnt_tbl(trans, txq, le16_to_cpu(tx_cmd->len),
					 iwl_txq_gen1_tfd_get_num_tbs(trans,
								      tfd));

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
