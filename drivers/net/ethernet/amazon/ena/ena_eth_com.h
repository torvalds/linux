/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef ENA_ETH_COM_H_
#define ENA_ETH_COM_H_

#include "ena_com.h"

/* we allow 2 DMA descriptors per LLQ entry */
#define ENA_LLQ_ENTRY_DESC_CHUNK_SIZE	(2 * sizeof(struct ena_eth_io_tx_desc))
#define ENA_LLQ_HEADER		(128UL - ENA_LLQ_ENTRY_DESC_CHUNK_SIZE)
#define ENA_LLQ_LARGE_HEADER	(256UL - ENA_LLQ_ENTRY_DESC_CHUNK_SIZE)

struct ena_com_tx_ctx {
	struct ena_com_tx_meta ena_meta;
	struct ena_com_buf *ena_bufs;
	/* For LLQ, header buffer - pushed to the device mem space */
	void *push_header;

	enum ena_eth_io_l3_proto_index l3_proto;
	enum ena_eth_io_l4_proto_index l4_proto;
	u16 num_bufs;
	u16 req_id;
	/* For regular queue, indicate the size of the header
	 * For LLQ, indicate the size of the pushed buffer
	 */
	u16 header_len;

	u8 meta_valid;
	u8 tso_enable;
	u8 l3_csum_enable;
	u8 l4_csum_enable;
	u8 l4_csum_partial;
	u8 df; /* Don't fragment */
};

struct ena_com_rx_ctx {
	struct ena_com_rx_buf_info *ena_bufs;
	enum ena_eth_io_l3_proto_index l3_proto;
	enum ena_eth_io_l4_proto_index l4_proto;
	bool l3_csum_err;
	bool l4_csum_err;
	u8 l4_csum_checked;
	/* fragmented packet */
	bool frag;
	u32 hash;
	u16 descs;
	int max_bufs;
	u8 pkt_offset;
};

int ena_com_prepare_tx(struct ena_com_io_sq *io_sq,
		       struct ena_com_tx_ctx *ena_tx_ctx,
		       int *nb_hw_desc);

int ena_com_rx_pkt(struct ena_com_io_cq *io_cq,
		   struct ena_com_io_sq *io_sq,
		   struct ena_com_rx_ctx *ena_rx_ctx);

int ena_com_add_single_rx_desc(struct ena_com_io_sq *io_sq,
			       struct ena_com_buf *ena_buf,
			       u16 req_id);

bool ena_com_cq_empty(struct ena_com_io_cq *io_cq);

static inline void ena_com_unmask_intr(struct ena_com_io_cq *io_cq,
				       struct ena_eth_io_intr_reg *intr_reg)
{
	writel(intr_reg->intr_control, io_cq->unmask_reg);
}

static inline int ena_com_free_q_entries(struct ena_com_io_sq *io_sq)
{
	u16 tail, next_to_comp, cnt;

	next_to_comp = io_sq->next_to_comp;
	tail = io_sq->tail;
	cnt = tail - next_to_comp;

	return io_sq->q_depth - 1 - cnt;
}

/* Check if the submission queue has enough space to hold required_buffers */
static inline bool ena_com_sq_have_enough_space(struct ena_com_io_sq *io_sq,
						u16 required_buffers)
{
	int temp;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
		return ena_com_free_q_entries(io_sq) >= required_buffers;

	/* This calculation doesn't need to be 100% accurate. So to reduce
	 * the calculation overhead just Subtract 2 lines from the free descs
	 * (one for the header line and one to compensate the devision
	 * down calculation.
	 */
	temp = required_buffers / io_sq->llq_info.descs_per_entry + 2;

	return ena_com_free_q_entries(io_sq) > temp;
}

static inline bool ena_com_meta_desc_changed(struct ena_com_io_sq *io_sq,
					     struct ena_com_tx_ctx *ena_tx_ctx)
{
	if (!ena_tx_ctx->meta_valid)
		return false;

	return !!memcmp(&io_sq->cached_tx_meta,
			&ena_tx_ctx->ena_meta,
			sizeof(struct ena_com_tx_meta));
}

static inline bool is_llq_max_tx_burst_exists(struct ena_com_io_sq *io_sq)
{
	return (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) &&
	       io_sq->llq_info.max_entries_in_tx_burst > 0;
}

static inline bool ena_com_is_doorbell_needed(struct ena_com_io_sq *io_sq,
					      struct ena_com_tx_ctx *ena_tx_ctx)
{
	struct ena_com_llq_info *llq_info;
	int descs_after_first_entry;
	int num_entries_needed = 1;
	u16 num_descs;

	if (!is_llq_max_tx_burst_exists(io_sq))
		return false;

	llq_info = &io_sq->llq_info;
	num_descs = ena_tx_ctx->num_bufs;

	if (llq_info->disable_meta_caching ||
	    unlikely(ena_com_meta_desc_changed(io_sq, ena_tx_ctx)))
		++num_descs;

	if (num_descs > llq_info->descs_num_before_header) {
		descs_after_first_entry = num_descs - llq_info->descs_num_before_header;
		num_entries_needed += DIV_ROUND_UP(descs_after_first_entry,
						   llq_info->descs_per_entry);
	}

	netdev_dbg(ena_com_io_sq_to_ena_dev(io_sq)->net_device,
		   "Queue: %d num_descs: %d num_entries_needed: %d\n", io_sq->qid, num_descs,
		   num_entries_needed);

	return num_entries_needed > io_sq->entries_in_tx_burst_left;
}

static inline int ena_com_write_sq_doorbell(struct ena_com_io_sq *io_sq)
{
	u16 max_entries_in_tx_burst = io_sq->llq_info.max_entries_in_tx_burst;
	u16 tail = io_sq->tail;

	netdev_dbg(ena_com_io_sq_to_ena_dev(io_sq)->net_device,
		   "Write submission queue doorbell for queue: %d tail: %d\n", io_sq->qid, tail);

	writel(tail, io_sq->db_addr);

	if (is_llq_max_tx_burst_exists(io_sq)) {
		netdev_dbg(ena_com_io_sq_to_ena_dev(io_sq)->net_device,
			   "Reset available entries in tx burst for queue %d to %d\n", io_sq->qid,
			   max_entries_in_tx_burst);
		io_sq->entries_in_tx_burst_left = max_entries_in_tx_burst;
	}

	return 0;
}

static inline void ena_com_update_numa_node(struct ena_com_io_cq *io_cq,
					    u8 numa_node)
{
	struct ena_eth_io_numa_node_cfg_reg numa_cfg;

	if (!io_cq->numa_node_cfg_reg)
		return;

	numa_cfg.numa_cfg = (numa_node & ENA_ETH_IO_NUMA_NODE_CFG_REG_NUMA_MASK)
		| ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_MASK;

	writel(numa_cfg.numa_cfg, io_cq->numa_node_cfg_reg);
}

static inline void ena_com_comp_ack(struct ena_com_io_sq *io_sq, u16 elem)
{
	io_sq->next_to_comp += elem;
}

static inline void ena_com_cq_inc_head(struct ena_com_io_cq *io_cq)
{
	io_cq->head++;

	/* Switch phase bit in case of wrap around */
	if (unlikely((io_cq->head & (io_cq->q_depth - 1)) == 0))
		io_cq->phase ^= 1;
}

static inline int ena_com_tx_comp_req_id_get(struct ena_com_io_cq *io_cq,
					     u16 *req_id)
{
	u8 expected_phase, cdesc_phase;
	struct ena_eth_io_tx_cdesc *cdesc;
	u16 masked_head;

	masked_head = io_cq->head & (io_cq->q_depth - 1);
	expected_phase = io_cq->phase;

	cdesc = (struct ena_eth_io_tx_cdesc *)
		((uintptr_t)io_cq->cdesc_addr.virt_addr +
		(masked_head * io_cq->cdesc_entry_size_in_bytes));

	/* When the current completion descriptor phase isn't the same as the
	 * expected, it mean that the device still didn't update
	 * this completion.
	 */
	cdesc_phase = READ_ONCE(cdesc->flags) & ENA_ETH_IO_TX_CDESC_PHASE_MASK;
	if (cdesc_phase != expected_phase)
		return -EAGAIN;

	dma_rmb();

	*req_id = READ_ONCE(cdesc->req_id);
	if (unlikely(*req_id >= io_cq->q_depth)) {
		netdev_err(ena_com_io_cq_to_ena_dev(io_cq)->net_device, "Invalid req id %d\n",
			   cdesc->req_id);
		return -EINVAL;
	}

	ena_com_cq_inc_head(io_cq);

	return 0;
}

#endif /* ENA_ETH_COM_H_ */
