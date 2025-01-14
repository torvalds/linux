/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#ifndef __MLX5_FS_HWS_POOLS_H__
#define __MLX5_FS_HWS_POOLS_H__

#include <linux/if_vlan.h>
#include "fs_pool.h"
#include "fs_core.h"

#define MLX5_FS_INSERT_HDR_VLAN_ANCHOR MLX5_REFORMAT_CONTEXT_ANCHOR_MAC_START
#define MLX5_FS_INSERT_HDR_VLAN_OFFSET offsetof(struct vlan_ethhdr, h_vlan_proto)
#define MLX5_FS_INSERT_HDR_VLAN_SIZE sizeof(struct vlan_hdr)

enum {
	MLX5_FS_DL3TNLTOL2_MAC_HDR_IDX = 0,
	MLX5_FS_DL3TNLTOL2_MAC_VLAN_HDR_IDX,
};

struct mlx5_fs_hws_pr {
	struct mlx5_fs_hws_pr_bulk *bulk;
	u32 offset;
	u8 hdr_idx;
	u8 *data;
	size_t data_size;
};

struct mlx5_fs_hws_pr_bulk {
	struct mlx5_fs_bulk fs_bulk;
	struct mlx5hws_action *hws_action;
	struct mlx5_fs_hws_pr prs_data[];
};

struct mlx5_fs_hws_pr_pool_ctx {
	enum mlx5hws_action_type reformat_type;
	size_t encap_data_size;
};

struct mlx5_fs_hws_mh {
	struct mlx5_fs_hws_mh_bulk *bulk;
	u32 offset;
	u8 *data;
};

struct mlx5_fs_hws_mh_bulk {
	struct mlx5_fs_bulk fs_bulk;
	struct mlx5_fs_pool *mh_pool;
	struct mlx5hws_action *hws_action;
	struct mlx5_fs_hws_mh mhs_data[];
};

int mlx5_fs_hws_pr_pool_init(struct mlx5_fs_pool *pr_pool,
			     struct mlx5_core_dev *dev, size_t encap_data_size,
			     enum mlx5hws_action_type reformat_type);
void mlx5_fs_hws_pr_pool_cleanup(struct mlx5_fs_pool *pr_pool);

struct mlx5_fs_hws_pr *mlx5_fs_hws_pr_pool_acquire_pr(struct mlx5_fs_pool *pr_pool);
void mlx5_fs_hws_pr_pool_release_pr(struct mlx5_fs_pool *pr_pool,
				    struct mlx5_fs_hws_pr *pr_data);
struct mlx5hws_action *mlx5_fs_hws_pr_get_action(struct mlx5_fs_hws_pr *pr_data);
int mlx5_fs_hws_mh_pool_init(struct mlx5_fs_pool *fs_hws_mh_pool,
			     struct mlx5_core_dev *dev,
			     struct mlx5hws_action_mh_pattern *pattern);
void mlx5_fs_hws_mh_pool_cleanup(struct mlx5_fs_pool *fs_hws_mh_pool);
struct mlx5_fs_hws_mh *mlx5_fs_hws_mh_pool_acquire_mh(struct mlx5_fs_pool *mh_pool);
void mlx5_fs_hws_mh_pool_release_mh(struct mlx5_fs_pool *mh_pool,
				    struct mlx5_fs_hws_mh *mh_data);
bool mlx5_fs_hws_mh_pool_match(struct mlx5_fs_pool *mh_pool,
			       struct mlx5hws_action_mh_pattern *pattern);
struct mlx5hws_action *mlx5_fc_get_hws_action(struct mlx5hws_context *ctx,
					      struct mlx5_fc *counter);
void mlx5_fc_put_hws_action(struct mlx5_fc *counter);
#endif /* __MLX5_FS_HWS_POOLS_H__ */
