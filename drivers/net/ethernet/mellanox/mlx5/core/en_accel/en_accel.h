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
#include "en_accel/tls_rxtx.h"
#include "en.h"

static inline void
mlx5e_udp_gso_handle_tx_skb(struct sk_buff *skb)
{
	int payload_len = skb_shinfo(skb)->gso_size + sizeof(struct udphdr);

	udp_hdr(skb)->len = htons(payload_len);
}

static inline struct sk_buff *
mlx5e_accel_handle_tx(struct sk_buff *skb,
		      struct mlx5e_txqsq *sq,
		      struct net_device *dev,
		      struct mlx5e_tx_wqe **wqe,
		      u16 *pi)
{
#ifdef CONFIG_MLX5_EN_TLS
	if (test_bit(MLX5E_SQ_STATE_TLS, &sq->state)) {
		skb = mlx5e_tls_handle_tx_skb(dev, sq, skb, wqe, pi);
		if (unlikely(!skb))
			return NULL;
	}
#endif

#ifdef CONFIG_MLX5_EN_IPSEC
	if (test_bit(MLX5E_SQ_STATE_IPSEC, &sq->state)) {
		skb = mlx5e_ipsec_handle_tx_skb(dev, *wqe, skb);
		if (unlikely(!skb))
			return NULL;
	}
#endif

	if (skb_is_gso(skb) && skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4)
		mlx5e_udp_gso_handle_tx_skb(skb);

	return skb;
}

#endif /* __MLX5E_EN_ACCEL_H__ */
