/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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

#include <linux/irq.h>
#include "en.h"
#include "en/xdp.h"
#include "en/xsk/rx.h"
#include "en/xsk/tx.h"

static inline bool mlx5e_channel_no_affinity_change(struct mlx5e_channel *c)
{
	int current_cpu = smp_processor_id();
	const struct cpumask *aff;
	struct irq_data *idata;

	idata = irq_desc_get_irq_data(c->irq_desc);
	aff = irq_data_get_affinity_mask(idata);
	return cpumask_test_cpu(current_cpu, aff);
}

static void mlx5e_handle_tx_dim(struct mlx5e_txqsq *sq)
{
	struct mlx5e_sq_stats *stats = sq->stats;
	struct dim_sample dim_sample = {};

	if (unlikely(!test_bit(MLX5E_SQ_STATE_AM, &sq->state)))
		return;

	dim_update_sample(sq->cq.event_ctr, stats->packets, stats->bytes, &dim_sample);
	net_dim(&sq->dim, dim_sample);
}

static void mlx5e_handle_rx_dim(struct mlx5e_rq *rq)
{
	struct mlx5e_rq_stats *stats = rq->stats;
	struct dim_sample dim_sample = {};

	if (unlikely(!test_bit(MLX5E_RQ_STATE_AM, &rq->state)))
		return;

	dim_update_sample(rq->cq.event_ctr, stats->packets, stats->bytes, &dim_sample);
	net_dim(&rq->dim, dim_sample);
}

void mlx5e_trigger_irq(struct mlx5e_icosq *sq)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	struct mlx5e_tx_wqe *nopwqe;
	u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);

	sq->db.ico_wqe[pi].opcode = MLX5_OPCODE_NOP;
	nopwqe = mlx5e_post_nop(wq, sq->sqn, &sq->pc);
	mlx5e_notify_hw(wq, sq->pc, sq->uar_map, &nopwqe->ctrl);
}

static bool mlx5e_napi_xsk_post(struct mlx5e_xdpsq *xsksq, struct mlx5e_rq *xskrq)
{
	bool busy_xsk = false, xsk_rx_alloc_err;

	/* Handle the race between the application querying need_wakeup and the
	 * driver setting it:
	 * 1. Update need_wakeup both before and after the TX. If it goes to
	 * "yes", it can only happen with the first update.
	 * 2. If the application queried need_wakeup before we set it, the
	 * packets will be transmitted anyway, even w/o a wakeup.
	 * 3. Give a chance to clear need_wakeup after new packets were queued
	 * for TX.
	 */
	mlx5e_xsk_update_tx_wakeup(xsksq);
	busy_xsk |= mlx5e_xsk_tx(xsksq, MLX5E_TX_XSK_POLL_BUDGET);
	mlx5e_xsk_update_tx_wakeup(xsksq);

	xsk_rx_alloc_err = xskrq->post_wqes(xskrq);
	busy_xsk |= mlx5e_xsk_update_rx_wakeup(xskrq, xsk_rx_alloc_err);

	return busy_xsk;
}

int mlx5e_napi_poll(struct napi_struct *napi, int budget)
{
	struct mlx5e_channel *c = container_of(napi, struct mlx5e_channel,
					       napi);
	struct mlx5e_ch_stats *ch_stats = c->stats;
	struct mlx5e_xdpsq *xsksq = &c->xsksq;
	struct mlx5e_rq *xskrq = &c->xskrq;
	struct mlx5e_rq *rq = &c->rq;
	bool xsk_open = test_bit(MLX5E_CHANNEL_STATE_XSK, c->state);
	bool aff_change = false;
	bool busy_xsk = false;
	bool busy = false;
	int work_done = 0;
	int i;

	ch_stats->poll++;

	for (i = 0; i < c->num_tc; i++)
		busy |= mlx5e_poll_tx_cq(&c->sq[i].cq, budget);

	busy |= mlx5e_poll_xdpsq_cq(&c->xdpsq.cq);

	if (c->xdp)
		busy |= mlx5e_poll_xdpsq_cq(&c->rq_xdpsq.cq);

	if (likely(budget)) { /* budget=0 means: don't poll rx rings */
		if (xsk_open)
			work_done = mlx5e_poll_rx_cq(&xskrq->cq, budget);

		if (likely(budget - work_done))
			work_done += mlx5e_poll_rx_cq(&rq->cq, budget - work_done);

		busy |= work_done == budget;
	}

	mlx5e_poll_ico_cq(&c->icosq.cq);

	busy |= rq->post_wqes(rq);
	if (xsk_open) {
		mlx5e_poll_ico_cq(&c->xskicosq.cq);
		busy |= mlx5e_poll_xdpsq_cq(&xsksq->cq);
		busy_xsk |= mlx5e_napi_xsk_post(xsksq, xskrq);
	}

	busy |= busy_xsk;

	if (busy) {
		if (likely(mlx5e_channel_no_affinity_change(c)))
			return budget;
		ch_stats->aff_change++;
		aff_change = true;
		if (budget && work_done == budget)
			work_done--;
	}

	if (unlikely(!napi_complete_done(napi, work_done)))
		return work_done;

	ch_stats->arm++;

	for (i = 0; i < c->num_tc; i++) {
		mlx5e_handle_tx_dim(&c->sq[i]);
		mlx5e_cq_arm(&c->sq[i].cq);
	}

	mlx5e_handle_rx_dim(rq);

	mlx5e_cq_arm(&rq->cq);
	mlx5e_cq_arm(&c->icosq.cq);
	mlx5e_cq_arm(&c->xdpsq.cq);

	if (xsk_open) {
		mlx5e_handle_rx_dim(xskrq);
		mlx5e_cq_arm(&c->xskicosq.cq);
		mlx5e_cq_arm(&xsksq->cq);
		mlx5e_cq_arm(&xskrq->cq);
	}

	if (unlikely(aff_change && busy_xsk)) {
		mlx5e_trigger_irq(&c->icosq);
		ch_stats->force_irq++;
	}

	return work_done;
}

void mlx5e_completion_event(struct mlx5_core_cq *mcq, struct mlx5_eqe *eqe)
{
	struct mlx5e_cq *cq = container_of(mcq, struct mlx5e_cq, mcq);

	napi_schedule(cq->napi);
	cq->event_ctr++;
	cq->channel->stats->events++;
}

void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event)
{
	struct mlx5e_cq *cq = container_of(mcq, struct mlx5e_cq, mcq);
	struct mlx5e_channel *c = cq->channel;
	struct net_device *netdev = c->netdev;

	netdev_err(netdev, "%s: cqn=0x%.6x event=0x%.2x\n",
		   __func__, mcq->cqn, event);
}
