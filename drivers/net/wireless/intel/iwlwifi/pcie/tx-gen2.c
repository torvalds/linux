/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include "iwl-debug.h"
#include "iwl-csr.h"
#include "iwl-io.h"
#include "internal.h"
#include "mvm/fw-api.h"

/*
 * iwl_pcie_txq_update_byte_tbl - Set up entry in Tx byte-count array
 */
static void iwl_pcie_gen2_update_byte_tbl(struct iwl_trans *trans,
					  struct iwl_txq *txq, u16 byte_cnt,
					   int num_tbs)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int write_ptr = txq->write_ptr;
	u8 filled_tfd_size, num_fetch_chunks;
	u16 len = byte_cnt;
	__le16 bc_ent;

	scd_bc_tbl = trans_pcie->scd_bc_tbls.addr;

	len = DIV_ROUND_UP(len, 4);

	if (WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX))
		return;

	filled_tfd_size = offsetof(struct iwl_tfh_tfd, tbs) +
				   num_tbs * sizeof(struct iwl_tfh_tb);
	/*
	 * filled_tfd_size contains the number of filled bytes in the TFD.
	 * Dividing it by 64 will give the number of chunks to fetch
	 * to SRAM- 0 for one chunk, 1 for 2 and so on.
	 * If, for example, TFD contains only 3 TBs then 32 bytes
	 * of the TFD are used, and only one chunk of 64 bytes should
	 * be fetched
	 */
	num_fetch_chunks = DIV_ROUND_UP(filled_tfd_size, 64) - 1;

	bc_ent = cpu_to_le16(len | (num_fetch_chunks << 12));
	scd_bc_tbl[txq->id].tfd_offset[write_ptr] = bc_ent;
}

/*
 * iwl_pcie_gen2_txq_inc_wr_ptr - Send new write index to hardware
 */
static void iwl_pcie_gen2_txq_inc_wr_ptr(struct iwl_trans *trans,
					 struct iwl_txq *txq)
{
	lockdep_assert_held(&txq->lock);

	IWL_DEBUG_TX(trans, "Q:%d WR: 0x%x\n", txq->id, txq->write_ptr);

	/*
	 * if not in power-save mode, uCode will never sleep when we're
	 * trying to tx (during RFKILL, we're not trying to tx).
	 */
	if (!txq->block)
		iwl_write32(trans, HBUS_TARG_WRPTR,
			    txq->write_ptr | (txq->id << 8));
}

static u8 iwl_pcie_gen2_get_num_tbs(struct iwl_trans *trans,
				    struct iwl_tfh_tfd *tfd)
{
	return le16_to_cpu(tfd->num_tbs) & 0x1f;
}

static void iwl_pcie_gen2_tfd_unmap(struct iwl_trans *trans,
				    struct iwl_cmd_meta *meta,
				    struct iwl_tfh_tfd *tfd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i, num_tbs;

	/* Sanity check on number of chunks */
	num_tbs = iwl_pcie_gen2_get_num_tbs(trans, tfd);

	if (num_tbs >= trans_pcie->max_tbs) {
		IWL_ERR(trans, "Too many chunks: %i\n", num_tbs);
		return;
	}

	/* first TB is never freed - it's the bidirectional DMA data */
	for (i = 1; i < num_tbs; i++) {
		if (meta->tbs & BIT(i))
			dma_unmap_page(trans->dev,
				       le64_to_cpu(tfd->tbs[i].addr),
				       le16_to_cpu(tfd->tbs[i].tb_len),
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(trans->dev,
					 le64_to_cpu(tfd->tbs[i].addr),
					 le16_to_cpu(tfd->tbs[i].tb_len),
					 DMA_TO_DEVICE);
	}

	tfd->num_tbs = 0;
}

static void iwl_pcie_gen2_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	/* rd_ptr is bounded by TFD_QUEUE_SIZE_MAX and
	 * idx is bounded by n_window
	 */
	int rd_ptr = txq->read_ptr;
	int idx = get_cmd_index(txq, rd_ptr);

	lockdep_assert_held(&txq->lock);

	/* We have only q->n_window txq->entries, but we use
	 * TFD_QUEUE_SIZE_MAX tfds
	 */
	iwl_pcie_gen2_tfd_unmap(trans, &txq->entries[idx].meta,
				iwl_pcie_get_tfd(trans_pcie, txq, rd_ptr));

	/* free SKB */
	if (txq->entries) {
		struct sk_buff *skb;

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
}

static int iwl_pcie_gen2_set_tb(struct iwl_trans *trans,
				struct iwl_tfh_tfd *tfd, dma_addr_t addr,
				u16 len)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int idx = iwl_pcie_gen2_get_num_tbs(trans, tfd);
	struct iwl_tfh_tb *tb = &tfd->tbs[idx];

	/* Each TFD can point to a maximum max_tbs Tx buffers */
	if (tfd->num_tbs >= trans_pcie->max_tbs) {
		IWL_ERR(trans, "Error can not send more than %d chunks\n",
			trans_pcie->max_tbs);
		return -EINVAL;
	}

	put_unaligned_le64(addr, &tb->addr);
	tb->tb_len = cpu_to_le16(len);

	tfd->num_tbs = cpu_to_le16(idx + 1);

	return idx;
}

static
struct iwl_tfh_tfd *iwl_pcie_gen2_build_tfd(struct iwl_trans *trans,
					    struct iwl_txq *txq,
					    struct iwl_device_cmd *dev_cmd,
					    struct sk_buff *skb,
					    struct iwl_cmd_meta *out_meta)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_tfh_tfd *tfd =
		iwl_pcie_get_tfd(trans_pcie, txq, txq->write_ptr);
	dma_addr_t tb_phys;
	int i, len, tb1_len, tb2_len, hdr_len;
	void *tb1_addr;

	memset(tfd, 0, sizeof(*tfd));

	tb_phys = iwl_pcie_get_first_tb_dma(txq, txq->write_ptr);
	/* The first TB points to bi-directional DMA data */
	memcpy(&txq->first_tb_bufs[txq->write_ptr], &dev_cmd->hdr,
	       IWL_FIRST_TB_SIZE);

	iwl_pcie_gen2_set_tb(trans, tfd, tb_phys, IWL_FIRST_TB_SIZE);

	/* there must be data left over for TB1 or this code must be changed */
	BUILD_BUG_ON(sizeof(struct iwl_tx_cmd_gen2) < IWL_FIRST_TB_SIZE);

	/*
	 * The second TB (tb1) points to the remainder of the TX command
	 * and the 802.11 header - dword aligned size
	 * (This calculation modifies the TX command, so do it before the
	 * setup of the first TB)
	 */
	len = sizeof(struct iwl_tx_cmd_gen2) + sizeof(struct iwl_cmd_header) +
	      ieee80211_hdrlen(hdr->frame_control) - IWL_FIRST_TB_SIZE;

	tb1_len = ALIGN(len, 4);

	/* map the data for TB1 */
	tb1_addr = ((u8 *)&dev_cmd->hdr) + IWL_FIRST_TB_SIZE;
	tb_phys = dma_map_single(trans->dev, tb1_addr, tb1_len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
		goto out_err;
	iwl_pcie_gen2_set_tb(trans, tfd, tb_phys, tb1_len);

	/* set up TFD's third entry to point to remainder of skb's head */
	hdr_len = ieee80211_hdrlen(hdr->frame_control);
	tb2_len = skb_headlen(skb) - hdr_len;

	if (tb2_len > 0) {
		tb_phys = dma_map_single(trans->dev, skb->data + hdr_len,
					 tb2_len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
			goto out_err;
		iwl_pcie_gen2_set_tb(trans, tfd, tb_phys, tb2_len);
	}

	/* set up the remaining entries to point to the data */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int tb_idx;

		if (!skb_frag_size(frag))
			continue;

		tb_phys = skb_frag_dma_map(trans->dev, frag, 0,
					   skb_frag_size(frag), DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
			goto out_err;
		tb_idx = iwl_pcie_gen2_set_tb(trans, tfd, tb_phys,
					      skb_frag_size(frag));

		out_meta->tbs |= BIT(tb_idx);
	}

	trace_iwlwifi_dev_tx(trans->dev, skb, tfd, sizeof(*tfd), &dev_cmd->hdr,
			     IWL_FIRST_TB_SIZE + tb1_len,
			     skb->data + hdr_len, tb2_len);
	trace_iwlwifi_dev_tx_data(trans->dev, skb, hdr_len,
				  skb->len - hdr_len);

	return tfd;

out_err:
	iwl_pcie_gen2_tfd_unmap(trans, out_meta, tfd);
	return NULL;
}

int iwl_trans_pcie_gen2_tx(struct iwl_trans *trans, struct sk_buff *skb,
			   struct iwl_device_cmd *dev_cmd, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_cmd_gen2 *tx_cmd = (void *)dev_cmd->payload;
	struct iwl_cmd_meta *out_meta;
	struct iwl_txq *txq = &trans_pcie->txq[txq_id];
	void *tfd;

	if (WARN_ONCE(!test_bit(txq_id, trans_pcie->queue_used),
		      "TX on unused queue %d\n", txq_id))
		return -EINVAL;

	if (skb_is_nonlinear(skb) &&
	    skb_shinfo(skb)->nr_frags > IWL_PCIE_MAX_FRAGS(trans_pcie) &&
	    __skb_linearize(skb))
		return -ENOMEM;

	spin_lock(&txq->lock);

	/* Set up driver data for this TFD */
	txq->entries[txq->write_ptr].skb = skb;
	txq->entries[txq->write_ptr].cmd = dev_cmd;

	dev_cmd->hdr.sequence =
		cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
			    INDEX_TO_SEQ(txq->write_ptr)));

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_meta = &txq->entries[txq->write_ptr].meta;
	out_meta->flags = 0;

	tfd = iwl_pcie_gen2_build_tfd(trans, txq, dev_cmd, skb, out_meta);
	if (!tfd) {
		spin_unlock(&txq->lock);
		return -1;
	}

	/* Set up entry for this TFD in Tx byte-count array */
	iwl_pcie_gen2_update_byte_tbl(trans, txq, le16_to_cpu(tx_cmd->len),
				      iwl_pcie_gen2_get_num_tbs(trans, tfd));

	/* start timer if queue currently empty */
	if (txq->read_ptr == txq->write_ptr) {
		if (txq->wd_timeout) {
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
		IWL_DEBUG_RPM(trans, "Q: %d first tx - take ref\n", txq->id);
		iwl_trans_ref(trans);
	}

	/* Tell device the write index *just past* this latest filled TFD */
	txq->write_ptr = iwl_queue_inc_wrap(txq->write_ptr);
	iwl_pcie_gen2_txq_inc_wr_ptr(trans, txq);
	if (iwl_queue_space(txq) < txq->high_mark)
		iwl_stop_queue(trans, txq);

	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually.
	 */
	spin_unlock(&txq->lock);
	return 0;
}

/*
 * iwl_pcie_gen2_txq_unmap -  Unmap any remaining DMA mappings and free skb's
 */
void iwl_pcie_gen2_txq_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = &trans_pcie->txq[txq_id];

	spin_lock_bh(&txq->lock);
	while (txq->write_ptr != txq->read_ptr) {
		IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n",
				   txq_id, txq->read_ptr);

		iwl_pcie_gen2_free_tfd(trans, txq);
		txq->read_ptr = iwl_queue_inc_wrap(txq->read_ptr);

		if (txq->read_ptr == txq->write_ptr) {
			unsigned long flags;

			spin_lock_irqsave(&trans_pcie->reg_lock, flags);
			if (txq_id != trans_pcie->cmd_queue) {
				IWL_DEBUG_RPM(trans, "Q %d - last tx freed\n",
					      txq->id);
				iwl_trans_unref(trans);
			} else if (trans_pcie->ref_cmd_in_flight) {
				trans_pcie->ref_cmd_in_flight = false;
				IWL_DEBUG_RPM(trans,
					      "clear ref_cmd_in_flight\n");
				iwl_trans_unref(trans);
			}
			spin_unlock_irqrestore(&trans_pcie->reg_lock, flags);
		}
	}
	spin_unlock_bh(&txq->lock);

	/* just in case - this queue may have been stopped */
	iwl_wake_queue(trans, txq);
}

int iwl_trans_pcie_dyn_txq_alloc(struct iwl_trans *trans,
				 struct iwl_tx_queue_cfg_cmd *cmd,
				 int cmd_id,
				 unsigned int timeout)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = &trans_pcie->txq[cmd->scd_queue];
	struct iwl_host_cmd hcmd = {
		.id = cmd_id,
		.len = { sizeof(*cmd) },
		.data = { cmd, },
		.flags = 0,
	};
	u16 ssn = le16_to_cpu(cmd->ssn);

	if (test_and_set_bit(cmd->scd_queue, trans_pcie->queue_used)) {
		WARN_ONCE(1, "queue %d already used", cmd->scd_queue);
		return -EINVAL;
	}

	txq->wd_timeout = msecs_to_jiffies(timeout);

	/*
	 * Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF)
	 */
	txq->read_ptr = (ssn & 0xff);
	txq->write_ptr = (ssn & 0xff);
	iwl_write_direct32(trans, HBUS_TARG_WRPTR,
			   (ssn & 0xff) | (cmd->scd_queue << 8));

	IWL_DEBUG_TX_QUEUES(trans, "Activate queue %d WrPtr: %d\n",
			    cmd->scd_queue, ssn & 0xff);

	cmd->tfdq_addr = cpu_to_le64(txq->dma_addr);
	cmd->byte_cnt_addr = cpu_to_le64(trans_pcie->scd_bc_tbls.dma +
					 cmd->scd_queue *
					 sizeof(struct iwlagn_scd_bc_tbl));
	cmd->cb_size = cpu_to_le64(TFD_QUEUE_CB_SIZE(TFD_QUEUE_SIZE_MAX));

	return iwl_trans_send_cmd(trans, &hcmd);
}

void iwl_trans_pcie_dyn_txq_free(struct iwl_trans *trans, int queue)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->txq[queue].frozen_expiry_remainder = 0;
	trans_pcie->txq[queue].frozen = false;

	/*
	 * Upon HW Rfkill - we stop the device, and then stop the queues
	 * in the op_mode. Just for the sake of the simplicity of the op_mode,
	 * allow the op_mode to call txq_disable after it already called
	 * stop_device.
	 */
	if (!test_and_clear_bit(queue, trans_pcie->queue_used)) {
		WARN_ONCE(test_bit(STATUS_DEVICE_ENABLED, &trans->status),
			  "queue %d not used", queue);
		return;
	}

	iwl_pcie_gen2_txq_unmap(trans, queue);

	IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", queue);
}

