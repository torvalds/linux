/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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
#ifndef __MLX5_EN_XDP_H__
#define __MLX5_EN_XDP_H__

#include "en.h"

#define MLX5E_XDP_MIN_INLINE (ETH_HLEN + VLAN_HLEN)
#define MLX5E_XDP_TX_EMPTY_DS_COUNT \
	(sizeof(struct mlx5e_tx_wqe) / MLX5_SEND_WQE_DS)
#define MLX5E_XDP_TX_DS_COUNT (MLX5E_XDP_TX_EMPTY_DS_COUNT + 1 /* SG DS */)

int mlx5e_xdp_max_mtu(struct mlx5e_params *params);
bool mlx5e_xdp_handle(struct mlx5e_rq *rq, struct mlx5e_dma_info *di,
		      void *va, u16 *rx_headroom, u32 *len);
bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq, struct mlx5e_rq *rq);
void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq, struct mlx5e_rq *rq);
void mlx5e_set_xmit_fp(struct mlx5e_xdpsq *sq, bool is_mpw);
void mlx5e_xdp_rx_poll_complete(struct mlx5e_rq *rq);
int mlx5e_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		   u32 flags);

static inline void mlx5e_xdp_tx_enable(struct mlx5e_priv *priv)
{
	set_bit(MLX5E_STATE_XDP_TX_ENABLED, &priv->state);
}

static inline void mlx5e_xdp_tx_disable(struct mlx5e_priv *priv)
{
	clear_bit(MLX5E_STATE_XDP_TX_ENABLED, &priv->state);
	/* let other device's napi(s) see our new state */
	synchronize_rcu();
}

static inline bool mlx5e_xdp_tx_is_enabled(struct mlx5e_priv *priv)
{
	return test_bit(MLX5E_STATE_XDP_TX_ENABLED, &priv->state);
}

static inline void mlx5e_xmit_xdp_doorbell(struct mlx5e_xdpsq *sq)
{
	if (sq->doorbell_cseg) {
		mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, sq->doorbell_cseg);
		sq->doorbell_cseg = NULL;
	}
}

static inline void
mlx5e_xdp_mpwqe_add_dseg(struct mlx5e_xdpsq *sq, dma_addr_t dma_addr, u16 dma_len)
{
	struct mlx5e_xdp_mpwqe *session = &sq->mpwqe;
	struct mlx5_wqe_data_seg *dseg =
		(struct mlx5_wqe_data_seg *)session->wqe + session->ds_count++;

	dseg->addr       = cpu_to_be64(dma_addr);
	dseg->byte_count = cpu_to_be32(dma_len);
	dseg->lkey       = sq->mkey_be;
}

static inline void mlx5e_xdpsq_fetch_wqe(struct mlx5e_xdpsq *sq,
					 struct mlx5e_tx_wqe **wqe)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);

	*wqe = mlx5_wq_cyc_get_wqe(wq, pi);
	memset(*wqe, 0, sizeof(**wqe));
}

static inline void
mlx5e_xdpi_fifo_push(struct mlx5e_xdp_info_fifo *fifo,
		     struct mlx5e_xdp_info *xi)
{
	u32 i = (*fifo->pc)++ & fifo->mask;

	fifo->xi[i] = *xi;
}

static inline struct mlx5e_xdp_info
mlx5e_xdpi_fifo_pop(struct mlx5e_xdp_info_fifo *fifo)
{
	return fifo->xi[(*fifo->cc)++ & fifo->mask];
}

#endif
