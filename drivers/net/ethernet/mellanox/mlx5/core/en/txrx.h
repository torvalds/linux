/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_TXRX_H___
#define __MLX5_EN_TXRX_H___

#include "en.h"

#define MLX5E_SQ_NOPS_ROOM  MLX5_SEND_WQE_MAX_WQEBBS
#define MLX5E_SQ_STOP_ROOM (MLX5_SEND_WQE_MAX_WQEBBS +\
			    MLX5E_SQ_NOPS_ROOM)

#ifndef CONFIG_MLX5_EN_TLS
#define MLX5E_SQ_TLS_ROOM (0)
#else
/* TLS offload requires additional stop_room for:
 *  - a resync SKB.
 * kTLS offload requires additional stop_room for:
 * - static params WQE,
 * - progress params WQE, and
 * - resync DUMP per frag.
 */
#define MLX5E_SQ_TLS_ROOM  \
	(MLX5_SEND_WQE_MAX_WQEBBS + \
	 MLX5E_KTLS_STATIC_WQEBBS + MLX5E_KTLS_PROGRESS_WQEBBS + \
	 MAX_SKB_FRAGS * MLX5E_KTLS_MAX_DUMP_WQEBBS)
#endif

#define INL_HDR_START_SZ (sizeof(((struct mlx5_wqe_eth_seg *)NULL)->inline_hdr.start))

static inline bool
mlx5e_wqc_has_room_for(struct mlx5_wq_cyc *wq, u16 cc, u16 pc, u16 n)
{
	return (mlx5_wq_cyc_ctr2ix(wq, cc - pc) >= n) || (cc == pc);
}

static inline void *
mlx5e_sq_fetch_wqe(struct mlx5e_txqsq *sq, size_t size, u16 *pi)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	void *wqe;

	*pi  = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	wqe = mlx5_wq_cyc_get_wqe(wq, *pi);
	memset(wqe, 0, size);

	return wqe;
}

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

static inline void
mlx5e_fill_sq_frag_edge(struct mlx5e_txqsq *sq, struct mlx5_wq_cyc *wq,
			u16 pi, u16 nnops)
{
	struct mlx5e_tx_wqe_info *edge_wi, *wi = &sq->db.wqe_info[pi];

	edge_wi = wi + nnops;

	/* fill sq frag edge with nops to avoid wqe wrapping two pages */
	for (; wi < edge_wi; wi++) {
		wi->skb        = NULL;
		wi->num_wqebbs = 1;
		mlx5e_post_nop(wq, sq->sqn, &sq->pc);
	}
	sq->stats->nop += nnops;
}

static inline void
mlx5e_notify_hw(struct mlx5_wq_cyc *wq, u16 pc, void __iomem *uar_map,
		struct mlx5_wqe_ctrl_seg *ctrl)
{
	ctrl->fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	*wq->db = cpu_to_be32(pc);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();

	mlx5_write64((__be32 *)ctrl, uar_map);
}

static inline bool mlx5e_transport_inline_tx_wqe(struct mlx5e_tx_wqe *wqe)
{
	return !!wqe->ctrl.tisn;
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
		/* fall through */
	case IPPROTO_TCP:
		eseg->swp_inner_l4_offset = skb_inner_transport_offset(skb) / 2;
		break;
	}
}

#endif
