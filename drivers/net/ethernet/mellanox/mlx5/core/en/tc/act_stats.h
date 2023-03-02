/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_ACT_STATS_H__
#define __MLX5_EN_ACT_STATS_H__

#include <net/flow_offload.h>
#include "en/tc_priv.h"

struct mlx5e_tc_act_stats_handle;

struct mlx5e_tc_act_stats_handle *mlx5e_tc_act_stats_create(void);
void mlx5e_tc_act_stats_free(struct mlx5e_tc_act_stats_handle *handle);

int
mlx5e_tc_act_stats_add_flow(struct mlx5e_tc_act_stats_handle *handle,
			    struct mlx5e_tc_flow *flow);

void
mlx5e_tc_act_stats_del_flow(struct mlx5e_tc_act_stats_handle *handle,
			    struct mlx5e_tc_flow *flow);

int
mlx5e_tc_act_stats_fill_stats(struct mlx5e_tc_act_stats_handle *handle,
			      struct flow_offload_action *fl_act);

#endif /* __MLX5_EN_ACT_STATS_H__ */
