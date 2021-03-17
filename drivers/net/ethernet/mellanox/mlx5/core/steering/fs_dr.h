/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 * Copyright (c) 2019 Mellanox Technologies
 */

#ifndef _MLX5_FS_DR_
#define _MLX5_FS_DR_

#include "mlx5dr.h"

struct mlx5_flow_root_namespace;
struct fs_fte;

struct mlx5_fs_dr_action {
	struct mlx5dr_action *dr_action;
};

struct mlx5_fs_dr_ns {
	struct mlx5_dr_ns *dr_ns;
};

struct mlx5_fs_dr_rule {
	struct mlx5dr_rule    *dr_rule;
	/* Only actions created by fs_dr */
	struct mlx5dr_action  **dr_actions;
	int                      num_actions;
};

struct mlx5_fs_dr_domain {
	struct mlx5dr_domain	*dr_domain;
};

struct mlx5_fs_dr_matcher {
	struct mlx5dr_matcher *dr_matcher;
};

struct mlx5_fs_dr_table {
	struct mlx5dr_table  *dr_table;
	struct mlx5dr_action *miss_action;
};

#ifdef CONFIG_MLX5_SW_STEERING

bool mlx5_fs_dr_is_supported(struct mlx5_core_dev *dev);

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_dr_cmds(void);

#else

static inline const struct mlx5_flow_cmds *mlx5_fs_cmd_get_dr_cmds(void)
{
	return NULL;
}

static inline bool mlx5_fs_dr_is_supported(struct mlx5_core_dev *dev)
{
	return false;
}

#endif /* CONFIG_MLX5_SW_STEERING */
#endif
