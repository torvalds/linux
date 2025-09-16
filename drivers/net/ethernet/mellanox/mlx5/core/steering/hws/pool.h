/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_POOL_H_
#define MLX5HWS_POOL_H_

#define MLX5HWS_POOL_STC_LOG_SZ 15

enum mlx5hws_pool_type {
	MLX5HWS_POOL_TYPE_STE,
	MLX5HWS_POOL_TYPE_STC,
};

struct mlx5hws_pool_chunk {
	int offset;
	int order;
};

struct mlx5hws_pool_resource {
	struct mlx5hws_pool *pool;
	u32 base_id;
	u32 range;
};

enum mlx5hws_pool_flags {
	/* Managed by a buddy allocator. If this is not set only allocations of
	 * order 0 are supported.
	 */
	MLX5HWS_POOL_FLAG_BUDDY = BIT(0),
};

enum mlx5hws_pool_optimize {
	MLX5HWS_POOL_OPTIMIZE_NONE = 0x0,
	MLX5HWS_POOL_OPTIMIZE_ORIG = 0x1,
	MLX5HWS_POOL_OPTIMIZE_MIRROR = 0x2,
	MLX5HWS_POOL_OPTIMIZE_MAX = 0x3,
};

struct mlx5hws_pool_attr {
	enum mlx5hws_pool_type pool_type;
	enum mlx5hws_table_type table_type;
	enum mlx5hws_pool_flags flags;
	enum mlx5hws_pool_optimize opt_type;
	/* Allocation size once memory is depleted */
	size_t alloc_log_sz;
};

enum mlx5hws_db_type {
	/* Uses a bitmap, supports only allocations of order 0. */
	MLX5HWS_POOL_DB_TYPE_BITMAP,
	/* Entries are managed using a buddy mechanism. */
	MLX5HWS_POOL_DB_TYPE_BUDDY,
};

struct mlx5hws_pool_db {
	enum mlx5hws_db_type type;
	union {
		unsigned long *bitmap;
		struct mlx5hws_buddy_mem *buddy;
	};
};

typedef int (*mlx5hws_pool_db_get_chunk)(struct mlx5hws_pool *pool,
					struct mlx5hws_pool_chunk *chunk);
typedef void (*mlx5hws_pool_db_put_chunk)(struct mlx5hws_pool *pool,
					 struct mlx5hws_pool_chunk *chunk);
typedef void (*mlx5hws_pool_unint_db)(struct mlx5hws_pool *pool);

struct mlx5hws_pool {
	struct mlx5hws_context *ctx;
	enum mlx5hws_pool_type type;
	enum mlx5hws_pool_flags flags;
	struct mutex lock; /* protect the pool */
	size_t alloc_log_sz;
	size_t available_elems;
	enum mlx5hws_table_type tbl_type;
	enum mlx5hws_pool_optimize opt_type;
	struct mlx5hws_pool_resource *resource;
	struct mlx5hws_pool_resource *mirror_resource;
	struct mlx5hws_pool_db db;
	/* Functions */
	mlx5hws_pool_unint_db p_db_uninit;
	mlx5hws_pool_db_get_chunk p_get_chunk;
	mlx5hws_pool_db_put_chunk p_put_chunk;
};

struct mlx5hws_pool *
mlx5hws_pool_create(struct mlx5hws_context *ctx,
		    struct mlx5hws_pool_attr *pool_attr);

void mlx5hws_pool_destroy(struct mlx5hws_pool *pool);

int mlx5hws_pool_chunk_alloc(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk);

void mlx5hws_pool_chunk_free(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk);

static inline u32 mlx5hws_pool_get_base_id(struct mlx5hws_pool *pool)
{
	return pool->resource->base_id;
}

static inline u32 mlx5hws_pool_get_base_mirror_id(struct mlx5hws_pool *pool)
{
	return pool->mirror_resource->base_id;
}

static inline bool
mlx5hws_pool_empty(struct mlx5hws_pool *pool)
{
	bool ret;

	mutex_lock(&pool->lock);
	ret = pool->available_elems == 0;
	mutex_unlock(&pool->lock);

	return ret;
}

static inline bool
mlx5hws_pool_full(struct mlx5hws_pool *pool)
{
	bool ret;

	mutex_lock(&pool->lock);
	ret = pool->available_elems == (1 << pool->alloc_log_sz);
	mutex_unlock(&pool->lock);

	return ret;
}
#endif /* MLX5HWS_POOL_H_ */
