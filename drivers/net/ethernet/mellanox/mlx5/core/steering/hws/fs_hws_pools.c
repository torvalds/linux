// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#include <mlx5_core.h>
#include "fs_hws_pools.h"

#define MLX5_FS_HWS_DEFAULT_BULK_LEN 65536
#define MLX5_FS_HWS_POOL_MAX_THRESHOLD BIT(18)
#define MLX5_FS_HWS_POOL_USED_BUFF_RATIO 10

static struct mlx5hws_action *
mlx5_fs_dl3tnltol2_bulk_action_create(struct mlx5hws_context *ctx)
{
	struct mlx5hws_action_reformat_header reformat_hdr[2] = {};
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB;
	enum mlx5hws_action_type reformat_type;
	u32 log_bulk_size;

	reformat_type = MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2;
	reformat_hdr[MLX5_FS_DL3TNLTOL2_MAC_HDR_IDX].sz = ETH_HLEN;
	reformat_hdr[MLX5_FS_DL3TNLTOL2_MAC_VLAN_HDR_IDX].sz = ETH_HLEN + VLAN_HLEN;

	log_bulk_size = ilog2(MLX5_FS_HWS_DEFAULT_BULK_LEN);
	return mlx5hws_action_create_reformat(ctx, reformat_type, 2,
					      reformat_hdr, log_bulk_size, flags);
}

static struct mlx5hws_action *
mlx5_fs_el2tol3tnl_bulk_action_create(struct mlx5hws_context *ctx, size_t data_size)
{
	struct mlx5hws_action_reformat_header reformat_hdr = {};
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB;
	enum mlx5hws_action_type reformat_type;
	u32 log_bulk_size;

	reformat_type = MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3;
	reformat_hdr.sz = data_size;

	log_bulk_size = ilog2(MLX5_FS_HWS_DEFAULT_BULK_LEN);
	return mlx5hws_action_create_reformat(ctx, reformat_type, 1,
					      &reformat_hdr, log_bulk_size, flags);
}

static struct mlx5hws_action *
mlx5_fs_el2tol2tnl_bulk_action_create(struct mlx5hws_context *ctx, size_t data_size)
{
	struct mlx5hws_action_reformat_header reformat_hdr = {};
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB;
	enum mlx5hws_action_type reformat_type;
	u32 log_bulk_size;

	reformat_type = MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2;
	reformat_hdr.sz = data_size;

	log_bulk_size = ilog2(MLX5_FS_HWS_DEFAULT_BULK_LEN);
	return mlx5hws_action_create_reformat(ctx, reformat_type, 1,
					      &reformat_hdr, log_bulk_size, flags);
}

static struct mlx5hws_action *
mlx5_fs_insert_hdr_bulk_action_create(struct mlx5hws_context *ctx)
{
	struct mlx5hws_action_insert_header insert_hdr = {};
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB;
	u32 log_bulk_size;

	log_bulk_size = ilog2(MLX5_FS_HWS_DEFAULT_BULK_LEN);
	insert_hdr.hdr.sz = MLX5_FS_INSERT_HDR_VLAN_SIZE;
	insert_hdr.anchor = MLX5_FS_INSERT_HDR_VLAN_ANCHOR;
	insert_hdr.offset = MLX5_FS_INSERT_HDR_VLAN_OFFSET;

	return mlx5hws_action_create_insert_header(ctx, 1, &insert_hdr,
						   log_bulk_size, flags);
}

static struct mlx5hws_action *
mlx5_fs_pr_bulk_action_create(struct mlx5_core_dev *dev,
			      struct mlx5_fs_hws_pr_pool_ctx *pr_pool_ctx)
{
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5hws_context *ctx;
	size_t encap_data_size;

	root_ns = mlx5_get_root_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns || root_ns->mode != MLX5_FLOW_STEERING_MODE_HMFS)
		return NULL;

	ctx = root_ns->fs_hws_context.hws_ctx;
	if (!ctx)
		return NULL;

	encap_data_size = pr_pool_ctx->encap_data_size;
	switch (pr_pool_ctx->reformat_type) {
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2:
		return mlx5_fs_dl3tnltol2_bulk_action_create(ctx);
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3:
		return mlx5_fs_el2tol3tnl_bulk_action_create(ctx, encap_data_size);
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2:
		return mlx5_fs_el2tol2tnl_bulk_action_create(ctx, encap_data_size);
	case MLX5HWS_ACTION_TYP_INSERT_HEADER:
		return mlx5_fs_insert_hdr_bulk_action_create(ctx);
	default:
		return NULL;
	}
	return NULL;
}

static struct mlx5_fs_bulk *
mlx5_fs_hws_pr_bulk_create(struct mlx5_core_dev *dev, void *pool_ctx)
{
	struct mlx5_fs_hws_pr_pool_ctx *pr_pool_ctx;
	struct mlx5_fs_hws_pr_bulk *pr_bulk;
	int bulk_len;
	int i;

	if (!pool_ctx)
		return NULL;
	pr_pool_ctx = pool_ctx;
	bulk_len = MLX5_FS_HWS_DEFAULT_BULK_LEN;
	pr_bulk = kvzalloc(struct_size(pr_bulk, prs_data, bulk_len), GFP_KERNEL);
	if (!pr_bulk)
		return NULL;

	if (mlx5_fs_bulk_init(dev, &pr_bulk->fs_bulk, bulk_len))
		goto free_pr_bulk;

	for (i = 0; i < bulk_len; i++) {
		pr_bulk->prs_data[i].bulk = pr_bulk;
		pr_bulk->prs_data[i].offset = i;
	}

	pr_bulk->hws_action = mlx5_fs_pr_bulk_action_create(dev, pr_pool_ctx);
	if (!pr_bulk->hws_action)
		goto cleanup_fs_bulk;

	return &pr_bulk->fs_bulk;

cleanup_fs_bulk:
	mlx5_fs_bulk_cleanup(&pr_bulk->fs_bulk);
free_pr_bulk:
	kvfree(pr_bulk);
	return NULL;
}

static int
mlx5_fs_hws_pr_bulk_destroy(struct mlx5_core_dev *dev, struct mlx5_fs_bulk *fs_bulk)
{
	struct mlx5_fs_hws_pr_bulk *pr_bulk;

	pr_bulk = container_of(fs_bulk, struct mlx5_fs_hws_pr_bulk, fs_bulk);
	if (mlx5_fs_bulk_get_free_amount(fs_bulk) < fs_bulk->bulk_len) {
		mlx5_core_err(dev, "Freeing bulk before all reformats were released\n");
		return -EBUSY;
	}

	mlx5hws_action_destroy(pr_bulk->hws_action);
	mlx5_fs_bulk_cleanup(fs_bulk);
	kvfree(pr_bulk);

	return 0;
}

static void mlx5_hws_pool_update_threshold(struct mlx5_fs_pool *hws_pool)
{
	hws_pool->threshold = min_t(int, MLX5_FS_HWS_POOL_MAX_THRESHOLD,
				    hws_pool->used_units / MLX5_FS_HWS_POOL_USED_BUFF_RATIO);
}

static const struct mlx5_fs_pool_ops mlx5_fs_hws_pr_pool_ops = {
	.bulk_create = mlx5_fs_hws_pr_bulk_create,
	.bulk_destroy = mlx5_fs_hws_pr_bulk_destroy,
	.update_threshold = mlx5_hws_pool_update_threshold,
};

int mlx5_fs_hws_pr_pool_init(struct mlx5_fs_pool *pr_pool,
			     struct mlx5_core_dev *dev, size_t encap_data_size,
			     enum mlx5hws_action_type reformat_type)
{
	struct mlx5_fs_hws_pr_pool_ctx *pr_pool_ctx;

	if (reformat_type != MLX5HWS_ACTION_TYP_INSERT_HEADER &&
	    reformat_type != MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2 &&
	    reformat_type != MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3 &&
	    reformat_type != MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2)
		return -EOPNOTSUPP;

	pr_pool_ctx = kzalloc(sizeof(*pr_pool_ctx), GFP_KERNEL);
	if (!pr_pool_ctx)
		return -ENOMEM;
	pr_pool_ctx->reformat_type = reformat_type;
	pr_pool_ctx->encap_data_size = encap_data_size;
	mlx5_fs_pool_init(pr_pool, dev, &mlx5_fs_hws_pr_pool_ops, pr_pool_ctx);
	return 0;
}

void mlx5_fs_hws_pr_pool_cleanup(struct mlx5_fs_pool *pr_pool)
{
	struct mlx5_fs_hws_pr_pool_ctx *pr_pool_ctx;

	mlx5_fs_pool_cleanup(pr_pool);
	pr_pool_ctx = pr_pool->pool_ctx;
	if (!pr_pool_ctx)
		return;
	kfree(pr_pool_ctx);
}

struct mlx5_fs_hws_pr *
mlx5_fs_hws_pr_pool_acquire_pr(struct mlx5_fs_pool *pr_pool)
{
	struct mlx5_fs_pool_index pool_index = {};
	struct mlx5_fs_hws_pr_bulk *pr_bulk;
	int err;

	err = mlx5_fs_pool_acquire_index(pr_pool, &pool_index);
	if (err)
		return ERR_PTR(err);
	pr_bulk = container_of(pool_index.fs_bulk, struct mlx5_fs_hws_pr_bulk,
			       fs_bulk);
	return &pr_bulk->prs_data[pool_index.index];
}

void mlx5_fs_hws_pr_pool_release_pr(struct mlx5_fs_pool *pr_pool,
				    struct mlx5_fs_hws_pr *pr_data)
{
	struct mlx5_fs_bulk *fs_bulk = &pr_data->bulk->fs_bulk;
	struct mlx5_fs_pool_index pool_index = {};
	struct mlx5_core_dev *dev = pr_pool->dev;

	pool_index.fs_bulk = fs_bulk;
	pool_index.index = pr_data->offset;
	if (mlx5_fs_pool_release_index(pr_pool, &pool_index))
		mlx5_core_warn(dev, "Attempted to release packet reformat which is not acquired\n");
}

struct mlx5hws_action *mlx5_fs_hws_pr_get_action(struct mlx5_fs_hws_pr *pr_data)
{
	return pr_data->bulk->hws_action;
}

static struct mlx5hws_action *
mlx5_fs_mh_bulk_action_create(struct mlx5hws_context *ctx,
			      struct mlx5hws_action_mh_pattern *pattern)
{
	u32 flags = MLX5HWS_ACTION_FLAG_HWS_FDB;
	u32 log_bulk_size;

	log_bulk_size = ilog2(MLX5_FS_HWS_DEFAULT_BULK_LEN);
	return mlx5hws_action_create_modify_header(ctx, 1, pattern,
						   log_bulk_size, flags);
}

static struct mlx5_fs_bulk *
mlx5_fs_hws_mh_bulk_create(struct mlx5_core_dev *dev, void *pool_ctx)
{
	struct mlx5hws_action_mh_pattern *pattern;
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_fs_hws_mh_bulk *mh_bulk;
	struct mlx5hws_context *ctx;
	int bulk_len;

	if (!pool_ctx)
		return NULL;

	root_ns = mlx5_get_root_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns || root_ns->mode != MLX5_FLOW_STEERING_MODE_HMFS)
		return NULL;

	ctx = root_ns->fs_hws_context.hws_ctx;
	if (!ctx)
		return NULL;

	pattern = pool_ctx;
	bulk_len = MLX5_FS_HWS_DEFAULT_BULK_LEN;
	mh_bulk = kvzalloc(struct_size(mh_bulk, mhs_data, bulk_len), GFP_KERNEL);
	if (!mh_bulk)
		return NULL;

	if (mlx5_fs_bulk_init(dev, &mh_bulk->fs_bulk, bulk_len))
		goto free_mh_bulk;

	for (int i = 0; i < bulk_len; i++) {
		mh_bulk->mhs_data[i].bulk = mh_bulk;
		mh_bulk->mhs_data[i].offset = i;
	}

	mh_bulk->hws_action = mlx5_fs_mh_bulk_action_create(ctx, pattern);
	if (!mh_bulk->hws_action)
		goto cleanup_fs_bulk;

	return &mh_bulk->fs_bulk;

cleanup_fs_bulk:
	mlx5_fs_bulk_cleanup(&mh_bulk->fs_bulk);
free_mh_bulk:
	kvfree(mh_bulk);
	return NULL;
}

static int
mlx5_fs_hws_mh_bulk_destroy(struct mlx5_core_dev *dev,
			    struct mlx5_fs_bulk *fs_bulk)
{
	struct mlx5_fs_hws_mh_bulk *mh_bulk;

	mh_bulk = container_of(fs_bulk, struct mlx5_fs_hws_mh_bulk, fs_bulk);
	if (mlx5_fs_bulk_get_free_amount(fs_bulk) < fs_bulk->bulk_len) {
		mlx5_core_err(dev, "Freeing bulk before all modify header were released\n");
		return -EBUSY;
	}

	mlx5hws_action_destroy(mh_bulk->hws_action);
	mlx5_fs_bulk_cleanup(fs_bulk);
	kvfree(mh_bulk);

	return 0;
}

static const struct mlx5_fs_pool_ops mlx5_fs_hws_mh_pool_ops = {
	.bulk_create = mlx5_fs_hws_mh_bulk_create,
	.bulk_destroy = mlx5_fs_hws_mh_bulk_destroy,
	.update_threshold = mlx5_hws_pool_update_threshold,
};

int mlx5_fs_hws_mh_pool_init(struct mlx5_fs_pool *fs_hws_mh_pool,
			     struct mlx5_core_dev *dev,
			     struct mlx5hws_action_mh_pattern *pattern)
{
	struct mlx5hws_action_mh_pattern *pool_pattern;

	pool_pattern = kzalloc(sizeof(*pool_pattern), GFP_KERNEL);
	if (!pool_pattern)
		return -ENOMEM;
	pool_pattern->data = kmemdup(pattern->data, pattern->sz, GFP_KERNEL);
	if (!pool_pattern->data) {
		kfree(pool_pattern);
		return -ENOMEM;
	}
	pool_pattern->sz = pattern->sz;
	mlx5_fs_pool_init(fs_hws_mh_pool, dev, &mlx5_fs_hws_mh_pool_ops,
			  pool_pattern);
	return 0;
}

void mlx5_fs_hws_mh_pool_cleanup(struct mlx5_fs_pool *fs_hws_mh_pool)
{
	struct mlx5hws_action_mh_pattern *pool_pattern;

	mlx5_fs_pool_cleanup(fs_hws_mh_pool);
	pool_pattern = fs_hws_mh_pool->pool_ctx;
	if (!pool_pattern)
		return;
	kfree(pool_pattern->data);
	kfree(pool_pattern);
}

struct mlx5_fs_hws_mh *
mlx5_fs_hws_mh_pool_acquire_mh(struct mlx5_fs_pool *mh_pool)
{
	struct mlx5_fs_pool_index pool_index = {};
	struct mlx5_fs_hws_mh_bulk *mh_bulk;
	int err;

	err = mlx5_fs_pool_acquire_index(mh_pool, &pool_index);
	if (err)
		return ERR_PTR(err);
	mh_bulk = container_of(pool_index.fs_bulk, struct mlx5_fs_hws_mh_bulk,
			       fs_bulk);
	return &mh_bulk->mhs_data[pool_index.index];
}

void mlx5_fs_hws_mh_pool_release_mh(struct mlx5_fs_pool *mh_pool,
				    struct mlx5_fs_hws_mh *mh_data)
{
	struct mlx5_fs_bulk *fs_bulk = &mh_data->bulk->fs_bulk;
	struct mlx5_fs_pool_index pool_index = {};
	struct mlx5_core_dev *dev = mh_pool->dev;

	pool_index.fs_bulk = fs_bulk;
	pool_index.index = mh_data->offset;
	if (mlx5_fs_pool_release_index(mh_pool, &pool_index))
		mlx5_core_warn(dev, "Attempted to release modify header which is not acquired\n");
}

bool mlx5_fs_hws_mh_pool_match(struct mlx5_fs_pool *mh_pool,
			       struct mlx5hws_action_mh_pattern *pattern)
{
	struct mlx5hws_action_mh_pattern *pool_pattern;
	int num_actions, i;

	pool_pattern = mh_pool->pool_ctx;
	if (WARN_ON_ONCE(!pool_pattern))
		return false;

	if (pattern->sz != pool_pattern->sz)
		return false;
	num_actions = pattern->sz / MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto);
	for (i = 0; i < num_actions; i++) {
		if ((__force __be32)pattern->data[i] !=
		    (__force __be32)pool_pattern->data[i])
			return false;
	}
	return true;
}

struct mlx5hws_action *mlx5_fc_get_hws_action(struct mlx5hws_context *ctx,
					      struct mlx5_fc *counter)
{
	struct mlx5_fs_hws_create_action_ctx create_ctx;
	struct mlx5_fc_bulk *fc_bulk = counter->bulk;

	create_ctx.hws_ctx = ctx;
	create_ctx.id = fc_bulk->base_id;
	create_ctx.actions_type = MLX5HWS_ACTION_TYP_CTR;

	return mlx5_fs_get_hws_action(&fc_bulk->hws_data, &create_ctx);
}

void mlx5_fc_put_hws_action(struct mlx5_fc *counter)
{
	mlx5_fs_put_hws_action(&counter->bulk->hws_data);
}
