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

#include <linux/bpf_trace.h>
#include "en/xdp.h"

static inline bool
mlx5e_xmit_xdp_buff(struct mlx5e_xdpsq *sq, struct mlx5e_dma_info *di,
		    struct xdp_buff *xdp)
{
	struct mlx5e_xdp_info xdpi;

	xdpi.xdpf = convert_to_xdp_frame(xdp);
	if (unlikely(!xdpi.xdpf))
		return false;
	xdpi.dma_addr = di->addr + (xdpi.xdpf->data - (void *)xdpi.xdpf);
	dma_sync_single_for_device(sq->pdev, xdpi.dma_addr,
				   xdpi.xdpf->len, PCI_DMA_TODEVICE);
	xdpi.di = *di;

	return mlx5e_xmit_xdp_frame(sq, &xdpi);
}

/* returns true if packet was consumed by xdp */
bool mlx5e_xdp_handle(struct mlx5e_rq *rq, struct mlx5e_dma_info *di,
		      void *va, u16 *rx_headroom, u32 *len)
{
	struct bpf_prog *prog = READ_ONCE(rq->xdp_prog);
	struct xdp_buff xdp;
	u32 act;
	int err;

	if (!prog)
		return false;

	xdp.data = va + *rx_headroom;
	xdp_set_data_meta_invalid(&xdp);
	xdp.data_end = xdp.data + *len;
	xdp.data_hard_start = va;
	xdp.rxq = &rq->xdp_rxq;

	act = bpf_prog_run_xdp(prog, &xdp);
	switch (act) {
	case XDP_PASS:
		*rx_headroom = xdp.data - xdp.data_hard_start;
		*len = xdp.data_end - xdp.data;
		return false;
	case XDP_TX:
		if (unlikely(!mlx5e_xmit_xdp_buff(&rq->xdpsq, di, &xdp)))
			goto xdp_abort;
		__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags); /* non-atomic */
		return true;
	case XDP_REDIRECT:
		/* When XDP enabled then page-refcnt==1 here */
		err = xdp_do_redirect(rq->netdev, &xdp, prog);
		if (unlikely(err))
			goto xdp_abort;
		__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags);
		rq->xdpsq.redirect_flush = true;
		mlx5e_page_dma_unmap(rq, di);
		rq->stats->xdp_redirect++;
		return true;
	default:
		bpf_warn_invalid_xdp_action(act);
		/* fall through */
	case XDP_ABORTED:
xdp_abort:
		trace_xdp_exception(rq->netdev, prog, act);
		/* fall through */
	case XDP_DROP:
		rq->stats->xdp_drop++;
		return true;
	}
}

bool mlx5e_xmit_xdp_frame(struct mlx5e_xdpsq *sq, struct mlx5e_xdp_info *xdpi)
{
	struct mlx5_wq_cyc       *wq   = &sq->wq;
	u16                       pi   = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	struct mlx5e_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);

	struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
	struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;
	struct mlx5_wqe_data_seg *dseg = wqe->data;

	struct xdp_frame *xdpf = xdpi->xdpf;
	dma_addr_t dma_addr  = xdpi->dma_addr;
	unsigned int dma_len = xdpf->len;

	struct mlx5e_xdpsq_stats *stats = sq->stats;

	prefetchw(wqe);

	if (unlikely(dma_len < MLX5E_XDP_MIN_INLINE || sq->hw_mtu < dma_len)) {
		stats->err++;
		return false;
	}

	if (unlikely(!mlx5e_wqc_has_room_for(wq, sq->cc, sq->pc, 1))) {
		if (sq->doorbell) {
			/* SQ is full, ring doorbell */
			mlx5e_xmit_xdp_doorbell(sq);
			sq->doorbell = false;
		}
		stats->full++;
		return false;
	}

	cseg->fm_ce_se = 0;

	/* copy the inline part if required */
	if (sq->min_inline_mode != MLX5_INLINE_MODE_NONE) {
		memcpy(eseg->inline_hdr.start, xdpf->data, MLX5E_XDP_MIN_INLINE);
		eseg->inline_hdr.sz = cpu_to_be16(MLX5E_XDP_MIN_INLINE);
		dma_len  -= MLX5E_XDP_MIN_INLINE;
		dma_addr += MLX5E_XDP_MIN_INLINE;
		dseg++;
	}

	/* write the dma part */
	dseg->addr       = cpu_to_be64(dma_addr);
	dseg->byte_count = cpu_to_be32(dma_len);

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | MLX5_OPCODE_SEND);

	/* move page to reference to sq responsibility,
	 * and mark so it's not put back in page-cache.
	 */
	sq->db.xdpi[pi] = *xdpi;
	sq->pc++;

	sq->doorbell = true;

	stats->xmit++;
	return true;
}

bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq)
{
	struct mlx5e_xdpsq *sq;
	struct mlx5_cqe64 *cqe;
	struct mlx5e_rq *rq;
	bool is_redirect;
	u16 sqcc;
	int i;

	sq = container_of(cq, struct mlx5e_xdpsq, cq);

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state)))
		return false;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return false;

	is_redirect = test_bit(MLX5E_SQ_STATE_REDIRECT, &sq->state);
	rq = container_of(sq, struct mlx5e_rq, xdpsq);

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	i = 0;
	do {
		u16 wqe_counter;
		bool last_wqe;

		mlx5_cqwq_pop(&cq->wq);

		wqe_counter = be16_to_cpu(cqe->wqe_counter);

		do {
			u16 ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sqcc);
			struct mlx5e_xdp_info *xdpi = &sq->db.xdpi[ci];

			last_wqe = (sqcc == wqe_counter);
			sqcc++;

			if (is_redirect) {
				xdp_return_frame(xdpi->xdpf);
				dma_unmap_single(sq->pdev, xdpi->dma_addr,
						 xdpi->xdpf->len, DMA_TO_DEVICE);
			} else {
				/* Recycle RX page */
				mlx5e_page_release(rq, &xdpi->di, true);
			}
		} while (!last_wqe);
	} while ((++i < MLX5E_TX_CQ_POLL_BUDGET) && (cqe = mlx5_cqwq_get_cqe(&cq->wq)));

	sq->stats->cqes += i;

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->cc = sqcc;
	return (i == MLX5E_TX_CQ_POLL_BUDGET);
}

void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq)
{
	struct mlx5e_rq *rq;
	bool is_redirect;

	is_redirect = test_bit(MLX5E_SQ_STATE_REDIRECT, &sq->state);
	rq = is_redirect ? NULL : container_of(sq, struct mlx5e_rq, xdpsq);

	while (sq->cc != sq->pc) {
		u16 ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->cc);
		struct mlx5e_xdp_info *xdpi = &sq->db.xdpi[ci];

		sq->cc++;

		if (is_redirect) {
			xdp_return_frame(xdpi->xdpf);
			dma_unmap_single(sq->pdev, xdpi->dma_addr,
					 xdpi->xdpf->len, DMA_TO_DEVICE);
		} else {
			/* Recycle RX page */
			mlx5e_page_release(rq, &xdpi->di, false);
		}
	}
}

int mlx5e_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		   u32 flags)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_xdpsq *sq;
	int drops = 0;
	int sq_num;
	int i;

	if (unlikely(!test_bit(MLX5E_STATE_OPENED, &priv->state)))
		return -ENETDOWN;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	sq_num = smp_processor_id();

	if (unlikely(sq_num >= priv->channels.num))
		return -ENXIO;

	sq = &priv->channels.c[sq_num]->xdpsq;

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state)))
		return -ENETDOWN;

	for (i = 0; i < n; i++) {
		struct xdp_frame *xdpf = frames[i];
		struct mlx5e_xdp_info xdpi;

		xdpi.dma_addr = dma_map_single(sq->pdev, xdpf->data, xdpf->len,
					       DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(sq->pdev, xdpi.dma_addr))) {
			xdp_return_frame_rx_napi(xdpf);
			drops++;
			continue;
		}

		xdpi.xdpf = xdpf;

		if (unlikely(!mlx5e_xmit_xdp_frame(sq, &xdpi))) {
			dma_unmap_single(sq->pdev, xdpi.dma_addr,
					 xdpf->len, DMA_TO_DEVICE);
			xdp_return_frame_rx_napi(xdpf);
			drops++;
		}
	}

	if (flags & XDP_XMIT_FLUSH)
		mlx5e_xmit_xdp_doorbell(sq);

	return n - drops;
}
