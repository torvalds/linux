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
#include "en/txrx.h"

#define MLX5E_XDP_MIN_INLINE (ETH_HLEN + VLAN_HLEN)
#define MLX5E_XDP_TX_EMPTY_DS_COUNT \
	(sizeof(struct mlx5e_tx_wqe) / MLX5_SEND_WQE_DS)
#define MLX5E_XDP_TX_DS_COUNT (MLX5E_XDP_TX_EMPTY_DS_COUNT + 1 /* SG DS */)

#define MLX5E_XDPSQ_STOP_ROOM (MLX5E_SQ_STOP_ROOM)

#define MLX5E_XDP_INLINE_WQE_SZ_THRSD (256 - sizeof(struct mlx5_wqe_inline_seg))
#define MLX5E_XDP_INLINE_WQE_MAX_DS_CNT \
	DIV_ROUND_UP(MLX5E_XDP_INLINE_WQE_SZ_THRSD, MLX5_SEND_WQE_DS)

/* The mult of MLX5_SEND_WQE_MAX_WQEBBS * MLX5_SEND_WQEBB_NUM_DS
 * (16 * 4 == 64) does not fit in the 6-bit DS field of Ctrl Segment.
 * We use a bound lower that MLX5_SEND_WQE_MAX_WQEBBS to let a
 * full-session WQE be cache-aligned.
 */
#if L1_CACHE_BYTES < 128
#define MLX5E_XDP_MPW_MAX_WQEBBS (MLX5_SEND_WQE_MAX_WQEBBS - 1)
#else
#define MLX5E_XDP_MPW_MAX_WQEBBS (MLX5_SEND_WQE_MAX_WQEBBS - 2)
#endif

#define MLX5E_XDP_MPW_MAX_NUM_DS \
	(MLX5E_XDP_MPW_MAX_WQEBBS * MLX5_SEND_WQEBB_NUM_DS)

struct mlx5e_xsk_param;
int mlx5e_xdp_max_mtu(struct mlx5e_params *params, struct mlx5e_xsk_param *xsk);
bool mlx5e_xdp_handle(struct mlx5e_rq *rq, struct mlx5e_dma_info *di,
		      void *va, u16 *rx_headroom, u32 *len, bool xsk);
void mlx5e_xdp_mpwqe_complete(struct mlx5e_xdpsq *sq);
bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq);
void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq);
void mlx5e_set_xmit_fp(struct mlx5e_xdpsq *sq, bool is_mpw);
void mlx5e_xdp_rx_poll_complete(struct mlx5e_rq *rq);
int mlx5e_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		   u32 flags);

static inline void mlx5e_xdp_tx_enable(struct mlx5e_priv *priv)
{
	set_bit(MLX5E_STATE_XDP_TX_ENABLED, &priv->state);

	if (priv->channels.params.xdp_prog)
		set_bit(MLX5E_STATE_XDP_ACTIVE, &priv->state);
}

static inline void mlx5e_xdp_tx_disable(struct mlx5e_priv *priv)
{
	if (priv->channels.params.xdp_prog)
		clear_bit(MLX5E_STATE_XDP_ACTIVE, &priv->state);

	clear_bit(MLX5E_STATE_XDP_TX_ENABLED, &priv->state);
	/* Let other device's napi(s) and XSK wakeups see our new state. */
	synchronize_rcu();
}

static inline bool mlx5e_xdp_tx_is_enabled(struct mlx5e_priv *priv)
{
	return test_bit(MLX5E_STATE_XDP_TX_ENABLED, &priv->state);
}

static inline bool mlx5e_xdp_is_active(struct mlx5e_priv *priv)
{
	return test_bit(MLX5E_STATE_XDP_ACTIVE, &priv->state);
}

static inline void mlx5e_xmit_xdp_doorbell(struct mlx5e_xdpsq *sq)
{
	if (sq->doorbell_cseg) {
		mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, sq->doorbell_cseg);
		sq->doorbell_cseg = NULL;
	}
}

/* Enable inline WQEs to shift some load from a congested HCA (HW) to
 * a less congested cpu (SW).
 */
static inline void mlx5e_xdp_update_inline_state(struct mlx5e_xdpsq *sq)
{
	u16 outstanding = sq->xdpi_fifo_pc - sq->xdpi_fifo_cc;
	struct mlx5e_xdp_mpwqe *session = &sq->mpwqe;

#define MLX5E_XDP_INLINE_WATERMARK_LOW	10
#define MLX5E_XDP_INLINE_WATERMARK_HIGH 128

	if (session->inline_on) {
		if (outstanding <= MLX5E_XDP_INLINE_WATERMARK_LOW)
			session->inline_on = 0;
		return;
	}

	/* inline is false */
	if (outstanding >= MLX5E_XDP_INLINE_WATERMARK_HIGH)
		session->inline_on = 1;
}

static inline bool
mlx5e_xdp_no_room_for_inline_pkt(struct mlx5e_xdp_mpwqe *session)
{
	return session->inline_on &&
	       session->ds_count + MLX5E_XDP_INLINE_WQE_MAX_DS_CNT > MLX5E_XDP_MPW_MAX_NUM_DS;
}

static inline void
mlx5e_fill_xdpsq_frag_edge(struct mlx5e_xdpsq *sq, struct mlx5_wq_cyc *wq,
			   u16 pi, u16 nnops)
{
	struct mlx5e_xdp_wqe_info *edge_wi, *wi = &sq->db.wqe_info[pi];

	edge_wi = wi + nnops;
	/* fill sq frag edge with nops to avoid wqe wrapping two pages */
	for (; wi < edge_wi; wi++) {
		wi->num_wqebbs = 1;
		wi->num_pkts   = 0;
		mlx5e_post_nop(wq, sq->sqn, &sq->pc);
	}

	sq->stats->nops += nnops;
}

static inline void
mlx5e_xdp_mpwqe_add_dseg(struct mlx5e_xdpsq *sq,
			 struct mlx5e_xdp_xmit_data *xdptxd,
			 struct mlx5e_xdpsq_stats *stats)
{
	struct mlx5e_xdp_mpwqe *session = &sq->mpwqe;
	struct mlx5_wqe_data_seg *dseg =
		(struct mlx5_wqe_data_seg *)session->wqe + session->ds_count;
	u32 dma_len = xdptxd->len;

	session->pkt_count++;

	if (session->inline_on && dma_len <= MLX5E_XDP_INLINE_WQE_SZ_THRSD) {
		struct mlx5_wqe_inline_seg *inline_dseg =
			(struct mlx5_wqe_inline_seg *)dseg;
		u16 ds_len = sizeof(*inline_dseg) + dma_len;
		u16 ds_cnt = DIV_ROUND_UP(ds_len, MLX5_SEND_WQE_DS);

		inline_dseg->byte_count = cpu_to_be32(dma_len | MLX5_INLINE_SEG);
		memcpy(inline_dseg->data, xdptxd->data, dma_len);

		session->ds_count += ds_cnt;
		stats->inlnw++;
		return;
	}

	dseg->addr       = cpu_to_be64(xdptxd->dma_addr);
	dseg->byte_count = cpu_to_be32(dma_len);
	dseg->lkey       = sq->mkey_be;
	session->ds_count++;
}

static inline struct mlx5e_tx_wqe *
mlx5e_xdpsq_fetch_wqe(struct mlx5e_xdpsq *sq, u16 *pi)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	struct mlx5e_tx_wqe *wqe;

	*pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	wqe = mlx5_wq_cyc_get_wqe(wq, *pi);
	memset(wqe, 0, sizeof(*wqe));

	return wqe;
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
