// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include <linux/kernel.h>
#include <linux/mlx5/driver.h>

#include "smfs.h"

struct mlx5dr_matcher *
mlx5_smfs_matcher_create(struct mlx5dr_table *table, u32 priority, struct mlx5_flow_spec *spec)
{
	struct mlx5dr_match_parameters matcher_mask = {};

	matcher_mask.match_buf = (u64 *)&spec->match_criteria;
	matcher_mask.match_sz = DR_SZ_MATCH_PARAM;

	return mlx5dr_matcher_create(table, priority, spec->match_criteria_enable, &matcher_mask);
}

void
mlx5_smfs_matcher_destroy(struct mlx5dr_matcher *matcher)
{
	mlx5dr_matcher_destroy(matcher);
}

struct mlx5dr_table *
mlx5_smfs_table_get_from_fs_ft(struct mlx5_flow_table *ft)
{
	return mlx5dr_table_get_from_fs_ft(ft);
}

struct mlx5dr_action *
mlx5_smfs_action_create_dest_table(struct mlx5dr_table *table)
{
	return mlx5dr_action_create_dest_table(table);
}

struct mlx5dr_action *
mlx5_smfs_action_create_flow_counter(u32 counter_id)
{
	return mlx5dr_action_create_flow_counter(counter_id);
}

void
mlx5_smfs_action_destroy(struct mlx5dr_action *action)
{
	mlx5dr_action_destroy(action);
}

struct mlx5dr_rule *
mlx5_smfs_rule_create(struct mlx5dr_matcher *matcher, struct mlx5_flow_spec *spec,
		      size_t num_actions, struct mlx5dr_action *actions[],
		      u32 flow_source)
{
	struct mlx5dr_match_parameters value = {};

	value.match_buf = (u64 *)spec->match_value;
	value.match_sz = DR_SZ_MATCH_PARAM;

	return mlx5dr_rule_create(matcher, &value, num_actions, actions, flow_source);
}

void
mlx5_smfs_rule_destroy(struct mlx5dr_rule *rule)
{
	mlx5dr_rule_destroy(rule);
}

