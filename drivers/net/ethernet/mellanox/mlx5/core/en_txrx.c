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
#include "en/txrx.h"
#include "en/xdp.h"
#include "en/xsk/rx.h"
#include "en/xsk/tx.h"
#include "en_accel/ktls_txrx.h"

static inline bool mlx5e_channel_no_affinity_change(struct mlx5e_channel *c)
{
	int current_cpu = smp_processor_id();

	return cpumask_test_cpu(current_cpu, c->aff_mask);
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

	sq->db.wqe_info[pi] = (struct mlx5e_icosq_wqe_info) {
		.wqe_type   = MLX5E_ICOSQ_WQE_NOP,
		.num_wqebbs = 1,
	};

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

	xsk_rx_alloc_err = INDIRECT_CALL_2(xskrq->post_wqes,
					   mlx5e_post_rx_mpwqes,
					   mlx5e_post_rx_wqes,
					   xskrq);
	busy_xsk |= mlx5e_xsk_update_rx_wakeup(xskrq, xsk_rx_alloc_err);

	return busy_xsk;
}

int mlx5e_napi_poll(struct napi_struct *napi, int budget)
{
	struct mlx5e_channel *c = container_of(napi, struct mlx5e_channel,
					       napi);
	struct mlx5e_ch_stats *ch_stats = c->stats;
	struct mlx5e_xdpsq *xsksq = &c->xsksq;
	struct mlx5e_txqsq __rcu **qos_sqs;
	struct mlx5e_rq *xskrq = &c->xskrq;
	struct mlx5e_rq *rq = &c->rq;
	bool aff_change = false;
	bool busy_xsk = false;
	bool busy = false;
	int work_done = 0;
	u16 qos_sqs_size;
	bool xsk_open;
	int i;

	rcu_read_lock();

	qos_sqs = rcu_dereference(c->qos_sqs);

	xsk_open = test_bit(MLX5E_CHANNEL_STATE_XSK, c->state);

	ch_stats->poll++;

	for (i = 0; i < c->num_tc; i++)
		busy |= mlx5e_poll_tx_cq(&c->sq[i].cq, budget);

	if (unlikely(qos_sqs)) {
		smp_rmb(); /* Pairs with mlx5e_qos_alloc_queues. */
		qos_sqs_size = READ_ONCE(c->qos_sqs_size);

		for (i = 0; i < qos_sqs_size; i++) {
			struct mlx5e_txqsq *sq = rcu_dereference(qos_sqs[i]);

			if (sq)
				busy |= mlx5e_poll_tx_cq(&sq->cq, budget);
		}
	}

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
	if (mlx5e_poll_ico_cq(&c->async_icosq.cq))
		/* Don't clear the flag if nothing was polled to prevent
		 * queueing more WQEs and overflowing the async ICOSQ.
		 */
		clear_bit(MLX5E_SQ_STATE_PENDING_XSK_TX, &c->async_icosq.state);

	/* Keep after async ICOSQ CQ poll */
	if (unlikely(mlx5e_ktls_rx_pending_resync_list(c, budget)))
		busy |= mlx5e_ktls_rx_handle_resync_list(c, budget);

	busy |= INDIRECT_CALL_2(rq->post_wqes,
				mlx5e_post_rx_mpwqes,
				mlx5e_post_rx_wqes,
				rq);
	if (xsk_open) {
		busy |= mlx5e_poll_xdpsq_cq(&xsksq->cq);
		busy_xsk |= mlx5e_napi_xsk_post(xsksq, xskrq);
	}

	busy |= busy_xsk;

	if (busy) {
		if (likely(mlx5e_channel_no_affinity_change(c))) {
			work_done = budget;
			goto out;
		}
		ch_stats->aff_change++;
		aff_change = true;
		if (budget && work_done == budget)
			work_done--;
	}

	if (unlikely(!napi_complete_done(napi, work_done)))
		goto out;

	ch_stats->arm++;

	for (i = 0; i < c->num_tc; i++) {
		mlx5e_handle_tx_dim(&c->sq[i]);
		mlx5e_cq_arm(&c->sq[i].cq);
	}
	if (unlikely(qos_sqs)) {
		for (i = 0; i < qos_sqs_size; i++) {
			struct mlx5e_txqsq *sq = rcu_dereference(qos_sqs[i]);

			if (sq) {
				mlx5e_handle_tx_dim(sq);
				mlx5e_cq_arm(&sq->cq);
			}
		}
	}

	mlx5e_handle_rx_dim(rq);

	mlx5e_cq_arm(&rq->cq);
	mlx5e_cq_arm(&c->icosq.cq);
	mlx5e_cq_arm(&c->async_icosq.cq);
	mlx5e_cq_arm(&c->xdpsq.cq);

	if (xsk_open) {
		mlx5e_handle_rx_dim(xskrq);
		mlx5e_cq_arm(&xsksq->cq);
		mlx5e_cq_arm(&xskrq->cq);
	}

	if (unlikely(aff_change && busy_xsk)) {
		mlx5e_trigger_irq(&c->icosq);
		ch_stats->force_irq++;
	}

out:
	rcu_read_unlock();

	return work_done;
}

void mlx5e_completion_event(struct mlx5_core_cq *mcq, struct mlx5_eqe *eqe)
{
	struct mlx5e_cq *cq = container_of(mcq, struct mlx5e_cq, mcq);

	napi_schedule(cq->napi);
	cq->event_ctr++;
	cq->ch_stats->events++;
}

void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event)
{
	struct mlx5e_cq *cq = container_of(mcq, struct mlx5e_cq, mcq);
	struct net_device *netdev = cq->netdev;

	netdev_err(netdev, "%s: cqn=0x%.6x event=0x%.2x\n",
		   __func__, mcq->cqn, event);
}
