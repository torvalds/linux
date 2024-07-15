/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef __MLX5_EN_DIM_H__
#define __MLX5_EN_DIM_H__

#include <linux/dim.h>
#include <linux/types.h>
#include <linux/mlx5/mlx5_ifc.h>

/* Forward declarations */
struct mlx5e_rq;
struct mlx5e_txqsq;
struct work_struct;

/* convert a boolean value for cqe mode to appropriate dim constant
 * true  : DIM_CQ_PERIOD_MODE_START_FROM_CQE
 * false : DIM_CQ_PERIOD_MODE_START_FROM_EQE
 */
static inline int mlx5e_dim_cq_period_mode(bool start_from_cqe)
{
	return start_from_cqe ? DIM_CQ_PERIOD_MODE_START_FROM_CQE :
		DIM_CQ_PERIOD_MODE_START_FROM_EQE;
}

static inline enum mlx5_cq_period_mode
mlx5e_cq_period_mode(enum dim_cq_period_mode cq_period_mode)
{
	switch (cq_period_mode) {
	case DIM_CQ_PERIOD_MODE_START_FROM_EQE:
		return MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
	case DIM_CQ_PERIOD_MODE_START_FROM_CQE:
		return MLX5_CQ_PERIOD_MODE_START_FROM_CQE;
	default:
		WARN_ON_ONCE(true);
		return MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
	}
}

void mlx5e_rx_dim_work(struct work_struct *work);
void mlx5e_tx_dim_work(struct work_struct *work);
int mlx5e_dim_rx_change(struct mlx5e_rq *rq, bool enabled);
int mlx5e_dim_tx_change(struct mlx5e_txqsq *sq, bool enabled);

#endif /* __MLX5_EN_DIM_H__ */
