/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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
#include "en/dim.h"

static void
mlx5e_complete_dim_work(struct dim *dim, struct dim_cq_moder moder,
			struct mlx5_core_dev *mdev, struct mlx5_core_cq *mcq)
{
	mlx5e_modify_cq_moderation(mdev, mcq, moder.usec, moder.pkts,
				   mlx5e_cq_period_mode(moder.cq_period_mode));
	dim->state = DIM_START_MEASURE;
}

void mlx5e_rx_dim_work(struct work_struct *work)
{
	struct dim *dim = container_of(work, struct dim, work);
	struct mlx5e_rq *rq = dim->priv;
	struct dim_cq_moder cur_moder =
		net_dim_get_rx_moderation(dim->mode, dim->profile_ix);

	mlx5e_complete_dim_work(dim, cur_moder, rq->mdev, &rq->cq.mcq);
}

void mlx5e_tx_dim_work(struct work_struct *work)
{
	struct dim *dim = container_of(work, struct dim, work);
	struct mlx5e_txqsq *sq = dim->priv;
	struct dim_cq_moder cur_moder =
		net_dim_get_tx_moderation(dim->mode, dim->profile_ix);

	mlx5e_complete_dim_work(dim, cur_moder, sq->cq.mdev, &sq->cq.mcq);
}

static struct dim *mlx5e_dim_enable(struct mlx5_core_dev *mdev,
				    void (*work_fun)(struct work_struct *), int cpu,
				    u8 cq_period_mode, struct mlx5_core_cq *mcq,
				    void *queue)
{
	struct dim *dim;
	int err;

	dim = kvzalloc_node(sizeof(*dim), GFP_KERNEL, cpu_to_node(cpu));
	if (!dim)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&dim->work, work_fun);

	dim->mode = cq_period_mode;
	dim->priv = queue;

	err = mlx5e_modify_cq_period_mode(mdev, mcq, dim->mode);
	if (err) {
		kvfree(dim);
		return ERR_PTR(err);
	}

	return dim;
}

static void mlx5e_dim_disable(struct dim *dim)
{
	cancel_work_sync(&dim->work);
	kvfree(dim);
}

int mlx5e_dim_rx_change(struct mlx5e_rq *rq, bool enable)
{
	if (enable == !!rq->dim)
		return 0;

	if (enable) {
		struct mlx5e_channel *c = rq->channel;
		struct dim *dim;

		dim = mlx5e_dim_enable(rq->mdev, mlx5e_rx_dim_work, c->cpu,
				       c->rx_cq_moder.cq_period_mode, &rq->cq.mcq, rq);
		if (IS_ERR(dim))
			return PTR_ERR(dim);

		rq->dim = dim;

		__set_bit(MLX5E_RQ_STATE_DIM, &rq->state);
	} else {
		__clear_bit(MLX5E_RQ_STATE_DIM, &rq->state);

		mlx5e_dim_disable(rq->dim);
		rq->dim = NULL;
	}

	return 0;
}

int mlx5e_dim_tx_change(struct mlx5e_txqsq *sq, bool enable)
{
	if (enable == !!sq->dim)
		return 0;

	if (enable) {
		struct mlx5e_channel *c = sq->channel;
		struct dim *dim;

		dim = mlx5e_dim_enable(sq->mdev, mlx5e_tx_dim_work, c->cpu,
				       c->tx_cq_moder.cq_period_mode, &sq->cq.mcq, sq);
		if (IS_ERR(dim))
			return PTR_ERR(dim);

		sq->dim = dim;

		__set_bit(MLX5E_SQ_STATE_DIM, &sq->state);
	} else {
		__clear_bit(MLX5E_SQ_STATE_DIM, &sq->state);

		mlx5e_dim_disable(sq->dim);
		sq->dim = NULL;
	}

	return 0;
}
