/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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

#ifndef __MLX5E_EN_ACCEL_H__
#define __MLX5E_EN_ACCEL_H__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "en_accel/ipsec_rxtx.h"
#include "en_accel/ktls.h"
#include "en_accel/ktls_txrx.h"
#include <en_accel/macsec.h>
#include "en.h"
#include "en/txrx.h"

#if IS_ENABLED(CONFIG_GENEVE)
#include <net/geneve.h>

static inline bool mlx5_geneve_tx_allowed(struct mlx5_core_dev *mdev)
{
	return mlx5_tx_swp_supported(mdev);
}

static inline void
mlx5e_tx_tunnel_accel(struct sk_buff *skb, struct mlx5_wqe_eth_seg *eseg, u16 ihs)
{
	struct mlx5e_swp_spec swp_spec = {};
	unsigned int offset = 0;
	__be16 l3_proto;
	u8 l4_proto;

	l3_proto = vlan_get_protocol(skb);
	switch (l3_proto) {
	case htons(ETH_P_IP):
		l4_proto = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_proto = ipv6_find_hdr(skb, &offset, -1, NULL, NULL);
		break;
	default:
		return;
	}

	if (l4_proto != IPPROTO_UDP ||
	    udp_hdr(skb)->dest != cpu_to_be16(GENEVE_UDP_PORT))
		return;
	swp_spec.l3_proto = l3_proto;
	swp_spec.l4_proto = l4_proto;
	swp_spec.is_tun = true;
	if (inner_ip_hdr(skb)->version == 6) {
		swp_spec.tun_l3_proto = htons(ETH_P_IPV6);
		swp_spec.tun_l4_proto = inner_ipv6_hdr(skb)->nexthdr;
	} else {
		swp_spec.tun_l3_proto = htons(ETH_P_IP);
		swp_spec.tun_l4_proto = inner_ip_hdr(skb)->protocol;
	}

	mlx5e_set_eseg_swp(skb, eseg, &swp_spec);
	if (skb_vlan_tag_present(skb) && ihs)
		mlx5e_eseg_swp_offsets_add_vlan(eseg);
}

#else
static inline bool mlx5_geneve_tx_allowed(struct mlx5_core_dev *mdev)
{
	return false;
}

#endif /* CONFIG_GENEVE */

static inline void
mlx5e_udp_gso_handle_tx_skb(struct sk_buff *skb)
{
	int payload_len = skb_shinfo(skb)->gso_size + sizeof(struct udphdr);

	udp_hdr(skb)->len = htons(payload_len);
}

struct mlx5e_accel_tx_state {
#ifdef CONFIG_MLX5_EN_TLS
	struct mlx5e_accel_tx_tls_state tls;
#endif
#ifdef CONFIG_MLX5_EN_IPSEC
	struct mlx5e_accel_tx_ipsec_state ipsec;
#endif
};

static inline bool mlx5e_accel_tx_begin(struct net_device *dev,
					struct mlx5e_txqsq *sq,
					struct sk_buff *skb,
					struct mlx5e_accel_tx_state *state)
{
	if (skb_is_gso(skb) && skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4)
		mlx5e_udp_gso_handle_tx_skb(skb);

#ifdef CONFIG_MLX5_EN_TLS
	/* May send WQEs. */
	if (mlx5e_ktls_skb_offloaded(skb))
		if (unlikely(!mlx5e_ktls_handle_tx_skb(dev, sq, skb,
						       &state->tls)))
			return false;
#endif

#ifdef CONFIG_MLX5_EN_IPSEC
	if (test_bit(MLX5E_SQ_STATE_IPSEC, &sq->state) && xfrm_offload(skb)) {
		if (unlikely(!mlx5e_ipsec_handle_tx_skb(dev, skb, &state->ipsec)))
			return false;
	}
#endif

#ifdef CONFIG_MLX5_EN_MACSEC
	if (unlikely(mlx5e_macsec_skb_is_offload(skb))) {
		struct mlx5e_priv *priv = netdev_priv(dev);

		if (unlikely(!mlx5e_macsec_handle_tx_skb(priv->macsec, skb)))
			return false;
	}
#endif

	return true;
}

static inline unsigned int mlx5e_accel_tx_ids_len(struct mlx5e_txqsq *sq,
						  struct mlx5e_accel_tx_state *state)
{
#ifdef CONFIG_MLX5_EN_IPSEC
	if (test_bit(MLX5E_SQ_STATE_IPSEC, &sq->state))
		return mlx5e_ipsec_tx_ids_len(&state->ipsec);
#endif

	return 0;
}

/* Part of the eseg touched by TX offloads */
#define MLX5E_ACCEL_ESEG_LEN offsetof(struct mlx5_wqe_eth_seg, mss)

static inline void mlx5e_accel_tx_eseg(struct mlx5e_priv *priv,
				       struct sk_buff *skb,
				       struct mlx5_wqe_eth_seg *eseg, u16 ihs)
{
#ifdef CONFIG_MLX5_EN_IPSEC
	if (xfrm_offload(skb))
		mlx5e_ipsec_tx_build_eseg(priv, skb, eseg);
#endif

#ifdef CONFIG_MLX5_EN_MACSEC
	if (unlikely(mlx5e_macsec_skb_is_offload(skb)))
		mlx5e_macsec_tx_build_eseg(priv->macsec, skb, eseg);
#endif

#if IS_ENABLED(CONFIG_GENEVE)
	if (skb->encapsulation && skb->ip_summed == CHECKSUM_PARTIAL)
		mlx5e_tx_tunnel_accel(skb, eseg, ihs);
#endif
}

static inline void mlx5e_accel_tx_finish(struct mlx5e_txqsq *sq,
					 struct mlx5e_tx_wqe *wqe,
					 struct mlx5e_accel_tx_state *state,
					 struct mlx5_wqe_inline_seg *inlseg)
{
#ifdef CONFIG_MLX5_EN_TLS
	mlx5e_ktls_handle_tx_wqe(&wqe->ctrl, &state->tls);
#endif

#ifdef CONFIG_MLX5_EN_IPSEC
	if (test_bit(MLX5E_SQ_STATE_IPSEC, &sq->state) &&
	    state->ipsec.xo && state->ipsec.tailen)
		mlx5e_ipsec_handle_tx_wqe(wqe, &state->ipsec, inlseg);
#endif
}

static inline int mlx5e_accel_init_rx(struct mlx5e_priv *priv)
{
	return mlx5e_ktls_init_rx(priv);
}

static inline void mlx5e_accel_cleanup_rx(struct mlx5e_priv *priv)
{
	mlx5e_ktls_cleanup_rx(priv);
}

static inline int mlx5e_accel_init_tx(struct mlx5e_priv *priv)
{
	return mlx5e_ktls_init_tx(priv);
}

static inline void mlx5e_accel_cleanup_tx(struct mlx5e_priv *priv)
{
	mlx5e_ktls_cleanup_tx(priv);
}
#endif /* __MLX5E_EN_ACCEL_H__ */
