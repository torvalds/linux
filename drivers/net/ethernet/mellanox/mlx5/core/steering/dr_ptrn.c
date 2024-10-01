// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "dr_types.h"
#include "mlx5_ifc_dr_ste_v1.h"

enum dr_ptrn_modify_hdr_action_id {
	DR_PTRN_MODIFY_HDR_ACTION_ID_NOP = 0x00,
	DR_PTRN_MODIFY_HDR_ACTION_ID_COPY = 0x05,
	DR_PTRN_MODIFY_HDR_ACTION_ID_SET = 0x06,
	DR_PTRN_MODIFY_HDR_ACTION_ID_ADD = 0x07,
	DR_PTRN_MODIFY_HDR_ACTION_ID_INSERT_INLINE = 0x0a,
};

struct mlx5dr_ptrn_mgr {
	struct mlx5dr_domain *dmn;
	struct mlx5dr_icm_pool *ptrn_icm_pool;
	/* cache for modify_header ptrn */
	struct list_head ptrn_list;
	struct mutex modify_hdr_mutex; /* protect the pattern cache */
};

/* Cache structure and functions */
static bool dr_ptrn_compare_modify_hdr(size_t cur_num_of_actions,
				       __be64 cur_hw_actions[],
				       size_t num_of_actions,
				       __be64 hw_actions[])
{
	int i;

	if (cur_num_of_actions != num_of_actions)
		return false;

	for (i = 0; i < num_of_actions; i++) {
		u8 action_id =
			MLX5_GET(ste_double_action_set_v1, &hw_actions[i], action_id);

		if (action_id == DR_PTRN_MODIFY_HDR_ACTION_ID_COPY) {
			if (hw_actions[i] != cur_hw_actions[i])
				return false;
		} else {
			if ((__force __be32)hw_actions[i] !=
			    (__force __be32)cur_hw_actions[i])
				return false;
		}
	}

	return true;
}

static struct mlx5dr_ptrn_obj *
dr_ptrn_find_cached_pattern(struct mlx5dr_ptrn_mgr *mgr,
			    size_t num_of_actions,
			    __be64 hw_actions[])
{
	struct mlx5dr_ptrn_obj *cached_pattern;
	struct mlx5dr_ptrn_obj *tmp;

	list_for_each_entry_safe(cached_pattern, tmp, &mgr->ptrn_list, list) {
		if (dr_ptrn_compare_modify_hdr(cached_pattern->num_of_actions,
					       (__be64 *)cached_pattern->data,
					       num_of_actions,
					       hw_actions)) {
			/* Put this pattern in the head of the list,
			 * as we will probably use it more.
			 */
			list_del_init(&cached_pattern->list);
			list_add(&cached_pattern->list, &mgr->ptrn_list);
			return cached_pattern;
		}
	}

	return NULL;
}

static struct mlx5dr_ptrn_obj *
dr_ptrn_alloc_pattern(struct mlx5dr_ptrn_mgr *mgr,
		      u16 num_of_actions, u8 *data)
{
	struct mlx5dr_ptrn_obj *pattern;
	struct mlx5dr_icm_chunk *chunk;
	u32 chunk_size;
	u32 index;

	chunk_size = ilog2(roundup_pow_of_two(num_of_actions));
	/* HW modify action index granularity is at least 64B */
	chunk_size = max_t(u32, chunk_size, DR_CHUNK_SIZE_8);

	chunk = mlx5dr_icm_alloc_chunk(mgr->ptrn_icm_pool, chunk_size);
	if (!chunk)
		return NULL;

	index = (mlx5dr_icm_pool_get_chunk_icm_addr(chunk) -
		 mgr->dmn->info.caps.hdr_modify_pattern_icm_addr) /
		DR_ACTION_CACHE_LINE_SIZE;

	pattern = kzalloc(sizeof(*pattern), GFP_KERNEL);
	if (!pattern)
		goto free_chunk;

	pattern->data = kzalloc(num_of_actions * DR_MODIFY_ACTION_SIZE *
				sizeof(*pattern->data), GFP_KERNEL);
	if (!pattern->data)
		goto free_pattern;

	memcpy(pattern->data, data, num_of_actions * DR_MODIFY_ACTION_SIZE);
	pattern->chunk = chunk;
	pattern->index = index;
	pattern->num_of_actions = num_of_actions;

	list_add(&pattern->list, &mgr->ptrn_list);
	refcount_set(&pattern->refcount, 1);

	return pattern;

free_pattern:
	kfree(pattern);
free_chunk:
	mlx5dr_icm_free_chunk(chunk);
	return NULL;
}

static void
dr_ptrn_free_pattern(struct mlx5dr_ptrn_obj *pattern)
{
	list_del(&pattern->list);
	mlx5dr_icm_free_chunk(pattern->chunk);
	kfree(pattern->data);
	kfree(pattern);
}

struct mlx5dr_ptrn_obj *
mlx5dr_ptrn_cache_get_pattern(struct mlx5dr_ptrn_mgr *mgr,
			      u16 num_of_actions,
			      u8 *data)
{
	struct mlx5dr_ptrn_obj *pattern;
	u64 *hw_actions;
	u8 action_id;
	int i;

	mutex_lock(&mgr->modify_hdr_mutex);
	pattern = dr_ptrn_find_cached_pattern(mgr,
					      num_of_actions,
					      (__be64 *)data);
	if (!pattern) {
		/* Alloc and add new pattern to cache */
		pattern = dr_ptrn_alloc_pattern(mgr, num_of_actions, data);
		if (!pattern)
			goto out_unlock;

		hw_actions = (u64 *)pattern->data;
		/* Here we mask the pattern data to create a valid pattern
		 * since we do an OR operation between the arg and pattern
		 */
		for (i = 0; i < num_of_actions; i++) {
			action_id = MLX5_GET(ste_double_action_set_v1, &hw_actions[i], action_id);

			if (action_id == DR_PTRN_MODIFY_HDR_ACTION_ID_SET ||
			    action_id == DR_PTRN_MODIFY_HDR_ACTION_ID_ADD ||
			    action_id == DR_PTRN_MODIFY_HDR_ACTION_ID_INSERT_INLINE)
				MLX5_SET(ste_double_action_set_v1, &hw_actions[i], inline_data, 0);
		}

		if (mlx5dr_send_postsend_pattern(mgr->dmn, pattern->chunk,
						 num_of_actions, pattern->data)) {
			refcount_dec(&pattern->refcount);
			goto free_pattern;
		}
	} else {
		refcount_inc(&pattern->refcount);
	}

	mutex_unlock(&mgr->modify_hdr_mutex);

	return pattern;

free_pattern:
	dr_ptrn_free_pattern(pattern);
out_unlock:
	mutex_unlock(&mgr->modify_hdr_mutex);
	return NULL;
}

void
mlx5dr_ptrn_cache_put_pattern(struct mlx5dr_ptrn_mgr *mgr,
			      struct mlx5dr_ptrn_obj *pattern)
{
	mutex_lock(&mgr->modify_hdr_mutex);

	if (refcount_dec_and_test(&pattern->refcount))
		dr_ptrn_free_pattern(pattern);

	mutex_unlock(&mgr->modify_hdr_mutex);
}

struct mlx5dr_ptrn_mgr *mlx5dr_ptrn_mgr_create(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_ptrn_mgr *mgr;

	if (!mlx5dr_domain_is_support_ptrn_arg(dmn))
		return NULL;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	mgr->dmn = dmn;
	mgr->ptrn_icm_pool = mlx5dr_icm_pool_create(dmn, DR_ICM_TYPE_MODIFY_HDR_PTRN);
	if (!mgr->ptrn_icm_pool) {
		mlx5dr_err(dmn, "Couldn't get modify-header-pattern memory\n");
		goto free_mgr;
	}

	INIT_LIST_HEAD(&mgr->ptrn_list);
	mutex_init(&mgr->modify_hdr_mutex);

	return mgr;

free_mgr:
	kfree(mgr);
	return NULL;
}

void mlx5dr_ptrn_mgr_destroy(struct mlx5dr_ptrn_mgr *mgr)
{
	struct mlx5dr_ptrn_obj *pattern;
	struct mlx5dr_ptrn_obj *tmp;

	if (!mgr)
		return;

	WARN_ON(!list_empty(&mgr->ptrn_list));

	list_for_each_entry_safe(pattern, tmp, &mgr->ptrn_list, list) {
		list_del(&pattern->list);
		kfree(pattern->data);
		kfree(pattern);
	}

	mlx5dr_icm_pool_destroy(mgr->ptrn_icm_pool);
	mutex_destroy(&mgr->modify_hdr_mutex);
	kfree(mgr);
}
