/*
 * Copyright (c) 2015-2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include "en.h"

#define MLX5E_SQ_NOPS_ROOM  MLX5_SEND_WQE_MAX_WQEBBS
#define MLX5E_SQ_STOP_ROOM (MLX5_SEND_WQE_MAX_WQEBBS +\
			    MLX5E_SQ_NOPS_ROOM)

void mlx5e_send_nop(struct mlx5e_sq *sq, bool notify_hw)
{
	struct mlx5_wq_cyc                *wq  = &sq->wq;

	u16 pi = sq->pc & wq->sz_m1;
	struct mlx5e_tx_wqe              *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);

	struct mlx5_wqe_ctrl_seg         *cseg = &wqe->ctrl;

	memset(cseg, 0, sizeof(*cseg));

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | MLX5_OPCODE_NOP);
	cseg->qpn_ds           = cpu_to_be32((sq->sqn << 8) | 0x01);

	sq->skb[pi] = NULL;
	sq->pc++;
	sq->stats.nop++;

	if (notify_hw) {
		cseg->fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
		mlx5e_tx_notify_hw(sq, &wqe->ctrl, 0);
	}
}

static inline void mlx5e_tx_dma_unmap(struct device *pdev,
				      struct mlx5e_sq_dma *dma)
{
	switch (dma->type) {
	case MLX5E_DMA_MAP_SINGLE:
		dma_unmap_single(pdev, dma->addr, dma->size, DMA_TO_DEVICE);
		break;
	case MLX5E_DMA_MAP_PAGE:
		dma_unmap_page(pdev, dma->addr, dma->size, DMA_TO_DEVICE);
		break;
	default:
		WARN_ONCE(true, "mlx5e_tx_dma_unmap unknown DMA type!\n");
	}
}

static inline void mlx5e_dma_push(struct mlx5e_sq *sq,
				  dma_addr_t addr,
				  u32 size,
				  enum mlx5e_dma_map_type map_type)
{
	sq->dma_fifo[sq->dma_fifo_pc & sq->dma_fifo_mask].addr = addr;
	sq->dma_fifo[sq->dma_fifo_pc & sq->dma_fifo_mask].size = size;
	sq->dma_fifo[sq->dma_fifo_pc & sq->dma_fifo_mask].type = map_type;
	sq->dma_fifo_pc++;
}

static inline struct mlx5e_sq_dma *mlx5e_dma_get(struct mlx5e_sq *sq, u32 i)
{
	return &sq->dma_fifo[i & sq->dma_fifo_mask];
}

static void mlx5e_dma_unmap_wqe_err(struct mlx5e_sq *sq, u8 num_dma)
{
	int i;

	for (i = 0; i < num_dma; i++) {
		struct mlx5e_sq_dma *last_pushed_dma =
			mlx5e_dma_get(sq, --sq->dma_fifo_pc);

		mlx5e_tx_dma_unmap(sq->pdev, last_pushed_dma);
	}
}

u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       void *accel_priv, select_queue_fallback_t fallback)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int channel_ix = fallback(dev, skb);
	int up = (netdev_get_num_tc(dev) && skb_vlan_tag_present(skb)) ?
		 skb->vlan_tci >> VLAN_PRIO_SHIFT : 0;

	return priv->channeltc_to_txq_map[channel_ix][up];
}

static inline u16 mlx5e_get_inline_hdr_size(struct mlx5e_sq *sq,
					    struct sk_buff *skb, bool bf)
{
	/* Some NIC TX decisions, e.g loopback, are based on the packet
	 * headers and occur before the data gather.
	 * Therefore these headers must be copied into the WQE
	 */
#define MLX5E_MIN_INLINE ETH_HLEN

	if (bf) {
		u16 ihs = skb_headlen(skb);

		if (skb_vlan_tag_present(skb))
			ihs += VLAN_HLEN;

		if (ihs <= sq->max_inline)
			return skb_headlen(skb);
	}

	return MLX5E_MIN_INLINE;
}

static inline void mlx5e_tx_skb_pull_inline(unsigned char **skb_data,
					    unsigned int *skb_len,
					    unsigned int len)
{
	*skb_len -= len;
	*skb_data += len;
}

static inline void mlx5e_insert_vlan(void *start, struct sk_buff *skb, u16 ihs,
				     unsigned char **skb_data,
				     unsigned int *skb_len)
{
	struct vlan_ethhdr *vhdr = (struct vlan_ethhdr *)start;
	int cpy1_sz = 2 * ETH_ALEN;
	int cpy2_sz = ihs - cpy1_sz;

	memcpy(vhdr, *skb_data, cpy1_sz);
	mlx5e_tx_skb_pull_inline(skb_data, skb_len, cpy1_sz);
	vhdr->h_vlan_proto = skb->vlan_proto;
	vhdr->h_vlan_TCI = cpu_to_be16(skb_vlan_tag_get(skb));
	memcpy(&vhdr->h_vlan_encapsulated_proto, *skb_data, cpy2_sz);
	mlx5e_tx_skb_pull_inline(skb_data, skb_len, cpy2_sz);
}

static netdev_tx_t mlx5e_sq_xmit(struct mlx5e_sq *sq, struct sk_buff *skb)
{
	struct mlx5_wq_cyc       *wq   = &sq->wq;

	u16 pi = sq->pc & wq->sz_m1;
	struct mlx5e_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);
	struct mlx5e_tx_wqe_info *wi   = &sq->wqe_info[pi];

	struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
	struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;
	struct mlx5_wqe_data_seg *dseg;

	unsigned char *skb_data = skb->data;
	unsigned int skb_len = skb->len;
	u8  opcode = MLX5_OPCODE_SEND;
	dma_addr_t dma_addr = 0;
	unsigned int num_bytes;
	bool bf = false;
	u16 headlen;
	u16 ds_cnt;
	u16 ihs;
	int i;

	memset(wqe, 0, sizeof(*wqe));

	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM;
		if (skb->encapsulation) {
			eseg->cs_flags |= MLX5_ETH_WQE_L3_INNER_CSUM |
					  MLX5_ETH_WQE_L4_INNER_CSUM;
			sq->stats.csum_offload_inner++;
		} else {
			eseg->cs_flags |= MLX5_ETH_WQE_L4_CSUM;
		}
	} else
		sq->stats.csum_offload_none++;

	if (sq->cc != sq->prev_cc) {
		sq->prev_cc = sq->cc;
		sq->bf_budget = (sq->cc == sq->pc) ? MLX5E_SQ_BF_BUDGET : 0;
	}

	if (skb_is_gso(skb)) {
		eseg->mss    = cpu_to_be16(skb_shinfo(skb)->gso_size);
		opcode       = MLX5_OPCODE_LSO;

		if (skb->encapsulation) {
			ihs = skb_inner_transport_offset(skb) + inner_tcp_hdrlen(skb);
			sq->stats.tso_inner_packets++;
			sq->stats.tso_inner_bytes += skb->len - ihs;
		} else {
			ihs = skb_transport_offset(skb) + tcp_hdrlen(skb);
			sq->stats.tso_packets++;
			sq->stats.tso_bytes += skb->len - ihs;
		}

		num_bytes = skb->len + (skb_shinfo(skb)->gso_segs - 1) * ihs;
	} else {
		bf = sq->bf_budget &&
		     !skb->xmit_more &&
		     !skb_shinfo(skb)->nr_frags;
		ihs = mlx5e_get_inline_hdr_size(sq, skb, bf);
		num_bytes = max_t(unsigned int, skb->len, ETH_ZLEN);
	}

	wi->num_bytes = num_bytes;

	if (skb_vlan_tag_present(skb)) {
		mlx5e_insert_vlan(eseg->inline_hdr_start, skb, ihs, &skb_data,
				  &skb_len);
		ihs += VLAN_HLEN;
	} else {
		memcpy(eseg->inline_hdr_start, skb_data, ihs);
		mlx5e_tx_skb_pull_inline(&skb_data, &skb_len, ihs);
	}

	eseg->inline_hdr_sz = cpu_to_be16(ihs);

	ds_cnt  = sizeof(*wqe) / MLX5_SEND_WQE_DS;
	ds_cnt += DIV_ROUND_UP(ihs - sizeof(eseg->inline_hdr_start),
			       MLX5_SEND_WQE_DS);
	dseg    = (struct mlx5_wqe_data_seg *)cseg + ds_cnt;

	wi->num_dma = 0;

	headlen = skb_len - skb->data_len;
	if (headlen) {
		dma_addr = dma_map_single(sq->pdev, skb_data, headlen,
					  DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(sq->pdev, dma_addr)))
			goto dma_unmap_wqe_err;

		dseg->addr       = cpu_to_be64(dma_addr);
		dseg->lkey       = sq->mkey_be;
		dseg->byte_count = cpu_to_be32(headlen);

		mlx5e_dma_push(sq, dma_addr, headlen, MLX5E_DMA_MAP_SINGLE);
		wi->num_dma++;

		dseg++;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i];
		int fsz = skb_frag_size(frag);

		dma_addr = skb_frag_dma_map(sq->pdev, frag, 0, fsz,
					    DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(sq->pdev, dma_addr)))
			goto dma_unmap_wqe_err;

		dseg->addr       = cpu_to_be64(dma_addr);
		dseg->lkey       = sq->mkey_be;
		dseg->byte_count = cpu_to_be32(fsz);

		mlx5e_dma_push(sq, dma_addr, fsz, MLX5E_DMA_MAP_PAGE);
		wi->num_dma++;

		dseg++;
	}

	ds_cnt += wi->num_dma;

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | opcode);
	cseg->qpn_ds           = cpu_to_be32((sq->sqn << 8) | ds_cnt);

	sq->skb[pi] = skb;

	wi->num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
	sq->pc += wi->num_wqebbs;

	netdev_tx_sent_queue(sq->txq, wi->num_bytes);

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	if (unlikely(!mlx5e_sq_has_room_for(sq, MLX5E_SQ_STOP_ROOM))) {
		netif_tx_stop_queue(sq->txq);
		sq->stats.stopped++;
	}

	if (!skb->xmit_more || netif_xmit_stopped(sq->txq)) {
		int bf_sz = 0;

		if (bf && test_bit(MLX5E_SQ_STATE_BF_ENABLE, &sq->state))
			bf_sz = wi->num_wqebbs << 3;

		cseg->fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
		mlx5e_tx_notify_hw(sq, &wqe->ctrl, bf_sz);
	}

	/* fill sq edge with nops to avoid wqe wrap around */
	while ((sq->pc & wq->sz_m1) > sq->edge)
		mlx5e_send_nop(sq, false);

	if (bf)
		sq->bf_budget--;

	sq->stats.packets++;
	sq->stats.bytes += num_bytes;
	return NETDEV_TX_OK;

dma_unmap_wqe_err:
	sq->stats.dropped++;
	mlx5e_dma_unmap_wqe_err(sq, wi->num_dma);

	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_sq *sq = priv->txq_to_sq_map[skb_get_queue_mapping(skb)];

	return mlx5e_sq_xmit(sq, skb);
}

bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget)
{
	struct mlx5e_sq *sq;
	u32 dma_fifo_cc;
	u32 nbytes;
	u16 npkts;
	u16 sqcc;
	int i;

	sq = container_of(cq, struct mlx5e_sq, cq);

	npkts = 0;
	nbytes = 0;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	/* avoid dirtying sq cache line every cqe */
	dma_fifo_cc = sq->dma_fifo_cc;

	for (i = 0; i < MLX5E_TX_CQ_POLL_BUDGET; i++) {
		struct mlx5_cqe64 *cqe;
		u16 wqe_counter;
		bool last_wqe;

		cqe = mlx5e_get_cqe(cq);
		if (!cqe)
			break;

		mlx5_cqwq_pop(&cq->wq);

		wqe_counter = be16_to_cpu(cqe->wqe_counter);

		do {
			struct mlx5e_tx_wqe_info *wi;
			struct sk_buff *skb;
			u16 ci;
			int j;

			last_wqe = (sqcc == wqe_counter);

			ci = sqcc & sq->wq.sz_m1;
			skb = sq->skb[ci];
			wi = &sq->wqe_info[ci];

			if (unlikely(!skb)) { /* nop */
				sqcc++;
				continue;
			}

			if (unlikely(skb_shinfo(skb)->tx_flags &
				     SKBTX_HW_TSTAMP)) {
				struct skb_shared_hwtstamps hwts = {};

				mlx5e_fill_hwstamp(sq->tstamp,
						   get_cqe_ts(cqe), &hwts);
				skb_tstamp_tx(skb, &hwts);
			}

			for (j = 0; j < wi->num_dma; j++) {
				struct mlx5e_sq_dma *dma =
					mlx5e_dma_get(sq, dma_fifo_cc++);

				mlx5e_tx_dma_unmap(sq->pdev, dma);
			}

			npkts++;
			nbytes += wi->num_bytes;
			sqcc += wi->num_wqebbs;
			napi_consume_skb(skb, napi_budget);
		} while (!last_wqe);
	}

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->dma_fifo_cc = dma_fifo_cc;
	sq->cc = sqcc;

	netdev_tx_completed_queue(sq->txq, npkts, nbytes);

	if (netif_tx_queue_stopped(sq->txq) &&
	    mlx5e_sq_has_room_for(sq, MLX5E_SQ_STOP_ROOM) &&
	    likely(test_bit(MLX5E_SQ_STATE_WAKE_TXQ_ENABLE, &sq->state))) {
				netif_tx_wake_queue(sq->txq);
				sq->stats.wake++;
	}

	return (i == MLX5E_TX_CQ_POLL_BUDGET);
}
