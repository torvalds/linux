/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2020 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2020 Intel Corporation
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
#ifndef __iwl_trans_queue_tx_h__
#define __iwl_trans_queue_tx_h__
#include "iwl-fh.h"
#include "fw/api/tx.h"

struct iwl_tso_hdr_page {
	struct page *page;
	u8 *pos;
};

static inline dma_addr_t
iwl_txq_get_first_tb_dma(struct iwl_txq *txq, int idx)
{
	return txq->first_tb_dma +
	       sizeof(struct iwl_pcie_first_tb_buf) * idx;
}

static inline u16 iwl_txq_get_cmd_index(const struct iwl_txq *q, u32 index)
{
	return index & (q->n_window - 1);
}

void iwl_txq_gen2_unmap(struct iwl_trans *trans, int txq_id);

static inline void iwl_wake_queue(struct iwl_trans *trans,
				  struct iwl_txq *txq)
{
	if (test_and_clear_bit(txq->id, trans->txqs.queue_stopped)) {
		IWL_DEBUG_TX_QUEUES(trans, "Wake hwq %d\n", txq->id);
		iwl_op_mode_queue_not_full(trans->op_mode, txq->id);
	}
}

static inline void *iwl_txq_get_tfd(struct iwl_trans *trans,
				    struct iwl_txq *txq, int idx)
{
	if (trans->trans_cfg->use_tfh)
		idx = iwl_txq_get_cmd_index(txq, idx);

	return txq->tfds + trans->txqs.tfd.size * idx;
}

int iwl_txq_alloc(struct iwl_trans *trans, struct iwl_txq *txq, int slots_num,
		  bool cmd_queue);
/*
 * We need this inline in case dma_addr_t is only 32-bits - since the
 * hardware is always 64-bit, the issue can still occur in that case,
 * so use u64 for 'phys' here to force the addition in 64-bit.
 */
static inline bool iwl_txq_crosses_4g_boundary(u64 phys, u16 len)
{
	return upper_32_bits(phys) != upper_32_bits(phys + len);
}

int iwl_txq_space(struct iwl_trans *trans, const struct iwl_txq *q);

static inline void iwl_txq_stop(struct iwl_trans *trans, struct iwl_txq *txq)
{
	if (!test_and_set_bit(txq->id, trans->txqs.queue_stopped)) {
		iwl_op_mode_queue_full(trans->op_mode, txq->id);
		IWL_DEBUG_TX_QUEUES(trans, "Stop hwq %d\n", txq->id);
	} else {
		IWL_DEBUG_TX_QUEUES(trans, "hwq %d already stopped\n",
				    txq->id);
	}
}

/**
 * iwl_txq_inc_wrap - increment queue index, wrap back to beginning
 * @index -- current index
 */
static inline int iwl_txq_inc_wrap(struct iwl_trans *trans, int index)
{
	return ++index &
		(trans->trans_cfg->base_params->max_tfd_queue_size - 1);
}

/**
 * iwl_txq_dec_wrap - decrement queue index, wrap back to end
 * @index -- current index
 */
static inline int iwl_txq_dec_wrap(struct iwl_trans *trans, int index)
{
	return --index &
		(trans->trans_cfg->base_params->max_tfd_queue_size - 1);
}

static inline bool iwl_txq_used(const struct iwl_txq *q, int i)
{
	int index = iwl_txq_get_cmd_index(q, i);
	int r = iwl_txq_get_cmd_index(q, q->read_ptr);
	int w = iwl_txq_get_cmd_index(q, q->write_ptr);

	return w >= r ?
		(index >= r && index < w) :
		!(index < r && index >= w);
}

void iwl_txq_free_tso_page(struct iwl_trans *trans, struct sk_buff *skb);

void iwl_txq_log_scd_error(struct iwl_trans *trans, struct iwl_txq *txq);

int iwl_txq_gen2_set_tb(struct iwl_trans *trans,
			struct iwl_tfh_tfd *tfd, dma_addr_t addr,
			u16 len);

void iwl_txq_gen2_tfd_unmap(struct iwl_trans *trans,
			    struct iwl_cmd_meta *meta,
			    struct iwl_tfh_tfd *tfd);

int iwl_txq_dyn_alloc(struct iwl_trans *trans,
		      __le16 flags, u8 sta_id, u8 tid,
		      int cmd_id, int size,
		      unsigned int timeout);

int iwl_txq_gen2_tx(struct iwl_trans *trans, struct sk_buff *skb,
		    struct iwl_device_tx_cmd *dev_cmd, int txq_id);

void iwl_txq_dyn_free(struct iwl_trans *trans, int queue);
void iwl_txq_gen2_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq);
void iwl_txq_inc_wr_ptr(struct iwl_trans *trans, struct iwl_txq *txq);
void iwl_txq_gen2_tx_stop(struct iwl_trans *trans);
void iwl_txq_gen2_tx_free(struct iwl_trans *trans);
int iwl_txq_init(struct iwl_trans *trans, struct iwl_txq *txq, int slots_num,
		 bool cmd_queue);
int iwl_txq_gen2_init(struct iwl_trans *trans, int txq_id, int queue_size);
#ifdef CONFIG_INET
struct iwl_tso_hdr_page *get_page_hdr(struct iwl_trans *trans, size_t len,
				      struct sk_buff *skb);
#endif
#endif /* __iwl_trans_queue_tx_h__ */
