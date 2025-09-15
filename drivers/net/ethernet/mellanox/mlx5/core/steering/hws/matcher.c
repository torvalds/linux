// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

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

int mlx5hws_matcher_update_end_ft_isolated(struct mlx5hws_table *tbl,
					   u32 miss_ft_id)
{
	struct mlx5hws_matcher *tmp_matcher;

	if (list_empty(&tbl->matchers_list))
		return -EINVAL;

	/* Update isolated_matcher_end_ft_id attribute for all
	 * the matchers in isolated table.
	 */
	list_for_each_entry(tmp_matcher, &tbl->matchers_list, list_node)
		tmp_matcher->attr.isolated_matcher_end_ft_id = miss_ft_id;

	tmp_matcher = list_last_entry(&tbl->matchers_list,
				      struct mlx5hws_matcher,
				      list_node);

	return mlx5hws_table_ft_set_next_ft(tbl->ctx,
					    tmp_matcher->end_ft_id,
					    tbl->fw_ft_type,
					    miss_ft_id);
}

static int hws_matcher_connect_end_ft_isolated(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	u32 end_ft_id;
	int ret;

	/* Reset end_ft next RTCs */
	ret = mlx5hws_table_ft_set_next_rtc(tbl->ctx,
					    matcher->end_ft_id,
					    matcher->tbl->fw_ft_type,
					    0, 0);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Isolated matcher: failed to reset FT's next RTCs\n");
		return ret;
	}

	/* Connect isolated matcher's end_ft to the complex matcher's end FT */
	end_ft_id = matcher->attr.isolated_matcher_end_ft_id;
	ret = mlx5hws_table_ft_set_next_ft(tbl->ctx,
					   matcher->end_ft_id,
					   matcher->tbl->fw_ft_type,
					   end_ft_id);

	if (ret) {
		mlx5hws_err(tbl->ctx, "Isolated matcher: failed to set FT's miss_ft_id\n");
		return ret;
	}

	return 0;
}

static int hws_matcher_create_end_ft_isolated(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	int ret;

	ret = mlx5hws_table_create_default_ft(tbl->ctx->mdev,
					      tbl,
					      0,
					      &matcher->end_ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Isolated matcher: failed to create end flow table\n");
		return ret;
	}

	ret = hws_matcher_connect_end_ft_isolated(matcher);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Isolated matcher: failed to connect end FT\n");
		goto destroy_default_ft;
	}

	return 0;

destroy_default_ft:
	mlx5hws_table_destroy_default_ft(tbl, matcher->end_ft_id);
	return ret;
}

static int hws_matcher_create_end_ft(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	int ret;

	if (mlx5hws_matcher_is_isolated(matcher))
		ret = hws_matcher_create_end_ft_isolated(matcher);
	else
		ret = mlx5hws_table_create_default_ft(tbl->ctx->mdev,
						      tbl,
						      0,
						      &matcher->end_ft_id);

	if (ret) {
		mlx5hws_err(tbl->ctx, "Failed to create matcher end flow table\n");
		return ret;
	}

	return 0;
}

static int hws_matcher_connect_isolated_first(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_context *ctx = tbl->ctx;
	int ret;

	/* Isolated matcher's end_ft is already pointing to the end_ft
	 * of the complex matcher - it was set at creation of end_ft,
	 * so no need to connect it.
	 * We still need to connect the isolated table's start FT to
	 * this matcher's RTC.
	 */
	ret = mlx5hws_table_ft_set_next_rtc(ctx,
					    tbl->ft_id,
					    tbl->fw_ft_type,
					    matcher->match_ste.rtc_0_id,
					    matcher->match_ste.rtc_1_id);
	if (ret) {
		mlx5hws_err(ctx, "Isolated matcher: failed to connect start FT to match RTC\n");
		return ret;
	}

	/* Reset table's FT default miss (drop refcount) */
	ret = mlx5hws_table_ft_set_default_next_ft(tbl, tbl->ft_id);
	if (ret) {
		mlx5hws_err(ctx, "Isolated matcher: failed to reset table ft default miss\n");
		return ret;
	}

	list_add(&matcher->list_node, &tbl->matchers_list);

	return ret;
}

static int hws_matcher_connect_isolated_last(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_context *ctx = tbl->ctx;
	struct mlx5hws_matcher *last;
	int ret;

	last = list_last_entry(&tbl->matchers_list,
			       struct mlx5hws_matcher,
			       list_node);

	/* New matcher's end_ft is already pointing to the end_ft of
	 * the complex matcher.
	 * Connect previous matcher's end_ft to this new matcher RTC.
	 */
	ret = mlx5hws_table_ft_set_next_rtc(ctx,
					    last->end_ft_id,
					    tbl->fw_ft_type,
					    matcher->match_ste.rtc_0_id,
					    matcher->match_ste.rtc_1_id);
	if (ret) {
		mlx5hws_err(ctx,
			    "Isolated matcher: failed to connect matcher end_ft to new match RTC\n");
		return ret;
	}

	/* Reset prev matcher FT default miss (drop refcount) */
	ret = mlx5hws_table_ft_set_default_next_ft(tbl, last->end_ft_id);
	if (ret) {
		mlx5hws_err(ctx, "Isolated matcher: failed to reset matcher ft default miss\n");
		return ret;
	}

	/* Insert after the last matcher */
	list_add(&matcher->list_node, &last->list_node);

	return 0;
}

static int hws_matcher_connect_isolated(struct mlx5hws_matcher *matcher)
{
	/* Isolated matcher is expected to be the only one in its table.
	 * However, it can have a collision matcher, and it can go through
	 * rehash process, in which case we will temporary have both old and
	 * new matchers in the isolated table.
	 * Check if this is the first matcher in the isolated table.
	 */
	if (list_empty(&matcher->tbl->matchers_list))
		return hws_matcher_connect_isolated_first(matcher);

	/* If this wasn't the first matcher, then we have 3 possible cases:
	 *  - this is a collision matcher for the first matcher
	 *  - this is a new rehash dest matcher
	 *  - this is a collision matcher for the new rehash dest matcher
	 * The logic to add new matcher is the same for all these cases.
	 */
	return hws_matcher_connect_isolated_last(matcher);
}

static int hws_matcher_connect(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_context *ctx = tbl->ctx;
	struct mlx5hws_matcher *prev = NULL;
	struct mlx5hws_matcher *next = NULL;
	struct mlx5hws_matcher *tmp_matcher;
	int ret;

	if (mlx5hws_matcher_is_isolated(matcher))
		return hws_matcher_connect_isolated(matcher);

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

static int hws_matcher_disconnect_isolated(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher *first, *last, *prev, *next;
	struct mlx5hws_table *tbl = matcher->tbl;
	struct mlx5hws_context *ctx = tbl->ctx;
	u32 end_ft_id;
	int ret;

	first = list_first_entry(&tbl->matchers_list,
				 struct mlx5hws_matcher,
				 list_node);
	last = list_last_entry(&tbl->matchers_list,
			       struct mlx5hws_matcher,
			       list_node);
	prev = list_prev_entry(matcher, list_node);
	next = list_next_entry(matcher, list_node);

	list_del_init(&matcher->list_node);

	if (first == last) {
		/* This was the only matcher in the list.
		 * Reset isolated table FT next RTCs and connect it
		 * to the whole complex matcher end FT instead.
		 */
		ret = mlx5hws_table_ft_set_next_rtc(ctx,
						    tbl->ft_id,
						    tbl->fw_ft_type,
						    0, 0);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Isolated matcher: failed to reset FT's next RTCs\n");
			return ret;
		}

		end_ft_id = matcher->attr.isolated_matcher_end_ft_id;
		ret = mlx5hws_table_ft_set_next_ft(tbl->ctx,
						   tbl->ft_id,
						   tbl->fw_ft_type,
						   end_ft_id);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Isolated matcher: failed to set FT's miss_ft_id\n");
			return ret;
		}

		return 0;
	}

	/* At this point we know that there are more matchers in the list */

	if (matcher == first) {
		/* We've disconnected the first matcher.
		 * Now update isolated table default FT.
		 */
		if (!next)
			return -EINVAL;
		return mlx5hws_table_ft_set_next_rtc(ctx,
						     tbl->ft_id,
						     tbl->fw_ft_type,
						     next->match_ste.rtc_0_id,
						     next->match_ste.rtc_1_id);
	}

	if (matcher == last) {
		/* If we've disconnected the last matcher - update prev
		 * matcher's end_ft to point to the complex matcher end_ft.
		 */
		if (!prev)
			return -EINVAL;
		return hws_matcher_connect_end_ft_isolated(prev);
	}

	/* This wasn't the first or the last matcher, which means that it has
	 * both prev and next matchers. Note that this only happens if we're
	 * disconnecting collision matcher of the old matcher during rehash.
	 */
	if (!prev || !next ||
	    !(matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION))
		return -EINVAL;

	/* Update prev end FT to point to next match RTC */
	return mlx5hws_table_ft_set_next_rtc(ctx,
					     prev->end_ft_id,
					     tbl->fw_ft_type,
					     next->match_ste.rtc_0_id,
					     next->match_ste.rtc_1_id);
}

static int hws_matcher_disconnect(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher *next = NULL, *prev = NULL;
	struct mlx5hws_table *tbl = matcher->tbl;
	u32 prev_ft_id = tbl->ft_id;
	int ret;

	if (mlx5hws_matcher_is_isolated(matcher))
		return hws_matcher_disconnect_isolated(matcher);

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
			mlx5hws_err(tbl->ctx, "Fatal error, failed to disconnect matcher\n");
			return ret;
		}
	} else {
		ret = mlx5hws_table_connect_to_miss_table(tbl, tbl->default_miss.miss_tbl);
		if (ret) {
			mlx5hws_err(tbl->ctx, "Fatal error, failed to disconnect last matcher\n");
			return ret;
		}
	}

	/* Removing first matcher, update connected miss tables if exists */
	if (prev_ft_id == tbl->ft_id) {
		ret = mlx5hws_table_update_connected_miss_tables(tbl);
		if (ret) {
			mlx5hws_err(tbl->ctx,
				    "Fatal error, failed to update connected miss table\n");
			return ret;
		}
	}

	ret = mlx5hws_table_ft_set_default_next_ft(tbl, prev_ft_id);
	if (ret) {
		mlx5hws_err(tbl->ctx, "Fatal error, failed to restore matcher ft default miss\n");
		return ret;
	}

	return 0;
}

static void hws_matcher_set_rtc_attr_sz(struct mlx5hws_matcher *matcher,
					struct mlx5hws_cmd_rtc_create_attr *rtc_attr,
					bool is_mirror)
{
	enum mlx5hws_matcher_flow_src flow_src = matcher->attr.optimize_flow_src;

	if ((flow_src == MLX5HWS_MATCHER_FLOW_SRC_VPORT && !is_mirror) ||
	    (flow_src == MLX5HWS_MATCHER_FLOW_SRC_WIRE && is_mirror)) {
		/* Optimize FDB RTC */
		rtc_attr->log_size = 0;
		rtc_attr->log_depth = 0;
	}
}

static int hws_matcher_create_rtc(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;
	struct mlx5hws_cmd_rtc_create_attr rtc_attr = {0};
	struct mlx5hws_match_template *mt = matcher->mt;
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	union mlx5hws_matcher_size *size_rx, *size_tx;
	struct mlx5hws_table *tbl = matcher->tbl;
	u32 obj_id;
	int ret;

	size_rx = &attr->size[MLX5HWS_MATCHER_SIZE_TYPE_RX];
	size_tx = &attr->size[MLX5HWS_MATCHER_SIZE_TYPE_TX];

	rtc_attr.log_size = size_rx->table.sz_row_log;
	rtc_attr.log_depth = size_rx->table.sz_col_log;
	rtc_attr.is_frst_jumbo = mlx5hws_matcher_mt_is_jumbo(mt);
	rtc_attr.is_scnd_range = 0;
	rtc_attr.miss_ft_id = matcher->end_ft_id;

	if (attr->insert_mode == MLX5HWS_MATCHER_INSERT_BY_HASH) {
		/* The usual Hash Table */
		rtc_attr.update_index_mode =
			MLX5_IFC_RTC_STE_UPDATE_MODE_BY_HASH;

		/* The first mt is used since all share the same definer */
		rtc_attr.match_definer_0 = mlx5hws_definer_get_id(mt->definer);
	} else if (attr->insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX) {
		rtc_attr.update_index_mode =
			MLX5_IFC_RTC_STE_UPDATE_MODE_BY_OFFSET;
		rtc_attr.num_hash_definer = 1;

		if (attr->distribute_mode ==
		    MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH) {
			/* Hash Split Table */
			rtc_attr.access_index_mode =
				MLX5_IFC_RTC_STE_ACCESS_MODE_BY_HASH;
			rtc_attr.match_definer_0 =
				mlx5hws_definer_get_id(mt->definer);
		} else if (attr->distribute_mode ==
			   MLX5HWS_MATCHER_DISTRIBUTE_BY_LINEAR) {
			/* Linear Lookup Table */
			rtc_attr.access_index_mode =
				MLX5_IFC_RTC_STE_ACCESS_MODE_LINEAR;
			rtc_attr.match_definer_0 =
				ctx->caps->linear_match_definer;
		}
	}

	rtc_attr.pd = ctx->pd_num;
	rtc_attr.ste_base = matcher->match_ste.ste_0_base;
	rtc_attr.reparse_mode = mlx5hws_context_get_reparse_mode(ctx);
	rtc_attr.table_type = mlx5hws_table_get_res_fw_ft_type(tbl->type, false);
	hws_matcher_set_rtc_attr_sz(matcher, &rtc_attr, false);

	/* STC is a single resource (obj_id), use any STC for the ID */
	obj_id = mlx5hws_pool_get_base_id(ctx->stc_pool);
	rtc_attr.stc_base = obj_id;

	ret = mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr,
				     &matcher->match_ste.rtc_0_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create matcher RTC\n");
		return ret;
	}

	if (tbl->type == MLX5HWS_TABLE_TYPE_FDB) {
		rtc_attr.log_size = size_tx->table.sz_row_log;
		rtc_attr.log_depth = size_tx->table.sz_col_log;
		rtc_attr.ste_base = matcher->match_ste.ste_1_base;
		rtc_attr.table_type = mlx5hws_table_get_res_fw_ft_type(tbl->type, true);

		obj_id = mlx5hws_pool_get_base_mirror_id(ctx->stc_pool);
		rtc_attr.stc_base = obj_id;
		hws_matcher_set_rtc_attr_sz(matcher, &rtc_attr, true);

		ret = mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr,
					     &matcher->match_ste.rtc_1_id);
		if (ret) {
			mlx5hws_err(ctx, "Failed to create mirror matcher RTC\n");
			goto destroy_rtc_0;
		}
	}

	return 0;

destroy_rtc_0:
	mlx5hws_cmd_rtc_destroy(ctx->mdev, matcher->match_ste.rtc_0_id);
	return ret;
}

static void hws_matcher_destroy_rtc(struct mlx5hws_matcher *matcher)
{
	struct mlx5_core_dev *mdev = matcher->tbl->ctx->mdev;

	if (matcher->tbl->type == MLX5HWS_TABLE_TYPE_FDB)
		mlx5hws_cmd_rtc_destroy(mdev, matcher->match_ste.rtc_1_id);

	mlx5hws_cmd_rtc_destroy(mdev, matcher->match_ste.rtc_0_id);
}

static int
hws_matcher_check_attr_sz(struct mlx5hws_cmd_query_caps *caps,
			  struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	union mlx5hws_matcher_size *size;
	int i;

	for (i = 0; i < 2; i++) {
		size = &attr->size[i];

		if (size->table.sz_col_log > caps->rtc_log_depth_max) {
			mlx5hws_err(ctx, "Matcher depth exceeds limit %d\n",
				    caps->rtc_log_depth_max);
			return -EOPNOTSUPP;
		}

		if (size->table.sz_col_log + size->table.sz_row_log >
		    caps->ste_alloc_log_max) {
			mlx5hws_err(ctx,
				    "Total matcher size exceeds limit %d\n",
				    caps->ste_alloc_log_max);
			return -EOPNOTSUPP;
		}

		if (size->table.sz_col_log + size->table.sz_row_log <
		    caps->ste_alloc_log_gran) {
			mlx5hws_err(ctx, "Total matcher size below limit %d\n",
				    caps->ste_alloc_log_gran);
			return -EOPNOTSUPP;
		}
	}

	return 0;
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

static int hws_matcher_bind_at(struct mlx5hws_matcher *matcher)
{
	bool is_jumbo = mlx5hws_matcher_mt_is_jumbo(matcher->mt);
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	u8 required_stes, max_stes;
	int i, ret;

	if (matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION)
		return 0;

	max_stes = 0;
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

	matcher->num_of_action_stes = max_stes;

	return 0;
}

static void hws_matcher_set_ip_version_match(struct mlx5hws_matcher *matcher)
{
	int i;

	for (i = 0; i < matcher->mt->fc_sz; i++) {
		switch (matcher->mt->fc[i].fname) {
		case MLX5HWS_DEFINER_FNAME_ETH_TYPE_O:
			matcher->matches_outer_ethertype = 1;
			break;
		case MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_O:
			matcher->matches_outer_ip_version = 1;
			break;
		case MLX5HWS_DEFINER_FNAME_ETH_TYPE_I:
			matcher->matches_inner_ethertype = 1;
			break;
		case MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_I:
			matcher->matches_inner_ip_version = 1;
			break;
		default:
			break;
		}
	}
}

static int hws_matcher_bind_mt(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_cmd_ste_create_attr ste_attr = {};
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	union mlx5hws_matcher_size *size;
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

	hws_matcher_set_ip_version_match(matcher);

	/* Create an STE range each for RX and TX. */
	ste_attr.table_type = FS_FT_FDB_RX;
	size = &matcher->attr.size[MLX5HWS_MATCHER_SIZE_TYPE_RX];
	ste_attr.log_obj_range =
		matcher->attr.optimize_flow_src ==
			MLX5HWS_MATCHER_FLOW_SRC_VPORT ?
			0 : size->table.sz_col_log + size->table.sz_row_log;

	ret = mlx5hws_cmd_ste_create(ctx->mdev, &ste_attr,
				     &matcher->match_ste.ste_0_base);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate RX STE range (%d)\n", ret);
		goto uninit_match_definer;
	}

	ste_attr.table_type = FS_FT_FDB_TX;
	size = &matcher->attr.size[MLX5HWS_MATCHER_SIZE_TYPE_TX];
	ste_attr.log_obj_range =
		matcher->attr.optimize_flow_src ==
			MLX5HWS_MATCHER_FLOW_SRC_WIRE ?
			0 : size->table.sz_col_log + size->table.sz_row_log;

	ret = mlx5hws_cmd_ste_create(ctx->mdev, &ste_attr,
				     &matcher->match_ste.ste_1_base);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate TX STE range (%d)\n", ret);
		goto destroy_rx_ste_range;
	}

	return 0;

destroy_rx_ste_range:
	mlx5hws_cmd_ste_destroy(ctx->mdev, matcher->match_ste.ste_0_base);
uninit_match_definer:
	if (!(matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION))
		mlx5hws_definer_mt_uninit(ctx, matcher->mt);
	return ret;
}

static void hws_matcher_unbind_mt(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;

	mlx5hws_cmd_ste_destroy(ctx->mdev, matcher->match_ste.ste_1_base);
	mlx5hws_cmd_ste_destroy(ctx->mdev, matcher->match_ste.ste_0_base);
	if (!(matcher->flags & MLX5HWS_MATCHER_FLAGS_COLLISION))
		mlx5hws_definer_mt_uninit(ctx, matcher->mt);
}

static int
hws_matcher_validate_insert_mode(struct mlx5hws_cmd_query_caps *caps,
				 struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_matcher_attr *attr = &matcher->attr;
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	union mlx5hws_matcher_size *size_rx, *size_tx;

	size_rx = &matcher->attr.size[MLX5HWS_MATCHER_SIZE_TYPE_RX];
	size_tx = &matcher->attr.size[MLX5HWS_MATCHER_SIZE_TYPE_TX];

	switch (attr->insert_mode) {
	case MLX5HWS_MATCHER_INSERT_BY_HASH:
		if (matcher->attr.distribute_mode != MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH) {
			mlx5hws_err(ctx, "Invalid matcher distribute mode\n");
			return -EOPNOTSUPP;
		}
		break;

	case MLX5HWS_MATCHER_INSERT_BY_INDEX:
		if (size_rx->table.sz_col_log || size_tx->table.sz_col_log) {
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

			if (size_rx->table.sz_row_log >
				MLX5_IFC_RTC_LINEAR_LOOKUP_TBL_LOG_MAX ||
			    size_tx->table.sz_row_log >
				MLX5_IFC_RTC_LINEAR_LOOKUP_TBL_LOG_MAX) {
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
	union mlx5hws_matcher_size *size_rx, *size_tx;

	size_rx = &attr->size[MLX5HWS_MATCHER_SIZE_TYPE_RX];
	size_tx = &attr->size[MLX5HWS_MATCHER_SIZE_TYPE_TX];

	if (hws_matcher_validate_insert_mode(caps, matcher))
		return -EOPNOTSUPP;

	if (matcher->tbl->type != MLX5HWS_TABLE_TYPE_FDB && attr->optimize_flow_src) {
		mlx5hws_err(matcher->tbl->ctx, "NIC domain doesn't support flow_src\n");
		return -EOPNOTSUPP;
	}

	/* Convert number of rules to the required depth */
	if (attr->mode == MLX5HWS_MATCHER_RESOURCE_MODE_RULE &&
	    attr->insert_mode == MLX5HWS_MATCHER_INSERT_BY_HASH) {
		size_rx->table.sz_col_log =
			hws_matcher_rules_to_tbl_depth(size_rx->rule.num_log);
		size_tx->table.sz_col_log =
			hws_matcher_rules_to_tbl_depth(size_tx->rule.num_log);
	}

	matcher->flags |= attr->resizable ? MLX5HWS_MATCHER_FLAGS_RESIZABLE : 0;
	matcher->flags |= attr->isolated_matcher_end_ft_id ?
			  MLX5HWS_MATCHER_FLAGS_ISOLATED : 0;

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
		goto unbind_mt;

	/* Allocate the RTC for the new matcher */
	ret = hws_matcher_create_rtc(matcher);
	if (ret)
		goto destroy_end_ft;

	/* Connect the matcher to the matcher list */
	ret = hws_matcher_connect(matcher);
	if (ret)
		goto destroy_rtc;

	return 0;

destroy_rtc:
	hws_matcher_destroy_rtc(matcher);
destroy_end_ft:
	hws_matcher_destroy_end_ft(matcher);
unbind_mt:
	hws_matcher_unbind_mt(matcher);
	return ret;
}

static void hws_matcher_destroy_and_disconnect(struct mlx5hws_matcher *matcher)
{
	hws_matcher_disconnect(matcher);
	hws_matcher_destroy_rtc(matcher);
	hws_matcher_destroy_end_ft(matcher);
	hws_matcher_unbind_mt(matcher);
}

static int
hws_matcher_create_col_matcher(struct mlx5hws_matcher *matcher)
{
	struct mlx5hws_context *ctx = matcher->tbl->ctx;
	union mlx5hws_matcher_size *size_rx, *size_tx;
	struct mlx5hws_matcher *col_matcher;
	int i, ret;

	size_rx = &matcher->attr.size[MLX5HWS_MATCHER_SIZE_TYPE_RX];
	size_tx = &matcher->attr.size[MLX5HWS_MATCHER_SIZE_TYPE_TX];

	if (matcher->attr.mode != MLX5HWS_MATCHER_RESOURCE_MODE_RULE ||
	    matcher->attr.insert_mode == MLX5HWS_MATCHER_INSERT_BY_INDEX)
		return 0;

	if (!hws_matcher_requires_col_tbl(size_rx->rule.num_log) &&
	    !hws_matcher_requires_col_tbl(size_tx->rule.num_log))
		return 0;

	col_matcher = kzalloc(sizeof(*matcher), GFP_KERNEL);
	if (!col_matcher)
		return -ENOMEM;

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
	for (i = 0; i < 2; i++) {
		union mlx5hws_matcher_size *dst = &col_matcher->attr.size[i];
		union mlx5hws_matcher_size *src = &matcher->attr.size[i];

		dst->table.sz_row_log = src->rule.num_log;
		dst->table.sz_col_log = MLX5HWS_MATCHER_ASSURED_COL_TBL_DEPTH;
		if (dst->table.sz_row_log > MLX5HWS_MATCHER_ASSURED_ROW_RATIO)
			dst->table.sz_row_log -=
				MLX5HWS_MATCHER_ASSURED_ROW_RATIO;
	}

	col_matcher->attr.max_num_of_at_attach = matcher->attr.max_num_of_at_attach;
	col_matcher->attr.isolated_matcher_end_ft_id =
		matcher->attr.isolated_matcher_end_ft_id;

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

static int hws_matcher_grow_at_array(struct mlx5hws_matcher *matcher)
{
	void *p;

	if (matcher->size_of_at_array >= MLX5HWS_MATCHER_MAX_AT)
		return -ENOMEM;

	matcher->size_of_at_array *= 2;
	p = krealloc(matcher->at,
		     matcher->size_of_at_array * sizeof(*matcher->at),
		     __GFP_ZERO | GFP_KERNEL);
	if (!p) {
		matcher->size_of_at_array /= 2;
		return -ENOMEM;
	}

	matcher->at = p;

	return 0;
}

int mlx5hws_matcher_attach_at(struct mlx5hws_matcher *matcher,
			      struct mlx5hws_action_template *at)
{
	bool is_jumbo = mlx5hws_matcher_mt_is_jumbo(matcher->mt);
	u32 required_stes;
	int ret;

	if (unlikely(matcher->num_of_at >= matcher->size_of_at_array)) {
		ret = hws_matcher_grow_at_array(matcher);
		if (ret)
			return ret;

		if (matcher->col_matcher) {
			ret = hws_matcher_grow_at_array(matcher->col_matcher);
			if (ret)
				return ret;
		}
	}

	ret = hws_matcher_check_and_process_at(matcher, at);
	if (ret)
		return ret;

	required_stes = at->num_of_action_stes - (!is_jumbo || at->only_term);
	if (matcher->num_of_action_stes < required_stes)
		matcher->num_of_action_stes = required_stes;

	matcher->at[matcher->num_of_at] = *at;
	matcher->num_of_at += 1;

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

	matcher->size_of_at_array =
		num_of_at + matcher->attr.max_num_of_at_attach;
	matcher->at = kvcalloc(matcher->size_of_at_array, sizeof(*matcher->at),
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
	kvfree(matcher->at);
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

	if (src_matcher->num_of_action_stes > dst_matcher->num_of_action_stes) {
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
