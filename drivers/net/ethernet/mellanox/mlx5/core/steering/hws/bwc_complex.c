// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

/* We chain submatchers by applying three rules on a subrule: modify header (to
 * set register C6), jump to table (to the next submatcher) and the mandatory
 * last rule.
 */
#define HWS_NUM_CHAIN_ACTIONS 3

static const struct rhashtable_params hws_rules_hash_params = {
	.key_len = sizeof_field(struct mlx5hws_bwc_complex_subrule_data,
				match_tag),
	.key_offset =
		offsetof(struct mlx5hws_bwc_complex_subrule_data, match_tag),
	.head_offset =
		offsetof(struct mlx5hws_bwc_complex_subrule_data, hash_node),
	.automatic_shrinking = true, .min_size = 1,
};

static bool
hws_match_params_exceeds_definer(struct mlx5hws_context *ctx,
				 u8 match_criteria_enable,
				 struct mlx5hws_match_parameters *mask,
				 bool allow_jumbo)
{
	struct mlx5hws_definer match_layout = {0};
	struct mlx5hws_match_template *mt;
	bool is_complex = false;
	int ret;

	if (!match_criteria_enable)
		return false; /* empty matcher */

	mt = mlx5hws_match_template_create(ctx,
					   mask->match_buf,
					   mask->match_sz,
					   match_criteria_enable);
	if (!mt) {
		mlx5hws_err(ctx, "Complex matcher: failed creating match template\n");
		return false;
	}

	ret = mlx5hws_definer_calc_layout(ctx, mt, &match_layout, allow_jumbo);
	if (ret) {
		/* The only case that we're interested in is E2BIG,
		 * which means that the match parameters need to be
		 * split into complex martcher.
		 * For all other cases (good or bad) - just return true
		 * and let the usual match creation path handle it,
		 * both for good and bad flows.
		 */
		if (ret == -E2BIG) {
			is_complex = true;
			mlx5hws_dbg(ctx, "Matcher definer layout: need complex matcher\n");
		} else {
			mlx5hws_err(ctx, "Failed to calculate matcher definer layout\n");
		}
	} else {
		kfree(mt->fc);
	}

	mlx5hws_match_template_destroy(mt);

	return is_complex;
}

bool mlx5hws_bwc_match_params_is_complex(struct mlx5hws_context *ctx,
					 u8 match_criteria_enable,
					 struct mlx5hws_match_parameters *mask)
{
	return hws_match_params_exceeds_definer(ctx, match_criteria_enable,
						mask, true);
}

static int
hws_get_last_set_dword_idx(const struct mlx5hws_match_parameters *mask)
{
	int i;

	for (i = mask->match_sz / 4 - 1; i >= 0; i--)
		if (mask->match_buf[i])
			return i;

	return -1;
}

static bool hws_match_mask_is_empty(const struct mlx5hws_match_parameters *mask)
{
	return hws_get_last_set_dword_idx(mask) == -1;
}

static bool hws_dword_is_inner_ipaddr_off(int dword_off)
{
	/* IPv4 and IPv6 addresses share the same entry via a union, and the
	 * source and dest addresses are contiguous in the fte_match_param. So
	 * we need to check 8 words.
	 */
	static const int inner_ip_dword_off =
		__mlx5_dw_off(fte_match_param, inner_headers.src_ipv4_src_ipv6);

	return dword_off >= inner_ip_dword_off &&
	       dword_off < inner_ip_dword_off + 8;
}

static bool hws_dword_is_outer_ipaddr_off(int dword_off)
{
	static const int outer_ip_dword_off =
		__mlx5_dw_off(fte_match_param, outer_headers.src_ipv4_src_ipv6);

	return dword_off >= outer_ip_dword_off &&
	       dword_off < outer_ip_dword_off + 8;
}

static void hws_add_dword_to_mask(struct mlx5hws_match_parameters *mask,
				  const struct mlx5hws_match_parameters *orig,
				  int dword_idx, bool *added_inner_ipv,
				  bool *added_outer_ipv)
{
	mask->match_buf[dword_idx] |= orig->match_buf[dword_idx];

	*added_inner_ipv = false;
	*added_outer_ipv = false;

	/* Any IP address fragment must be accompanied by a match on IP version.
	 * Use the `added_ipv` variables to keep track if we added IP versions
	 * specifically for this dword, so that we can roll them back if the
	 * match params become too large to fit into a definer.
	 */
	if (hws_dword_is_inner_ipaddr_off(dword_idx) &&
	    !MLX5_GET(fte_match_param, mask->match_buf,
		      inner_headers.ip_version)) {
		MLX5_SET(fte_match_param, mask->match_buf,
			 inner_headers.ip_version, 0xf);
		*added_inner_ipv = true;
	}
	if (hws_dword_is_outer_ipaddr_off(dword_idx) &&
	    !MLX5_GET(fte_match_param, mask->match_buf,
		      outer_headers.ip_version)) {
		MLX5_SET(fte_match_param, mask->match_buf,
			 outer_headers.ip_version, 0xf);
		*added_outer_ipv = true;
	}
}

static void hws_remove_dword_from_mask(struct mlx5hws_match_parameters *mask,
				       int dword_idx, bool added_inner_ipv,
				       bool added_outer_ipv)
{
	mask->match_buf[dword_idx] = 0;
	if (added_inner_ipv)
		MLX5_SET(fte_match_param, mask->match_buf,
			 inner_headers.ip_version, 0);
	if (added_outer_ipv)
		MLX5_SET(fte_match_param, mask->match_buf,
			 outer_headers.ip_version, 0);
}

/* Avoid leaving a single lower dword in `mask` if there are others present in
 * `orig`. Splitting IPv6 addresses like this causes them to be interpreted as
 * IPv4.
 */
static void hws_avoid_ipv6_split_of(struct mlx5hws_match_parameters *orig,
				    struct mlx5hws_match_parameters *mask,
				    int off)
{
	/* Masks are allocated to a full fte_match_param, but it can't hurt to
	 * double check.
	 */
	if (orig->match_sz <= off + 3 || mask->match_sz <= off + 3)
		return;

	/* Lower dword is not set, nothing to do. */
	if (!mask->match_buf[off + 3])
		return;

	/* Higher dwords also present in `mask`, no ambiguity. */
	if (mask->match_buf[off] || mask->match_buf[off + 1] ||
	    mask->match_buf[off + 2])
		return;

	/* There are no higher dwords in `orig`, i.e. we match on IPv4. */
	if (!orig->match_buf[off] && !orig->match_buf[off + 1] &&
	    !orig->match_buf[off + 2])
		return;

	/* Put the lower dword back in `orig`. It is always safe to do this, the
	 * dword will just be picked up in the next submask.
	 */
	orig->match_buf[off + 3] = mask->match_buf[off + 3];
	mask->match_buf[off + 3] = 0;
}

static void hws_avoid_ipv6_split(struct mlx5hws_match_parameters *orig,
				 struct mlx5hws_match_parameters *mask)
{
	hws_avoid_ipv6_split_of(orig, mask,
				__mlx5_dw_off(fte_match_param,
					      outer_headers.src_ipv4_src_ipv6));
	hws_avoid_ipv6_split_of(orig, mask,
				__mlx5_dw_off(fte_match_param,
					      outer_headers.dst_ipv4_dst_ipv6));
	hws_avoid_ipv6_split_of(orig, mask,
				__mlx5_dw_off(fte_match_param,
					      inner_headers.src_ipv4_src_ipv6));
	hws_avoid_ipv6_split_of(orig, mask,
				__mlx5_dw_off(fte_match_param,
					      inner_headers.dst_ipv4_dst_ipv6));
}

/* Build a subset of the `orig` match parameters into `mask`. This subset is
 * guaranteed to fit in a single definer an as such is a candidate for being a
 * part of a complex matcher. Upon successful execution, the match params that
 * go into `mask` are cleared from `orig`.
 */
static int hws_get_simple_params(struct mlx5hws_context *ctx, u8 match_criteria,
				 struct mlx5hws_match_parameters *orig,
				 struct mlx5hws_match_parameters *mask)
{
	bool added_inner_ipv, added_outer_ipv;
	int dword_idx;
	u32 *backup;
	int ret;

	dword_idx = hws_get_last_set_dword_idx(orig);
	/* Nothing to do, we consumed all of the match params before. */
	if (dword_idx == -1)
		return 0;

	backup = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!backup)
		return -ENOMEM;

	while (1) {
		dword_idx = hws_get_last_set_dword_idx(orig);
		/* Nothing to do, we consumed all of the original match params
		 * into this subset, which still fits into a single matcher.
		 */
		if (dword_idx == -1) {
			ret = 0;
			goto free_backup;
		}

		memcpy(backup, mask->match_buf, mask->match_sz);

		/* Try to add this dword to the current subset. */
		hws_add_dword_to_mask(mask, orig, dword_idx, &added_inner_ipv,
				      &added_outer_ipv);

		if (hws_match_params_exceeds_definer(ctx, match_criteria, mask,
						     false)) {
			/* We just added a match param that makes the definer
			 * too large. Revert and return what we had before.
			 * Note that we can't just zero out the affected fields,
			 * because it's possible that the dword we're looking at
			 * wasn't zero before (e.g. it included auto-added
			 * matches in IP version. This is why we employ the
			 * rather cumbersome memcpy for backing up.
			 */
			memcpy(mask->match_buf, backup, mask->match_sz);
			/* Possible future improvement: We can't add any more
			 * dwords, but it may be possible to squeeze in
			 * individual bytes, as definers have special slots for
			 * those.
			 *
			 * For now, keep the code simple. This results in an
			 * extra submatcher in some cases, but it's good enough.
			 */
			ret = 0;
			break;
		}

		/* The current subset of match params still fits in a single
		 * definer. Remove the dword from the original mask.
		 *
		 * Also remove any explicit match on IP version if we just
		 * included one here. We will still automatically add it to
		 * accompany any IP address fragment, but do not need to
		 * consider it by itself.
		 */
		hws_remove_dword_from_mask(orig, dword_idx, added_inner_ipv,
					   added_outer_ipv);
	}

	/* Make sure we have not picked up a single lower dword of an IPv6
	 * address, as the firmware will erroneously treat it as an IPv4
	 * address.
	 */
	hws_avoid_ipv6_split(orig, mask);

free_backup:
	kfree(backup);

	return ret;
}

static int
hws_bwc_matcher_split_mask(struct mlx5hws_context *ctx, u8 match_criteria,
			   const struct mlx5hws_match_parameters *mask,
			   struct mlx5hws_match_parameters *submasks,
			   int *num_submasks)
{
	struct mlx5hws_match_parameters mask_copy;
	int ret, i = 0;

	mask_copy.match_sz = MLX5_ST_SZ_BYTES(fte_match_param);
	mask_copy.match_buf = kzalloc(mask_copy.match_sz, GFP_KERNEL);
	if (!mask_copy.match_buf)
		return -ENOMEM;

	memcpy(mask_copy.match_buf, mask->match_buf, mask->match_sz);

	while (!hws_match_mask_is_empty(&mask_copy)) {
		if (i >= MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS) {
			mlx5hws_err(ctx,
				    "Complex matcher: mask too large for %d matchers\n",
				    MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS);
			ret = -E2BIG;
			goto free_copy;
		}
		/* All but the first matcher need to match on register C6 to
		 * connect pieces of the complex rule together.
		 */
		if (i > 0) {
			MLX5_SET(fte_match_param, submasks[i].match_buf,
				 misc_parameters_2.metadata_reg_c_6, -1);
			match_criteria |= MLX5HWS_DEFINER_MATCH_CRITERIA_MISC2;
		}
		ret = hws_get_simple_params(ctx, match_criteria, &mask_copy,
					    &submasks[i]);
		if (ret < 0)
			goto free_copy;
		i++;
	}

	*num_submasks = i;
	ret = 0;

free_copy:
	kfree(mask_copy.match_buf);

	return ret;
}

static struct mlx5hws_table *
hws_isolated_table_create(const struct mlx5hws_bwc_matcher *cmatcher)
{
	struct mlx5hws_bwc_complex_submatcher *first_subm;
	struct mlx5hws_cmd_ft_modify_attr ft_attr = {0};
	struct mlx5hws_table_attr tbl_attr = {0};
	struct mlx5hws_table *orig_tbl;
	struct mlx5hws_context *ctx;
	struct mlx5hws_table *tbl;
	int ret;

	first_subm = &cmatcher->complex->submatchers[0];
	orig_tbl = first_subm->tbl;
	ctx = orig_tbl->ctx;

	tbl_attr.type = orig_tbl->type;
	tbl_attr.level = orig_tbl->level;
	tbl = mlx5hws_table_create(ctx, &tbl_attr);
	if (!tbl)
		return ERR_PTR(-EINVAL);

	/* Set the default miss of the isolated table to point
	 * to the end anchor of the original matcher.
	 */
	mlx5hws_cmd_set_attr_connect_miss_tbl(ctx, tbl->fw_ft_type,
					      tbl->type, &ft_attr);
	ft_attr.table_miss_id = first_subm->bwc_matcher->matcher->end_ft_id;

	ret = mlx5hws_cmd_flow_table_modify(ctx->mdev, &ft_attr, tbl->ft_id);
	if (ret) {
		mlx5hws_err(ctx, "Complex matcher: failed to set isolated tbl default miss\n");
		goto destroy_tbl;
	}

	return tbl;

destroy_tbl:
	mlx5hws_table_destroy(tbl);

	return ERR_PTR(ret);
}

static int hws_submatcher_init_first(struct mlx5hws_bwc_matcher *cmatcher,
				     struct mlx5hws_table *table, u32 priority,
				     u8 match_criteria,
				     struct mlx5hws_match_parameters *mask)
{
	enum mlx5hws_action_type action_types[HWS_NUM_CHAIN_ACTIONS];
	struct mlx5hws_bwc_complex_submatcher *subm;
	int ret;

	subm = &cmatcher->complex->submatchers[0];

	/* The first submatcher lives in the original table and does not have an
	 * associated jump to table action. It also points to the outer complex
	 * matcher.
	 */
	subm->tbl = table;
	subm->action_tbl = NULL;
	subm->bwc_matcher = cmatcher;

	action_types[0] = MLX5HWS_ACTION_TYP_MODIFY_HDR;
	action_types[1] = MLX5HWS_ACTION_TYP_TBL;
	action_types[2] = MLX5HWS_ACTION_TYP_LAST;

	ret = mlx5hws_bwc_matcher_create_simple(subm->bwc_matcher, subm->tbl,
						priority, match_criteria, mask,
						action_types);
	if (ret)
		return ret;

	subm->bwc_matcher->matcher_type = MLX5HWS_BWC_MATCHER_COMPLEX_FIRST;

	ret = rhashtable_init(&subm->rules_hash, &hws_rules_hash_params);
	if (ret)
		goto destroy_matcher;
	mutex_init(&subm->hash_lock);
	ida_init(&subm->chain_ida);

	return 0;

destroy_matcher:
	mlx5hws_bwc_matcher_destroy_simple(subm->bwc_matcher);

	return ret;
}

static int hws_submatcher_init(struct mlx5hws_bwc_matcher *cmatcher, int idx,
			       struct mlx5hws_table *table, u32 priority,
			       u8 match_criteria,
			       struct mlx5hws_match_parameters *mask)
{
	enum mlx5hws_action_type action_types[HWS_NUM_CHAIN_ACTIONS];
	struct mlx5hws_bwc_complex_submatcher *subm;
	bool is_last;
	int ret;

	if (!idx)
		return hws_submatcher_init_first(cmatcher, table, priority,
						 match_criteria, mask);

	subm = &cmatcher->complex->submatchers[idx];
	is_last = idx == cmatcher->complex->num_submatchers - 1;

	subm->tbl = hws_isolated_table_create(cmatcher);
	if (IS_ERR(subm->tbl))
		return PTR_ERR(subm->tbl);

	subm->action_tbl =
		mlx5hws_action_create_dest_table(subm->tbl->ctx, subm->tbl,
						 MLX5HWS_ACTION_FLAG_HWS_FDB);
	if (!subm->action_tbl) {
		ret = -EINVAL;
		goto destroy_tbl;
	}

	subm->bwc_matcher = kzalloc(sizeof(*subm->bwc_matcher), GFP_KERNEL);
	if (!subm->bwc_matcher) {
		ret = -ENOMEM;
		goto destroy_action;
	}

	/* Every matcher other than the first also matches of register C6 to
	 * bind subrules together in the complex rule using the chain ids.
	 */
	match_criteria |= MLX5HWS_DEFINER_MATCH_CRITERIA_MISC2;

	action_types[0] = MLX5HWS_ACTION_TYP_MODIFY_HDR;
	action_types[1] = MLX5HWS_ACTION_TYP_TBL;
	action_types[2] = MLX5HWS_ACTION_TYP_LAST;

	/* Every matcher other than the last sets register C6 and jumps to the
	 * next submatcher's table. The final submatcher will use the
	 * user-supplied actions and will attach an action template at rule
	 * insertion time.
	 */
	ret = mlx5hws_bwc_matcher_create_simple(subm->bwc_matcher, subm->tbl,
						priority, match_criteria, mask,
						is_last ? NULL : action_types);
	if (ret)
		goto free_matcher;

	subm->bwc_matcher->matcher_type =
		MLX5HWS_BWC_MATCHER_COMPLEX_SUBMATCHER;

	ret = rhashtable_init(&subm->rules_hash, &hws_rules_hash_params);
	if (ret)
		goto destroy_matcher;
	mutex_init(&subm->hash_lock);
	ida_init(&subm->chain_ida);

	return 0;

destroy_matcher:
	mlx5hws_bwc_matcher_destroy_simple(subm->bwc_matcher);
free_matcher:
	kfree(subm->bwc_matcher);
destroy_action:
	mlx5hws_action_destroy(subm->action_tbl);
destroy_tbl:
	mlx5hws_table_destroy(subm->tbl);

	return ret;
}

static void hws_submatcher_destroy(struct mlx5hws_bwc_matcher *cmatcher,
				   int idx)
{
	struct mlx5hws_bwc_complex_submatcher *subm;

	subm = &cmatcher->complex->submatchers[idx];

	ida_destroy(&subm->chain_ida);
	mutex_destroy(&subm->hash_lock);
	rhashtable_destroy(&subm->rules_hash);

	if (subm->bwc_matcher) {
		mlx5hws_bwc_matcher_destroy_simple(subm->bwc_matcher);
		if (idx)
			kfree(subm->bwc_matcher);
	}

	/* We own all of the isolated tables, but not the original one. */
	if (idx) {
		mlx5hws_action_destroy(subm->action_tbl);
		mlx5hws_table_destroy(subm->tbl);
	}
}

static int
hws_complex_data_actions_init(struct mlx5hws_bwc_matcher_complex_data *cdata)
{
	struct mlx5hws_context *ctx = cdata->submatchers[0].tbl->ctx;
	u8 modify_hdr_action[MLX5_ST_SZ_BYTES(set_action_in)] = {0};
	struct mlx5hws_action_mh_pattern ptrn;
	int ret = 0;

	/* Create modify header action to set REG_C_6 */
	MLX5_SET(set_action_in, modify_hdr_action,
		 action_type, MLX5_MODIFICATION_TYPE_SET);
	MLX5_SET(set_action_in, modify_hdr_action,
		 field, MLX5_MODI_META_REG_C_6);
	MLX5_SET(set_action_in, modify_hdr_action,
		 length, 0); /* zero means length of 32 */
	MLX5_SET(set_action_in, modify_hdr_action, offset, 0);
	MLX5_SET(set_action_in, modify_hdr_action, data, 0);

	ptrn.data = (void *)modify_hdr_action;
	ptrn.sz = MLX5HWS_ACTION_DOUBLE_SIZE;

	cdata->action_metadata =
		mlx5hws_action_create_modify_header(ctx, 1, &ptrn, 0,
						    MLX5HWS_ACTION_FLAG_HWS_FDB);
	if (!cdata->action_metadata) {
		mlx5hws_err(ctx, "Complex matcher: failed to create set reg C6 action\n");
		return -EINVAL;
	}

	/* Create last action */
	cdata->action_last =
		mlx5hws_action_create_last(ctx, MLX5HWS_ACTION_FLAG_HWS_FDB);
	if (!cdata->action_last) {
		mlx5hws_err(ctx, "Complex matcher: failed to create last action\n");
		ret = -EINVAL;
		goto destroy_action_metadata;
	}

	return 0;

destroy_action_metadata:
	mlx5hws_action_destroy(cdata->action_metadata);

	return ret;
}

static void
hws_complex_data_actions_destroy(struct mlx5hws_bwc_matcher_complex_data *cdata)
{
	mlx5hws_action_destroy(cdata->action_last);
	mlx5hws_action_destroy(cdata->action_metadata);
}

int mlx5hws_bwc_matcher_create_complex(struct mlx5hws_bwc_matcher *bwc_matcher,
				       struct mlx5hws_table *table,
				       u32 priority, u8 match_criteria_enable,
				       struct mlx5hws_match_parameters *mask)
{
	struct mlx5hws_match_parameters
		submasks[MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS] = {0};
	struct mlx5hws_bwc_matcher_complex_data *cdata;
	struct mlx5hws_context *ctx = table->ctx;
	int num_submatchers;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(submasks); i++) {
		submasks[i].match_sz = MLX5_ST_SZ_BYTES(fte_match_param);
		submasks[i].match_buf = kzalloc(submasks[i].match_sz,
						GFP_KERNEL);
		if (!submasks[i].match_buf) {
			ret = -ENOMEM;
			goto free_submasks;
		}
	}

	ret = hws_bwc_matcher_split_mask(ctx, match_criteria_enable, mask,
					 submasks, &num_submatchers);
	if (ret)
		goto free_submasks;

	cdata = kzalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata) {
		ret = -ENOMEM;
		goto free_submasks;
	}

	bwc_matcher->complex = cdata;
	cdata->num_submatchers = num_submatchers;

	for (i = 0; i < num_submatchers; i++) {
		ret = hws_submatcher_init(bwc_matcher, i, table, priority,
					  match_criteria_enable, &submasks[i]);
		if (ret)
			goto destroy_submatchers;
	}

	ret = hws_complex_data_actions_init(cdata);
	if (ret)
		goto destroy_submatchers;

	ret = 0;
	goto free_submasks;

destroy_submatchers:
	while (i--)
		hws_submatcher_destroy(bwc_matcher, i);
	kfree(cdata);
	bwc_matcher->complex = NULL;

free_submasks:
	for (i = 0; i < ARRAY_SIZE(submasks); i++)
		kfree(submasks[i].match_buf);

	return ret;
}

void
mlx5hws_bwc_matcher_destroy_complex(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	int i;

	hws_complex_data_actions_destroy(bwc_matcher->complex);
	for (i = 0; i < bwc_matcher->complex->num_submatchers; i++)
		hws_submatcher_destroy(bwc_matcher, i);
	kfree(bwc_matcher->complex);
	bwc_matcher->complex = NULL;
}

static int
hws_complex_get_subrule_data(struct mlx5hws_bwc_rule *bwc_rule,
			     struct mlx5hws_bwc_complex_submatcher *subm,
			     u32 *match_params)
__must_hold(&subm->hash_lock)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = subm->bwc_matcher;
	struct mlx5hws_bwc_complex_subrule_data *sr_data, *old_data;
	struct mlx5hws_match_template *mt;
	int ret;

	sr_data = kzalloc(sizeof(*sr_data), GFP_KERNEL);
	if (!sr_data)
		return -ENOMEM;

	ret = ida_alloc(&subm->chain_ida, GFP_KERNEL);
	if (ret < 0)
		goto free_sr_data;
	sr_data->chain_id = ret;

	refcount_set(&sr_data->refcount, 1);

	mt  = bwc_matcher->matcher->mt;
	mlx5hws_definer_create_tag(match_params, mt->fc, mt->fc_sz,
				   (u8 *)&sr_data->match_tag);

	old_data = rhashtable_lookup_get_insert_fast(&subm->rules_hash,
						     &sr_data->hash_node,
						     hws_rules_hash_params);
	if (IS_ERR(old_data)) {
		ret = PTR_ERR(old_data);
		goto free_ida;
	}

	if (old_data) {
		/* Rule with the same tag already exists - update refcount */
		refcount_inc(&old_data->refcount);
		/* Let the new rule use the same tag as the existing rule.
		 * Note that we don't have any indication for the rule creation
		 * process that a rule with similar matching params already
		 * exists - no harm done when this rule is be overwritten by
		 * the same STE.
		 * There's some performance advantage in skipping such cases,
		 * so this is left for future optimizations.
		 */
		bwc_rule->subrule_data = old_data;
		ret = 0;
		goto free_ida;
	}

	bwc_rule->subrule_data = sr_data;
	return 0;

free_ida:
	ida_free(&subm->chain_ida, sr_data->chain_id);
free_sr_data:
	kfree(sr_data);

	return ret;
}

static void
hws_complex_put_subrule_data(struct mlx5hws_bwc_rule *bwc_rule,
			     struct mlx5hws_bwc_complex_submatcher *subm,
			     bool *is_last_rule)
__must_hold(&subm->hash_lock)
{
	struct mlx5hws_bwc_complex_subrule_data *sr_data;

	if (is_last_rule)
		*is_last_rule = false;

	sr_data = bwc_rule->subrule_data;
	if (refcount_dec_and_test(&sr_data->refcount)) {
		rhashtable_remove_fast(&subm->rules_hash,
				       &sr_data->hash_node,
				       hws_rules_hash_params);
		ida_free(&subm->chain_ida, sr_data->chain_id);
		kfree(sr_data);
		if (is_last_rule)
			*is_last_rule = true;
	}

	bwc_rule->subrule_data = NULL;
}

static int hws_complex_subrule_create(struct mlx5hws_bwc_matcher *cmatcher,
				      struct mlx5hws_bwc_rule *subrule,
				      u32 *match_params, u32 flow_source,
				      int bwc_queue_idx, int subm_idx,
				      struct mlx5hws_rule_action *actions,
				      u32 *chain_id)
{
	struct mlx5hws_rule_action chain_actions[HWS_NUM_CHAIN_ACTIONS] = {0};
	u8 modify_hdr_action[MLX5_ST_SZ_BYTES(set_action_in)] = {0};
	struct mlx5hws_bwc_matcher_complex_data *cdata;
	struct mlx5hws_bwc_complex_submatcher *subm;
	int ret;

	cdata = cmatcher->complex;
	subm = &cdata->submatchers[subm_idx];

	mutex_lock(&subm->hash_lock);

	ret = hws_complex_get_subrule_data(subrule, subm, match_params);
	if (ret)
		goto unlock;

	*chain_id = subrule->subrule_data->chain_id;

	if (!actions) {
		MLX5_SET(set_action_in, modify_hdr_action, data, *chain_id);
		chain_actions[0].action = cdata->action_metadata;
		chain_actions[0].modify_header.data = modify_hdr_action;
		chain_actions[1].action =
			cdata->submatchers[subm_idx + 1].action_tbl;
		chain_actions[2].action = cdata->action_last;
		actions = chain_actions;
	}

	ret = mlx5hws_bwc_rule_create_simple(subrule, match_params, actions,
					     flow_source, bwc_queue_idx);
	if (ret)
		goto put_subrule_data;

	ret = 0;
	goto unlock;

put_subrule_data:
	hws_complex_put_subrule_data(subrule, subm, NULL);
unlock:
	mutex_unlock(&subm->hash_lock);

	return ret;
}

static int hws_complex_subrule_destroy(struct mlx5hws_bwc_rule *bwc_rule,
				       struct mlx5hws_bwc_matcher *cmatcher,
				       int subm_idx)
{
	struct mlx5hws_bwc_matcher_complex_data *cdata;
	struct mlx5hws_bwc_complex_submatcher *subm;
	struct mlx5hws_context *ctx;
	bool is_last_rule;
	int ret = 0;

	cdata = cmatcher->complex;
	subm = &cdata->submatchers[subm_idx];
	ctx = subm->tbl->ctx;

	mutex_lock(&subm->hash_lock);

	hws_complex_put_subrule_data(bwc_rule, subm, &is_last_rule);
	bwc_rule->rule->skip_delete = !is_last_rule;
	ret = mlx5hws_bwc_rule_destroy_simple(bwc_rule);
	if (unlikely(ret))
		mlx5hws_err(ctx,
			    "Complex rule: failed to delete subrule %d (%d)\n",
			    subm_idx, ret);

	if (subm_idx)
		mlx5hws_bwc_rule_free(bwc_rule);

	mutex_unlock(&subm->hash_lock);

	return ret;
}

int mlx5hws_bwc_rule_create_complex(struct mlx5hws_bwc_rule *bwc_rule,
				    struct mlx5hws_match_parameters *params,
				    u32 flow_source,
				    struct mlx5hws_rule_action rule_actions[],
				    u16 bwc_queue_idx)
{
	struct mlx5hws_bwc_rule
		*subrules[MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS] = {0};
	struct mlx5hws_bwc_matcher *cmatcher = bwc_rule->bwc_matcher;
	struct mlx5hws_bwc_matcher_complex_data *cdata;
	struct mlx5hws_rule_action *subrule_actions;
	struct mlx5hws_bwc_complex_submatcher *subm;
	struct mlx5hws_bwc_rule *subrule;
	u32 *match_params;
	u32 chain_id;
	int i, ret;

	cdata = cmatcher->complex;
	if (!cdata)
		return -EINVAL;

	/* Duplicate user data because we will modify it to set register C6
	 * values. For the same reason, make sure that we allocate a full
	 * match_param even if the user gave us fewer bytes. We need to ensure
	 * there is space for the match on C6.
	 */
	match_params = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!match_params)
		return -ENOMEM;

	memcpy(match_params, params->match_buf, params->match_sz);

	ret = hws_complex_subrule_create(cmatcher, bwc_rule, match_params,
					 flow_source, bwc_queue_idx, 0,
					 NULL, &chain_id);
	if (ret)
		goto free_match_params;
	subrules[0] = bwc_rule;

	for (i = 1; i < cdata->num_submatchers; i++) {
		subm = &cdata->submatchers[i];
		subrule = mlx5hws_bwc_rule_alloc(subm->bwc_matcher);
		if (!subrule) {
			ret = -ENOMEM;
			goto destroy_subrules;
		}

		/* Match on the previous subrule's chain_id. This is how
		 * subrules are connected in steering.
		 */
		MLX5_SET(fte_match_param, match_params,
			 misc_parameters_2.metadata_reg_c_6, chain_id);

		/* The last subrule uses the complex rule's user-specified
		 * actions. Everything else uses the chaining rules based on the
		 * next table and chain_id.
		 */
		subrule_actions =
			i == cdata->num_submatchers - 1 ? rule_actions : NULL;

		ret = hws_complex_subrule_create(cmatcher, subrule,
						 match_params, flow_source,
						 bwc_queue_idx, i,
						 subrule_actions, &chain_id);
		if (ret) {
			mlx5hws_bwc_rule_free(subrule);
			goto destroy_subrules;
		}

		subrules[i] = subrule;
	}

	for (i = 0; i < cdata->num_submatchers - 1; i++)
		subrules[i]->next_subrule = subrules[i + 1];

	kfree(match_params);

	return 0;

destroy_subrules:
	while (i--)
		hws_complex_subrule_destroy(subrules[i], cmatcher, i);
free_match_params:
	kfree(match_params);

	return ret;
}

int mlx5hws_bwc_rule_destroy_complex(struct mlx5hws_bwc_rule *bwc_rule)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_bwc_rule
		*subrules[MLX5HWS_BWC_COMPLEX_MAX_SUBMATCHERS] = {0};
	struct mlx5hws_bwc_matcher_complex_data *cdata;
	int i, err, ret_val;

	cdata = bwc_matcher->complex;

	/* Construct a list of all the subrules we need to destroy. */
	subrules[0] = bwc_rule;
	for (i = 1; i < cdata->num_submatchers; i++)
		subrules[i] = subrules[i - 1]->next_subrule;

	ret_val = 0;
	for (i = 0; i < cdata->num_submatchers; i++) {
		err = hws_complex_subrule_destroy(subrules[i], bwc_matcher, i);
		/* If something goes wrong, plow along to destroy all of the
		 * subrules but return an error upstack.
		 */
		if (unlikely(err))
			ret_val = err;
	}

	return ret_val;
}

static void
hws_bwc_matcher_init_move(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mlx5hws_bwc_rule *bwc_rule;
	struct list_head *rules_list;
	int i;

	for (i = 0; i < bwc_queues; i++) {
		rules_list = &bwc_matcher->rules[i];
		if (list_empty(rules_list))
			continue;

		list_for_each_entry(bwc_rule, rules_list, list_node) {
			if (!bwc_rule->subrule_data)
				continue;
			bwc_rule->subrule_data->was_moved = false;
		}
	}
}

int mlx5hws_bwc_matcher_complex_move(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_matcher *matcher = bwc_matcher->matcher;
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mlx5hws_bwc_rule *tmp_bwc_rule;
	struct mlx5hws_rule_attr rule_attr;
	int move_error = 0, poll_error = 0;
	struct mlx5hws_rule *tmp_rule;
	struct list_head *rules_list;
	u32 expected_completions = 1;
	int i, ret = 0;

	hws_bwc_matcher_init_move(bwc_matcher);

	mlx5hws_bwc_rule_fill_attr(bwc_matcher, 0, 0, &rule_attr);

	for (i = 0; i < bwc_queues; i++) {
		rules_list = &bwc_matcher->rules[i];
		if (list_empty(rules_list))
			continue;

		rule_attr.queue_id = mlx5hws_bwc_get_queue_id(ctx, i);

		list_for_each_entry(tmp_bwc_rule, rules_list, list_node) {
			/* Check if a rule with similar tag has already
			 * been moved.
			 */
			if (tmp_bwc_rule->subrule_data->was_moved) {
				/* This rule is a duplicate of rule with
				 * identical tag that has already been moved
				 * earlier. Just update this rule's RTCs.
				 */
				tmp_bwc_rule->rule->rtc_0 =
					tmp_bwc_rule->subrule_data->rtc_0;
				tmp_bwc_rule->rule->rtc_1 =
					tmp_bwc_rule->subrule_data->rtc_1;
				tmp_bwc_rule->rule->matcher =
					tmp_bwc_rule->rule->matcher->resize_dst;
				continue;
			}

			/* First time we're moving rule with this tag.
			 * Move it for real.
			 */
			tmp_rule = tmp_bwc_rule->rule;
			tmp_rule->skip_delete = false;
			ret = mlx5hws_matcher_resize_rule_move(matcher,
							       tmp_rule,
							       &rule_attr);
			if (unlikely(ret)) {
				if (!move_error) {
					mlx5hws_err(ctx,
						    "Moving complex BWC rule: move failed (%d), attempting to move rest of the rules\n",
						    ret);
					move_error = ret;
				}
				/* Rule wasn't queued, no need to poll */
				continue;
			}

			expected_completions = 1;
			ret = mlx5hws_bwc_queue_poll(ctx,
						     rule_attr.queue_id,
						     &expected_completions,
						     true);
			if (unlikely(ret)) {
				if (ret == -ETIMEDOUT) {
					mlx5hws_err(ctx,
						    "Moving complex BWC rule: timeout polling for completions (%d), aborting rehash\n",
						    ret);
					return ret;
				}
				if (!poll_error) {
					mlx5hws_err(ctx,
						    "Moving complex BWC rule: polling for completions failed (%d), attempting to move rest of the rules\n",
						    ret);
					poll_error = ret;
				}
			}

			/* Done moving the rule to the new matcher,
			 * now update RTCs for all the duplicated rules.
			 */
			tmp_bwc_rule->subrule_data->rtc_0 =
				tmp_bwc_rule->rule->rtc_0;
			tmp_bwc_rule->subrule_data->rtc_1 =
				tmp_bwc_rule->rule->rtc_1;

			tmp_bwc_rule->subrule_data->was_moved = true;
		}
	}

	/* Return the first error that happened */
	if (unlikely(move_error))
		return move_error;
	if (unlikely(poll_error))
		return poll_error;

	return ret;
}

int
mlx5hws_bwc_matcher_complex_move_first(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_bwc_matcher_complex_data *cdata;
	struct mlx5hws_table *isolated_tbl;
	u32 end_ft_id;
	int i, ret;

	cdata = bwc_matcher->complex;

	/* We are rehashing the first submatcher. We need to update the
	 * subsequent submatchers to point to the end_ft of this new matcher.
	 * This needs to be done before moving any rules to prevent possible
	 * steering loops.
	 */
	end_ft_id = bwc_matcher->matcher->resize_dst->end_ft_id;
	for (i = 1; i < cdata->num_submatchers; i++) {
		isolated_tbl = cdata->submatchers[i].tbl;
		ret = mlx5hws_matcher_update_end_ft_isolated(isolated_tbl,
							     end_ft_id);
		if (ret) {
			mlx5hws_err(ctx,
				    "Complex matcher: failed updating end_ft of isolated matcher (%d)\n",
				    ret);
			return ret;
		}
	}

	return mlx5hws_bwc_matcher_complex_move(bwc_matcher);
}
