// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "dr_types.h"

#define DR_ICM_MODIFY_HDR_GRANULARITY_4K 12

/* modify-header arg pool */
enum dr_arg_chunk_size {
	DR_ARG_CHUNK_SIZE_1,
	DR_ARG_CHUNK_SIZE_MIN = DR_ARG_CHUNK_SIZE_1, /* keep updated when changing */
	DR_ARG_CHUNK_SIZE_2,
	DR_ARG_CHUNK_SIZE_3,
	DR_ARG_CHUNK_SIZE_4,
	DR_ARG_CHUNK_SIZE_MAX,
};

/* argument pool area */
struct dr_arg_pool {
	enum dr_arg_chunk_size log_chunk_size;
	struct mlx5dr_domain *dmn;
	struct list_head free_list;
	struct mutex mutex; /* protect arg pool */
};

struct mlx5dr_arg_mgr {
	struct mlx5dr_domain *dmn;
	struct dr_arg_pool *pools[DR_ARG_CHUNK_SIZE_MAX];
};

static int dr_arg_pool_alloc_objs(struct dr_arg_pool *pool)
{
	struct mlx5dr_arg_obj *arg_obj, *tmp_arg;
	struct list_head cur_list;
	u16 object_range;
	int num_of_objects;
	u32 obj_id = 0;
	int i, ret;

	INIT_LIST_HEAD(&cur_list);

	object_range =
		pool->dmn->info.caps.log_header_modify_argument_granularity;

	object_range =
		max_t(u32, pool->dmn->info.caps.log_header_modify_argument_granularity,
		      DR_ICM_MODIFY_HDR_GRANULARITY_4K);
	object_range =
		min_t(u32, pool->dmn->info.caps.log_header_modify_argument_max_alloc,
		      object_range);

	if (pool->log_chunk_size > object_range) {
		mlx5dr_err(pool->dmn, "Required chunk size (%d) is not supported\n",
			   pool->log_chunk_size);
		return -ENOMEM;
	}

	num_of_objects = (1 << (object_range - pool->log_chunk_size));
	/* Only one devx object per range */
	ret = mlx5dr_cmd_create_modify_header_arg(pool->dmn->mdev,
						  object_range,
						  pool->dmn->pdn,
						  &obj_id);
	if (ret) {
		mlx5dr_err(pool->dmn, "failed allocating object with range: %d:\n",
			   object_range);
		return -EAGAIN;
	}

	for (i = 0; i < num_of_objects; i++) {
		arg_obj = kzalloc(sizeof(*arg_obj), GFP_KERNEL);
		if (!arg_obj) {
			ret = -ENOMEM;
			goto clean_arg_obj;
		}

		arg_obj->log_chunk_size = pool->log_chunk_size;

		list_add_tail(&arg_obj->list_node, &cur_list);

		arg_obj->obj_id = obj_id;
		arg_obj->obj_offset = i * (1 << pool->log_chunk_size);
	}
	list_splice_tail_init(&cur_list, &pool->free_list);

	return 0;

clean_arg_obj:
	mlx5dr_cmd_destroy_modify_header_arg(pool->dmn->mdev, obj_id);
	list_for_each_entry_safe(arg_obj, tmp_arg, &cur_list, list_node) {
		list_del(&arg_obj->list_node);
		kfree(arg_obj);
	}
	return ret;
}

static struct mlx5dr_arg_obj *dr_arg_pool_get_arg_obj(struct dr_arg_pool *pool)
{
	struct mlx5dr_arg_obj *arg_obj = NULL;
	int ret;

	mutex_lock(&pool->mutex);
	if (list_empty(&pool->free_list)) {
		ret = dr_arg_pool_alloc_objs(pool);
		if (ret)
			goto out;
	}

	arg_obj = list_first_entry_or_null(&pool->free_list,
					   struct mlx5dr_arg_obj,
					   list_node);
	WARN(!arg_obj, "couldn't get dr arg obj from pool");

	if (arg_obj)
		list_del_init(&arg_obj->list_node);

out:
	mutex_unlock(&pool->mutex);
	return arg_obj;
}

static void dr_arg_pool_put_arg_obj(struct dr_arg_pool *pool,
				    struct mlx5dr_arg_obj *arg_obj)
{
	mutex_lock(&pool->mutex);
	list_add(&arg_obj->list_node, &pool->free_list);
	mutex_unlock(&pool->mutex);
}

static struct dr_arg_pool *dr_arg_pool_create(struct mlx5dr_domain *dmn,
					      enum dr_arg_chunk_size chunk_size)
{
	struct dr_arg_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	pool->dmn = dmn;

	INIT_LIST_HEAD(&pool->free_list);
	mutex_init(&pool->mutex);

	pool->log_chunk_size = chunk_size;
	if (dr_arg_pool_alloc_objs(pool))
		goto free_pool;

	return pool;

free_pool:
	kfree(pool);

	return NULL;
}

static void dr_arg_pool_destroy(struct dr_arg_pool *pool)
{
	struct mlx5dr_arg_obj *arg_obj, *tmp_arg;

	list_for_each_entry_safe(arg_obj, tmp_arg, &pool->free_list, list_node) {
		list_del(&arg_obj->list_node);
		if (!arg_obj->obj_offset) /* the first in range */
			mlx5dr_cmd_destroy_modify_header_arg(pool->dmn->mdev, arg_obj->obj_id);
		kfree(arg_obj);
	}

	mutex_destroy(&pool->mutex);
	kfree(pool);
}

static enum dr_arg_chunk_size dr_arg_get_chunk_size(u16 num_of_actions)
{
	if (num_of_actions <= 8)
		return DR_ARG_CHUNK_SIZE_1;
	if (num_of_actions <= 16)
		return DR_ARG_CHUNK_SIZE_2;
	if (num_of_actions <= 32)
		return DR_ARG_CHUNK_SIZE_3;
	if (num_of_actions <= 64)
		return DR_ARG_CHUNK_SIZE_4;

	return DR_ARG_CHUNK_SIZE_MAX;
}

u32 mlx5dr_arg_get_obj_id(struct mlx5dr_arg_obj *arg_obj)
{
	return (arg_obj->obj_id + arg_obj->obj_offset);
}

struct mlx5dr_arg_obj *mlx5dr_arg_get_obj(struct mlx5dr_arg_mgr *mgr,
					  u16 num_of_actions,
					  u8 *data)
{
	u32 size = dr_arg_get_chunk_size(num_of_actions);
	struct mlx5dr_arg_obj *arg_obj;
	int ret;

	if (size >= DR_ARG_CHUNK_SIZE_MAX)
		return NULL;

	arg_obj = dr_arg_pool_get_arg_obj(mgr->pools[size]);
	if (!arg_obj) {
		mlx5dr_err(mgr->dmn, "Failed allocating args object for modify header\n");
		return NULL;
	}

	/* write it into the hw */
	ret = mlx5dr_send_postsend_args(mgr->dmn,
					mlx5dr_arg_get_obj_id(arg_obj),
					num_of_actions, data);
	if (ret) {
		mlx5dr_err(mgr->dmn, "Failed writing args object\n");
		goto put_obj;
	}

	return arg_obj;

put_obj:
	mlx5dr_arg_put_obj(mgr, arg_obj);
	return NULL;
}

void mlx5dr_arg_put_obj(struct mlx5dr_arg_mgr *mgr,
			struct mlx5dr_arg_obj *arg_obj)
{
	dr_arg_pool_put_arg_obj(mgr->pools[arg_obj->log_chunk_size], arg_obj);
}

struct mlx5dr_arg_mgr*
mlx5dr_arg_mgr_create(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_arg_mgr *pool_mgr;
	int i;

	if (!mlx5dr_domain_is_support_ptrn_arg(dmn))
		return NULL;

	pool_mgr = kzalloc(sizeof(*pool_mgr), GFP_KERNEL);
	if (!pool_mgr)
		return NULL;

	pool_mgr->dmn = dmn;

	for (i = 0; i < DR_ARG_CHUNK_SIZE_MAX; i++) {
		pool_mgr->pools[i] = dr_arg_pool_create(dmn, i);
		if (!pool_mgr->pools[i])
			goto clean_pools;
	}

	return pool_mgr;

clean_pools:
	for (i--; i >= 0; i--)
		dr_arg_pool_destroy(pool_mgr->pools[i]);

	kfree(pool_mgr);
	return NULL;
}

void mlx5dr_arg_mgr_destroy(struct mlx5dr_arg_mgr *mgr)
{
	struct dr_arg_pool **pools;
	int i;

	if (!mgr)
		return;

	pools = mgr->pools;
	for (i = 0; i < DR_ARG_CHUNK_SIZE_MAX; i++)
		dr_arg_pool_destroy(pools[i]);

	kfree(mgr);
}
