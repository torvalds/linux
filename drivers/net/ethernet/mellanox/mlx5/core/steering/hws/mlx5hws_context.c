// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA CORPORATION. All rights reserved. */

#include "mlx5hws_internal.h"

bool mlx5hws_context_cap_dynamic_reparse(struct mlx5hws_context *ctx)
{
	return IS_BIT_SET(ctx->caps->rtc_reparse_mode, MLX5_IFC_RTC_REPARSE_BY_STC);
}

u8 mlx5hws_context_get_reparse_mode(struct mlx5hws_context *ctx)
{
	/* Prefer to use dynamic reparse, reparse only specific actions */
	if (mlx5hws_context_cap_dynamic_reparse(ctx))
		return MLX5_IFC_RTC_REPARSE_NEVER;

	/* Otherwise use less efficient static */
	return MLX5_IFC_RTC_REPARSE_ALWAYS;
}

static int hws_context_pools_init(struct mlx5hws_context *ctx)
{
	struct mlx5hws_pool_attr pool_attr = {0};
	u8 max_log_sz;
	int ret;
	int i;

	ret = mlx5hws_pat_init_pattern_cache(&ctx->pattern_cache);
	if (ret)
		return ret;

	ret = mlx5hws_definer_init_cache(&ctx->definer_cache);
	if (ret)
		goto uninit_pat_cache;

	/* Create an STC pool per FT type */
	pool_attr.pool_type = MLX5HWS_POOL_TYPE_STC;
	pool_attr.flags = MLX5HWS_POOL_FLAGS_FOR_STC_POOL;
	max_log_sz = min(MLX5HWS_POOL_STC_LOG_SZ, ctx->caps->stc_alloc_log_max);
	pool_attr.alloc_log_sz = max(max_log_sz, ctx->caps->stc_alloc_log_gran);

	for (i = 0; i < MLX5HWS_TABLE_TYPE_MAX; i++) {
		pool_attr.table_type = i;
		ctx->stc_pool[i] = mlx5hws_pool_create(ctx, &pool_attr);
		if (!ctx->stc_pool[i]) {
			mlx5hws_err(ctx, "Failed to allocate STC pool [%d]", i);
			ret = -ENOMEM;
			goto free_stc_pools;
		}
	}

	return 0;

free_stc_pools:
	for (i = 0; i < MLX5HWS_TABLE_TYPE_MAX; i++)
		if (ctx->stc_pool[i])
			mlx5hws_pool_destroy(ctx->stc_pool[i]);

	mlx5hws_definer_uninit_cache(ctx->definer_cache);
uninit_pat_cache:
	mlx5hws_pat_uninit_pattern_cache(ctx->pattern_cache);
	return ret;
}

static void hws_context_pools_uninit(struct mlx5hws_context *ctx)
{
	int i;

	for (i = 0; i < MLX5HWS_TABLE_TYPE_MAX; i++) {
		if (ctx->stc_pool[i])
			mlx5hws_pool_destroy(ctx->stc_pool[i]);
	}

	mlx5hws_definer_uninit_cache(ctx->definer_cache);
	mlx5hws_pat_uninit_pattern_cache(ctx->pattern_cache);
}

static int hws_context_init_pd(struct mlx5hws_context *ctx)
{
	int ret = 0;

	ret = mlx5_core_alloc_pd(ctx->mdev, &ctx->pd_num);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate PD\n");
		return ret;
	}

	ctx->flags |= MLX5HWS_CONTEXT_FLAG_PRIVATE_PD;

	return 0;
}

static int hws_context_uninit_pd(struct mlx5hws_context *ctx)
{
	if (ctx->flags & MLX5HWS_CONTEXT_FLAG_PRIVATE_PD)
		mlx5_core_dealloc_pd(ctx->mdev, ctx->pd_num);

	return 0;
}

static void hws_context_check_hws_supp(struct mlx5hws_context *ctx)
{
	struct mlx5hws_cmd_query_caps *caps = ctx->caps;

	/* HWS not supported on device / FW */
	if (!caps->wqe_based_update) {
		mlx5hws_err(ctx, "Required HWS WQE based insertion cap not supported\n");
		return;
	}

	if (!caps->eswitch_manager) {
		mlx5hws_err(ctx, "HWS is not supported for non eswitch manager port\n");
		return;
	}

	/* Current solution requires all rules to set reparse bit */
	if ((!caps->nic_ft.reparse ||
	     (!caps->fdb_ft.reparse && caps->eswitch_manager)) ||
	    !IS_BIT_SET(caps->rtc_reparse_mode, MLX5_IFC_RTC_REPARSE_ALWAYS)) {
		mlx5hws_err(ctx, "Required HWS reparse cap not supported\n");
		return;
	}

	/* FW/HW must support 8DW STE */
	if (!IS_BIT_SET(caps->ste_format, MLX5_IFC_RTC_STE_FORMAT_8DW)) {
		mlx5hws_err(ctx, "Required HWS STE format not supported\n");
		return;
	}

	/* Adding rules by hash and by offset are requirements */
	if (!IS_BIT_SET(caps->rtc_index_mode, MLX5_IFC_RTC_STE_UPDATE_MODE_BY_HASH) ||
	    !IS_BIT_SET(caps->rtc_index_mode, MLX5_IFC_RTC_STE_UPDATE_MODE_BY_OFFSET)) {
		mlx5hws_err(ctx, "Required HWS RTC update mode not supported\n");
		return;
	}

	/* Support for SELECT definer ID is required */
	if (!IS_BIT_SET(caps->definer_format_sup, MLX5_IFC_DEFINER_FORMAT_ID_SELECT)) {
		mlx5hws_err(ctx, "Required HWS Dynamic definer not supported\n");
		return;
	}

	ctx->flags |= MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT;
}

static int hws_context_init_hws(struct mlx5hws_context *ctx,
				struct mlx5hws_context_attr *attr)
{
	int ret;

	hws_context_check_hws_supp(ctx);

	if (!(ctx->flags & MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT))
		return 0;

	ret = hws_context_init_pd(ctx);
	if (ret)
		return ret;

	ret = hws_context_pools_init(ctx);
	if (ret)
		goto uninit_pd;

	if (attr->bwc)
		ctx->flags |= MLX5HWS_CONTEXT_FLAG_BWC_SUPPORT;

	ret = mlx5hws_send_queues_open(ctx, attr->queues, attr->queue_size);
	if (ret)
		goto pools_uninit;

	INIT_LIST_HEAD(&ctx->tbl_list);

	return 0;

pools_uninit:
	hws_context_pools_uninit(ctx);
uninit_pd:
	hws_context_uninit_pd(ctx);
	return ret;
}

static void hws_context_uninit_hws(struct mlx5hws_context *ctx)
{
	if (!(ctx->flags & MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT))
		return;

	mlx5hws_send_queues_close(ctx);
	hws_context_pools_uninit(ctx);
	hws_context_uninit_pd(ctx);
}

struct mlx5hws_context *mlx5hws_context_open(struct mlx5_core_dev *mdev,
					     struct mlx5hws_context_attr *attr)
{
	struct mlx5hws_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->mdev = mdev;

	mutex_init(&ctx->ctrl_lock);
	xa_init(&ctx->peer_ctx_xa);

	ctx->caps = kzalloc(sizeof(*ctx->caps), GFP_KERNEL);
	if (!ctx->caps)
		goto free_ctx;

	ret = mlx5hws_cmd_query_caps(mdev, ctx->caps);
	if (ret)
		goto free_caps;

	ret = mlx5hws_vport_init_vports(ctx);
	if (ret)
		goto free_caps;

	ret = hws_context_init_hws(ctx, attr);
	if (ret)
		goto uninit_vports;

	mlx5hws_debug_init_dump(ctx);

	return ctx;

uninit_vports:
	mlx5hws_vport_uninit_vports(ctx);
free_caps:
	kfree(ctx->caps);
free_ctx:
	xa_destroy(&ctx->peer_ctx_xa);
	mutex_destroy(&ctx->ctrl_lock);
	kfree(ctx);
	return NULL;
}

int mlx5hws_context_close(struct mlx5hws_context *ctx)
{
	mlx5hws_debug_uninit_dump(ctx);
	hws_context_uninit_hws(ctx);
	mlx5hws_vport_uninit_vports(ctx);
	kfree(ctx->caps);
	xa_destroy(&ctx->peer_ctx_xa);
	mutex_destroy(&ctx->ctrl_lock);
	kfree(ctx);
	return 0;
}

void mlx5hws_context_set_peer(struct mlx5hws_context *ctx,
			      struct mlx5hws_context *peer_ctx,
			      u16 peer_vhca_id)
{
	mutex_lock(&ctx->ctrl_lock);

	if (xa_err(xa_store(&ctx->peer_ctx_xa, peer_vhca_id, peer_ctx, GFP_KERNEL)))
		pr_warn("HWS: failed storing peer vhca ID in peer xarray\n");

	mutex_unlock(&ctx->ctrl_lock);
}
