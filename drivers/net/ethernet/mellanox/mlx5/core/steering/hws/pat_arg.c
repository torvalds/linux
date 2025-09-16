// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

enum mlx5hws_arg_chunk_size
mlx5hws_arg_data_size_to_arg_log_size(u16 data_size)
{
	/* Return the roundup of log2(data_size) */
	if (data_size <= MLX5HWS_ARG_DATA_SIZE)
		return MLX5HWS_ARG_CHUNK_SIZE_1;
	if (data_size <= MLX5HWS_ARG_DATA_SIZE * 2)
		return MLX5HWS_ARG_CHUNK_SIZE_2;
	if (data_size <= MLX5HWS_ARG_DATA_SIZE * 4)
		return MLX5HWS_ARG_CHUNK_SIZE_3;
	if (data_size <= MLX5HWS_ARG_DATA_SIZE * 8)
		return MLX5HWS_ARG_CHUNK_SIZE_4;

	return MLX5HWS_ARG_CHUNK_SIZE_MAX;
}

u32 mlx5hws_arg_data_size_to_arg_size(u16 data_size)
{
	return BIT(mlx5hws_arg_data_size_to_arg_log_size(data_size));
}

enum mlx5hws_arg_chunk_size
mlx5hws_arg_get_arg_log_size(u16 num_of_actions)
{
	return mlx5hws_arg_data_size_to_arg_log_size(num_of_actions *
						    MLX5HWS_MODIFY_ACTION_SIZE);
}

u32 mlx5hws_arg_get_arg_size(u16 num_of_actions)
{
	return BIT(mlx5hws_arg_get_arg_log_size(num_of_actions));
}

bool mlx5hws_pat_require_reparse(__be64 *actions, u16 num_of_actions)
{
	u16 i, field;
	u8 action_id;

	for (i = 0; i < num_of_actions; i++) {
		action_id = MLX5_GET(set_action_in, &actions[i], action_type);

		switch (action_id) {
		case MLX5_MODIFICATION_TYPE_NOP:
			field = MLX5_MODI_OUT_NONE;
			break;

		case MLX5_MODIFICATION_TYPE_SET:
		case MLX5_MODIFICATION_TYPE_ADD:
			field = MLX5_GET(set_action_in, &actions[i], field);
			break;

		case MLX5_MODIFICATION_TYPE_COPY:
		case MLX5_MODIFICATION_TYPE_ADD_FIELD:
			field = MLX5_GET(copy_action_in, &actions[i], dst_field);
			break;

		default:
			/* Insert/Remove/Unknown actions require reparse */
			return true;
		}

		/* Below fields can change packet structure require a reparse */
		if (field == MLX5_MODI_OUT_ETHERTYPE ||
		    field == MLX5_MODI_OUT_IPV6_NEXT_HDR)
			return true;
	}

	return false;
}

/* Cache and cache element handling */
int mlx5hws_pat_init_pattern_cache(struct mlx5hws_pattern_cache **cache)
{
	struct mlx5hws_pattern_cache *new_cache;

	new_cache = kzalloc(sizeof(*new_cache), GFP_KERNEL);
	if (!new_cache)
		return -ENOMEM;

	INIT_LIST_HEAD(&new_cache->ptrn_list);
	mutex_init(&new_cache->lock);

	*cache = new_cache;

	return 0;
}

void mlx5hws_pat_uninit_pattern_cache(struct mlx5hws_pattern_cache *cache)
{
	mutex_destroy(&cache->lock);
	kfree(cache);
}

static bool mlx5hws_pat_compare_pattern(int cur_num_of_actions,
					__be64 cur_actions[],
					int num_of_actions,
					__be64 actions[])
{
	int i;

	if (cur_num_of_actions != num_of_actions)
		return false;

	for (i = 0; i < num_of_actions; i++) {
		u8 action_id =
			MLX5_GET(set_action_in, &actions[i], action_type);

		if (action_id == MLX5_MODIFICATION_TYPE_COPY ||
		    action_id == MLX5_MODIFICATION_TYPE_ADD_FIELD) {
			if (actions[i] != cur_actions[i])
				return false;
		} else {
			/* Compare just the control, not the values */
			if ((__force __be32)actions[i] !=
			    (__force __be32)cur_actions[i])
				return false;
		}
	}

	return true;
}

static struct mlx5hws_pattern_cache_item *
mlx5hws_pat_find_cached_pattern(struct mlx5hws_pattern_cache *cache,
				u16 num_of_actions,
				__be64 *actions)
{
	struct mlx5hws_pattern_cache_item *cached_pat = NULL;

	list_for_each_entry(cached_pat, &cache->ptrn_list, ptrn_list_node) {
		if (mlx5hws_pat_compare_pattern(cached_pat->mh_data.num_of_actions,
						(__be64 *)cached_pat->mh_data.data,
						num_of_actions,
						actions))
			return cached_pat;
	}

	return NULL;
}

static struct mlx5hws_pattern_cache_item *
mlx5hws_pat_get_existing_cached_pattern(struct mlx5hws_pattern_cache *cache,
					u16 num_of_actions,
					__be64 *actions)
{
	struct mlx5hws_pattern_cache_item *cached_pattern;

	cached_pattern = mlx5hws_pat_find_cached_pattern(cache, num_of_actions, actions);
	if (cached_pattern) {
		/* LRU: move it to be first in the list */
		list_move(&cached_pattern->ptrn_list_node, &cache->ptrn_list);
		cached_pattern->refcount++;
	}

	return cached_pattern;
}

static struct mlx5hws_pattern_cache_item *
mlx5hws_pat_add_pattern_to_cache(struct mlx5hws_pattern_cache *cache,
				 u32 pattern_id,
				 u16 num_of_actions,
				 __be64 *actions)
{
	struct mlx5hws_pattern_cache_item *cached_pattern;

	cached_pattern = kzalloc(sizeof(*cached_pattern), GFP_KERNEL);
	if (!cached_pattern)
		return NULL;

	cached_pattern->mh_data.num_of_actions = num_of_actions;
	cached_pattern->mh_data.pattern_id = pattern_id;
	cached_pattern->mh_data.data =
		kmemdup(actions, num_of_actions * MLX5HWS_MODIFY_ACTION_SIZE, GFP_KERNEL);
	if (!cached_pattern->mh_data.data)
		goto free_cached_obj;

	list_add(&cached_pattern->ptrn_list_node, &cache->ptrn_list);
	cached_pattern->refcount = 1;

	return cached_pattern;

free_cached_obj:
	kfree(cached_pattern);
	return NULL;
}

static struct mlx5hws_pattern_cache_item *
mlx5hws_pat_find_cached_pattern_by_id(struct mlx5hws_pattern_cache *cache,
				      u32 ptrn_id)
{
	struct mlx5hws_pattern_cache_item *cached_pattern = NULL;

	list_for_each_entry(cached_pattern, &cache->ptrn_list, ptrn_list_node) {
		if (cached_pattern->mh_data.pattern_id == ptrn_id)
			return cached_pattern;
	}

	return NULL;
}

static void
mlx5hws_pat_remove_pattern(struct mlx5hws_pattern_cache_item *cached_pattern)
{
	list_del_init(&cached_pattern->ptrn_list_node);

	kfree(cached_pattern->mh_data.data);
	kfree(cached_pattern);
}

void mlx5hws_pat_put_pattern(struct mlx5hws_context *ctx, u32 ptrn_id)
{
	struct mlx5hws_pattern_cache *cache = ctx->pattern_cache;
	struct mlx5hws_pattern_cache_item *cached_pattern;

	mutex_lock(&cache->lock);
	cached_pattern = mlx5hws_pat_find_cached_pattern_by_id(cache, ptrn_id);
	if (!cached_pattern) {
		mlx5hws_err(ctx, "Failed to find cached pattern with provided ID\n");
		pr_warn("HWS: pattern ID %d is not found\n", ptrn_id);
		goto out;
	}

	if (--cached_pattern->refcount)
		goto out;

	mlx5hws_pat_remove_pattern(cached_pattern);
	mlx5hws_cmd_header_modify_pattern_destroy(ctx->mdev, ptrn_id);

out:
	mutex_unlock(&cache->lock);
}

int mlx5hws_pat_get_pattern(struct mlx5hws_context *ctx,
			    __be64 *pattern, size_t pattern_sz,
			    u32 *pattern_id)
{
	u16 num_of_actions = pattern_sz / MLX5HWS_MODIFY_ACTION_SIZE;
	struct mlx5hws_pattern_cache_item *cached_pattern;
	u32 ptrn_id = 0;
	int ret = 0;

	mutex_lock(&ctx->pattern_cache->lock);

	cached_pattern = mlx5hws_pat_get_existing_cached_pattern(ctx->pattern_cache,
								 num_of_actions,
								 pattern);
	if (cached_pattern) {
		*pattern_id = cached_pattern->mh_data.pattern_id;
		goto out_unlock;
	}

	ret = mlx5hws_cmd_header_modify_pattern_create(ctx->mdev,
						       pattern_sz,
						       (u8 *)pattern,
						       &ptrn_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create pattern FW object\n");
		goto out_unlock;
	}

	cached_pattern = mlx5hws_pat_add_pattern_to_cache(ctx->pattern_cache,
							  ptrn_id,
							  num_of_actions,
							  pattern);
	if (!cached_pattern) {
		mlx5hws_err(ctx, "Failed to add pattern to cache\n");
		ret = -EINVAL;
		goto clean_pattern;
	}

	mutex_unlock(&ctx->pattern_cache->lock);
	*pattern_id = ptrn_id;

	return ret;

clean_pattern:
	mlx5hws_cmd_header_modify_pattern_destroy(ctx->mdev, ptrn_id);
out_unlock:
	mutex_unlock(&ctx->pattern_cache->lock);
	return ret;
}

static void
mlx5d_arg_init_send_attr(struct mlx5hws_send_engine_post_attr *send_attr,
			 void *comp_data,
			 u32 arg_idx)
{
	send_attr->opcode = MLX5HWS_WQE_OPCODE_TBL_ACCESS;
	send_attr->opmod = MLX5HWS_WQE_GTA_OPMOD_MOD_ARG;
	send_attr->len = MLX5HWS_WQE_SZ_GTA_CTRL + MLX5HWS_WQE_SZ_GTA_DATA;
	send_attr->id = arg_idx;
	send_attr->user_data = comp_data;
}

void mlx5hws_arg_decapl3_write(struct mlx5hws_send_engine *queue,
			       u32 arg_idx,
			       u8 *arg_data,
			       u16 num_of_actions)
{
	struct mlx5hws_send_engine_post_attr send_attr = {0};
	struct mlx5hws_wqe_gta_data_seg_arg *wqe_arg = NULL;
	struct mlx5hws_wqe_gta_ctrl_seg *wqe_ctrl = NULL;
	struct mlx5hws_send_engine_post_ctrl ctrl;
	size_t wqe_len;

	mlx5d_arg_init_send_attr(&send_attr, NULL, arg_idx);

	ctrl = mlx5hws_send_engine_post_start(queue);
	mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_ctrl, &wqe_len);
	memset(wqe_ctrl, 0, wqe_len);
	mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_arg, &wqe_len);
	mlx5hws_action_prepare_decap_l3_data(arg_data, (u8 *)wqe_arg,
					     num_of_actions);
	mlx5hws_send_engine_post_end(&ctrl, &send_attr);
}

void mlx5hws_arg_write(struct mlx5hws_send_engine *queue,
		       void *comp_data,
		       u32 arg_idx,
		       u8 *arg_data,
		       size_t data_size)
{
	struct mlx5hws_send_engine_post_attr send_attr = {0};
	struct mlx5hws_wqe_gta_data_seg_arg *wqe_arg;
	struct mlx5hws_send_engine_post_ctrl ctrl;
	struct mlx5hws_wqe_gta_ctrl_seg *wqe_ctrl;
	int i, full_iter, leftover;
	size_t wqe_len;

	mlx5d_arg_init_send_attr(&send_attr, comp_data, arg_idx);

	/* Each WQE can hold 64B of data, it might require multiple iteration */
	full_iter = data_size / MLX5HWS_ARG_DATA_SIZE;
	leftover = data_size & (MLX5HWS_ARG_DATA_SIZE - 1);

	for (i = 0; i < full_iter; i++) {
		ctrl = mlx5hws_send_engine_post_start(queue);
		mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_ctrl, &wqe_len);
		memset(wqe_ctrl, 0, wqe_len);
		mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_arg, &wqe_len);
		memcpy(wqe_arg, arg_data, MLX5HWS_ARG_DATA_SIZE);
		send_attr.id = arg_idx++;
		mlx5hws_send_engine_post_end(&ctrl, &send_attr);

		/* Move to next argument data */
		arg_data += MLX5HWS_ARG_DATA_SIZE;
	}

	if (leftover) {
		ctrl = mlx5hws_send_engine_post_start(queue);
		mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_ctrl, &wqe_len);
		memset(wqe_ctrl, 0, wqe_len);
		mlx5hws_send_engine_post_req_wqe(&ctrl, (void *)&wqe_arg, &wqe_len);
		memcpy(wqe_arg, arg_data, leftover);
		send_attr.id = arg_idx;
		mlx5hws_send_engine_post_end(&ctrl, &send_attr);
	}
}

int mlx5hws_arg_write_inline_arg_data(struct mlx5hws_context *ctx,
				      u32 arg_idx,
				      u8 *arg_data,
				      size_t data_size)
{
	struct mlx5hws_send_engine *queue;
	int ret;

	mutex_lock(&ctx->ctrl_lock);

	/* Get the control queue */
	queue = &ctx->send_queue[ctx->queues - 1];

	mlx5hws_arg_write(queue, arg_data, arg_idx, arg_data, data_size);

	mlx5hws_send_engine_flush_queue(queue);

	/* Poll for completion */
	ret = mlx5hws_send_queue_action(ctx, ctx->queues - 1,
					MLX5HWS_SEND_QUEUE_ACTION_DRAIN_SYNC);

	if (ret)
		mlx5hws_err(ctx, "Failed to drain arg queue\n");

	mutex_unlock(&ctx->ctrl_lock);

	return ret;
}

bool mlx5hws_arg_is_valid_arg_request_size(struct mlx5hws_context *ctx,
					   u32 arg_size)
{
	if (arg_size < ctx->caps->log_header_modify_argument_granularity ||
	    arg_size > ctx->caps->log_header_modify_argument_max_alloc) {
		return false;
	}
	return true;
}

int mlx5hws_arg_create(struct mlx5hws_context *ctx,
		       u8 *data,
		       size_t data_sz,
		       u32 log_bulk_sz,
		       bool write_data,
		       u32 *arg_id)
{
	u16 single_arg_log_sz;
	u16 multi_arg_log_sz;
	int ret;
	u32 id;

	single_arg_log_sz = mlx5hws_arg_data_size_to_arg_log_size(data_sz);
	multi_arg_log_sz = single_arg_log_sz + log_bulk_sz;

	if (single_arg_log_sz >= MLX5HWS_ARG_CHUNK_SIZE_MAX) {
		mlx5hws_err(ctx, "Requested single arg %u not supported\n", single_arg_log_sz);
		return -EOPNOTSUPP;
	}

	if (!mlx5hws_arg_is_valid_arg_request_size(ctx, multi_arg_log_sz)) {
		mlx5hws_err(ctx, "Argument log size %d not supported by FW\n", multi_arg_log_sz);
		return -EOPNOTSUPP;
	}

	/* Alloc bulk of args */
	ret = mlx5hws_cmd_arg_create(ctx->mdev, multi_arg_log_sz, ctx->pd_num, &id);
	if (ret) {
		mlx5hws_err(ctx, "Failed allocating arg in order: %d\n", multi_arg_log_sz);
		return ret;
	}

	if (write_data) {
		ret = mlx5hws_arg_write_inline_arg_data(ctx, id,
							data, data_sz);
		if (ret) {
			mlx5hws_err(ctx, "Failed writing arg data\n");
			mlx5hws_cmd_arg_destroy(ctx->mdev, id);
			return ret;
		}
	}

	*arg_id = id;
	return ret;
}

void mlx5hws_arg_destroy(struct mlx5hws_context *ctx, u32 arg_id)
{
	mlx5hws_cmd_arg_destroy(ctx->mdev, arg_id);
}

int mlx5hws_arg_create_modify_header_arg(struct mlx5hws_context *ctx,
					 __be64 *data,
					 u8 num_of_actions,
					 u32 log_bulk_sz,
					 bool write_data,
					 u32 *arg_id)
{
	size_t data_sz = num_of_actions * MLX5HWS_MODIFY_ACTION_SIZE;
	int ret;

	ret = mlx5hws_arg_create(ctx,
				 (u8 *)data,
				 data_sz,
				 log_bulk_sz,
				 write_data,
				 arg_id);
	if (ret)
		mlx5hws_err(ctx, "Failed creating modify header arg\n");

	return ret;
}

static int
hws_action_modify_check_field_limitation(u8 action_type, __be64 *pattern)
{
	/* Need to check field limitation here, but for now - return OK */
	return 0;
}

#define INVALID_FIELD 0xffff

static void
hws_action_modify_get_target_fields(u8 action_type, __be64 *pattern,
				    u16 *src_field, u16 *dst_field)
{
	switch (action_type) {
	case MLX5_ACTION_TYPE_SET:
	case MLX5_ACTION_TYPE_ADD:
		*src_field = INVALID_FIELD;
		*dst_field = MLX5_GET(set_action_in, pattern, field);
		break;
	case MLX5_ACTION_TYPE_COPY:
		*src_field = MLX5_GET(copy_action_in, pattern, src_field);
		*dst_field = MLX5_GET(copy_action_in, pattern, dst_field);
		break;
	default:
		pr_warn("HWS: invalid modify header action type %d\n", action_type);
	}
}

bool mlx5hws_pat_verify_actions(struct mlx5hws_context *ctx, __be64 pattern[], size_t sz)
{
	size_t i;

	for (i = 0; i < sz / MLX5HWS_MODIFY_ACTION_SIZE; i++) {
		u8 action_type =
			MLX5_GET(set_action_in, &pattern[i], action_type);
		if (action_type >= MLX5_MODIFICATION_TYPE_MAX) {
			mlx5hws_err(ctx, "Unsupported action id %d\n", action_type);
			return false;
		}
		if (hws_action_modify_check_field_limitation(action_type, &pattern[i])) {
			mlx5hws_err(ctx, "Unsupported action number %zu\n", i);
			return false;
		}
	}

	return true;
}

int mlx5hws_pat_calc_nop(__be64 *pattern, size_t num_actions,
			 size_t max_actions, size_t *new_size,
			 u32 *nop_locations, __be64 *new_pat)
{
	u16 prev_src_field = INVALID_FIELD, prev_dst_field = INVALID_FIELD;
	u8 action_type;
	bool dependent;
	size_t i, j;

	*new_size = num_actions;
	*nop_locations = 0;

	if (num_actions == 1)
		return 0;

	for (i = 0, j = 0; i < num_actions; i++, j++) {
		u16 src_field = INVALID_FIELD;
		u16 dst_field = INVALID_FIELD;

		if (j >= max_actions)
			return -EINVAL;

		action_type = MLX5_GET(set_action_in, &pattern[i], action_type);
		hws_action_modify_get_target_fields(action_type, &pattern[i],
						    &src_field, &dst_field);

		/* For every action, look at it and the previous one. The two
		 * actions are dependent if:
		 */
		dependent =
			(i > 0) &&
			/* At least one of the actions is a write and */
			(dst_field != INVALID_FIELD ||
			 prev_dst_field != INVALID_FIELD) &&
			/* One reads from the other's source */
			(dst_field == prev_src_field ||
			 src_field == prev_dst_field ||
			 /* Or both write to the same destination */
			 dst_field == prev_dst_field);

		if (dependent) {
			*new_size += 1;
			*nop_locations |= BIT(i);
			memset(&new_pat[j], 0, MLX5HWS_MODIFY_ACTION_SIZE);
			MLX5_SET(set_action_in, &new_pat[j], action_type,
				 MLX5_MODIFICATION_TYPE_NOP);
			j++;
			if (j >= max_actions)
				return -EINVAL;
		}

		memcpy(&new_pat[j], &pattern[i], MLX5HWS_MODIFY_ACTION_SIZE);
		prev_src_field = src_field;
		prev_dst_field = dst_field;
	}

	return 0;
}
