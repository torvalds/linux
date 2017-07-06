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

#include "en.h"

static inline void mlx5e_poll_ico_single_cqe(struct mlx5e_cq *cq,
					     struct mlx5e_icosq *sq,
					     struct mlx5_cqe64 *cqe,
					     u16 *sqcc)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 ci = be16_to_cpu(cqe->wqe_counter) & wq->sz_m1;
	struct mlx5e_sq_wqe_info *icowi = &sq->db.ico_wqe[ci];
	struct mlx5e_rq *rq = &sq->channel->rq;

	prefetch(rq);
	mlx5_cqwq_pop(&cq->wq);
	*sqcc += icowi->num_wqebbs;

	if (unlikely((cqe->op_own >> 4) != MLX5_CQE_REQ)) {
		WARN_ONCE(true, "mlx5e: Bad OP in ICOSQ CQE: 0x%x\n",
			  cqe->op_own);
		return;
	}

	if (likely(icowi->opcode == MLX5_OPCODE_UMR)) {
		mlx5e_post_rx_mpwqe(rq);
		return;
	}

	if (unlikely(icowi->opcode != MLX5_OPCODE_NOP))
		WARN_ONCE(true,
			  "mlx5e: Bad OPCODE in ICOSQ WQE info: 0x%x\n",
			  icowi->opcode);
}

static void mlx5e_poll_ico_cq(struct mlx5e_cq *cq)
{
	struct mlx5e_icosq *sq = container_of(cq, struct mlx5e_icosq, cq);
	struct mlx5_cqe64 *cqe;
	u16 sqcc;

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state)))
		return;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (likely(!cqe))
		return;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	/* by design, there's only a single cqe */
	mlx5e_poll_ico_single_cqe(cq, sq, cqe, &sqcc);

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->cc = sqcc;
}

int mlx5e_napi_poll(struct napi_struct *napi, int budget)
{
	struct mlx5e_channel *c = container_of(napi, struct mlx5e_channel,
					       napi);
	bool busy = false;
	int work_done;
	int i;

	clear_bit(MLX5E_CHANNEL_NAPI_SCHED, &c->flags);

	for (i = 0; i < c->num_tc; i++)
		busy |= mlx5e_poll_tx_cq(&c->sq[i].cq, budget);

	if (c->xdp)
		busy |= mlx5e_poll_xdpsq_cq(&c->rq.xdpsq.cq);

	work_done = mlx5e_poll_rx_cq(&c->rq.cq, budget);
	busy |= work_done == budget;

	mlx5e_poll_ico_cq(&c->icosq.cq);

	busy |= mlx5e_post_rx_wqes(&c->rq);

	if (busy)
		return budget;

	napi_complete_done(napi, work_done);

	/* avoid losing completion event during/after polling cqs */
	if (test_bit(MLX5E_CHANNEL_NAPI_SCHED, &c->flags)) {
		napi_schedule(napi);
		return work_done;
	}

	for (i = 0; i < c->num_tc; i++)
		mlx5e_cq_arm(&c->sq[i].cq);

	if (test_bit(MLX5E_RQ_STATE_AM, &c->rq.state))
		mlx5e_rx_am(&c->rq);

	mlx5e_cq_arm(&c->rq.cq);
	mlx5e_cq_arm(&c->icosq.cq);

	return work_done;
}

void mlx5e_completion_event(struct mlx5_core_cq *mcq)
{
	struct mlx5e_cq *cq = container_of(mcq, struct mlx5e_cq, mcq);

	cq->event_ctr++;
	set_bit(MLX5E_CHANNEL_NAPI_SCHED, &cq->channel->flags);
	napi_schedule(cq->napi);
}

void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event)
{
	struct mlx5e_cq *cq = container_of(mcq, struct mlx5e_cq, mcq);
	struct mlx5e_channel *c = cq->channel;
	struct net_device *netdev = c->netdev;

	netdev_err(netdev, "%s: cqn=0x%.6x event=0x%.2x\n",
		   __func__, mcq->cqn, event);
}
