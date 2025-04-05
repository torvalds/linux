/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_CONTEXT_H_
#define HWS_CONTEXT_H_

enum mlx5hws_context_flags {
	MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT = 1 << 0,
	MLX5HWS_CONTEXT_FLAG_PRIVATE_PD = 1 << 1,
	MLX5HWS_CONTEXT_FLAG_BWC_SUPPORT = 1 << 2,
	MLX5HWS_CONTEXT_FLAG_NATIVE_SUPPORT = 1 << 3,
};

enum mlx5hws_context_shared_stc_type {
	MLX5HWS_CONTEXT_SHARED_STC_DECAP_L3 = 0,
	MLX5HWS_CONTEXT_SHARED_STC_DOUBLE_POP = 1,
	MLX5HWS_CONTEXT_SHARED_STC_MAX = 2,
};

struct mlx5hws_context_common_res {
	struct mlx5hws_action_default_stc *default_stc;
	struct mlx5hws_action_shared_stc *shared_stc[MLX5HWS_CONTEXT_SHARED_STC_MAX];
	struct mlx5hws_cmd_forward_tbl *default_miss;
};

struct mlx5hws_context_debug_info {
	struct dentry *steering_debugfs;
	struct dentry *fdb_debugfs;
};

struct mlx5hws_context_vports {
	u16 esw_manager_gvmi;
	u16 uplink_gvmi;
	struct xarray vport_gvmi_xa;
};

struct mlx5hws_context {
	struct mlx5_core_dev *mdev;
	struct mlx5hws_cmd_query_caps *caps;
	u32 pd_num;
	struct mlx5hws_pool *stc_pool;
	struct mlx5hws_context_common_res common_res;
	struct mlx5hws_pattern_cache *pattern_cache;
	struct mlx5hws_definer_cache *definer_cache;
	struct mutex ctrl_lock; /* control lock to protect the whole context */
	enum mlx5hws_context_flags flags;
	struct mlx5hws_send_engine *send_queue;
	size_t queues;
	struct mutex *bwc_send_queue_locks; /* protect BWC queues */
	struct lock_class_key *bwc_lock_class_keys;
	struct list_head tbl_list;
	struct mlx5hws_context_debug_info debug_info;
	struct xarray peer_ctx_xa;
	struct mlx5hws_context_vports vports;
};

static inline bool mlx5hws_context_bwc_supported(struct mlx5hws_context *ctx)
{
	return ctx->flags & MLX5HWS_CONTEXT_FLAG_BWC_SUPPORT;
}

static inline bool mlx5hws_context_native_supported(struct mlx5hws_context *ctx)
{
	return ctx->flags & MLX5HWS_CONTEXT_FLAG_NATIVE_SUPPORT;
}

bool mlx5hws_context_cap_dynamic_reparse(struct mlx5hws_context *ctx);

u8 mlx5hws_context_get_reparse_mode(struct mlx5hws_context *ctx);

#endif /* HWS_CONTEXT_H_ */
