/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_LIB_SMFS_H__
#define __MLX5_LIB_SMFS_H__

#include "steering/sws/mlx5dr.h"
#include "steering/sws/dr_types.h"

struct mlx5dr_matcher *
mlx5_smfs_matcher_create(struct mlx5dr_table *table, u32 priority, struct mlx5_flow_spec *spec);

void
mlx5_smfs_matcher_destroy(struct mlx5dr_matcher *matcher);

struct mlx5dr_table *
mlx5_smfs_table_get_from_fs_ft(struct mlx5_flow_table *ft);

struct mlx5dr_action *
mlx5_smfs_action_create_dest_table(struct mlx5dr_table *table);

struct mlx5dr_action *
mlx5_smfs_action_create_flow_counter(u32 counter_id);

void
mlx5_smfs_action_destroy(struct mlx5dr_action *action);

struct mlx5dr_rule *
mlx5_smfs_rule_create(struct mlx5dr_matcher *matcher, struct mlx5_flow_spec *spec,
		      size_t num_actions, struct mlx5dr_action *actions[],
		      u32 flow_source);

void
mlx5_smfs_rule_destroy(struct mlx5dr_rule *rule);

#endif /* __MLX5_LIB_SMFS_H__ */
