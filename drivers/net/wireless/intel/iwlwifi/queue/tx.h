/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2020 Intel Corporation
 */
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
static inline u8 iwl_txq_gen1_tfd_get_num_tbs(struct iwl_trans *trans,
					      void *_tfd)
{
	struct iwl_tfd *tfd;

	if (trans->trans_cfg->use_tfh) {
		struct iwl_tfh_tfd *tfd = _tfd;

		return le16_to_cpu(tfd->num_tbs) & 0x1f;
	}

	tfd = (struct iwl_tfd *)_tfd;
	return tfd->num_tbs & 0x1f;
}

static inline u16 iwl_txq_gen1_tfd_tb_get_len(struct iwl_trans *trans,
					      void *_tfd, u8 idx)
{
	struct iwl_tfd *tfd;
	struct iwl_tfd_tb *tb;

	if (trans->trans_cfg->use_tfh) {
		struct iwl_tfh_tfd *tfd = _tfd;
		struct iwl_tfh_tb *tb = &tfd->tbs[idx];

		return le16_to_cpu(tb->tb_len);
	}

	tfd = (struct iwl_tfd *)_tfd;
	tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

void iwl_txq_gen1_tfd_unmap(struct iwl_trans *trans,
			    struct iwl_cmd_meta *meta,
			    struct iwl_txq *txq, int index);
void iwl_txq_gen1_inval_byte_cnt_tbl(struct iwl_trans *trans,
				     struct iwl_txq *txq);
void iwl_txq_gen1_update_byte_cnt_tbl(struct iwl_trans *trans,
				      struct iwl_txq *txq, u16 byte_cnt,
				      int num_tbs);
void iwl_txq_reclaim(struct iwl_trans *trans, int txq_id, int ssn,
		     struct sk_buff_head *skbs);
void iwl_txq_set_q_ptrs(struct iwl_trans *trans, int txq_id, int ptr);
void iwl_trans_txq_freeze_timer(struct iwl_trans *trans, unsigned long txqs,
				bool freeze);
void iwl_txq_progress(struct iwl_txq *txq);
void iwl_txq_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq);
#endif /* __iwl_trans_queue_tx_h__ */
