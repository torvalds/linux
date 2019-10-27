// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "tx.h"
#include "umem.h"
#include "en/xdp.h"
#include "en/params.h"
#include <net/xdp_sock.h>

int mlx5e_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5e_channel *c;
	u16 ix;

	if (unlikely(!mlx5e_xdp_is_open(priv)))
		return -ENETDOWN;

	if (unlikely(!mlx5e_qid_get_ch_if_in_group(params, qid, MLX5E_RQ_GROUP_XSK, &ix)))
		return -EINVAL;

	c = priv->channels.c[ix];

	if (unlikely(!test_bit(MLX5E_CHANNEL_STATE_XSK, c->state)))
		return -ENXIO;

	if (!napi_if_scheduled_mark_missed(&c->napi)) {
		/* To avoid WQE overrun, don't post a NOP if XSKICOSQ is not
		 * active and not polled by NAPI. Return 0, because the upcoming
		 * activate will trigger the IRQ for us.
		 */
		if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &c->xskicosq.state)))
			return 0;

		spin_lock(&c->xskicosq_lock);
		mlx5e_trigger_irq(&c->xskicosq);
		spin_unlock(&c->xskicosq_lock);
	}

	return 0;
}

/* When TX fails (because of the size of the packet), we need to get completions
 * in order, so post a NOP to get a CQE. Since AF_XDP doesn't distinguish
 * between successful TX and errors, handling in mlx5e_poll_xdpsq_cq is the
 * same.
 */
static void mlx5e_xsk_tx_post_err(struct mlx5e_xdpsq *sq,
				  struct mlx5e_xdp_info *xdpi)
{
	u16 pi = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->pc);
	struct mlx5e_xdp_wqe_info *wi = &sq->db.wqe_info[pi];
	struct mlx5e_tx_wqe *nopwqe;

	wi->num_wqebbs = 1;
	wi->num_pkts = 1;

	nopwqe = mlx5e_post_nop(&sq->wq, sq->sqn, &sq->pc);
	mlx5e_xdpi_fifo_push(&sq->db.xdpi_fifo, xdpi);
	sq->doorbell_cseg = &nopwqe->ctrl;
}

bool mlx5e_xsk_tx(struct mlx5e_xdpsq *sq, unsigned int budget)
{
	struct xdp_umem *umem = sq->umem;
	struct mlx5e_xdp_info xdpi;
	struct mlx5e_xdp_xmit_data xdptxd;
	bool work_done = true;
	bool flush = false;

	xdpi.mode = MLX5E_XDP_XMIT_MODE_XSK;

	for (; budget; budget--) {
		int check_result = sq->xmit_xdp_frame_check(sq);
		struct xdp_desc desc;

		if (unlikely(check_result < 0)) {
			work_done = false;
			break;
		}

		if (!xsk_umem_consume_tx(umem, &desc)) {
			/* TX will get stuck until something wakes it up by
			 * triggering NAPI. Currently it's expected that the
			 * application calls sendto() if there are consumed, but
			 * not completed frames.
			 */
			break;
		}

		xdptxd.dma_addr = xdp_umem_get_dma(umem, desc.addr);
		xdptxd.data = xdp_umem_get_data(umem, desc.addr);
		xdptxd.len = desc.len;

		dma_sync_single_for_device(sq->pdev, xdptxd.dma_addr,
					   xdptxd.len, DMA_BIDIRECTIONAL);

		if (unlikely(!sq->xmit_xdp_frame(sq, &xdptxd, &xdpi, check_result))) {
			if (sq->mpwqe.wqe)
				mlx5e_xdp_mpwqe_complete(sq);

			mlx5e_xsk_tx_post_err(sq, &xdpi);
		}

		flush = true;
	}

	if (flush) {
		if (sq->mpwqe.wqe)
			mlx5e_xdp_mpwqe_complete(sq);
		mlx5e_xmit_xdp_doorbell(sq);

		xsk_umem_consume_tx_done(umem);
	}

	return !(budget && work_done);
}
