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

	if (ret) {
		mlx5hws_err(pool->ctx, "Failed to allocate resource objects\n");
		goto free_resource;
	}

	resource->pool = pool;
	resource->range = 1 << log_range;
	resource->base_id = obj_id;

	return resource;

free_resource:
	kfree(resource);
	return NULL;
}

static int
hws_pool_resource_alloc(struct mlx5hws_pool *pool, u32 log_range)
{
	struct mlx5hws_pool_resource *resource;
	u32 fw_ft_type, opt_log_range;

	fw_ft_type = mlx5hws_table_get_res_fw_ft_type(pool->tbl_type, false);
	opt_log_range = pool->opt_type == MLX5HWS_POOL_OPTIMIZE_ORIG ? 0 : log_range;
	resource = hws_pool_create_one_resource(pool, opt_log_range, fw_ft_type);
	if (!resource) {
		mlx5hws_err(pool->ctx, "Failed allocating resource\n");
		return -EINVAL;
	}

	pool->resource = resource;

	if (pool->tbl_type == MLX5HWS_TABLE_TYPE_FDB) {
		struct mlx5hws_pool_resource *mirror_resource;

		fw_ft_type = mlx5hws_table_get_res_fw_ft_type(pool->tbl_type, true);
		opt_log_range = pool->opt_type == MLX5HWS_POOL_OPTIMIZE_MIRROR ? 0 : log_range;
		mirror_resource = hws_pool_create_one_resource(pool, opt_log_range, fw_ft_type);
		if (!mirror_resource) {
			mlx5hws_err(pool->ctx, "Failed allocating mirrored resource\n");
			hws_pool_free_one_resource(resource);
			pool->resource = NULL;
			return -EINVAL;
		}
		pool->mirror_resource = mirror_resource;
	}

	return 0;
}

static unsigned long *hws_pool_create_and_init_bitmap(u32 log_range)
{
	unsigned long *cur_bmp;

	cur_bmp = bitmap_zalloc(1 << log_range, GFP_KERNEL);
	if (!cur_bmp)
		return NULL;

	bitmap_fill(cur_bmp, 1 << log_range);

	return cur_bmp;
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

static struct mlx5hws_buddy_mem *
hws_pool_buddy_get_buddy(struct mlx5hws_pool *pool, u32 order)
{
	static struct mlx5hws_buddy_mem *buddy;
	u32 new_buddy_size;

	buddy = pool->db.buddy;
	if (buddy)
		return buddy;

	new_buddy_size = max(pool->alloc_log_sz, order);
	buddy = mlx5hws_buddy_create(new_buddy_size);
	if (!buddy) {
		mlx5hws_err(pool->ctx, "Failed to create buddy order: %d\n",
			    new_buddy_size);
		return NULL;
	}

	if (hws_pool_resource_alloc(pool, new_buddy_size) != 0) {
		mlx5hws_err(pool->ctx, "Failed to create resource type: %d: size %d\n",
			    pool->type, new_buddy_size);
		mlx5hws_buddy_cleanup(buddy);
		return NULL;
	}

	pool->db.buddy = buddy;

	return buddy;
}

static int hws_pool_buddy_get_mem_chunk(struct mlx5hws_pool *pool,
					int order,
					int *seg)
{
	struct mlx5hws_buddy_mem *buddy;

	buddy = hws_pool_buddy_get_buddy(pool, order);
	if (!buddy)
		return -ENOMEM;

	*seg = mlx5hws_buddy_alloc_mem(buddy, order);
	if (*seg >= 0)
		return 0;

	return -ENOMEM;
}

static int hws_pool_buddy_db_get_chunk(struct mlx5hws_pool *pool,
				       struct mlx5hws_pool_chunk *chunk)
{
	int ret = 0;

	ret = hws_pool_buddy_get_mem_chunk(pool, chunk->order,
					   &chunk->offset);
	if (ret)
		mlx5hws_err(pool->ctx, "Failed to get free slot for chunk with order: %d\n",
			    chunk->order);

	return ret;
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

static int hws_pool_buddy_db_init(struct mlx5hws_pool *pool, u32 log_range)
{
	if (pool->flags & MLX5HWS_POOL_FLAGS_ALLOC_MEM_ON_CREATE) {
		if (!hws_pool_buddy_get_buddy(pool, log_range)) {
			mlx5hws_err(pool->ctx,
				    "Failed allocating memory on create log_sz: %d\n", log_range);
			return -ENOMEM;
		}
	}

	pool->p_db_uninit = &hws_pool_buddy_db_uninit;
	pool->p_get_chunk = &hws_pool_buddy_db_get_chunk;
	pool->p_put_chunk = &hws_pool_buddy_db_put_chunk;

	return 0;
}

static int hws_pool_create_resource(struct mlx5hws_pool *pool, u32 alloc_size)
{
	int ret = hws_pool_resource_alloc(pool, alloc_size);

	if (ret) {
		mlx5hws_err(pool->ctx, "Failed to create resource type: %d: size %d\n",
			    pool->type, alloc_size);
		return ret;
	}

	return 0;
}

static struct mlx5hws_pool_elements *
hws_pool_element_create_new_elem(struct mlx5hws_pool *pool, u32 order)
{
	struct mlx5hws_pool_elements *elem;
	u32 alloc_size;

	alloc_size = pool->alloc_log_sz;

	elem = kzalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem)
		return NULL;

	/* Sharing the same resource, also means that all the elements are with size 1 */
	if ((pool->flags & MLX5HWS_POOL_FLAGS_FIXED_SIZE_OBJECTS) &&
	    !(pool->flags & MLX5HWS_POOL_FLAGS_RESOURCE_PER_CHUNK)) {
		 /* Currently all chunks in size 1 */
		elem->bitmap = hws_pool_create_and_init_bitmap(alloc_size - order);
		if (!elem->bitmap) {
			mlx5hws_err(pool->ctx,
				    "Failed to create bitmap type: %d: size %d\n",
				    pool->type, alloc_size);
			goto free_elem;
		}

		elem->log_size = alloc_size - order;
	}

	if (hws_pool_create_resource(pool, alloc_size)) {
		mlx5hws_err(pool->ctx, "Failed to create resource type: %d: size %d\n",
			    pool->type, alloc_size);
		goto free_db;
	}

	pool->db.element = elem;

	return elem;

free_db:
	bitmap_free(elem->bitmap);
free_elem:
	kfree(elem);
	return NULL;
}

static int hws_pool_element_find_seg(struct mlx5hws_pool_elements *elem, int *seg)
{
	unsigned int segment, size;

	size = 1 << elem->log_size;

	segment = find_first_bit(elem->bitmap, size);
	if (segment >= size) {
		elem->is_full = true;
		return -ENOMEM;
	}

	bitmap_clear(elem->bitmap, segment, 1);
	*seg = segment;
	return 0;
}

static int
hws_pool_onesize_element_get_mem_chunk(struct mlx5hws_pool *pool, u32 order,
				       int *seg)
{
	struct mlx5hws_pool_elements *elem;

	elem = pool->db.element;
	if (!elem)
		elem = hws_pool_element_create_new_elem(pool, order);
	if (!elem)
		goto err_no_elem;

	if (hws_pool_element_find_seg(elem, seg) != 0) {
		mlx5hws_err(pool->ctx, "No more resources (last request order: %d)\n", order);
		return -ENOMEM;
	}

	elem->num_of_elements++;
	return 0;

err_no_elem:
	mlx5hws_err(pool->ctx, "Failed to allocate element for order: %d\n", order);
	return -ENOMEM;
}

static int hws_pool_general_element_get_mem_chunk(struct mlx5hws_pool *pool,
						  u32 order, int *seg)
{
	int ret;

	if (!pool->resource) {
		ret = hws_pool_create_resource(pool, order);
		if (ret)
			goto err_no_res;
		*seg = 0; /* One memory slot in that element */
		return 0;
	}

	mlx5hws_err(pool->ctx, "No more resources (last request order: %d)\n", order);
	return -ENOMEM;

err_no_res:
	mlx5hws_err(pool->ctx, "Failed to allocate element for order: %d\n", order);
	return -ENOMEM;
}

static int hws_pool_general_element_db_get_chunk(struct mlx5hws_pool *pool,
						 struct mlx5hws_pool_chunk *chunk)
{
	int ret;

	ret = hws_pool_general_element_get_mem_chunk(pool, chunk->order,
						     &chunk->offset);
	if (ret)
		mlx5hws_err(pool->ctx, "Failed to get free slot for chunk with order: %d\n",
			    chunk->order);

	return ret;
}

static void hws_pool_general_element_db_put_chunk(struct mlx5hws_pool *pool,
						  struct mlx5hws_pool_chunk *chunk)
{
	if (pool->flags & MLX5HWS_POOL_FLAGS_RELEASE_FREE_RESOURCE)
		hws_pool_resource_free(pool);
}

static void hws_pool_general_element_db_uninit(struct mlx5hws_pool *pool)
{
	(void)pool;
}

/* This memory management works as the following:
 * - At start doesn't allocate no mem at all.
 * - When new request for chunk arrived:
 *	allocate resource and give it.
 * - When free that chunk:
 *	the resource is freed.
 */
static int hws_pool_general_element_db_init(struct mlx5hws_pool *pool)
{
	pool->p_db_uninit = &hws_pool_general_element_db_uninit;
	pool->p_get_chunk = &hws_pool_general_element_db_get_chunk;
	pool->p_put_chunk = &hws_pool_general_element_db_put_chunk;

	return 0;
}

static void
hws_onesize_element_db_destroy_element(struct mlx5hws_pool *pool,
				       struct mlx5hws_pool_elements *elem)
{
	hws_pool_resource_free(pool);
	bitmap_free(elem->bitmap);
	kfree(elem);
	pool->db.element = NULL;
}

static void hws_onesize_element_db_put_chunk(struct mlx5hws_pool *pool,
					     struct mlx5hws_pool_chunk *chunk)
{
	struct mlx5hws_pool_elements *elem;

	elem = pool->db.element;
	if (!elem) {
		mlx5hws_err(pool->ctx, "Pool element was not allocated\n");
		return;
	}

	bitmap_set(elem->bitmap, chunk->offset, 1);
	elem->is_full = false;
	elem->num_of_elements--;

	if (pool->flags & MLX5HWS_POOL_FLAGS_RELEASE_FREE_RESOURCE &&
	    !elem->num_of_elements)
		hws_onesize_element_db_destroy_element(pool, elem);
}

static int hws_onesize_element_db_get_chunk(struct mlx5hws_pool *pool,
					    struct mlx5hws_pool_chunk *chunk)
{
	int ret = 0;

	ret = hws_pool_onesize_element_get_mem_chunk(pool, chunk->order,
						     &chunk->offset);
	if (ret)
		mlx5hws_err(pool->ctx, "Failed to get free slot for chunk with order: %d\n",
			    chunk->order);

	return ret;
}

static void hws_onesize_element_db_uninit(struct mlx5hws_pool *pool)
{
	struct mlx5hws_pool_elements *elem = pool->db.element;

	if (elem) {
		bitmap_free(elem->bitmap);
		kfree(elem);
		pool->db.element = NULL;
	}
}

/* This memory management works as the following:
 * - At start doesn't allocate no mem at all.
 * - When new request for chunk arrived:
 *  aloocate the first and only slot of memory/resource
 *  when it ended return error.
 */
static int hws_pool_onesize_element_db_init(struct mlx5hws_pool *pool)
{
	pool->p_db_uninit = &hws_onesize_element_db_uninit;
	pool->p_get_chunk = &hws_onesize_element_db_get_chunk;
	pool->p_put_chunk = &hws_onesize_element_db_put_chunk;

	return 0;
}

static int hws_pool_db_init(struct mlx5hws_pool *pool,
			    enum mlx5hws_db_type db_type)
{
	int ret;

	if (db_type == MLX5HWS_POOL_DB_TYPE_GENERAL_SIZE)
		ret = hws_pool_general_element_db_init(pool);
	else if (db_type == MLX5HWS_POOL_DB_TYPE_ONE_SIZE_RESOURCE)
		ret = hws_pool_onesize_element_db_init(pool);
	else
		ret = hws_pool_buddy_db_init(pool, pool->alloc_log_sz);

	if (ret) {
		mlx5hws_err(pool->ctx, "Failed to init general db : %d (ret: %d)\n", db_type, ret);
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
	mutex_unlock(&pool->lock);

	return ret;
}

void mlx5hws_pool_chunk_free(struct mlx5hws_pool *pool,
			     struct mlx5hws_pool_chunk *chunk)
{
	mutex_lock(&pool->lock);
	pool->p_put_chunk(pool, chunk);
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

	/* Support general db */
	if (pool->flags == (MLX5HWS_POOL_FLAGS_RELEASE_FREE_RESOURCE |
			    MLX5HWS_POOL_FLAGS_RESOURCE_PER_CHUNK))
		res_db_type = MLX5HWS_POOL_DB_TYPE_GENERAL_SIZE;
	else if (pool->flags == (MLX5HWS_POOL_FLAGS_ONE_RESOURCE |
				 MLX5HWS_POOL_FLAGS_FIXED_SIZE_OBJECTS))
		res_db_type = MLX5HWS_POOL_DB_TYPE_ONE_SIZE_RESOURCE;
	else
		res_db_type = MLX5HWS_POOL_DB_TYPE_BUDDY;

	pool->alloc_log_sz = pool_attr->alloc_log_sz;

	if (hws_pool_db_init(pool, res_db_type))
		goto free_pool;

	mutex_init(&pool->lock);

	return pool;

free_pool:
	kfree(pool);
	return NULL;
}

int mlx5hws_pool_destroy(struct mlx5hws_pool *pool)
{
	mutex_destroy(&pool->lock);

	if (pool->resource)
		hws_pool_resource_free(pool);

	hws_pool_db_unint(pool);

	kfree(pool);
	return 0;
}
