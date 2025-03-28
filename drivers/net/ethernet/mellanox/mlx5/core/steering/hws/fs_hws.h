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
	struct xarray mh_pools;
	struct xarray table_dests;
	struct xarray vport_vhca_dests;
	struct xarray vport_dests;
	struct xarray aso_meters;
	struct xarray sample_dests;
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
	struct mlx5_fs_hws_mh *mh_data;
};

struct mlx5_fs_hws_matcher {
	struct mlx5hws_bwc_matcher *matcher;
};

struct mlx5_fs_hws_rule_action {
	struct mlx5hws_action *action;
	union {
		struct mlx5_fc *counter;
		struct mlx5_exe_aso *exe_aso;
		u32 sampler_id;
	};
};

struct mlx5_fs_hws_rule {
	struct mlx5hws_bwc_rule *bwc_rule;
	struct mlx5_fs_hws_rule_action *hws_fs_actions;
	int num_fs_actions;
};

struct mlx5_fs_hws_data {
	struct mlx5hws_action *hws_action;
	struct mutex lock; /* protects hws_action */
	refcount_t hws_action_refcount;
};

struct mlx5_fs_hws_create_action_ctx {
	enum mlx5hws_action_type actions_type;
	struct mlx5hws_context *hws_ctx;
	u32 id;
	union {
		u8 return_reg_id;
	};
};

struct mlx5hws_action *
mlx5_fs_get_hws_action(struct mlx5_fs_hws_data *fs_hws_data,
		       struct mlx5_fs_hws_create_action_ctx *create_ctx);
void mlx5_fs_put_hws_action(struct mlx5_fs_hws_data *fs_hws_data);

#ifdef CONFIG_MLX5_HW_STEERING

bool mlx5_fs_hws_is_supported(struct mlx5_core_dev *dev);

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void);

#else

static inline bool mlx5_fs_hws_is_supported(struct mlx5_core_dev *dev)
{
	return false;
}

static inline const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return NULL;
}

#endif /* CONFIG_MLX5_HWS_STEERING */
#endif
