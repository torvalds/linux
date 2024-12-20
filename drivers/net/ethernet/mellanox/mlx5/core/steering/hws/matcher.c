// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

enum mlx5hws_matcher_rtc_type {
	HWS_MATCHER_RTC_TYPE_MATCH,
	HWS_MATCHER_RTC_TYPE_STE_ARRAY,
	HWS_MATCHER_RTC_TYPE_MAX,
};

static const char * const mlx5hws_matcher_rtc_type_str[] = {
	[HWS_MATCHER_RTC_TYPE_MATCH] = "MATCH",
	[HWS_MATCHER_RTC_TYPE_STE_ARRAY] = "STE_ARRAY",
	[HWS_MATCHER_RTC_TYPE_MAX] = "UNKNOWN",
};

static const char *hws_matcher_rtc_type_to_str(enum mlx5hws_matcher_rtc_type rtc_type)
{
	if (rtc_type > HWS_MATCHER_RTC_TYPE_MAX)
		rtc_type = HWS_MATCHER_RTC_TYPE_MAX;
	return mlx5hws_matcher_rtc_type_str[rtc_type];
}

static bool hws_matcher_requires_col_tbl(u8 log_num_of_rules)
{
	/* Collision table concatenation is done only for large rule tables */
	return log_num_of_rules > MLX5HWS_MATCHER_ASSURED_RULES_TH;
}

static u8 hws_matcher_rules_to_tbl_depth(u8 log_num_of_rules)
{
	if (hws_matcher_requires_col_tbl(log_num_of_rules))
		return MLX5HWS_MATCHER_ASSURED_MAIN_TBL_DEPTH;

	/* For small rule tables we use a single deep table to assure insertion */
	return min(log_num_of_rules, MLX5HWS_MATCHER_ASSURED_COL_TBL_DEPTH);
}

static void hws_matcher_destroy_end_ft(struct mlx5hws_matcher *matcher)
{
	mlx5hws_table_destroy_default_ft(matcher->tbl, matcher->end_ft_id);
}

static int hws_matcher_create_end_ft(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	int ret;

	ret = mlx5hws_table_create_default_ft(tbl->ctx->mdev, tbl, &matcher->end_ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Failed to create matcher end flow table\n");
		return ret;
	}
	return 0;
}

static int hws_matcher_connect(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_context *ctx = tbl->ctx;
	struct mlx5hws_matcher *prev = NULL;
	struct mlx5hws_matcher *next = NULL;
	struct mlx5hws_matcher *tmp_matcher;
	int ret;

	/* Find location in matcher list */
	if (list_empty(&tbl->matchers_list)) {
		list_add(&matcher->list_node, &tbl->matchers_list);
		goto connect;
	}

	list_for_each_entry(tmp_matcher, &tbl->matchers_list, list_node) {
		if (tmp_matcher->attr.priority > matcher->attr.priority) {
			next = tmp_matcher;
			break;
		}
		prev = tmp_matcher;
	}

	if (next)
		/* insert before next */
		list_add_tail(&matcher->list_node, &next->list_node);
	else
		/* insert after prev */
		list_add(&matcher->list_node, &prev->list_node);

connect:
	if (next) {
		/* Connect to next RTC */
		ret = mlx5hws_table_ft_set_next_rtc(ctx,
						    matcher->end_ft_id,
						    tbl->fw_ft_type,
						    next->match_ste.rtc_0_id,
						    next->match_ste.rtc_1_id);
		if (ret) {
			mlx5hws_err(ctx, "Failed to connect new matcher to next RTC\n");
			goto remove_from_list;
		}
	} else {
		/* Connect last matcher to next miss_tbl if exists */
		ret = mlx5hws_table_connect_to_miss_table(tbl, tbl->default_miss.miss_tbl);
		if (ret) {
			mlx5hws_err(ctx, "Failed connect new matcher to miss_tbl\n");
			goto remove_from_list;
		}
	}

	/* Connect to previous FT */
	ret = mlx5hws_table_ft_set_next_rtc(ctx,
					    prev ? prev->end_ft_id : tbl->ft_id,
					    tbl->fw_ft_type,
					    matcher->match_ste.rtc_0_id,
					    matcher->match_ste.rtc_1_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to connect new matcher to previous FT\n");
		goto remove_from_list;
	}

	/* Reset prev matcher FT default miss (drop refcount) */
	ret = mlx5hws_table_ft_set_default_next_ft(tbl, prev ? prev->end_ft_id : tbl->ft_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to reset matcher ft default miss\n");
		goto remove_from_list;
	}

	if (!prev) {
		/* Update tables missing to current matcher in the table */
		ret = mlx5hws_table_update_connected_miss_tables(tbl);
		if (ret) {
			mlx5hws_err(ctx, "Fatal error, failed to update connected miss table\n");
			goto remove_from_list;
		}
	}

	return 0;

remove_from_list:
	list_del_init(&matcher->list_node);
	return ret;
}

static int hws_matcher_disconnect(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher *next = NULL, *prev = NULL;
	struct mlx5hws_table *tbl = matcher->tbl;
	u32 prev_ft_id = tbl->ft_id;
	int ret;

	if (!list_is_first(&matcher->list_node, &tbl->matchers_list)) {
		prev = list_prev_entry(matcher, list_node);
		prev_ft_id = prev->end_ft_id;
	}

	if (!list_is_last(&matcher->list_node, &tbl->matchers_list))
		next = list_next_entry(matcher, list_node);

	list_del_init(&matcher->list_node);

	if (next) {
		/* Connect previous end FT to next RTC */
		ret = mlx5hws_table_ft_set_next_rtc(tbl->ctx,
						    prev_ft_id,
						    tbl->fw_ft_type,
						    next->match_ste.rtc_0_id,
						    next->match_ste.rtc_1_id);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Failed to disconnect matcher\n");
			goto matcher_reconnect;
		}
	} else {
		ret = mlx5hws_table_connect_to_miss_table(tbl, tbl->default_miss.miss_tbl);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Failed to disconnect last matcher\n");
			goto matcher_reconnect;
		}
	}

	/* Removing first matcher, update connected miss tables if exists */
	if (prev_ft_id == tbl->ft_id) {
		ret = mlx5hws_table_update_connected_miss_tables(tbl);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Fatal error, failed to update connected miss table\n");
			goto matcher_reconnect;
		}
	}

	ret = mlx5hws_table_ft_set_default_next_ft(tbl, prev_ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Fatal error, failed to restore matcher ft default miss\n");
		goto matcher_reconnect;
	}

	return 0;

matcher_reconnect:
	if (list_empty(&tbl->matchers_list) || !prev)
		list_add(&matcher->list_node, &tbl->matchers_list);
	else
		/* insert after prev matcher */
		list_add(&matcher->list_node, &prev->list_node);

	return ret;
}

static void hws_matcher_set_rtc_attr_sz(struct mlx5hws_matcher *matcher,
					struct mlx5hws_cmd_rtc_create_attr *rtc_attr,
					enum mlx5hws_matcher_rtc_type rtc_type,
					bool is_mirror)
{
	struct mlx5hws_pool_chunk *ste = &matcher->action_ste[MLX5HWS_ACTION_STE_IDX_ANY].ste;
	enum mlx5hws_matcher_flow_src flow_src = matcher->attr.optimize_flow_src;
	bool is_match_rtc = rtc_type == HWS_MATCHER_RTC_TYPE_MATCH;

	if ((flow_src == MLX5HWS_MATCHER_FLOW_SRC_VPORT && !is_mirror) ||
	    (flow_src == MLX5HWS_MATCHER_FLOW_SRC_WIRE && is_mirror)) {
		/* Optimize FDB RTC */
		rtc_attr->log_size = 0;
		rtc_attr->log_depth = 0;
	} else {
		/* Keep original values */
		rtc_attr->log_size = is_match_rtc ? matcher->attr.table.sz_row_log : ste->order;
		rtc_attr->log_depth = is_match_rtc ? matcher->attr.table.sz_col_log : 0;
	}
}

static int hws_matcher_create_rtc(struct mlx5hws_matcher *matcher,
				  enum mlx5hws_matcher_rtc_type rtc_type,
				  u8 action_ste_selector)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;
	struct mlx5hws_cmd_rtc_create_attr rtc_attr = {0};
	struct mlx5hws_match_template *mt = matcher->mt;
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	struct mlx5hws_action_default_stc *default_stc;
	struct mlx5hws_matcher_action_ste *action_ste;
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_pool *ste_pool, *stc_pool;
	struct mlx5hws_pool_chunk *ste;
	u32 *rtc_0_id, *rtc_1_id;
	u32 obj_id;
	int ret;

	switch (rtc_type) {
	case HWS_MATCHER_RTC_TYPE_MATCH:
		rtc_0_id = &matcher->match_ste.rtc_0_id;
		rtc_1_id = &matcher->match_ste.rtc_1_id;
		ste_pool = matcher->match_ste.pool;
		ste = &matcher->match_ste.ste;
		ste->order = attr->table.sz_col_log + attr->table.sz_row_log;

		rtc_attr.log_size = attr->table.sz_row_log;
		rtc_attr.log_depth = attr->table.sz_col_log;
		rtc_attr.is_frst_jumbo = mlx5hws_matcher_mt_is_jumbo(mt);
		rtc_attr.is_scnd_range = 0;
		rtc_attr.miss_ft_id = matcher->end_ft_id;

		if (attr->insert_mode == MLX5HWS_MATCHER_INSERT_BY_HASH) {
			/* The usual Hash Table */
			rtc_attr.update_index_mode = MLX5_IFC_RTC_STE_UPDATE_MODE_BY_HASH;

			/* The first mt is used since all share the same definer */
			rtc_attr.match_definer_0 = mlx5hws_definer_get_id(mt->definer);
		} else if (attr->insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX) {
			rtc_attr.update_index_mode = MLX5_IFC_RTC_STE_UPDATE_MODE_BY_OFFSET;
			rtc_attr.num_hash_definer = 1;

			if (attr->distribute_mode == MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH) {
				/* Hash Split Table */
				rtc_attr.access_index_mode = MLX5_IFC_RTC_STE_ACCESS_MODE_BY_HASH;
				rtc_attr.match_definer_0 = mlx5hws_definer_get_id(mt->definer);
			} else if (attr->distribute_mode == MLX5HWS_MATCHER_DISTRIBUTE_BY_LINEAR) {
				/* Linear Lookup Table */
				rtc_attr.access_index_mode = MLX5_IFC_RTC_STE_ACCESS_MODE_LINEAR;
				rtc_attr.match_definer_0 = ctx->caps->linear_match_definer;
			}
		}

		/* Match pool requires implicit allocation */
		ret = mlx5hws_pool_chunk_alloc(ste_pool, ste);
		if (ret) {
			mlx5hws_err(ctx, "Failed to allocate STE for %s RTC",
				    hws_matcher_rtc_type_to_str(rtc_type));
			return ret;
		}
		break;

	case HWS_MATCHER_RTC_TYPE_STE_ARRAY:
		action_ste = &matcher->action_ste[action_ste_selector];

		rtc_0_id = &action_ste->rtc_0_id;
		rtc_1_id = &action_ste->rtc_1_id;
		ste_pool = action_ste->pool;
		ste = &action_ste->ste;
		ste->order = ilog2(roundup_pow_of_two(action_ste->max_stes)) +
			     attr->table.sz_row_log;
		rtc_attr.log_size = ste->order;
		rtc_attr.log_depth = 0;
		rtc_attr.update_index_mode = MLX5_IFC_RTC_STE_UPDATE_MODE_BY_OFFSET;
		/* The action STEs use the default always hit definer */
		rtc_attr.match_definer_0 = ctx->caps->trivial_match_definer;
		rtc_attr.is_frst_jumbo = false;
		rtc_attr.miss_ft_id = 0;
		break;

	default:
		mlx5hws_err(ctx, "HWS Invalid RTC type\n");
		return -EINVAL;
	}

	obj_id = mlx5hws_pool_chunk_get_base_id(ste_pool, ste);

	rtc_attr.pd = ctx->pd_num;
	rtc_attr.ste_base = obj_id;
	rtc_attr.ste_offset = ste->offset;
	rtc_attr.reparse_mode = mlx5hws_context_get_reparse_mode(ctx);
	rtc_attr.table_type = mlx5hws_table_get_res_fw_ft_type(tbl->type, false);
	hws_matcher_set_rtc_attr_sz(matcher, &rtc_attr, rtc_type, false);

	/* STC is a single resource (obj_id), use any STC for the ID */
	stc_pool = ctx->stc_pool[tbl->type];
	default_stc = ctx->common_res[tbl->type].default_stc;
	obj_id = mlx5hws_pool_chunk_get_base_id(stc_pool, &default_stc->default_hit);
	rtc_attr.stc_base = obj_id;

	ret = mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr, rtc_0_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create matcher RTC of type %s",
			    hws_matcher_rtc_type_to_str(rtc_type));
		goto free_ste;
	}

	if (tbl->type == MLX5HWS_TABLE_TYPE_FDB) {
		obj_id = mlx5hws_pool_chunk_get_base_mirror_id(ste_pool, ste);
		rtc_attr.ste_base = obj_id;
		rtc_attr.table_type = mlx5hws_table_get_res_fw_ft_type(tbl->type, true);

		obj_id = mlx5hws_pool_chunk_get_base_mirror_id(stc_pool, &default_stc->default_hit);
		rtc_attr.stc_base = obj_id;
		hws_matcher_set_rtc_attr_sz(matcher, &rtc_attr, rtc_type, true);

		ret = mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr, rtc_1_id);
		if (ret) {
			mlx5hws_err(ctx, "Failed to create peer matcher RTC of type %s",
				    hws_matcher_rtc_type_to_str(rtc_type));
			goto destroy_rtc_0;
		}
	}

	return 0;

destroy_rtc_0:
	mlx5hws_cmd_rtc_destroy(ctx->mdev, *rtc_0_id);
free_ste:
	if (rtc_type == HWS_MATCHER_RTC_TYPE_MATCH)
		mlx5hws_pool_chunk_free(ste_pool, ste);
	return ret;
}

static void hws_matcher_destroy_rtc(struct mlx5hws_matcher *matcher,
				    enum mlx5hws_matcher_rtc_type rtc_type,
				    u8 action_ste_selector)
{
	struct mlx5hws_matcher_action_ste *action_ste;
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_pool_chunk *ste;
	struct mlx5hws_pool *ste_pool;
	u32 rtc_0_id, rtc_1_id;

	switch (rtc_type) {
	case HWS_MATCHER_RTC_TYPE_MATCH:
		rtc_0_id = matcher->match_ste.rtc_0_id;
		rtc_1_id = matcher->match_ste.rtc_1_id;
		ste_pool = matcher->match_ste.pool;
		ste = &matcher->match_ste.ste;
		break;
	case HWS_MATCHER_RTC_TYPE_STE_ARRAY:
		action_ste = &matcher->action_ste[action_ste_selector];
		rtc_0_id = action_ste->rtc_0_id;
		rtc_1_id = action_ste->rtc_1_id;
		ste_pool = action_ste->pool;
		ste = &action_ste->ste;
		break;
	default:
		return;
	}

	if (tbl->type == MLX5HWS_TABLE_TYPE_FDB)
		mlx5hws_cmd_rtc_destroy(matcher->tbl->ctx->mdev, rtc_1_id);

	mlx5hws_cmd_rtc_destroy(matcher->tbl->ctx->mdev, rtc_0_id);
	if (rtc_type == HWS_MATCHER_RTC_TYPE_MATCH)
		mlx5hws_pool_chunk_free(ste_pool, ste);
}

static int
hws_matcher_check_attr_sz(struct mlx5hws_cmd_query_caps *caps,
			  struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;

	if (attr->table.sz_col_log > caps->rtc_log_depth_max) {
		mlx5hws_err(matcher->tbl->ctx, "Matcher depth exceeds limit %d\n",
			    caps->rtc_log_depth_max);
		return -EOPNOTSUPP;
	}

	if (attr->table.sz_col_log + attr->table.sz_row_log > caps->ste_alloc_log_max) {
		mlx5hws_err(matcher->tbl->ctx, "Total matcher size exceeds limit %d\n",
			    caps->ste_alloc_log_max);
		return -EOPNOTSUPP;
	}

	if (attr->table.sz_col_log + attr->table.sz_row_log < caps->ste_alloc_log_gran) {
		mlx5hws_err(matcher->tbl->ctx, "Total matcher size below limit %d\n",
			    caps->ste_alloc_log_gran);
		return -EOPNOTSUPP;
	}

	return 0;
}

static void hws_matcher_set_pool_attr(struct mlx5hws_pool_attr *attr,
				      struct mlx5hws_matcher *matcher)
{
	switch (matcher->attr.optimize_flow_src) {
	case MLX5HWS_MATCHER_FLOW_SRC_VPORT:
		attr->opt_type = MLX5HWS_POOL_OPTIMIZE_ORIG;
		break;
	case MLX5HWS_MATCHER_FLOW_SRC_WIRE:
		attr->opt_type = MLX5HWS_POOL_OPTIMIZE_MIRROR;
		break;
	default:
		break;
	}
}

static int hws_matcher_check_and_process_at(struct mlx5hws_matcher *matcher,
					    struct mlx5hws_action_template *at)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	bool valid;
	int ret;

	valid = mlx5hws_action_check_combo(ctx, at->action_type_arr, matcher->tbl->type);
	if (!valid) {
		mlx5hws_err(ctx, "Invalid combination in action template\n");
		return -EINVAL;
	}

	/* Process action template to setters */
	ret = mlx5hws_action_template_process(at);
	if (ret) {
		mlx5hws_err(ctx, "Failed to process action template\n");
		return ret;
	}

	return 0;
}

static int hws_matcher_resize_init(struct mlx5hws_matcher *src_matcher)
{
	struct mlx5hws_matcher_resize_data *resize_data;

	resize_data = kzalloc(sizeof(*resize_data), GFP_KERNEL);
	if (!resize_data)
		return -ENOMEM;

	resize_data->max_stes = src_matcher->action_ste[MLX5HWS_ACTION_STE_IDX_ANY].max_stes;

	resize_data->action_ste[0].stc = src_matcher->action_ste[0].stc;
	resize_data->action_ste[0].rtc_0_id = src_matcher->action_ste[0].rtc_0_id;
	resize_data->action_ste[0].rtc_1_id = src_matcher->action_ste[0].rtc_1_id;
	resize_data->action_ste[0].pool = src_matcher->action_ste[0].max_stes ?
					  src_matcher->action_ste[0].pool :
					  NULL;
	resize_data->action_ste[1].stc = src_matcher->action_ste[1].stc;
	resize_data->action_ste[1].rtc_0_id = src_matcher->action_ste[1].rtc_0_id;
	resize_data->action_ste[1].rtc_1_id = src_matcher->action_ste[1].rtc_1_id;
	resize_data->action_ste[1].pool = src_matcher->action_ste[1].max_stes ?
					  src_matcher->action_ste[1].pool :
					   NULL;

	/* Place the new resized matcher on the dst matcher's list */
	list_add(&resize_data->list_node, &src_matcher->resize_dst->resize_data);

	/* Move all the previous resized matchers to the dst matcher's list */
	while (!list_empty(&src_matcher->resize_data)) {
		resize_data = list_first_entry(&src_matcher->resize_data,
					       struct mlx5hws_matcher_resize_data,
					       list_node);
		list_del_init(&resize_data->list_node);
		list_add(&resize_data->list_node, &src_matcher->resize_dst->resize_data);
	}

	return 0;
}

static void hws_matcher_resize_uninit(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_resize_data *resize_data;

	if (!mlx5hws_matcher_is_resizable(matcher))
		return;

	while (!list_empty(&matcher->resize_data)) {
		resize_data = list_first_entry(&matcher->resize_data,
					       struct mlx5hws_matcher_resize_data,
					       list_node);
		list_del_init(&resize_data->list_node);

		if (resize_data->max_stes) {
			mlx5hws_action_free_single_stc(matcher->tbl->ctx,
						       matcher->tbl->type,
						       &resize_data->action_ste[1].stc);
			mlx5hws_action_free_single_stc(matcher->tbl->ctx,
						       matcher->tbl->type,
						       &resize_data->action_ste[0].stc);

			if (matcher->tbl->type == MLX5HWS_TABLE_TYPE_FDB) {
				mlx5hws_cmd_rtc_destroy(matcher->tbl->ctx->mdev,
							resize_data->action_ste[1].rtc_1_id);
				mlx5hws_cmd_rtc_destroy(matcher->tbl->ctx->mdev,
							resize_data->action_ste[0].rtc_1_id);
			}
			mlx5hws_cmd_rtc_destroy(matcher->tbl->ctx->mdev,
						resize_data->action_ste[1].rtc_0_id);
			mlx5hws_cmd_rtc_destroy(matcher->tbl->ctx->mdev,
						resize_data->action_ste[0].rtc_0_id);
			if (resize_data->action_ste[MLX5HWS_ACTION_STE_IDX_ANY].pool) {
				mlx5hws_pool_destroy(resize_data->action_ste[1].pool);
				mlx5hws_pool_destroy(resize_data->action_ste[0].pool);
			}
		}

		kfree(resize_data);
	}
}

static int
hws_matcher_bind_at_idx(struct mlx5hws_matcher *matcher, u8 action_ste_selector)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = {0};
	struct mlx5hws_matcher_action_ste *action_ste;
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_pool_attr pool_attr = {0};
	struct mlx5hws_context *ctx = tbl->ctx;
	int ret;

	action_ste = &matcher->action_ste[action_ste_selector];

	/* Allocate action STE mempool */
	pool_attr.table_type = tbl->type;
	pool_attr.pool_type = MLX5HWS_POOL_TYPE_STE;
	pool_attr.flags = MLX5HWS_POOL_FLAGS_FOR_STE_ACTION_POOL;
	pool_attr.alloc_log_sz = ilog2(roundup_pow_of_two(action_ste->max_stes)) +
				 matcher->attr.table.sz_row_log;
	hws_matcher_set_pool_attr(&pool_attr, matcher);
	action_ste->pool = mlx5hws_pool_create(ctx, &pool_attr);
	if (!action_ste->pool) {
		mlx5hws_err(ctx, "Failed to create action ste pool\n");
		return -EINVAL;
	}

	/* Allocate action RTC */
	ret = hws_matcher_create_rtc(matcher, HWS_MATCHER_RTC_TYPE_STE_ARRAY, action_ste_selector);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create action RTC\n");
		goto free_ste_pool;
	}

	/* Allocate STC for jumps to STE */
	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_HIT;
	stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_STE_TABLE;
	stc_attr.reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;
	stc_attr.ste_table.ste = action_ste->ste;
	stc_attr.ste_table.ste_pool = action_ste->pool;
	stc_attr.ste_table.match_definer_id = ctx->caps->trivial_match_definer;

	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl->type,
					      &action_ste->stc);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create action jump to table STC\n");
		goto free_rtc;
	}

	return 0;

free_rtc:
	hws_matcher_destroy_rtc(matcher, HWS_MATCHER_RTC_TYPE_STE_ARRAY, action_ste_selector);
free_ste_pool:
	mlx5hws_pool_destroy(action_ste->pool);
	return ret;
}

static void hws_matcher_unbind_at_idx(struct mlx5hws_matcher *matcher, u8 action_ste_selector)
{
	struct mlx5hws_matcher_action_ste *action_ste;
	struct mlx5hws_table *tbl = matcher->tbl;

	action_ste = &matcher->action_ste[action_ste_selector];

	if (!action_ste->max_stes ||
	    matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION ||
	    mlx5hws_matcher_is_in_resize(matcher))
		return;

	mlx5hws_action_free_single_stc(tbl->ctx, tbl->type, &action_ste->stc);
	hws_matcher_destroy_rtc(matcher, HWS_MATCHER_RTC_TYPE_STE_ARRAY, action_ste_selector);
	mlx5hws_pool_destroy(action_ste->pool);
}

static int hws_matcher_bind_at(struct mlx5hws_matcher *matcher)
{
	bool is_jumbo = mlx5hws_matcher_mt_is_jumbo(matcher->mt);
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_context *ctx = tbl->ctx;
	u32 required_stes;
	u8 max_stes = 0;
	int i, ret;

	if (matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION)
		return 0;

	for (i = 0; i < matcher->num_of_at; i++) {
		struct mlx5hws_action_template *at = &matcher->at[i];

		ret = hws_matcher_check_and_process_at(matcher, at);
		if (ret) {
			mlx5hws_err(ctx, "Invalid at %d", i);
			return ret;
		}

		required_stes = at->num_of_action_stes - (!is_jumbo || at->only_term);
		max_stes = max(max_stes, required_stes);

		/* Future: Optimize reparse */
	}

	/* There are no additional STEs required for matcher */
	if (!max_stes)
		return 0;

	matcher->action_ste[0].max_stes = max_stes;
	matcher->action_ste[1].max_stes = max_stes;

	ret = hws_matcher_bind_at_idx(matcher, 0);
	if (ret)
		return ret;

	ret = hws_matcher_bind_at_idx(matcher, 1);
	if (ret)
		goto free_at_0;

	return 0;

free_at_0:
	hws_matcher_unbind_at_idx(matcher, 0);
	return ret;
}

static void hws_matcher_unbind_at(struct mlx5hws_matcher *matcher)
{
	hws_matcher_unbind_at_idx(matcher, 1);
	hws_matcher_unbind_at_idx(matcher, 0);
}

static int hws_matcher_bind_mt(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	struct mlx5hws_pool_attr pool_attr = {0};
	int ret;

	/* Calculate match, range and hash definers */
	if (!(matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION)) {
		ret = mlx5hws_definer_mt_init(ctx, matcher->mt);
		if (ret) {
			if (ret == -E2BIG)
				mlx5hws_err(ctx, "Failed to set matcher templates with match definers\n");
			return ret;
		}
	}

	/* Create an STE pool per matcher*/
	pool_attr.table_type = matcher->tbl->type;
	pool_attr.pool_type = MLX5HWS_POOL_TYPE_STE;
	pool_attr.flags = MLX5HWS_POOL_FLAGS_FOR_MATCHER_STE_POOL;
	pool_attr.alloc_log_sz = matcher->attr.table.sz_col_log +
				 matcher->attr.table.sz_row_log;
	hws_matcher_set_pool_attr(&pool_attr, matcher);

	matcher->match_ste.pool = mlx5hws_pool_create(ctx, &pool_attr);
	if (!matcher->match_ste.pool) {
		mlx5hws_err(ctx, "Failed to allocate matcher STE pool\n");
		ret = -EOPNOTSUPP;
		goto uninit_match_definer;
	}

	return 0;

uninit_match_definer:
	if (!(matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION))
		mlx5hws_definer_mt_uninit(ctx, matcher->mt);
	return ret;
}

static void hws_matcher_unbind_mt(struct mlx5hws_matcher *matcher)
{
	mlx5hws_pool_destroy(matcher->match_ste.pool);
	if (!(matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION))
		mlx5hws_definer_mt_uninit(matcher->tbl->ctx, matcher->mt);
}

static int
hws_matcher_validate_insert_mode(struct mlx5hws_cmd_query_caps *caps,
				 struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;
	struct mlx5hws_context *ctx = matcher->tbl->ctx;

	switch (attr->insert_mode) {
	case MLX5HWS_MATCHER_INSERT_BY_HASH:
		if (matcher->attr.distribute_mode != MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH) {
			mlx5hws_err(ctx, "Invalid matcher distribute mode\n");
			return -EOPNOTSUPP;
		}
		break;

	case MLX5HWS_MATCHER_INSERT_BY_INDEX:
		if (attr->table.sz_col_log) {
			mlx5hws_err(ctx, "Matcher with INSERT_BY_INDEX supports only Nx1 table size\n");
			return -EOPNOTSUPP;
		}

		if (attr->distribute_mode == MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH) {
			/* Hash Split Table */
			if (!caps->rtc_hash_split_table) {
				mlx5hws_err(ctx, "FW doesn't support insert by index and hash distribute\n");
				return -EOPNOTSUPP;
			}
		} else if (attr->distribute_mode == MLX5HWS_MATCHER_DISTRIBUTE_BY_LINEAR) {
			/* Linear Lookup Table */
			if (!caps->rtc_linear_lookup_table ||
			    !IS_BIT_SET(caps->access_index_mode,
					MLX5_IFC_RTC_STE_ACCESS_MODE_LINEAR)) {
				mlx5hws_err(ctx, "FW doesn't support insert by index and linear distribute\n");
				return -EOPNOTSUPP;
			}

			if (attr->table.sz_row_log > MLX5_IFC_RTC_LINEAR_LOOKUP_TBL_LOG_MAX) {
				mlx5hws_err(ctx, "Matcher with linear distribute: rows exceed limit %d",
					    MLX5_IFC_RTC_LINEAR_LOOKUP_TBL_LOG_MAX);
				return -EOPNOTSUPP;
			}
		} else {
			mlx5hws_err(ctx, "Matcher has unsupported distribute mode\n");
			return -EOPNOTSUPP;
		}
		break;

	default:
		mlx5hws_err(ctx, "Matcher has unsupported insert mode\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
hws_matcher_process_attr(struct mlx5hws_cmd_query_caps *caps,
			 struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;

	if (hws_matcher_validate_insert_mode(caps, matcher))
		return -EOPNOTSUPP;

	if (matcher->tbl->type != MLX5HWS_TABLE_TYPE_FDB && attr->optimize_flow_src) {
		mlx5hws_err(matcher->tbl->ctx, "NIC domain doesn't support flow_src\n");
		return -EOPNOTSUPP;
	}

	/* Convert number of rules to the required depth */
	if (attr->mode == MLX5HWS_MATCHER_RESOURCE_MODE_RULE &&
	    attr->insert_mode == MLX5HWS_MATCHER_INSERT_BY_HASH)
		attr->table.sz_col_log = hws_matcher_rules_to_tbl_depth(attr->rule.num_log);

	matcher->flags |= attr->resizable ? MLX5HWS_MATCHER_FLAGS_RESIZABLE : 0;

	return hws_matcher_check_attr_sz(caps, matcher);
}

static int hws_matcher_create_and_connect(struct mlx5hws_matcher *matcher)
{
	int ret;

	/* Select and create the definers for current matcher */
	ret = hws_matcher_bind_mt(matcher);
	if (ret)
		return ret;

	/* Calculate and verify action combination */
	ret = hws_matcher_bind_at(matcher);
	if (ret)
		goto unbind_mt;

	/* Create matcher end flow table anchor */
	ret = hws_matcher_create_end_ft(matcher);
	if (ret)
		goto unbind_at;

	/* Allocate the RTC for the new matcher */
	ret = hws_matcher_create_rtc(matcher, HWS_MATCHER_RTC_TYPE_MATCH, 0);
	if (ret)
		goto destroy_end_ft;

	/* Connect the matcher to the matcher list */
	ret = hws_matcher_connect(matcher);
	if (ret)
		goto destroy_rtc;

	return 0;

destroy_rtc:
	hws_matcher_destroy_rtc(matcher, HWS_MATCHER_RTC_TYPE_MATCH, 0);
destroy_end_ft:
	hws_matcher_destroy_end_ft(matcher);
unbind_at:
	hws_matcher_unbind_at(matcher);
unbind_mt:
	hws_matcher_unbind_mt(matcher);
	return ret;
}

static void hws_matcher_destroy_and_disconnect(struct mlx5hws_matcher *matcher)
{
	hws_matcher_resize_uninit(matcher);
	hws_matcher_disconnect(matcher);
	hws_matcher_destroy_rtc(matcher, HWS_MATCHER_RTC_TYPE_MATCH, 0);
	hws_matcher_destroy_end_ft(matcher);
	hws_matcher_unbind_at(matcher);
	hws_matcher_unbind_mt(matcher);
}

static int
hws_matcher_create_col_matcher(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	struct mlx5hws_matcher *col_matcher;
	int ret;

	if (matcher->attr.mode != MLX5HWS_MATCHER_RESOURCE_MODE_RULE ||
	    matcher->attr.insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX)
		return 0;

	if (!hws_matcher_requires_col_tbl(matcher->attr.rule.num_log))
		return 0;

	col_matcher = kzalloc(sizeof(*matcher), GFP_KERNEL);
	if (!col_matcher)
		return -ENOMEM;

	INIT_LIST_HEAD(&col_matcher->resize_data);

	col_matcher->tbl = matcher->tbl;
	col_matcher->mt = matcher->mt;
	col_matcher->at = matcher->at;
	col_matcher->num_of_at = matcher->num_of_at;
	col_matcher->num_of_mt = matcher->num_of_mt;
	col_matcher->attr.priority = matcher->attr.priority;
	col_matcher->flags = matcher->flags;
	col_matcher->flags |= MLX5HWS_MATCHER_FLAGS_COLLISION;
	col_matcher->attr.mode = MLX5HWS_MATCHER_RESOURCE_MODE_HTABLE;
	col_matcher->attr.optimize_flow_src = matcher->attr.optimize_flow_src;
	col_matcher->attr.table.sz_row_log = matcher->attr.rule.num_log;
	col_matcher->attr.table.sz_col_log = MLX5HWS_MATCHER_ASSURED_COL_TBL_DEPTH;
	if (col_matcher->attr.table.sz_row_log > MLX5HWS_MATCHER_ASSURED_ROW_RATIO)
		col_matcher->attr.table.sz_row_log -= MLX5HWS_MATCHER_ASSURED_ROW_RATIO;

	col_matcher->attr.max_num_of_at_attach = matcher->attr.max_num_of_at_attach;

	ret = hws_matcher_process_attr(ctx->caps, col_matcher);
	if (ret)
		goto free_col_matcher;

	ret = hws_matcher_create_and_connect(col_matcher);
	if (ret)
		goto free_col_matcher;

	matcher->col_matcher = col_matcher;

	return 0;

free_col_matcher:
	kfree(col_matcher);
	mlx5hws_err(ctx, "Failed to create assured collision matcher\n");
	return ret;
}

static void
hws_matcher_destroy_col_matcher(struct mlx5hws_matcher *matcher)
{
	if (matcher->attr.mode != MLX5HWS_MATCHER_RESOURCE_MODE_RULE ||
	    matcher->attr.insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX)
		return;

	if (matcher->col_matcher) {
		hws_matcher_destroy_and_disconnect(matcher->col_matcher);
		kfree(matcher->col_matcher);
	}
}

static int hws_matcher_init(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	int ret;

	INIT_LIST_HEAD(&matcher->resize_data);

	mutex_lock(&ctx->ctrl_lock);

	/* Allocate matcher resource and connect to the packet pipe */
	ret = hws_matcher_create_and_connect(matcher);
	if (ret)
		goto unlock_err;

	/* Create additional matcher for collision handling */
	ret = hws_matcher_create_col_matcher(matcher);
	if (ret)
		goto destory_and_disconnect;
	mutex_unlock(&ctx->ctrl_lock);

	return 0;

destory_and_disconnect:
	hws_matcher_destroy_and_disconnect(matcher);
unlock_err:
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}

static int hws_matcher_uninit(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;

	mutex_lock(&ctx->ctrl_lock);
	hws_matcher_destroy_col_matcher(matcher);
	hws_matcher_destroy_and_disconnect(matcher);
	mutex_unlock(&ctx->ctrl_lock);

	return 0;
}

int mlx5hws_matcher_attach_at(struct mlx5hws_matcher *matcher,
			      struct mlx5hws_action_template *at)
{
	bool is_jumbo = mlx5hws_matcher_mt_is_jumbo(matcher->mt);
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	u32 required_stes;
	int ret;

	if (!matcher->attr.max_num_of_at_attach) {
		mlx5hws_dbg(ctx, "Num of current at (%d) exceed allowed value\n",
			    matcher->num_of_at);
		return -EOPNOTSUPP;
	}

	ret = hws_matcher_check_and_process_at(matcher, at);
	if (ret)
		return ret;

	required_stes = at->num_of_action_stes - (!is_jumbo || at->only_term);
	if (matcher->action_ste[MLX5HWS_ACTION_STE_IDX_ANY].max_stes < required_stes) {
		mlx5hws_dbg(ctx, "Required STEs [%d] exceeds initial action template STE [%d]\n",
			    required_stes,
			    matcher->action_ste[MLX5HWS_ACTION_STE_IDX_ANY].max_stes);
		return -ENOMEM;
	}

	matcher->at[matcher->num_of_at] = *at;
	matcher->num_of_at += 1;
	matcher->attr.max_num_of_at_attach -= 1;

	if (matcher->col_matcher)
		matcher->col_matcher->num_of_at = matcher->num_of_at;

	return 0;
}

static int
hws_matcher_set_templates(struct mlx5hws_matcher *matcher,
			  struct mlx5hws_match_template *mt[],
			  u8 num_of_mt,
			  struct mlx5hws_action_template *at[],
			  u8 num_of_at)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	int ret = 0;
	int i;

	if (!num_of_mt || !num_of_at) {
		mlx5hws_err(ctx, "Number of action/match template cannot be zero\n");
		return -EOPNOTSUPP;
	}

	matcher->mt = kcalloc(num_of_mt, sizeof(*matcher->mt), GFP_KERNEL);
	if (!matcher->mt)
		return -ENOMEM;

	matcher->at = kcalloc(num_of_at + matcher->attr.max_num_of_at_attach,
			      sizeof(*matcher->at),
			      GFP_KERNEL);
	if (!matcher->at) {
		mlx5hws_err(ctx, "Failed to allocate action template array\n");
		ret = -ENOMEM;
		goto free_mt;
	}

	for (i = 0; i < num_of_mt; i++)
		matcher->mt[i] = *mt[i];

	for (i = 0; i < num_of_at; i++)
		matcher->at[i] = *at[i];

	matcher->num_of_mt = num_of_mt;
	matcher->num_of_at = num_of_at;

	return 0;

free_mt:
	kfree(matcher->mt);
	return ret;
}

static void
hws_matcher_unset_templates(struct mlx5hws_matcher *matcher)
{
	kfree(matcher->at);
	kfree(matcher->mt);
}

struct mlx5hws_matcher *
mlx5hws_matcher_create(struct mlx5hws_table *tbl,
		       struct mlx5hws_match_template *mt[],
		       u8 num_of_mt,
		       struct mlx5hws_action_template *at[],
		       u8 num_of_at,
		       struct mlx5hws_matcher_attr *attr)
{
	struct mlx5hws_context *ctx = tbl->ctx;
	struct mlx5hws_matcher *matcher;
	int ret;

	matcher = kzalloc(sizeof(*matcher), GFP_KERNEL);
	if (!matcher)
		return NULL;

	matcher->tbl = tbl;
	matcher->attr = *attr;

	ret = hws_matcher_process_attr(tbl->ctx->caps, matcher);
	if (ret)
		goto free_matcher;

	ret = hws_matcher_set_templates(matcher, mt, num_of_mt, at, num_of_at);
	if (ret)
		goto free_matcher;

	ret = hws_matcher_init(matcher);
	if (ret) {
		mlx5hws_err(ctx, "Failed to initialise matcher: %d\n", ret);
		goto unset_templates;
	}

	return matcher;

unset_templates:
	hws_matcher_unset_templates(matcher);
free_matcher:
	kfree(matcher);
	return NULL;
}

int mlx5hws_matcher_destroy(struct mlx5hws_matcher *matcher)
{
	hws_matcher_uninit(matcher);
	hws_matcher_unset_templates(matcher);
	kfree(matcher);
	return 0;
}

struct mlx5hws_match_template *
mlx5hws_match_template_create(struct mlx5hws_context *ctx,
			      u32 *match_param,
			      u32 match_param_sz,
			      u8 match_criteria_enable)
{
	struct mlx5hws_match_template *mt;

	mt = kzalloc(sizeof(*mt), GFP_KERNEL);
	if (!mt)
		return NULL;

	mt->match_param = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!mt->match_param)
		goto free_template;

	memcpy(mt->match_param, match_param, match_param_sz);
	mt->match_criteria_enable = match_criteria_enable;

	return mt;

free_template:
	kfree(mt);
	return NULL;
}

int mlx5hws_match_template_destroy(struct mlx5hws_match_template *mt)
{
	kfree(mt->match_param);
	kfree(mt);
	return 0;
}

static int hws_matcher_resize_precheck(struct mlx5hws_matcher *src_matcher,
				       struct mlx5hws_matcher *dst_matcher)
{
	struct mlx5hws_context *ctx = src_matcher->tbl->ctx;
	int i;

	if (src_matcher->tbl->type != dst_matcher->tbl->type) {
		mlx5hws_err(ctx, "Table type mismatch for src/dst matchers\n");
		return -EINVAL;
	}

	if (!mlx5hws_matcher_is_resizable(src_matcher) ||
	    !mlx5hws_matcher_is_resizable(dst_matcher)) {
		mlx5hws_err(ctx, "Src/dst matcher is not resizable\n");
		return -EINVAL;
	}

	if (mlx5hws_matcher_is_insert_by_idx(src_matcher) !=
	    mlx5hws_matcher_is_insert_by_idx(dst_matcher)) {
		mlx5hws_err(ctx, "Src/dst matchers insert mode mismatch\n");
		return -EINVAL;
	}

	if (mlx5hws_matcher_is_in_resize(src_matcher) ||
	    mlx5hws_matcher_is_in_resize(dst_matcher)) {
		mlx5hws_err(ctx, "Src/dst matcher is already in resize\n");
		return -EINVAL;
	}

	/* Compare match templates - make sure the definers are equivalent */
	if (src_matcher->num_of_mt != dst_matcher->num_of_mt) {
		mlx5hws_err(ctx, "Src/dst matcher match templates mismatch\n");
		return -EINVAL;
	}

	if (src_matcher->action_ste[MLX5HWS_ACTION_STE_IDX_ANY].max_stes >
	    dst_matcher->action_ste[0].max_stes) {
		mlx5hws_err(ctx, "Src/dst matcher max STEs mismatch\n");
		return -EINVAL;
	}

	for (i = 0; i < src_matcher->num_of_mt; i++) {
		if (mlx5hws_definer_compare(src_matcher->mt[i].definer,
					    dst_matcher->mt[i].definer)) {
			mlx5hws_err(ctx, "Src/dst matcher definers mismatch\n");
			return -EINVAL;
		}
	}

	return 0;
}

int mlx5hws_matcher_resize_set_target(struct mlx5hws_matcher *src_matcher,
				      struct mlx5hws_matcher *dst_matcher)
{
	int ret = 0;

	mutex_lock(&src_matcher->tbl->ctx->ctrl_lock);

	ret = hws_matcher_resize_precheck(src_matcher, dst_matcher);
	if (ret)
		goto out;

	src_matcher->resize_dst = dst_matcher;

	ret = hws_matcher_resize_init(src_matcher);
	if (ret)
		src_matcher->resize_dst = NULL;

out:
	mutex_unlock(&src_matcher->tbl->ctx->ctrl_lock);
	return ret;
}

int mlx5hws_matcher_resize_rule_move(struct mlx5hws_matcher *src_matcher,
				     struct mlx5hws_rule *rule,
				     struct mlx5hws_rule_attr *attr)
{
	struct mlx5hws_context *ctx = src_matcher->tbl->ctx;

	if (unlikely(!mlx5hws_matcher_is_in_resize(src_matcher))) {
		mlx5hws_err(ctx, "Matcher is not resizable or not in resize\n");
		return -EINVAL;
	}

	if (unlikely(src_matcher != rule->matcher)) {
		mlx5hws_err(ctx, "Rule doesn't belong to src matcher\n");
		return -EINVAL;
	}

	return mlx5hws_rule_move_hws_add(rule, attr);
}
