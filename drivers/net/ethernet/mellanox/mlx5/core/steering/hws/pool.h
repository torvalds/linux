/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_POOL_H_
#define MLX5HWS_POOL_H_

#define MLX5HWS_POOL_STC_LOG_SZ 15

#define MLX5HWS_POOL_RESOURCE_ARR_SZ 100

enum mlx5hws_pool_type {
	MLX5HWS_POOL_TYPE_STE,
	MLX5HWS_POOL_TYPE_STC,
};

struct mlx5hws_pool_chunk {
	u32 resource_idx;
	/* Internal offset, relative to base index */
	int offset;
	int order;
};

struct mlx5hws_pool_resource {
	struct mlx5hws_pool *pool;
	u32 base_id;
	u32 range;
};

enum mlx5hws_pool_flags {
	/* Only a one resource in that pool */
	MLX5HWS_POOL_FLAGS_ONE_RESOURCE = 1 << 0,
	MLX5HWS_POOL_FLAGS_RELEASE_FREE_RESOURCE = 1 << 1,
	/* No sharing resources between chunks */
	MLX5HWS_POOL_FLAGS_RESOURCE_PER_CHUNK = 1 << 2,
	/* All objects are in the same size */
	MLX5HWS_POOL_FLAGS_FIXED_SIZE_OBJECTS = 1 << 3,
	/* Managed by buddy allocator */
	MLX5HWS_POOL_FLAGS_BUDDY_MANAGED = 1 << 4,
	/* Allocate pool_type memory on pool creation */
	MLX5HWS_POOL_FLAGS_ALLOC_MEM_ON_CREATE = 1 << 5,

	/* These values should be used by the caller */
	MLX5HWS_POOL_FLAGS_FOR_STC_POOL =
		MLX5HWS_POOL_FLAGS_ONE_RESOURCE |
		MLX5HWS_POOL_FLAGS_FIXED_SIZE_OBJECTS,
	MLX5HWS_POOL_FLAGS_FOR_MATCHER_STE_POOL =
		MLX5HWS_POOL_FLAGS_RELEASE_FREE_RESOURCE |
		MLX5HWS_POOL_FLAGS_RESOURCE_PER_CHUNK,
	MLX5HWS_POOL_FLAGS_FOR_STE_ACTION_POOL =
		MLX5HWS_POOL_FLAGS_ONE_RESOURCE |
		MLX5HWS_POOL_FLAGS_BUDDY_MANAGED |
		MLX5HWS_POOL_FLAGS_ALLOC_MEM_ON_CREATE,
};

enum mlx5hws_pool_optimize {
	MLX5HWS_POOL_OPTIMIZE_NONE = 0x0,
	MLX5HWS_POOL_OPTIMIZE_ORIG = 0x1,
	MLX5HWS_POOL_OPTIMIZE_MIRROR = 0x2,
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
	/* Uses for allocating chunk of big memory, each element has its own resource in the FW*/
	MLX5HWS_POOL_DB_TYPE_GENERAL_SIZE,
	/* One resource only, all the elements are with same one size */
	MLX5HWS_POOL_DB_TYPE_ONE_SIZE_RESOURCE,
	/* Many resources, the memory allocated with buddy mechanism */
	MLX5HWS_POOL_DB_TYPE_BUDDY,
};

struct mlx5hws_buddy_manager {
	struct mlx5hws_buddy_mem *buddies[MLX5HWS_POOL_RESOURCE_ARR_SZ];
};

struct mlx5hws_pool_elements {
	u32 num_of_elements;
	unsigned long *bitmap;
	u32 log_size;
	bool is_full;
};

struct mlx5hws_element_manager {
	struct mlx5hws_pool_elements *elements[MLX5HWS_POOL_RESOURCE_ARR_SZ];
};

struct mlx5hws_pool_db {
	enum mlx5hws_db_type type;
	union {
		struct mlx5hws_element_manager *element_manager;
		struct mlx5hws_buddy_manager *buddy_manager;
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
	enum mlx5hws_table_type tbl_type;
	enum mlx5hws_pool_optimize opt_type;
	struct mlx5hws_pool_resource *resource[MLX5HWS_POOL_RESOURCE_ARR_SZ];
	struct mlx5hws_pool_resource *mirror_resource[MLX5HWS_POOL_RESOURCE_ARR_SZ];
	/* DB */
	struct mlx5hws_pool_db db;
	/* Functions */
	mlx5hws_pool_unint_db p_db_uninit;
	mlx5hws_pool_db_get_chunk p_get_chunk;
	mlx5hws_pool_db_put_chunk p_put_chunk;
};

struct mlx5hws_pool *
mlx5hws_pool_create(struct mlx5hws_context *ctx,
		    struct mlx5hws_pool_attr *pool_attr);

int mlx5hws_pool_destroy(struct mlx5hws_pool *pool);

int mlx5hws_pool_chunk_alloc(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk);

void mlx5hws_pool_chunk_free(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk);

static inline u32
mlx5hws_pool_chunk_get_base_id(struct mlx5hws_pool *pool,
			       struct mlx5hws_pool_chunk *chunk)
{
	return pool->resource[chunk->resource_idx]->base_id;
}

static inline u32
mlx5hws_pool_chunk_get_base_mirror_id(struct mlx5hws_pool *pool,
				      struct mlx5hws_pool_chunk *chunk)
{
	return pool->mirror_resource[chunk->resource_idx]->base_id;
}
#endif /* MLX5HWS_POOL_H_ */
