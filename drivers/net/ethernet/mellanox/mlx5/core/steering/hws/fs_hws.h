/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#ifndef _MLX5_FS_HWS_
#define _MLX5_FS_HWS_

#include "mlx5hws.h"
#include "fs_hws_pools.h"

struct mlx5_fs_hws_actions_pool {
	struct mlx5hws_action *tag_action;
	struct mlx5hws_action *pop_vlan_action;
	struct mlx5hws_action *push_vlan_action;
	struct mlx5hws_action *drop_action;
	struct mlx5hws_action *decapl2_action;
	struct mlx5hws_action *remove_hdr_vlan_action;
	struct mlx5_fs_pool insert_hdr_pool;
	struct mlx5_fs_pool dl3tnltol2_pool;
	struct xarray el2tol3tnl_pools;
	struct xarray el2tol2tnl_pools;
};

struct mlx5_fs_hws_context {
	struct mlx5hws_context	*hws_ctx;
	struct mlx5_fs_hws_actions_pool hws_pool;
};

struct mlx5_fs_hws_table {
	struct mlx5hws_table *hws_table;
	bool miss_ft_set;
};

struct mlx5_fs_hws_action {
	struct mlx5hws_action *hws_action;
	struct mlx5_fs_pool *fs_pool;
	struct mlx5_fs_hws_pr *pr_data;
};

struct mlx5_fs_hws_matcher {
	struct mlx5hws_bwc_matcher *matcher;
};

#ifdef CONFIG_MLX5_HW_STEERING

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void);

#else

static inline const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return NULL;
}

#endif /* CONFIG_MLX5_HWS_STEERING */
#endif
