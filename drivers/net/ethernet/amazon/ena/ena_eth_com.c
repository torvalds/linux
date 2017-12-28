/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ena_eth_com.h"

static inline struct ena_eth_io_rx_cdesc_base *ena_com_get_next_rx_cdesc(
	struct ena_com_io_cq *io_cq)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;
	u16 expected_phase, head_masked;
	u16 desc_phase;

	head_masked = io_cq->head & (io_cq->q_depth - 1);
	expected_phase = io_cq->phase;

	cdesc = (struct ena_eth_io_rx_cdesc_base *)(io_cq->cdesc_addr.virt_addr
			+ (head_masked * io_cq->cdesc_entry_size_in_bytes));

	desc_phase = (READ_ONCE(cdesc->status) & ENA_ETH_IO_RX_CDESC_BASE_PHASE_MASK) >>
			ENA_ETH_IO_RX_CDESC_BASE_PHASE_SHIFT;

	if (desc_phase != expected_phase)
		return NULL;

	return cdesc;
}

static inline void ena_com_cq_inc_head(struct ena_com_io_cq *io_cq)
{
	io_cq->head++;

	/* Switch phase bit in case of wrap around */
	if (unlikely((io_cq->head & (io_cq->q_depth - 1)) == 0))
		io_cq->phase ^= 1;
}

static inline void *get_sq_desc(struct ena_com_io_sq *io_sq)
{
	u16 tail_masked;
	u32 offset;

	tail_masked = io_sq->tail & (io_sq->q_depth - 1);

	offset = tail_masked * io_sq->desc_entry_size;

	return (void *)((uintptr_t)io_sq->desc_addr.virt_addr + offset);
}

static inline void ena_com_copy_curr_sq_desc_to_dev(struct ena_com_io_sq *io_sq)
{
	u16 tail_masked = io_sq->tail & (io_sq->q_depth - 1);
	u32 offset = tail_masked * io_sq->desc_entry_size;

	/* In case this queue isn't a LLQ */
	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
		return;

	memcpy_toio(io_sq->desc_addr.pbuf_dev_addr + offset,
		    io_sq->desc_addr.virt_addr + offset,
		    io_sq->desc_entry_size);
}

static inline void ena_com_sq_update_tail(struct ena_com_io_sq *io_sq)
{
	io_sq->tail++;

	/* Switch phase bit in case of wrap around */
	if (unlikely((io_sq->tail & (io_sq->q_depth - 1)) == 0))
		io_sq->phase ^= 1;
}

static inline int ena_com_write_header(struct ena_com_io_sq *io_sq,
				       u8 *head_src, u16 header_len)
{
	u16 tail_masked = io_sq->tail & (io_sq->q_depth - 1);
	u8 __iomem *dev_head_addr =
		io_sq->header_addr + (tail_masked * io_sq->tx_max_header_size);

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
		return 0;

	if (unlikely(!io_sq->header_addr)) {
		pr_err("Push buffer header ptr is NULL\n");
		return -EINVAL;
	}

	memcpy_toio(dev_head_addr, head_src, header_len);

	return 0;
}

static inline struct ena_eth_io_rx_cdesc_base *
	ena_com_rx_cdesc_idx_to_ptr(struct ena_com_io_cq *io_cq, u16 idx)
{
	idx &= (io_cq->q_depth - 1);
	return (struct ena_eth_io_rx_cdesc_base *)
		((uintptr_t)io_cq->cdesc_addr.virt_addr +
		idx * io_cq->cdesc_entry_size_in_bytes);
}

static inline u16 ena_com_cdesc_rx_pkt_get(struct ena_com_io_cq *io_cq,
					   u16 *first_cdesc_idx)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;
	u16 count = 0, head_masked;
	u32 last = 0;

	do {
		cdesc = ena_com_get_next_rx_cdesc(io_cq);
		if (!cdesc)
			break;

		ena_com_cq_inc_head(io_cq);
		count++;
		last = (READ_ONCE(cdesc->status) & ENA_ETH_IO_RX_CDESC_BASE_LAST_MASK) >>
			ENA_ETH_IO_RX_CDESC_BASE_LAST_SHIFT;
	} while (!last);

	if (last) {
		*first_cdesc_idx = io_cq->cur_rx_pkt_cdesc_start_idx;
		count += io_cq->cur_rx_pkt_cdesc_count;

		head_masked = io_cq->head & (io_cq->q_depth - 1);

		io_cq->cur_rx_pkt_cdesc_count = 0;
		io_cq->cur_rx_pkt_cdesc_start_idx = head_masked;

		pr_debug("ena q_id: %d packets were completed. first desc idx %u descs# %d\n",
			 io_cq->qid, *first_cdesc_idx, count);
	} else {
		io_cq->cur_rx_pkt_cdesc_count += count;
		count = 0;
	}

	return count;
}

static inline bool ena_com_meta_desc_changed(struct ena_com_io_sq *io_sq,
					     struct ena_com_tx_ctx *ena_tx_ctx)
{
	int rc;

	if (ena_tx_ctx->meta_valid) {
		rc = memcmp(&io_sq->cached_tx_meta,
			    &ena_tx_ctx->ena_meta,
			    sizeof(struct ena_com_tx_meta));

		if (unlikely(rc != 0))
			return true;
	}

	return false;
}

static inline void ena_com_create_and_store_tx_meta_desc(struct ena_com_io_sq *io_sq,
							 struct ena_com_tx_ctx *ena_tx_ctx)
{
	struct ena_eth_io_tx_meta_desc *meta_desc = NULL;
	struct ena_com_tx_meta *ena_meta = &ena_tx_ctx->ena_meta;

	meta_desc = get_sq_desc(io_sq);
	memset(meta_desc, 0x0, sizeof(struct ena_eth_io_tx_meta_desc));

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_META_DESC_MASK;

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_EXT_VALID_MASK;

	/* bits 0-9 of the mss */
	meta_desc->word2 |= (ena_meta->mss <<
		ENA_ETH_IO_TX_META_DESC_MSS_LO_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_MSS_LO_MASK;
	/* bits 10-13 of the mss */
	meta_desc->len_ctrl |= ((ena_meta->mss >> 10) <<
		ENA_ETH_IO_TX_META_DESC_MSS_HI_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_MSS_HI_MASK;

	/* Extended meta desc */
	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_MASK;
	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_META_STORE_MASK;
	meta_desc->len_ctrl |= (io_sq->phase <<
		ENA_ETH_IO_TX_META_DESC_PHASE_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_PHASE_MASK;

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_FIRST_MASK;
	meta_desc->word2 |= ena_meta->l3_hdr_len &
		ENA_ETH_IO_TX_META_DESC_L3_HDR_LEN_MASK;
	meta_desc->word2 |= (ena_meta->l3_hdr_offset <<
		ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_MASK;

	meta_desc->word2 |= (ena_meta->l4_hdr_len <<
		ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_MASK;

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_META_STORE_MASK;

	/* Cached the meta desc */
	memcpy(&io_sq->cached_tx_meta, ena_meta,
	       sizeof(struct ena_com_tx_meta));

	ena_com_copy_curr_sq_desc_to_dev(io_sq);
	ena_com_sq_update_tail(io_sq);
}

static inline void ena_com_rx_set_flags(struct ena_com_rx_ctx *ena_rx_ctx,
					struct ena_eth_io_rx_cdesc_base *cdesc)
{
	ena_rx_ctx->l3_proto = cdesc->status &
		ENA_ETH_IO_RX_CDESC_BASE_L3_PROTO_IDX_MASK;
	ena_rx_ctx->l4_proto =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_SHIFT;
	ena_rx_ctx->l3_csum_err =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_SHIFT;
	ena_rx_ctx->l4_csum_err =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_SHIFT;
	ena_rx_ctx->hash = cdesc->hash;
	ena_rx_ctx->frag =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_SHIFT;

	pr_debug("ena_rx_ctx->l3_proto %d ena_rx_ctx->l4_proto %d\nena_rx_ctx->l3_csum_err %d ena_rx_ctx->l4_csum_err %d\nhash frag %d frag: %d cdesc_status: %x\n",
		 ena_rx_ctx->l3_proto, ena_rx_ctx->l4_proto,
		 ena_rx_ctx->l3_csum_err, ena_rx_ctx->l4_csum_err,
		 ena_rx_ctx->hash, ena_rx_ctx->frag, cdesc->status);
}

/*****************************************************************************/
/*****************************     API      **********************************/
/*****************************************************************************/

int ena_com_prepare_tx(struct ena_com_io_sq *io_sq,
		       struct ena_com_tx_ctx *ena_tx_ctx,
		       int *nb_hw_desc)
{
	struct ena_eth_io_tx_desc *desc = NULL;
	struct ena_com_buf *ena_bufs = ena_tx_ctx->ena_bufs;
	void *push_header = ena_tx_ctx->push_header;
	u16 header_len = ena_tx_ctx->header_len;
	u16 num_bufs = ena_tx_ctx->num_bufs;
	int total_desc, i, rc;
	bool have_meta;
	u64 addr_hi;

	WARN(io_sq->direction != ENA_COM_IO_QUEUE_DIRECTION_TX, "wrong Q type");

	/* num_bufs +1 for potential meta desc */
	if (ena_com_sq_empty_space(io_sq) < (num_bufs + 1)) {
		pr_err("Not enough space in the tx queue\n");
		return -ENOMEM;
	}

	if (unlikely(header_len > io_sq->tx_max_header_size)) {
		pr_err("header size is too large %d max header: %d\n",
		       header_len, io_sq->tx_max_header_size);
		return -EINVAL;
	}

	/* start with pushing the header (if needed) */
	rc = ena_com_write_header(io_sq, push_header, header_len);
	if (unlikely(rc))
		return rc;

	have_meta = ena_tx_ctx->meta_valid && ena_com_meta_desc_changed(io_sq,
			ena_tx_ctx);
	if (have_meta)
		ena_com_create_and_store_tx_meta_desc(io_sq, ena_tx_ctx);

	/* If the caller doesn't want send packets */
	if (unlikely(!num_bufs && !header_len)) {
		*nb_hw_desc = have_meta ? 0 : 1;
		return 0;
	}

	desc = get_sq_desc(io_sq);
	memset(desc, 0x0, sizeof(struct ena_eth_io_tx_desc));

	/* Set first desc when we don't have meta descriptor */
	if (!have_meta)
		desc->len_ctrl |= ENA_ETH_IO_TX_DESC_FIRST_MASK;

	desc->buff_addr_hi_hdr_sz |= (header_len <<
		ENA_ETH_IO_TX_DESC_HEADER_LENGTH_SHIFT) &
		ENA_ETH_IO_TX_DESC_HEADER_LENGTH_MASK;
	desc->len_ctrl |= (io_sq->phase << ENA_ETH_IO_TX_DESC_PHASE_SHIFT) &
		ENA_ETH_IO_TX_DESC_PHASE_MASK;

	desc->len_ctrl |= ENA_ETH_IO_TX_DESC_COMP_REQ_MASK;

	/* Bits 0-9 */
	desc->meta_ctrl |= (ena_tx_ctx->req_id <<
		ENA_ETH_IO_TX_DESC_REQ_ID_LO_SHIFT) &
		ENA_ETH_IO_TX_DESC_REQ_ID_LO_MASK;

	desc->meta_ctrl |= (ena_tx_ctx->df <<
		ENA_ETH_IO_TX_DESC_DF_SHIFT) &
		ENA_ETH_IO_TX_DESC_DF_MASK;

	/* Bits 10-15 */
	desc->len_ctrl |= ((ena_tx_ctx->req_id >> 10) <<
		ENA_ETH_IO_TX_DESC_REQ_ID_HI_SHIFT) &
		ENA_ETH_IO_TX_DESC_REQ_ID_HI_MASK;

	if (ena_tx_ctx->meta_valid) {
		desc->meta_ctrl |= (ena_tx_ctx->tso_enable <<
			ENA_ETH_IO_TX_DESC_TSO_EN_SHIFT) &
			ENA_ETH_IO_TX_DESC_TSO_EN_MASK;
		desc->meta_ctrl |= ena_tx_ctx->l3_proto &
			ENA_ETH_IO_TX_DESC_L3_PROTO_IDX_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l4_proto <<
			ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_SHIFT) &
			ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l3_csum_enable <<
			ENA_ETH_IO_TX_DESC_L3_CSUM_EN_SHIFT) &
			ENA_ETH_IO_TX_DESC_L3_CSUM_EN_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l4_csum_enable <<
			ENA_ETH_IO_TX_DESC_L4_CSUM_EN_SHIFT) &
			ENA_ETH_IO_TX_DESC_L4_CSUM_EN_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l4_csum_partial <<
			ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_SHIFT) &
			ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_MASK;
	}

	for (i = 0; i < num_bufs; i++) {
		/* The first desc share the same desc as the header */
		if (likely(i != 0)) {
			ena_com_copy_curr_sq_desc_to_dev(io_sq);
			ena_com_sq_update_tail(io_sq);

			desc = get_sq_desc(io_sq);
			memset(desc, 0x0, sizeof(struct ena_eth_io_tx_desc));

			desc->len_ctrl |= (io_sq->phase <<
				ENA_ETH_IO_TX_DESC_PHASE_SHIFT) &
				ENA_ETH_IO_TX_DESC_PHASE_MASK;
		}

		desc->len_ctrl |= ena_bufs->len &
			ENA_ETH_IO_TX_DESC_LENGTH_MASK;

		addr_hi = ((ena_bufs->paddr &
			GENMASK_ULL(io_sq->dma_addr_bits - 1, 32)) >> 32);

		desc->buff_addr_lo = (u32)ena_bufs->paddr;
		desc->buff_addr_hi_hdr_sz |= addr_hi &
			ENA_ETH_IO_TX_DESC_ADDR_HI_MASK;
		ena_bufs++;
	}

	/* set the last desc indicator */
	desc->len_ctrl |= ENA_ETH_IO_TX_DESC_LAST_MASK;

	ena_com_copy_curr_sq_desc_to_dev(io_sq);

	ena_com_sq_update_tail(io_sq);

	total_desc = max_t(u16, num_bufs, 1);
	total_desc += have_meta ? 1 : 0;

	*nb_hw_desc = total_desc;
	return 0;
}

int ena_com_rx_pkt(struct ena_com_io_cq *io_cq,
		   struct ena_com_io_sq *io_sq,
		   struct ena_com_rx_ctx *ena_rx_ctx)
{
	struct ena_com_rx_buf_info *ena_buf = &ena_rx_ctx->ena_bufs[0];
	struct ena_eth_io_rx_cdesc_base *cdesc = NULL;
	u16 cdesc_idx = 0;
	u16 nb_hw_desc;
	u16 i;

	WARN(io_cq->direction != ENA_COM_IO_QUEUE_DIRECTION_RX, "wrong Q type");

	nb_hw_desc = ena_com_cdesc_rx_pkt_get(io_cq, &cdesc_idx);
	if (nb_hw_desc == 0) {
		ena_rx_ctx->descs = nb_hw_desc;
		return 0;
	}

	pr_debug("fetch rx packet: queue %d completed desc: %d\n", io_cq->qid,
		 nb_hw_desc);

	if (unlikely(nb_hw_desc > ena_rx_ctx->max_bufs)) {
		pr_err("Too many RX cdescs (%d) > MAX(%d)\n", nb_hw_desc,
		       ena_rx_ctx->max_bufs);
		return -ENOSPC;
	}

	for (i = 0; i < nb_hw_desc; i++) {
		cdesc = ena_com_rx_cdesc_idx_to_ptr(io_cq, cdesc_idx + i);

		ena_buf->len = cdesc->length;
		ena_buf->req_id = cdesc->req_id;
		ena_buf++;
	}

	/* Update SQ head ptr */
	io_sq->next_to_comp += nb_hw_desc;

	pr_debug("[%s][QID#%d] Updating SQ head to: %d\n", __func__, io_sq->qid,
		 io_sq->next_to_comp);

	/* Get rx flags from the last pkt */
	ena_com_rx_set_flags(ena_rx_ctx, cdesc);

	ena_rx_ctx->descs = nb_hw_desc;
	return 0;
}

int ena_com_add_single_rx_desc(struct ena_com_io_sq *io_sq,
			       struct ena_com_buf *ena_buf,
			       u16 req_id)
{
	struct ena_eth_io_rx_desc *desc;

	WARN(io_sq->direction != ENA_COM_IO_QUEUE_DIRECTION_RX, "wrong Q type");

	if (unlikely(ena_com_sq_empty_space(io_sq) == 0))
		return -ENOSPC;

	desc = get_sq_desc(io_sq);
	memset(desc, 0x0, sizeof(struct ena_eth_io_rx_desc));

	desc->length = ena_buf->len;

	desc->ctrl |= ENA_ETH_IO_RX_DESC_FIRST_MASK;
	desc->ctrl |= ENA_ETH_IO_RX_DESC_LAST_MASK;
	desc->ctrl |= io_sq->phase & ENA_ETH_IO_RX_DESC_PHASE_MASK;
	desc->ctrl |= ENA_ETH_IO_RX_DESC_COMP_REQ_MASK;

	desc->req_id = req_id;

	desc->buff_addr_lo = (u32)ena_buf->paddr;
	desc->buff_addr_hi =
		((ena_buf->paddr & GENMASK_ULL(io_sq->dma_addr_bits - 1, 32)) >> 32);

	ena_com_sq_update_tail(io_sq);

	return 0;
}

int ena_com_tx_comp_req_id_get(struct ena_com_io_cq *io_cq, u16 *req_id)
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

	if (unlikely(cdesc->req_id >= io_cq->q_depth)) {
		pr_err("Invalid req id %d\n", cdesc->req_id);
		return -EINVAL;
	}

	ena_com_cq_inc_head(io_cq);

	*req_id = READ_ONCE(cdesc->req_id);

	return 0;
}

bool ena_com_cq_empty(struct ena_com_io_cq *io_cq)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;

	cdesc = ena_com_get_next_rx_cdesc(io_cq);
	if (cdesc)
		return false;
	else
		return true;
}
