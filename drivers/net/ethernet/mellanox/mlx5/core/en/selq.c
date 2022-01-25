// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "selq.h"
#include <linux/slab.h>
#include <linux/netdevice.h>
#include "en.h"

struct mlx5e_selq_params {
	unsigned int num_regular_queues;
	unsigned int num_channels;
	unsigned int num_tcs;
	bool is_htb;
	bool is_ptp;
};

int mlx5e_selq_init(struct mlx5e_selq *selq, struct mutex *state_lock)
{
	struct mlx5e_selq_params *init_params;

	selq->state_lock = state_lock;

	selq->standby = kvzalloc(sizeof(*selq->standby), GFP_KERNEL);
	if (!selq->standby)
		return -ENOMEM;

	init_params = kvzalloc(sizeof(*selq->active), GFP_KERNEL);
	if (!init_params) {
		kvfree(selq->standby);
		selq->standby = NULL;
		return -ENOMEM;
	}
	/* Assign dummy values, so that mlx5e_select_queue won't crash. */
	*init_params = (struct mlx5e_selq_params) {
		.num_regular_queues = 1,
		.num_channels = 1,
		.num_tcs = 1,
		.is_htb = false,
		.is_ptp = false,
	};
	rcu_assign_pointer(selq->active, init_params);

	return 0;
}

void mlx5e_selq_cleanup(struct mlx5e_selq *selq)
{
	WARN_ON_ONCE(selq->is_prepared);

	kvfree(selq->standby);
	selq->standby = NULL;
	selq->is_prepared = true;

	mlx5e_selq_apply(selq);

	kvfree(selq->standby);
	selq->standby = NULL;
}

void mlx5e_selq_prepare(struct mlx5e_selq *selq, struct mlx5e_params *params, bool htb)
{
	lockdep_assert_held(selq->state_lock);
	WARN_ON_ONCE(selq->is_prepared);

	selq->is_prepared = true;

	selq->standby->num_channels = params->num_channels;
	selq->standby->num_tcs = mlx5e_get_dcb_num_tc(params);
	selq->standby->num_regular_queues =
		selq->standby->num_channels * selq->standby->num_tcs;
	selq->standby->is_htb = htb;
	selq->standby->is_ptp = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_TX_PORT_TS);
}

void mlx5e_selq_apply(struct mlx5e_selq *selq)
{
	struct mlx5e_selq_params *old_params;

	WARN_ON_ONCE(!selq->is_prepared);

	selq->is_prepared = false;

	old_params = rcu_replace_pointer(selq->active, selq->standby,
					 lockdep_is_held(selq->state_lock));
	synchronize_net(); /* Wait until ndo_select_queue starts emitting correct values. */
	selq->standby = old_params;
}

void mlx5e_selq_cancel(struct mlx5e_selq *selq)
{
	lockdep_assert_held(selq->state_lock);
	WARN_ON_ONCE(!selq->is_prepared);

	selq->is_prepared = false;
}
