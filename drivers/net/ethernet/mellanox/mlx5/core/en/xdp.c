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
		if (unlikely(!mlx5e_xmit_xdp_frame(rq, di, &xdp)))
			trace_xdp_exception(rq->netdev, prog, act);
		return true;
	case XDP_REDIRECT:
		/* When XDP enabled then page-refcnt==1 here */
		err = xdp_do_redirect(rq->netdev, &xdp, prog);
		if (!err) {
			__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags);
			rq->xdpsq.db.redirect_flush = true;
			mlx5e_page_dma_unmap(rq, di);
		}
		return true;
	default:
		bpf_warn_invalid_xdp_action(act);
	case XDP_ABORTED:
		trace_xdp_exception(rq->netdev, prog, act);
	case XDP_DROP:
		rq->stats->xdp_drop++;
		return true;
	}
}

bool mlx5e_xmit_xdp_frame(struct mlx5e_rq *rq, struct mlx5e_dma_info *di,
			  const struct xdp_buff *xdp)
{
	struct mlx5e_xdpsq       *sq   = &rq->xdpsq;
	struct mlx5_wq_cyc       *wq   = &sq->wq;
	u16                       pi   = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	struct mlx5e_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);

	struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
	struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;
	struct mlx5_wqe_data_seg *dseg;

	ptrdiff_t data_offset = xdp->data - xdp->data_hard_start;
	dma_addr_t dma_addr  = di->addr + data_offset;
	unsigned int dma_len = xdp->data_end - xdp->data;

	struct mlx5e_rq_stats *stats = rq->stats;

	prefetchw(wqe);

	if (unlikely(dma_len < MLX5E_XDP_MIN_INLINE || rq->hw_mtu < dma_len)) {
		stats->xdp_drop++;
		return false;
	}

	if (unlikely(!mlx5e_wqc_has_room_for(wq, sq->cc, sq->pc, 1))) {
		if (sq->db.doorbell) {
			/* SQ is full, ring doorbell */
			mlx5e_xmit_xdp_doorbell(sq);
			sq->db.doorbell = false;
		}
		stats->xdp_tx_full++;
		return false;
	}

	dma_sync_single_for_device(sq->pdev, dma_addr, dma_len, PCI_DMA_TODEVICE);

	cseg->fm_ce_se = 0;

	dseg = (struct mlx5_wqe_data_seg *)eseg + 1;

	/* copy the inline part if required */
	if (sq->min_inline_mode != MLX5_INLINE_MODE_NONE) {
		memcpy(eseg->inline_hdr.start, xdp->data, MLX5E_XDP_MIN_INLINE);
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
	__set_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags); /* non-atomic */
	sq->db.di[pi] = *di;
	sq->pc++;

	sq->db.doorbell = true;

	stats->xdp_tx++;
	return true;
}

bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq)
{
	struct mlx5e_xdpsq *sq;
	struct mlx5_cqe64 *cqe;
	struct mlx5e_rq *rq;
	u16 sqcc;
	int i;

	sq = container_of(cq, struct mlx5e_xdpsq, cq);

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state)))
		return false;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return false;

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
			struct mlx5e_dma_info *di;
			u16 ci;

			last_wqe = (sqcc == wqe_counter);

			ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sqcc);
			di = &sq->db.di[ci];

			sqcc++;
			/* Recycle RX page */
			mlx5e_page_release(rq, di, true);
		} while (!last_wqe);
	} while ((++i < MLX5E_TX_CQ_POLL_BUDGET) && (cqe = mlx5_cqwq_get_cqe(&cq->wq)));

	rq->stats->xdp_tx_cqe += i;

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->cc = sqcc;
	return (i == MLX5E_TX_CQ_POLL_BUDGET);
}

void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq)
{
	struct mlx5e_rq *rq = container_of(sq, struct mlx5e_rq, xdpsq);
	struct mlx5e_dma_info *di;
	u16 ci;

	while (sq->cc != sq->pc) {
		ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->cc);
		di = &sq->db.di[ci];
		sq->cc++;

		mlx5e_page_release(rq, di, false);
	}
}

