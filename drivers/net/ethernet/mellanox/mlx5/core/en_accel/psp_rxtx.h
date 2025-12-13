/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5E_PSP_RXTX_H__
#define __MLX5E_PSP_RXTX_H__

#include <linux/skbuff.h>
#include <net/xfrm.h>
#include <net/psp.h>
#include "en.h"
#include "en/txrx.h"

/* Bit30: PSP marker, Bit29-23: PSP syndrome, Bit22-0: PSP obj id */
#define MLX5_PSP_METADATA_MARKER(metadata)  ((((metadata) >> 30) & 0x3) == 0x3)
#define MLX5_PSP_METADATA_SYNDROME(metadata) (((metadata) >> 23) & GENMASK(6, 0))
#define MLX5_PSP_METADATA_HANDLE(metadata)  ((metadata) & GENMASK(22, 0))

struct mlx5e_accel_tx_psp_state {
	u32 tailen;
	u32 keyid;
	__be32 spi;
	u8 inner_ipproto;
	u8 ver;
};

#ifdef CONFIG_MLX5_EN_PSP
static inline bool mlx5e_psp_is_offload_state(struct mlx5e_accel_tx_psp_state *psp_state)
{
	return (psp_state->tailen != 0);
}

static inline bool mlx5e_psp_is_offload(struct sk_buff *skb, struct net_device *netdev)
{
	bool ret;

	rcu_read_lock();
	ret = !!psp_skb_get_assoc_rcu(skb);
	rcu_read_unlock();
	return ret;
}

bool mlx5e_psp_handle_tx_skb(struct net_device *netdev,
			     struct sk_buff *skb,
			     struct mlx5e_accel_tx_psp_state *psp_st);

void mlx5e_psp_tx_build_eseg(struct mlx5e_priv *priv, struct sk_buff *skb,
			     struct mlx5e_accel_tx_psp_state *psp_st,
			     struct mlx5_wqe_eth_seg *eseg);

void mlx5e_psp_handle_tx_wqe(struct mlx5e_tx_wqe *wqe,
			     struct mlx5e_accel_tx_psp_state *psp_st,
			     struct mlx5_wqe_inline_seg *inlseg);

static inline bool mlx5e_psp_txwqe_build_eseg_csum(struct mlx5e_txqsq *sq, struct sk_buff *skb,
						   struct mlx5e_accel_tx_psp_state *psp_st,
						   struct mlx5_wqe_eth_seg *eseg)
{
	u8 inner_ipproto;

	if (!mlx5e_psp_is_offload_state(psp_st))
		return false;

	inner_ipproto = psp_st->inner_ipproto;
	eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM;
	if (inner_ipproto) {
		eseg->cs_flags |= MLX5_ETH_WQE_L3_INNER_CSUM;
		if (inner_ipproto == IPPROTO_TCP || inner_ipproto == IPPROTO_UDP)
			eseg->cs_flags |= MLX5_ETH_WQE_L4_INNER_CSUM;
		if (likely(skb->ip_summed == CHECKSUM_PARTIAL))
			sq->stats->csum_partial_inner++;
	} else if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		eseg->cs_flags |= MLX5_ETH_WQE_L4_INNER_CSUM;
		sq->stats->csum_partial_inner++;
	}

	return true;
}

static inline unsigned int mlx5e_psp_tx_ids_len(struct mlx5e_accel_tx_psp_state *psp_st)
{
	return psp_st->tailen;
}

static inline bool mlx5e_psp_is_rx_flow(struct mlx5_cqe64 *cqe)
{
	return MLX5_PSP_METADATA_MARKER(be32_to_cpu(cqe->ft_metadata));
}

bool mlx5e_psp_offload_handle_rx_skb(struct net_device *netdev, struct sk_buff *skb,
				     struct mlx5_cqe64 *cqe);
#else
static inline bool mlx5e_psp_is_offload_state(struct mlx5e_accel_tx_psp_state *psp_state)
{
	return false;
}

static inline bool mlx5e_psp_is_offload(struct sk_buff *skb, struct net_device *netdev)
{
	return false;
}

static inline bool mlx5e_psp_txwqe_build_eseg_csum(struct mlx5e_txqsq *sq, struct sk_buff *skb,
						   struct mlx5e_accel_tx_psp_state *psp_st,
						   struct mlx5_wqe_eth_seg *eseg)
{
	return false;
}

static inline bool mlx5e_psp_is_rx_flow(struct mlx5_cqe64 *cqe)
{
	return false;
}

static inline bool mlx5e_psp_offload_handle_rx_skb(struct net_device *netdev,
						   struct sk_buff *skb,
						   struct mlx5_cqe64 *cqe)
{
	return false;
}
#endif /* CONFIG_MLX5_EN_PSP */
#endif /* __MLX5E_PSP_RXTX_H__ */
