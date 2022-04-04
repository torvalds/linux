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

#ifndef __MLX5E_TLS_RXTX_H__
#define __MLX5E_TLS_RXTX_H__

#include "accel/accel.h"
#include "en_accel/ktls_txrx.h"

#ifdef CONFIG_MLX5_EN_TLS

#include <linux/skbuff.h>
#include "en.h"
#include "en/txrx.h"

u16 mlx5e_tls_get_stop_room(struct mlx5_core_dev *mdev, struct mlx5e_params *params);

bool mlx5e_tls_handle_tx_skb(struct net_device *netdev, struct mlx5e_txqsq *sq,
			     struct sk_buff *skb, struct mlx5e_accel_tx_tls_state *state);

static inline bool mlx5e_tls_skb_offloaded(struct sk_buff *skb)
{
	return skb->sk && tls_is_sk_tx_device_offloaded(skb->sk);
}

static inline void
mlx5e_tls_handle_tx_wqe(struct mlx5_wqe_ctrl_seg *cseg,
			struct mlx5e_accel_tx_tls_state *state)
{
	cseg->tis_tir_num = cpu_to_be32(state->tls_tisn << 8);
}

static inline void
mlx5e_tls_handle_rx_skb(struct mlx5e_rq *rq, struct sk_buff *skb,
			struct mlx5_cqe64 *cqe, u32 *cqe_bcnt)
{
	if (unlikely(get_cqe_tls_offload(cqe))) /* cqe bit indicates a TLS device */
		mlx5e_ktls_handle_rx_skb(rq, skb, cqe, cqe_bcnt);
}

#else

static inline bool
mlx5e_accel_is_tls(struct mlx5_cqe64 *cqe, struct sk_buff *skb) { return false; }
static inline void
mlx5e_tls_handle_rx_skb(struct mlx5e_rq *rq, struct sk_buff *skb,
			struct mlx5_cqe64 *cqe, u32 *cqe_bcnt) {}
static inline u16 mlx5e_tls_get_stop_room(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	return 0;
}

#endif /* CONFIG_MLX5_EN_TLS */

#endif /* __MLX5E_TLS_RXTX_H__ */
