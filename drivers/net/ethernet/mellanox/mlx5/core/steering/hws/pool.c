// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"
#include "buddy.h"

static void hws_pool_free_one_resource(struct mlx5hws_pool_resource *resource)
{
	switch (resource->pool->type) {
	case MLX5HWS_POOL_TYPE_STE:
		mlx5hws_cmd_ste_destroy(resource->pool->ctx->mdev, resource->base_id);
		break;
	case MLX5HWS_POOL_TYPE_STC:
		mlx5hws_cmd_stc_destroy(resource->pool->ctx->mdev, resource->base_id);
		break;
	default:
		break;
	}

	kfree(resource);
}

static void hws_pool_resource_free(struct mlx5hws_pool *pool)
{
	hws_pool_free_one_resource(pool->resource);
	pool->resource = NULL;

	if (pool->tbl_type == MLX5HWS_TABLE_TYPE_FDB) {
		hws_pool_free_one_resource(pool->mirror_resource);
		pool->mirror_resource = NULL;
	}
}

static struct mlx5hws_pool_resource *
hws_pool_create_one_resource(struct mlx5hws_pool *pool, u32 log_range,
			     u32 fw_ft_type)
{
	struct mlx5hws_cmd_ste_create_attr ste_attr;
	struct mlx5hws_cmd_stc_create_attr stc_attr;
	struct mlx5hws_pool_resource *resource;
	u32 obj_id = 0;
	int ret;

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		return NULL;

	switch (pool->type) {
	case MLX5HWS_POOL_TYPE_STE:
		ste_attr.log_obj_range = log_range;
		ste_attr.table_type = fw_ft_type;
		ret = mlx5hws_cmd_ste_create(pool->ctx->mdev, &ste_attr, &obj_id);
		break;
	case MLX5HWS_POOL_TYPE_STC:
		stc_attr.log_obj_range = log_range;
		stc_attr.table_type = fw_ft_type;
		ret = mlx5hws_cmd_stc_create(pool->ctx->mdev, &stc_attr, &obj_id);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		goto free_resource;

	resource->pool = pool;
	resource->range = 1 << log_range;
	resource->base_id = obj_id;

	return resource;

free_resource:
	kfree(resource);
	return NULL;
}

static int hws_pool_resource_alloc(struct mlx5hws_pool *pool)
{
	struct mlx5hws_pool_resource *resource;
	u32 fw_ft_type, opt_log_range;

	fw_ft_type = mlx5hws_table_get_res_fw_ft_type(pool->tbl_type, false);
	opt_log_range = pool->opt_type == MLX5HWS_POOL_OPTIMIZE_MIRROR ?
				0 : pool->alloc_log_sz;
	resource = hws_pool_create_one_resource(pool, opt_log_range, fw_ft_type);
	if (!resource) {
		mlx5hws_err(pool->ctx, "Failed to allocate resource\n");
		return -EINVAL;
	}

	pool->resource = resource;

	if (pool->tbl_type == MLX5HWS_TABLE_TYPE_FDB) {
		struct mlx5hws_pool_resource *mirror_resource;

		fw_ft_type = mlx5hws_table_get_res_fw_ft_type(pool->tbl_type, true);
		opt_log_range = pool->opt_type == MLX5HWS_POOL_OPTIMIZE_ORIG ?
					0 : pool->alloc_log_sz;
		mirror_resource = hws_pool_create_one_resource(pool, opt_log_range, fw_ft_type);
		if (!mirror_resource) {
			mlx5hws_err(pool->ctx, "Failed to allocate mirrored resource\n");
			hws_pool_free_one_resource(resource);
			pool->resource = NULL;
			return -EINVAL;
		}
		pool->mirror_resource = mirror_resource;
	}

	return 0;
}

static int hws_pool_buddy_init(struct mlx5hws_pool *pool)
{
	struct mlx5hws_buddy_mem *buddy;

	buddy = mlx5hws_buddy_create(pool->alloc_log_sz);
	if (!buddy) {
		mlx5hws_err(pool->ctx, "Failed to create buddy order: %zu\n",
			    pool->alloc_log_sz);
		return -ENOMEM;
	}

	if (hws_pool_resource_alloc(pool) != 0) {
		mlx5hws_err(pool->ctx, "Failed to create resource type: %d size %zu\n",
			    pool->type, pool->alloc_log_sz);
		mlx5hws_buddy_cleanup(buddy);
		kfree(buddy);
		return -ENOMEM;
	}

	pool->db.buddy = buddy;

	return 0;
}

static int hws_pool_buddy_db_get_chunk(struct mlx5hws_pool *pool,
				       struct mlx5hws_pool_chunk *chunk)
{
	struct mlx5hws_buddy_mem *buddy = pool->db.buddy;

	if (!buddy) {
		mlx5hws_err(pool->ctx, "Bad buddy state\n");
		return -EINVAL;
	}

	chunk->offset = mlx5hws_buddy_alloc_mem(buddy, chunk->order);
	if (chunk->offset >= 0)
		return 0;

	return -ENOMEM;
}

static void hws_pool_buddy_db_put_chunk(struct mlx5hws_pool *pool,
					struct mlx5hws_pool_chunk *chunk)
{
	struct mlx5hws_buddy_mem *buddy;

	buddy = pool->db.buddy;
	if (!buddy) {
		mlx5hws_err(pool->ctx, "Bad buddy state\n");
		return;
	}

	mlx5hws_buddy_free_mem(buddy, chunk->offset, chunk->order);
}

static void hws_pool_buddy_db_uninit(struct mlx5hws_pool *pool)
{
	struct mlx5hws_buddy_mem *buddy;

	buddy = pool->db.buddy;
	if (buddy) {
		mlx5hws_buddy_cleanup(buddy);
		kfree(buddy);
		pool->db.buddy = NULL;
	}
}

static int hws_pool_buddy_db_init(struct mlx5hws_pool *pool)
{
	int ret;

	ret = hws_pool_buddy_init(pool);
	if (ret)
		return ret;

	pool->p_db_uninit = &hws_pool_buddy_db_uninit;
	pool->p_get_chunk = &hws_pool_buddy_db_get_chunk;
	pool->p_put_chunk = &hws_pool_buddy_db_put_chunk;

	return 0;
}

static unsigned long *hws_pool_create_and_init_bitmap(u32 log_range)
{
	unsigned long *bitmap;

	bitmap = bitmap_zalloc(1 << log_range, GFP_KERNEL);
	if (!bitmap)
		return NULL;

	bitmap_fill(bitmap, 1 << log_range);

	return bitmap;
}

static int hws_pool_bitmap_init(struct mlx5hws_pool *pool)
{
	unsigned long *bitmap;

	bitmap = hws_pool_create_and_init_bitmap(pool->alloc_log_sz);
	if (!bitmap) {
		mlx5hws_err(pool->ctx, "Failed to create bitmap order: %zu\n",
			    pool->alloc_log_sz);
		return -ENOMEM;
	}

	if (hws_pool_resource_alloc(pool) != 0) {
		mlx5hws_err(pool->ctx, "Failed to create resource type: %d: size %zu\n",
			    pool->type, pool->alloc_log_sz);
		bitmap_free(bitmap);
		return -ENOMEM;
	}

	pool->db.bitmap = bitmap;

	return 0;
}

static int hws_pool_bitmap_db_get_chunk(struct mlx5hws_pool *pool,
					struct mlx5hws_pool_chunk *chunk)
{
	unsigned long *bitmap, size;

	if (chunk->order != 0) {
		mlx5hws_err(pool->ctx, "Pool only supports order 0 allocs\n");
		return -EINVAL;
	}

	bitmap = pool->db.bitmap;
	if (!bitmap) {
		mlx5hws_err(pool->ctx, "Bad bitmap state\n");
		return -EINVAL;
	}

	size = 1 << pool->alloc_log_sz;

	chunk->offset = find_first_bit(bitmap, size);
	if (chunk->offset >= size)
		return -ENOMEM;

	bitmap_clear(bitmap, chunk->offset, 1);

	return 0;
}

static void hws_pool_bitmap_db_put_chunk(struct mlx5hws_pool *pool,
					 struct mlx5hws_pool_chunk *chunk)
{
	unsigned long *bitmap;

	bitmap = pool->db.bitmap;
	if (!bitmap) {
		mlx5hws_err(pool->ctx, "Bad bitmap state\n");
		return;
	}

	bitmap_set(bitmap, chunk->offset, 1);
}

static void hws_pool_bitmap_db_uninit(struct mlx5hws_pool *pool)
{
	unsigned long *bitmap;

	bitmap = pool->db.bitmap;
	if (bitmap) {
		bitmap_free(bitmap);
		pool->db.bitmap = NULL;
	}
}

static int hws_pool_bitmap_db_init(struct mlx5hws_pool *pool)
{
	int ret;

	ret = hws_pool_bitmap_init(pool);
	if (ret)
		return ret;

	pool->p_db_uninit = &hws_pool_bitmap_db_uninit;
	pool->p_get_chunk = &hws_pool_bitmap_db_get_chunk;
	pool->p_put_chunk = &hws_pool_bitmap_db_put_chunk;

	return 0;
}

static int hws_pool_db_init(struct mlx5hws_pool *pool,
			    enum mlx5hws_db_type db_type)
{
	int ret;

	if (db_type == MLX5HWS_POOL_DB_TYPE_BITMAP)
		ret = hws_pool_bitmap_db_init(pool);
	else
		ret = hws_pool_buddy_db_init(pool);

	if (ret) {
		mlx5hws_err(pool->ctx, "Failed to init pool type: %d (ret: %d)\n",
			    db_type, ret);
		return ret;
	}

	return 0;
}

static void hws_pool_db_unint(struct mlx5hws_pool *pool)
{
	pool->p_db_uninit(pool);
}

int mlx5hws_pool_chunk_alloc(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk)
{
	int ret;

	mutex_lock(&pool->lock);
	ret = pool->p_get_chunk(pool, chunk);
	if (ret == 0)
		pool->available_elems -= 1 << chunk->order;
	mutex_unlock(&pool->lock);

	return ret;
}

void mlx5hws_pool_chunk_free(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk)
{
	mutex_lock(&pool->lock);
	pool->p_put_chunk(pool, chunk);
	pool->available_elems += 1 << chunk->order;
	mutex_unlock(&pool->lock);
}

struct mlx5hws_pool *
mlx5hws_pool_create(struct mlx5hws_context *ctx, struct mlx5hws_pool_attr *pool_attr)
{
	enum mlx5hws_db_type res_db_type;
	struct mlx5hws_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	pool->ctx = ctx;
	pool->type = pool_attr->pool_type;
	pool->alloc_log_sz = pool_attr->alloc_log_sz;
	pool->flags = pool_attr->flags;
	pool->tbl_type = pool_attr->table_type;
	pool->opt_type = pool_attr->opt_type;

	if (pool->flags & MLX5HWS_POOL_FLAG_BUDDY)
		res_db_type = MLX5HWS_POOL_DB_TYPE_BUDDY;
	else
		res_db_type = MLX5HWS_POOL_DB_TYPE_BITMAP;

	pool->alloc_log_sz = pool_attr->alloc_log_sz;
	pool->available_elems = 1 << pool_attr->alloc_log_sz;

	if (hws_pool_db_init(pool, res_db_type))
		goto free_pool;

	mutex_init(&pool->lock);

	return pool;

free_pool:
	kfree(pool);
	return NULL;
}

void mlx5hws_pool_destroy(struct mlx5hws_pool *pool)
{
	mutex_destroy(&pool->lock);

	if (pool->available_elems != 1 << pool->alloc_log_sz)
		mlx5hws_err(pool->ctx, "Attempting to destroy non-empty pool\n");

	if (pool->resource)
		hws_pool_resource_free(pool);

	hws_pool_db_unint(pool);

	kfree(pool);
}
