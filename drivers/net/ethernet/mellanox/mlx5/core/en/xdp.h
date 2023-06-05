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

#include <linux/indirect_call_wrapper.h>

#include "en.h"
#include "en/txrx.h"

#define MLX5E_XDP_MIN_INLINE (ETH_HLEN + VLAN_HLEN)

#define MLX5E_XDP_INLINE_WQE_MAX_DS_CNT 16
#define MLX5E_XDP_INLINE_WQE_SZ_THRSD \
	(MLX5E_XDP_INLINE_WQE_MAX_DS_CNT * MLX5_SEND_WQE_DS - \
	 sizeof(struct mlx5_wqe_inline_seg))

struct mlx5e_xdp_buff {
	struct xdp_buff xdp;
	struct mlx5_cqe64 *cqe;
	struct mlx5e_rq *rq;
};

/* XDP packets can be transmitted in different ways. On completion, we need to
 * distinguish between them to clean up things in a proper way.
 */
enum mlx5e_xdp_xmit_mode {
	/* An xdp_frame was transmitted due to either XDP_REDIRECT from another
	 * device or XDP_TX from an XSK RQ. The frame has to be unmapped and
	 * returned.
	 */
	MLX5E_XDP_XMIT_MODE_FRAME,

	/* The xdp_frame was created in place as a result of XDP_TX from a
	 * regular RQ. No DMA remapping happened, and the page belongs to us.
	 */
	MLX5E_XDP_XMIT_MODE_PAGE,

	/* No xdp_frame was created at all, the transmit happened from a UMEM
	 * page. The UMEM Completion Ring producer pointer has to be increased.
	 */
	MLX5E_XDP_XMIT_MODE_XSK,
};

/* xmit_mode entry is pushed to the fifo per packet, followed by multiple
 * entries, as follows:
 *
 * MLX5E_XDP_XMIT_MODE_FRAME:
 *    xdpf, dma_addr_1, dma_addr_2, ... , dma_addr_num.
 *    'num' is derived from xdpf.
 *
 * MLX5E_XDP_XMIT_MODE_PAGE:
 *    num, page_1, page_2, ... , page_num.
 *
 * MLX5E_XDP_XMIT_MODE_XSK:
 *    none.
 */
union mlx5e_xdp_info {
	enum mlx5e_xdp_xmit_mode mode;
	union {
		struct xdp_frame *xdpf;
		dma_addr_t dma_addr;
	} frame;
	union {
		struct mlx5e_rq *rq;
		u8 num;
		struct page *page;
	} page;
};

struct mlx5e_xsk_param;
int mlx5e_xdp_max_mtu(struct mlx5e_params *params, struct mlx5e_xsk_param *xsk);
bool mlx5e_xdp_handle(struct mlx5e_rq *rq,
		      struct bpf_prog *prog, struct mlx5e_xdp_buff *mlctx);
void mlx5e_xdp_mpwqe_complete(struct mlx5e_xdpsq *sq);
bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq);
void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq);
void mlx5e_set_xmit_fp(struct mlx5e_xdpsq *sq, bool is_mpw);
void mlx5e_xdp_rx_poll_complete(struct mlx5e_rq *rq);
int mlx5e_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		   u32 flags);

extern const struct xdp_metadata_ops mlx5e_xdp_metadata_ops;

INDIRECT_CALLABLE_DECLARE(bool mlx5e_xmit_xdp_frame_mpwqe(struct mlx5e_xdpsq *sq,
							  struct mlx5e_xmit_data *xdptxd,
							  int check_result));
INDIRECT_CALLABLE_DECLARE(bool mlx5e_xmit_xdp_frame(struct mlx5e_xdpsq *sq,
						    struct mlx5e_xmit_data *xdptxd,
						    int check_result));
INDIRECT_CALLABLE_DECLARE(int mlx5e_xmit_xdp_frame_check_mpwqe(struct mlx5e_xdpsq *sq));
INDIRECT_CALLABLE_DECLARE(int mlx5e_xmit_xdp_frame_check(struct mlx5e_xdpsq *sq));

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
	synchronize_net();
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
static inline bool mlx5e_xdp_get_inline_state(struct mlx5e_xdpsq *sq, bool cur)
{
	u16 outstanding = sq->xdpi_fifo_pc - sq->xdpi_fifo_cc;

#define MLX5E_XDP_INLINE_WATERMARK_LOW	10
#define MLX5E_XDP_INLINE_WATERMARK_HIGH 128

	if (cur && outstanding <= MLX5E_XDP_INLINE_WATERMARK_LOW)
		return false;

	if (!cur && outstanding >= MLX5E_XDP_INLINE_WATERMARK_HIGH)
		return true;

	return cur;
}

static inline bool mlx5e_xdp_mpwqe_is_full(struct mlx5e_tx_mpwqe *session, u8 max_sq_mpw_wqebbs)
{
	if (session->inline_on)
		return session->ds_count + MLX5E_XDP_INLINE_WQE_MAX_DS_CNT >
		       max_sq_mpw_wqebbs * MLX5_SEND_WQEBB_NUM_DS;

	return mlx5e_tx_mpwqe_is_full(session, max_sq_mpw_wqebbs);
}

struct mlx5e_xdp_wqe_info {
	u8 num_wqebbs;
	u8 num_pkts;
};

static inline void
mlx5e_xdp_mpwqe_add_dseg(struct mlx5e_xdpsq *sq,
			 struct mlx5e_xmit_data *xdptxd,
			 struct mlx5e_xdpsq_stats *stats)
{
	struct mlx5e_tx_mpwqe *session = &sq->mpwqe;
	struct mlx5_wqe_data_seg *dseg =
		(struct mlx5_wqe_data_seg *)session->wqe + session->ds_count;
	u32 dma_len = xdptxd->len;

	session->pkt_count++;
	session->bytes_count += dma_len;

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

static inline void
mlx5e_xdpi_fifo_push(struct mlx5e_xdp_info_fifo *fifo,
		     union mlx5e_xdp_info xi)
{
	u32 i = (*fifo->pc)++ & fifo->mask;

	fifo->xi[i] = xi;
}

static inline union mlx5e_xdp_info
mlx5e_xdpi_fifo_pop(struct mlx5e_xdp_info_fifo *fifo)
{
	return fifo->xi[(*fifo->cc)++ & fifo->mask];
}
#endif
