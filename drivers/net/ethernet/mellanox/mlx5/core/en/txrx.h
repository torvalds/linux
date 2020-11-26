/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_TXRX_H___
#define __MLX5_EN_TXRX_H___

#include "en.h"
#include <linux/indirect_call_wrapper.h>

#define MLX5E_TX_WQE_EMPTY_DS_COUNT (sizeof(struct mlx5e_tx_wqe) / MLX5_SEND_WQE_DS)

/* The mult of MLX5_SEND_WQE_MAX_WQEBBS * MLX5_SEND_WQEBB_NUM_DS
 * (16 * 4 == 64) does not fit in the 6-bit DS field of Ctrl Segment.
 * We use a bound lower that MLX5_SEND_WQE_MAX_WQEBBS to let a
 * full-session WQE be cache-aligned.
 */
#if L1_CACHE_BYTES < 128
#define MLX5E_TX_MPW_MAX_WQEBBS (MLX5_SEND_WQE_MAX_WQEBBS - 1)
#else
#define MLX5E_TX_MPW_MAX_WQEBBS (MLX5_SEND_WQE_MAX_WQEBBS - 2)
#endif

#define MLX5E_TX_MPW_MAX_NUM_DS (MLX5E_TX_MPW_MAX_WQEBBS * MLX5_SEND_WQEBB_NUM_DS)

#define INL_HDR_START_SZ (sizeof(((struct mlx5_wqe_eth_seg *)NULL)->inline_hdr.start))

enum mlx5e_icosq_wqe_type {
	MLX5E_ICOSQ_WQE_NOP,
	MLX5E_ICOSQ_WQE_UMR_RX,
#ifdef CONFIG_MLX5_EN_TLS
	MLX5E_ICOSQ_WQE_UMR_TLS,
	MLX5E_ICOSQ_WQE_SET_PSV_TLS,
	MLX5E_ICOSQ_WQE_GET_PSV_TLS,
#endif
};

/* General */
static inline bool mlx5e_skb_is_multicast(struct sk_buff *skb)
{
	return skb->pkt_type == PACKET_MULTICAST || skb->pkt_type == PACKET_BROADCAST;
}

void mlx5e_trigger_irq(struct mlx5e_icosq *sq);
void mlx5e_completion_event(struct mlx5_core_cq *mcq, struct mlx5_eqe *eqe);
void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event);
int mlx5e_napi_poll(struct napi_struct *napi, int budget);
int mlx5e_poll_ico_cq(struct mlx5e_cq *cq);

/* RX */
void mlx5e_page_dma_unmap(struct mlx5e_rq *rq, struct mlx5e_dma_info *dma_info);
void mlx5e_page_release_dynamic(struct mlx5e_rq *rq,
				struct mlx5e_dma_info *dma_info,
				bool recycle);
INDIRECT_CALLABLE_DECLARE(bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq));
INDIRECT_CALLABLE_DECLARE(bool mlx5e_post_rx_mpwqes(struct mlx5e_rq *rq));
int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget);
void mlx5e_free_rx_descs(struct mlx5e_rq *rq);
void mlx5e_free_rx_in_progress_descs(struct mlx5e_rq *rq);

/* TX */
u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       struct net_device *sb_dev);
netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev);
bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget);
void mlx5e_free_txqsq_descs(struct mlx5e_txqsq *sq);

static inline bool
mlx5e_wqc_has_room_for(struct mlx5_wq_cyc *wq, u16 cc, u16 pc, u16 n)
{
	return (mlx5_wq_cyc_ctr2ix(wq, cc - pc) >= n) || (cc == pc);
}

static inline void *mlx5e_fetch_wqe(struct mlx5_wq_cyc *wq, u16 pi, size_t wqe_size)
{
	void *wqe;

	wqe = mlx5_wq_cyc_get_wqe(wq, pi);
	memset(wqe, 0, wqe_size);

	return wqe;
}

#define MLX5E_TX_FETCH_WQE(sq, pi) \
	((struct mlx5e_tx_wqe *)mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5e_tx_wqe)))

static inline struct mlx5e_tx_wqe *
mlx5e_post_nop(struct mlx5_wq_cyc *wq, u32 sqn, u16 *pc)
{
	u16                         pi   = mlx5_wq_cyc_ctr2ix(wq, *pc);
	struct mlx5e_tx_wqe        *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);
	struct mlx5_wqe_ctrl_seg   *cseg = &wqe->ctrl;

	memset(cseg, 0, sizeof(*cseg));

	cseg->opmod_idx_opcode = cpu_to_be32((*pc << 8) | MLX5_OPCODE_NOP);
	cseg->qpn_ds           = cpu_to_be32((sqn << 8) | 0x01);

	(*pc)++;

	return wqe;
}

static inline struct mlx5e_tx_wqe *
mlx5e_post_nop_fence(struct mlx5_wq_cyc *wq, u32 sqn, u16 *pc)
{
	u16                         pi   = mlx5_wq_cyc_ctr2ix(wq, *pc);
	struct mlx5e_tx_wqe        *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);
	struct mlx5_wqe_ctrl_seg   *cseg = &wqe->ctrl;

	memset(cseg, 0, sizeof(*cseg));

	cseg->opmod_idx_opcode = cpu_to_be32((*pc << 8) | MLX5_OPCODE_NOP);
	cseg->qpn_ds           = cpu_to_be32((sqn << 8) | 0x01);
	cseg->fm_ce_se         = MLX5_FENCE_MODE_INITIATOR_SMALL;

	(*pc)++;

	return wqe;
}

struct mlx5e_tx_wqe_info {
	struct sk_buff *skb;
	u32 num_bytes;
	u8 num_wqebbs;
	u8 num_dma;
	u8 num_fifo_pkts;
#ifdef CONFIG_MLX5_EN_TLS
	struct page *resync_dump_frag_page;
#endif
};

static inline u16 mlx5e_txqsq_get_next_pi(struct mlx5e_txqsq *sq, u16 size)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi, contig_wqebbs;

	pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	contig_wqebbs = mlx5_wq_cyc_get_contig_wqebbs(wq, pi);
	if (unlikely(contig_wqebbs < size)) {
		struct mlx5e_tx_wqe_info *wi, *edge_wi;

		wi = &sq->db.wqe_info[pi];
		edge_wi = wi + contig_wqebbs;

		/* Fill SQ frag edge with NOPs to avoid WQE wrapping two pages. */
		for (; wi < edge_wi; wi++) {
			*wi = (struct mlx5e_tx_wqe_info) {
				.num_wqebbs = 1,
			};
			mlx5e_post_nop(wq, sq->sqn, &sq->pc);
		}
		sq->stats->nop += contig_wqebbs;

		pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	}

	return pi;
}

struct mlx5e_icosq_wqe_info {
	u8 wqe_type;
	u8 num_wqebbs;

	/* Auxiliary data for different wqe types. */
	union {
		struct {
			struct mlx5e_rq *rq;
		} umr;
#ifdef CONFIG_MLX5_EN_TLS
		struct {
			struct mlx5e_ktls_offload_context_rx *priv_rx;
		} tls_set_params;
		struct {
			struct mlx5e_ktls_rx_resync_buf *buf;
		} tls_get_params;
#endif
	};
};

void mlx5e_free_icosq_descs(struct mlx5e_icosq *sq);

static inline u16 mlx5e_icosq_get_next_pi(struct mlx5e_icosq *sq, u16 size)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi, contig_wqebbs;

	pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	contig_wqebbs = mlx5_wq_cyc_get_contig_wqebbs(wq, pi);
	if (unlikely(contig_wqebbs < size)) {
		struct mlx5e_icosq_wqe_info *wi, *edge_wi;

		wi = &sq->db.wqe_info[pi];
		edge_wi = wi + contig_wqebbs;

		/* Fill SQ frag edge with NOPs to avoid WQE wrapping two pages. */
		for (; wi < edge_wi; wi++) {
			*wi = (struct mlx5e_icosq_wqe_info) {
				.wqe_type   = MLX5E_ICOSQ_WQE_NOP,
				.num_wqebbs = 1,
			};
			mlx5e_post_nop(wq, sq->sqn, &sq->pc);
		}

		pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	}

	return pi;
}

static inline void
mlx5e_notify_hw(struct mlx5_wq_cyc *wq, u16 pc, void __iomem *uar_map,
		struct mlx5_wqe_ctrl_seg *ctrl)
{
	ctrl->fm_ce_se |= MLX5_WQE_CTRL_CQ_UPDATE;
	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	*wq->db = cpu_to_be32(pc);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();

	mlx5_write64((__be32 *)ctrl, uar_map);
}

static inline void mlx5e_cq_arm(struct mlx5e_cq *cq)
{
	struct mlx5_core_cq *mcq;

	mcq = &cq->mcq;
	mlx5_cq_arm(mcq, MLX5_CQ_DB_REQ_NOT, mcq->uar->map, cq->wq.cc);
}

static inline struct mlx5e_sq_dma *
mlx5e_dma_get(struct mlx5e_txqsq *sq, u32 i)
{
	return &sq->db.dma_fifo[i & sq->dma_fifo_mask];
}

static inline void
mlx5e_dma_push(struct mlx5e_txqsq *sq, dma_addr_t addr, u32 size,
	       enum mlx5e_dma_map_type map_type)
{
	struct mlx5e_sq_dma *dma = mlx5e_dma_get(sq, sq->dma_fifo_pc++);

	dma->addr = addr;
	dma->size = size;
	dma->type = map_type;
}

static inline struct sk_buff **mlx5e_skb_fifo_get(struct mlx5e_txqsq *sq, u16 i)
{
	return &sq->db.skb_fifo[i & sq->skb_fifo_mask];
}

static inline void mlx5e_skb_fifo_push(struct mlx5e_txqsq *sq, struct sk_buff *skb)
{
	struct sk_buff **skb_item = mlx5e_skb_fifo_get(sq, sq->skb_fifo_pc++);

	*skb_item = skb;
}

static inline struct sk_buff *mlx5e_skb_fifo_pop(struct mlx5e_txqsq *sq)
{
	return *mlx5e_skb_fifo_get(sq, sq->skb_fifo_cc++);
}

static inline void
mlx5e_tx_dma_unmap(struct device *pdev, struct mlx5e_sq_dma *dma)
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

void mlx5e_sq_xmit_simple(struct mlx5e_txqsq *sq, struct sk_buff *skb, bool xmit_more);
void mlx5e_tx_mpwqe_ensure_complete(struct mlx5e_txqsq *sq);

static inline bool mlx5e_tx_mpwqe_is_full(struct mlx5e_tx_mpwqe *session)
{
	return session->ds_count == MLX5E_TX_MPW_MAX_NUM_DS;
}

static inline void mlx5e_rqwq_reset(struct mlx5e_rq *rq)
{
	if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
		mlx5_wq_ll_reset(&rq->mpwqe.wq);
		rq->mpwqe.actual_wq_head = 0;
	} else {
		mlx5_wq_cyc_reset(&rq->wqe.wq);
	}
}

static inline void mlx5e_dump_error_cqe(struct mlx5e_cq *cq, u32 qn,
					struct mlx5_err_cqe *err_cqe)
{
	struct mlx5_cqwq *wq = &cq->wq;
	u32 ci;

	ci = mlx5_cqwq_ctr2ix(wq, wq->cc - 1);

	netdev_err(cq->channel->netdev,
		   "Error cqe on cqn 0x%x, ci 0x%x, qn 0x%x, opcode 0x%x, syndrome 0x%x, vendor syndrome 0x%x\n",
		   cq->mcq.cqn, ci, qn,
		   get_cqe_opcode((struct mlx5_cqe64 *)err_cqe),
		   err_cqe->syndrome, err_cqe->vendor_err_synd);
	mlx5_dump_err_cqe(cq->mdev, err_cqe);
}

static inline u32 mlx5e_rqwq_get_size(struct mlx5e_rq *rq)
{
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return mlx5_wq_ll_get_size(&rq->mpwqe.wq);
	default:
		return mlx5_wq_cyc_get_size(&rq->wqe.wq);
	}
}

static inline u32 mlx5e_rqwq_get_cur_sz(struct mlx5e_rq *rq)
{
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return rq->mpwqe.wq.cur_sz;
	default:
		return rq->wqe.wq.cur_sz;
	}
}

static inline u16 mlx5e_rqwq_get_head(struct mlx5e_rq *rq)
{
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return mlx5_wq_ll_get_head(&rq->mpwqe.wq);
	default:
		return mlx5_wq_cyc_get_head(&rq->wqe.wq);
	}
}

static inline u16 mlx5e_rqwq_get_wqe_counter(struct mlx5e_rq *rq)
{
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return mlx5_wq_ll_get_counter(&rq->mpwqe.wq);
	default:
		return mlx5_wq_cyc_get_counter(&rq->wqe.wq);
	}
}

/* SW parser related functions */

struct mlx5e_swp_spec {
	__be16 l3_proto;
	u8 l4_proto;
	u8 is_tun;
	__be16 tun_l3_proto;
	u8 tun_l4_proto;
};

static inline void
mlx5e_set_eseg_swp(struct sk_buff *skb, struct mlx5_wqe_eth_seg *eseg,
		   struct mlx5e_swp_spec *swp_spec)
{
	/* SWP offsets are in 2-bytes words */
	eseg->swp_outer_l3_offset = skb_network_offset(skb) / 2;
	if (swp_spec->l3_proto == htons(ETH_P_IPV6))
		eseg->swp_flags |= MLX5_ETH_WQE_SWP_OUTER_L3_IPV6;
	if (swp_spec->l4_proto) {
		eseg->swp_outer_l4_offset = skb_transport_offset(skb) / 2;
		if (swp_spec->l4_proto == IPPROTO_UDP)
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_OUTER_L4_UDP;
	}

	if (swp_spec->is_tun) {
		eseg->swp_inner_l3_offset = skb_inner_network_offset(skb) / 2;
		if (swp_spec->tun_l3_proto == htons(ETH_P_IPV6))
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L3_IPV6;
	} else { /* typically for ipsec when xfrm mode != XFRM_MODE_TUNNEL */
		eseg->swp_inner_l3_offset = skb_network_offset(skb) / 2;
		if (swp_spec->l3_proto == htons(ETH_P_IPV6))
			eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L3_IPV6;
	}
	switch (swp_spec->tun_l4_proto) {
	case IPPROTO_UDP:
		eseg->swp_flags |= MLX5_ETH_WQE_SWP_INNER_L4_UDP;
		fallthrough;
	case IPPROTO_TCP:
		eseg->swp_inner_l4_offset = skb_inner_transport_offset(skb) / 2;
		break;
	}
}

static inline u16 mlx5e_stop_room_for_wqe(u16 wqe_size)
{
	BUILD_BUG_ON(PAGE_SIZE / MLX5_SEND_WQE_BB < MLX5_SEND_WQE_MAX_WQEBBS);

	/* A WQE must not cross the page boundary, hence two conditions:
	 * 1. Its size must not exceed the page size.
	 * 2. If the WQE size is X, and the space remaining in a page is less
	 *    than X, this space needs to be padded with NOPs. So, one WQE of
	 *    size X may require up to X-1 WQEBBs of padding, which makes the
	 *    stop room of X-1 + X.
	 * WQE size is also limited by the hardware limit.
	 */

	if (__builtin_constant_p(wqe_size))
		BUILD_BUG_ON(wqe_size > MLX5_SEND_WQE_MAX_WQEBBS);
	else
		WARN_ON_ONCE(wqe_size > MLX5_SEND_WQE_MAX_WQEBBS);

	return wqe_size * 2 - 1;
}

#endif
