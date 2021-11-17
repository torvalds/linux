/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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
 *
 */

#ifndef __MLX5E_IPSEC_RXTX_H__
#define __MLX5E_IPSEC_RXTX_H__

#include <linux/skbuff.h>
#include <net/xfrm.h>
#include "en.h"
#include "en/txrx.h"

/* Bit31: IPsec marker, Bit30-24: IPsec syndrome, Bit23-0: IPsec obj id */
#define MLX5_IPSEC_METADATA_MARKER(metadata)  (((metadata) >> 31) & 0x1)
#define MLX5_IPSEC_METADATA_SYNDROM(metadata) (((metadata) >> 24) & GENMASK(6, 0))
#define MLX5_IPSEC_METADATA_HANDLE(metadata)  ((metadata) & GENMASK(23, 0))

struct mlx5e_accel_tx_ipsec_state {
	struct xfrm_offload *xo;
	struct xfrm_state *x;
	u32 tailen;
	u32 plen;
};

#ifdef CONFIG_MLX5_EN_IPSEC

struct sk_buff *mlx5e_ipsec_handle_rx_skb(struct net_device *netdev,
					  struct sk_buff *skb, u32 *cqe_bcnt);

void mlx5e_ipsec_inverse_table_init(void);
void mlx5e_ipsec_set_iv_esn(struct sk_buff *skb, struct xfrm_state *x,
			    struct xfrm_offload *xo);
void mlx5e_ipsec_set_iv(struct sk_buff *skb, struct xfrm_state *x,
			struct xfrm_offload *xo);
bool mlx5e_ipsec_handle_tx_skb(struct net_device *netdev,
			       struct sk_buff *skb,
			       struct mlx5e_accel_tx_ipsec_state *ipsec_st);
void mlx5e_ipsec_handle_tx_wqe(struct mlx5e_tx_wqe *wqe,
			       struct mlx5e_accel_tx_ipsec_state *ipsec_st,
			       struct mlx5_wqe_inline_seg *inlseg);
void mlx5e_ipsec_offload_handle_rx_skb(struct net_device *netdev,
				       struct sk_buff *skb,
				       struct mlx5_cqe64 *cqe);
static inline unsigned int mlx5e_ipsec_tx_ids_len(struct mlx5e_accel_tx_ipsec_state *ipsec_st)
{
	return ipsec_st->tailen;
}

static inline bool mlx5_ipsec_is_rx_flow(struct mlx5_cqe64 *cqe)
{
	return MLX5_IPSEC_METADATA_MARKER(be32_to_cpu(cqe->ft_metadata));
}

static inline bool mlx5e_ipsec_is_tx_flow(struct mlx5e_accel_tx_ipsec_state *ipsec_st)
{
	return ipsec_st->x;
}

static inline bool mlx5e_ipsec_eseg_meta(struct mlx5_wqe_eth_seg *eseg)
{
	return eseg->flow_table_metadata & cpu_to_be32(MLX5_ETH_WQE_FT_META_IPSEC);
}

void mlx5e_ipsec_tx_build_eseg(struct mlx5e_priv *priv, struct sk_buff *skb,
			       struct mlx5_wqe_eth_seg *eseg);

static inline netdev_features_t
mlx5e_ipsec_feature_check(struct sk_buff *skb, netdev_features_t features)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct sec_path *sp = skb_sec_path(skb);

	if (sp && sp->len && xo) {
		struct xfrm_state *x = sp->xvec[0];

		if (!x || !x->xso.offload_handle)
			goto out_disable;

		if (xo->inner_ipproto) {
			/* Cannot support tunnel packet over IPsec tunnel mode
			 * because we cannot offload three IP header csum
			 */
			if (x->props.mode == XFRM_MODE_TUNNEL)
				goto out_disable;

			/* Only support UDP or TCP L4 checksum */
			if (xo->inner_ipproto != IPPROTO_UDP &&
			    xo->inner_ipproto != IPPROTO_TCP)
				goto out_disable;
		}

		return features;

	}

	/* Disable CSUM and GSO for software IPsec */
out_disable:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

#else
static inline
void mlx5e_ipsec_offload_handle_rx_skb(struct net_device *netdev,
				       struct sk_buff *skb,
				       struct mlx5_cqe64 *cqe)
{}

static inline bool mlx5e_ipsec_eseg_meta(struct mlx5_wqe_eth_seg *eseg)
{
	return false;
}

static inline bool mlx5_ipsec_is_rx_flow(struct mlx5_cqe64 *cqe) { return false; }
static inline netdev_features_t
mlx5e_ipsec_feature_check(struct sk_buff *skb, netdev_features_t features)
{ return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK); }
#endif /* CONFIG_MLX5_EN_IPSEC */

#endif /* __MLX5E_IPSEC_RXTX_H__ */
