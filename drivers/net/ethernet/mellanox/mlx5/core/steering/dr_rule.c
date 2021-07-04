// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

#define DR_RULE_MAX_STE_CHAIN (DR_RULE_MAX_STES + DR_ACTION_MAX_STES)

struct mlx5dr_rule_action_member {
	struct mlx5dr_action *action;
	struct list_head list;
};

static int dr_rule_append_to_miss_list(struct mlx5dr_ste_ctx *ste_ctx,
				       struct mlx5dr_ste *new_last_ste,
				       struct list_head *miss_list,
				       struct list_head *send_list)
{
	struct mlx5dr_ste_send_info *ste_info_last;
	struct mlx5dr_ste *last_ste;

	/* The new entry will be inserted after the last */
	last_ste = list_last_entry(miss_list, struct mlx5dr_ste, miss_list_node);
	WARN_ON(!last_ste);

	ste_info_last = kzalloc(sizeof(*ste_info_last), GFP_KERNEL);
	if (!ste_info_last)
		return -ENOMEM;

	mlx5dr_ste_set_miss_addr(ste_ctx, last_ste->hw_ste,
				 mlx5dr_ste_get_icm_addr(new_last_ste));
	list_add_tail(&new_last_ste->miss_list_node, miss_list);

	mlx5dr_send_fill_and_append_ste_send_info(last_ste, DR_STE_SIZE_CTRL,
						  0, last_ste->hw_ste,
						  ste_info_last, send_list, true);

	return 0;
}

static struct mlx5dr_ste *
dr_rule_create_collision_htbl(struct mlx5dr_matcher *matcher,
			      struct mlx5dr_matcher_rx_tx *nic_matcher,
			      u8 *hw_ste)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_ctx *ste_ctx = dmn->ste_ctx;
	struct mlx5dr_ste_htbl *new_htbl;
	struct mlx5dr_ste *ste;

	/* Create new table for miss entry */
	new_htbl = mlx5dr_ste_htbl_alloc(dmn->ste_icm_pool,
					 DR_CHUNK_SIZE_1,
					 MLX5DR_STE_LU_TYPE_DONT_CARE,
					 0);
	if (!new_htbl) {
		mlx5dr_dbg(dmn, "Failed allocating collision table\n");
		return NULL;
	}

	/* One and only entry, never grows */
	ste = new_htbl->ste_arr;
	mlx5dr_ste_set_miss_addr(ste_ctx, hw_ste,
				 nic_matcher->e_anchor->chunk->icm_addr);
	mlx5dr_htbl_get(new_htbl);

	return ste;
}

static struct mlx5dr_ste *
dr_rule_create_collision_entry(struct mlx5dr_matcher *matcher,
			       struct mlx5dr_matcher_rx_tx *nic_matcher,
			       u8 *hw_ste,
			       struct mlx5dr_ste *orig_ste)
{
	struct mlx5dr_ste *ste;

	ste = dr_rule_create_collision_htbl(matcher, nic_matcher, hw_ste);
	if (!ste) {
		mlx5dr_dbg(matcher->tbl->dmn, "Failed creating collision entry\n");
		return NULL;
	}

	ste->ste_chain_location = orig_ste->ste_chain_location;

	/* In collision entry, all members share the same miss_list_head */
	ste->htbl->miss_list = mlx5dr_ste_get_miss_list(orig_ste);

	/* Next table */
	if (mlx5dr_ste_create_next_htbl(matcher, nic_matcher, ste, hw_ste,
					DR_CHUNK_SIZE_1)) {
		mlx5dr_dbg(matcher->tbl->dmn, "Failed allocating table\n");
		goto free_tbl;
	}

	return ste;

free_tbl:
	mlx5dr_ste_free(ste, matcher, nic_matcher);
	return NULL;
}

static int
dr_rule_handle_one_ste_in_update_list(struct mlx5dr_ste_send_info *ste_info,
				      struct mlx5dr_domain *dmn)
{
	int ret;

	list_del(&ste_info->send_list);

	/* Copy data to ste, only reduced size or control, the last 16B (mask)
	 * is already written to the hw.
	 */
	if (ste_info->size == DR_STE_SIZE_CTRL)
		memcpy(ste_info->ste->hw_ste, ste_info->data, DR_STE_SIZE_CTRL);
	else
		memcpy(ste_info->ste->hw_ste, ste_info->data, DR_STE_SIZE_REDUCED);

	ret = mlx5dr_send_postsend_ste(dmn, ste_info->ste, ste_info->data,
				       ste_info->size, ste_info->offset);
	if (ret)
		goto out;

out:
	kfree(ste_info);
	return ret;
}

static int dr_rule_send_update_list(struct list_head *send_ste_list,
				    struct mlx5dr_domain *dmn,
				    bool is_reverse)
{
	struct mlx5dr_ste_send_info *ste_info, *tmp_ste_info;
	int ret;

	if (is_reverse) {
		list_for_each_entry_safe_reverse(ste_info, tmp_ste_info,
						 send_ste_list, send_list) {
			ret = dr_rule_handle_one_ste_in_update_list(ste_info,
								    dmn);
			if (ret)
				return ret;
		}
	} else {
		list_for_each_entry_safe(ste_info, tmp_ste_info,
					 send_ste_list, send_list) {
			ret = dr_rule_handle_one_ste_in_update_list(ste_info,
								    dmn);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static struct mlx5dr_ste *
dr_rule_find_ste_in_miss_list(struct list_head *miss_list, u8 *hw_ste)
{
	struct mlx5dr_ste *ste;

	if (list_empty(miss_list))
		return NULL;

	/* Check if hw_ste is present in the list */
	list_for_each_entry(ste, miss_list, miss_list_node) {
		if (mlx5dr_ste_equal_tag(ste->hw_ste, hw_ste))
			return ste;
	}

	return NULL;
}

static struct mlx5dr_ste *
dr_rule_rehash_handle_collision(struct mlx5dr_matcher *matcher,
				struct mlx5dr_matcher_rx_tx *nic_matcher,
				struct list_head *update_list,
				struct mlx5dr_ste *col_ste,
				u8 *hw_ste)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste *new_ste;
	int ret;

	new_ste = dr_rule_create_collision_htbl(matcher, nic_matcher, hw_ste);
	if (!new_ste)
		return NULL;

	/* In collision entry, all members share the same miss_list_head */
	new_ste->htbl->miss_list = mlx5dr_ste_get_miss_list(col_ste);

	/* Update the previous from the list */
	ret = dr_rule_append_to_miss_list(dmn->ste_ctx, new_ste,
					  mlx5dr_ste_get_miss_list(col_ste),
					  update_list);
	if (ret) {
		mlx5dr_dbg(dmn, "Failed update dup entry\n");
		goto err_exit;
	}

	return new_ste;

err_exit:
	mlx5dr_ste_free(new_ste, matcher, nic_matcher);
	return NULL;
}

static void dr_rule_rehash_copy_ste_ctrl(struct mlx5dr_matcher *matcher,
					 struct mlx5dr_matcher_rx_tx *nic_matcher,
					 struct mlx5dr_ste *cur_ste,
					 struct mlx5dr_ste *new_ste)
{
	new_ste->next_htbl = cur_ste->next_htbl;
	new_ste->ste_chain_location = cur_ste->ste_chain_location;

	if (!mlx5dr_ste_is_last_in_rule(nic_matcher, new_ste->ste_chain_location))
		new_ste->next_htbl->pointing_ste = new_ste;

	/* We need to copy the refcount since this ste
	 * may have been traversed several times
	 */
	new_ste->refcount = cur_ste->refcount;

	/* Link old STEs rule_mem list to the new ste */
	mlx5dr_rule_update_rule_member(cur_ste, new_ste);
	INIT_LIST_HEAD(&new_ste->rule_list);
	list_splice_tail_init(&cur_ste->rule_list, &new_ste->rule_list);
}

static struct mlx5dr_ste *
dr_rule_rehash_copy_ste(struct mlx5dr_matcher *matcher,
			struct mlx5dr_matcher_rx_tx *nic_matcher,
			struct mlx5dr_ste *cur_ste,
			struct mlx5dr_ste_htbl *new_htbl,
			struct list_head *update_list)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_send_info *ste_info;
	bool use_update_list = false;
	u8 hw_ste[DR_STE_SIZE] = {};
	struct mlx5dr_ste *new_ste;
	int new_idx;
	u8 sb_idx;

	/* Copy STE mask from the matcher */
	sb_idx = cur_ste->ste_chain_location - 1;
	mlx5dr_ste_set_bit_mask(hw_ste, nic_matcher->ste_builder[sb_idx].bit_mask);

	/* Copy STE control and tag */
	memcpy(hw_ste, cur_ste->hw_ste, DR_STE_SIZE_REDUCED);
	mlx5dr_ste_set_miss_addr(dmn->ste_ctx, hw_ste,
				 nic_matcher->e_anchor->chunk->icm_addr);

	new_idx = mlx5dr_ste_calc_hash_index(hw_ste, new_htbl);
	new_ste = &new_htbl->ste_arr[new_idx];

	if (mlx5dr_ste_is_not_used(new_ste)) {
		mlx5dr_htbl_get(new_htbl);
		list_add_tail(&new_ste->miss_list_node,
			      mlx5dr_ste_get_miss_list(new_ste));
	} else {
		new_ste = dr_rule_rehash_handle_collision(matcher,
							  nic_matcher,
							  update_list,
							  new_ste,
							  hw_ste);
		if (!new_ste) {
			mlx5dr_dbg(dmn, "Failed adding collision entry, index: %d\n",
				   new_idx);
			return NULL;
		}
		new_htbl->ctrl.num_of_collisions++;
		use_update_list = true;
	}

	memcpy(new_ste->hw_ste, hw_ste, DR_STE_SIZE_REDUCED);

	new_htbl->ctrl.num_of_valid_entries++;

	if (use_update_list) {
		ste_info = kzalloc(sizeof(*ste_info), GFP_KERNEL);
		if (!ste_info)
			goto err_exit;

		mlx5dr_send_fill_and_append_ste_send_info(new_ste, DR_STE_SIZE, 0,
							  hw_ste, ste_info,
							  update_list, true);
	}

	dr_rule_rehash_copy_ste_ctrl(matcher, nic_matcher, cur_ste, new_ste);

	return new_ste;

err_exit:
	mlx5dr_ste_free(new_ste, matcher, nic_matcher);
	return NULL;
}

static int dr_rule_rehash_copy_miss_list(struct mlx5dr_matcher *matcher,
					 struct mlx5dr_matcher_rx_tx *nic_matcher,
					 struct list_head *cur_miss_list,
					 struct mlx5dr_ste_htbl *new_htbl,
					 struct list_head *update_list)
{
	struct mlx5dr_ste *tmp_ste, *cur_ste, *new_ste;

	if (list_empty(cur_miss_list))
		return 0;

	list_for_each_entry_safe(cur_ste, tmp_ste, cur_miss_list, miss_list_node) {
		new_ste = dr_rule_rehash_copy_ste(matcher,
						  nic_matcher,
						  cur_ste,
						  new_htbl,
						  update_list);
		if (!new_ste)
			goto err_insert;

		list_del(&cur_ste->miss_list_node);
		mlx5dr_htbl_put(cur_ste->htbl);
	}
	return 0;

err_insert:
	mlx5dr_err(matcher->tbl->dmn, "Fatal error during resize\n");
	WARN_ON(true);
	return -EINVAL;
}

static int dr_rule_rehash_copy_htbl(struct mlx5dr_matcher *matcher,
				    struct mlx5dr_matcher_rx_tx *nic_matcher,
				    struct mlx5dr_ste_htbl *cur_htbl,
				    struct mlx5dr_ste_htbl *new_htbl,
				    struct list_head *update_list)
{
	struct mlx5dr_ste *cur_ste;
	int cur_entries;
	int err = 0;
	int i;

	cur_entries = mlx5dr_icm_pool_chunk_size_to_entries(cur_htbl->chunk_size);

	if (cur_entries < 1) {
		mlx5dr_dbg(matcher->tbl->dmn, "Invalid number of entries\n");
		return -EINVAL;
	}

	for (i = 0; i < cur_entries; i++) {
		cur_ste = &cur_htbl->ste_arr[i];
		if (mlx5dr_ste_is_not_used(cur_ste)) /* Empty, nothing to copy */
			continue;

		err = dr_rule_rehash_copy_miss_list(matcher,
						    nic_matcher,
						    mlx5dr_ste_get_miss_list(cur_ste),
						    new_htbl,
						    update_list);
		if (err)
			goto clean_copy;
	}

clean_copy:
	return err;
}

static struct mlx5dr_ste_htbl *
dr_rule_rehash_htbl(struct mlx5dr_rule *rule,
		    struct mlx5dr_rule_rx_tx *nic_rule,
		    struct mlx5dr_ste_htbl *cur_htbl,
		    u8 ste_location,
		    struct list_head *update_list,
		    enum mlx5dr_icm_chunk_size new_size)
{
	struct mlx5dr_ste_send_info *del_ste_info, *tmp_ste_info;
	struct mlx5dr_matcher *matcher = rule->matcher;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_matcher_rx_tx *nic_matcher;
	struct mlx5dr_ste_send_info *ste_info;
	struct mlx5dr_htbl_connect_info info;
	struct mlx5dr_domain_rx_tx *nic_dmn;
	u8 formatted_ste[DR_STE_SIZE] = {};
	LIST_HEAD(rehash_table_send_list);
	struct mlx5dr_ste *ste_to_update;
	struct mlx5dr_ste_htbl *new_htbl;
	int err;

	nic_matcher = nic_rule->nic_matcher;
	nic_dmn = nic_matcher->nic_tbl->nic_dmn;

	ste_info = kzalloc(sizeof(*ste_info), GFP_KERNEL);
	if (!ste_info)
		return NULL;

	new_htbl = mlx5dr_ste_htbl_alloc(dmn->ste_icm_pool,
					 new_size,
					 cur_htbl->lu_type,
					 cur_htbl->byte_mask);
	if (!new_htbl) {
		mlx5dr_err(dmn, "Failed to allocate new hash table\n");
		goto free_ste_info;
	}

	/* Write new table to HW */
	info.type = CONNECT_MISS;
	info.miss_icm_addr = nic_matcher->e_anchor->chunk->icm_addr;
	mlx5dr_ste_set_formatted_ste(dmn->ste_ctx,
				     dmn->info.caps.gvmi,
				     nic_dmn->type,
				     new_htbl,
				     formatted_ste,
				     &info);

	new_htbl->pointing_ste = cur_htbl->pointing_ste;
	new_htbl->pointing_ste->next_htbl = new_htbl;
	err = dr_rule_rehash_copy_htbl(matcher,
				       nic_matcher,
				       cur_htbl,
				       new_htbl,
				       &rehash_table_send_list);
	if (err)
		goto free_new_htbl;

	if (mlx5dr_send_postsend_htbl(dmn, new_htbl, formatted_ste,
				      nic_matcher->ste_builder[ste_location - 1].bit_mask)) {
		mlx5dr_err(dmn, "Failed writing table to HW\n");
		goto free_new_htbl;
	}

	/* Writing to the hw is done in regular order of rehash_table_send_list,
	 * in order to have the origin data written before the miss address of
	 * collision entries, if exists.
	 */
	if (dr_rule_send_update_list(&rehash_table_send_list, dmn, false)) {
		mlx5dr_err(dmn, "Failed updating table to HW\n");
		goto free_ste_list;
	}

	/* Connect previous hash table to current */
	if (ste_location == 1) {
		/* The previous table is an anchor, anchors size is always one STE */
		struct mlx5dr_ste_htbl *prev_htbl = cur_htbl->pointing_ste->htbl;

		/* On matcher s_anchor we keep an extra refcount */
		mlx5dr_htbl_get(new_htbl);
		mlx5dr_htbl_put(cur_htbl);

		nic_matcher->s_htbl = new_htbl;

		/* It is safe to operate dr_ste_set_hit_addr on the hw_ste here
		 * (48B len) which works only on first 32B
		 */
		mlx5dr_ste_set_hit_addr(dmn->ste_ctx,
					prev_htbl->ste_arr[0].hw_ste,
					new_htbl->chunk->icm_addr,
					new_htbl->chunk->num_of_entries);

		ste_to_update = &prev_htbl->ste_arr[0];
	} else {
		mlx5dr_ste_set_hit_addr_by_next_htbl(dmn->ste_ctx,
						     cur_htbl->pointing_ste->hw_ste,
						     new_htbl);
		ste_to_update = cur_htbl->pointing_ste;
	}

	mlx5dr_send_fill_and_append_ste_send_info(ste_to_update, DR_STE_SIZE_CTRL,
						  0, ste_to_update->hw_ste, ste_info,
						  update_list, false);

	return new_htbl;

free_ste_list:
	/* Clean all ste_info's from the new table */
	list_for_each_entry_safe(del_ste_info, tmp_ste_info,
				 &rehash_table_send_list, send_list) {
		list_del(&del_ste_info->send_list);
		kfree(del_ste_info);
	}

free_new_htbl:
	mlx5dr_ste_htbl_free(new_htbl);
free_ste_info:
	kfree(ste_info);
	mlx5dr_info(dmn, "Failed creating rehash table\n");
	return NULL;
}

static struct mlx5dr_ste_htbl *dr_rule_rehash(struct mlx5dr_rule *rule,
					      struct mlx5dr_rule_rx_tx *nic_rule,
					      struct mlx5dr_ste_htbl *cur_htbl,
					      u8 ste_location,
					      struct list_head *update_list)
{
	struct mlx5dr_domain *dmn = rule->matcher->tbl->dmn;
	enum mlx5dr_icm_chunk_size new_size;

	new_size = mlx5dr_icm_next_higher_chunk(cur_htbl->chunk_size);
	new_size = min_t(u32, new_size, dmn->info.max_log_sw_icm_sz);

	if (new_size == cur_htbl->chunk_size)
		return NULL; /* Skip rehash, we already at the max size */

	return dr_rule_rehash_htbl(rule, nic_rule, cur_htbl, ste_location,
				   update_list, new_size);
}

static struct mlx5dr_ste *
dr_rule_handle_collision(struct mlx5dr_matcher *matcher,
			 struct mlx5dr_matcher_rx_tx *nic_matcher,
			 struct mlx5dr_ste *ste,
			 u8 *hw_ste,
			 struct list_head *miss_list,
			 struct list_head *send_list)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_ctx *ste_ctx = dmn->ste_ctx;
	struct mlx5dr_ste_send_info *ste_info;
	struct mlx5dr_ste *new_ste;

	ste_info = kzalloc(sizeof(*ste_info), GFP_KERNEL);
	if (!ste_info)
		return NULL;

	new_ste = dr_rule_create_collision_entry(matcher, nic_matcher, hw_ste, ste);
	if (!new_ste)
		goto free_send_info;

	if (dr_rule_append_to_miss_list(ste_ctx, new_ste,
					miss_list, send_list)) {
		mlx5dr_dbg(dmn, "Failed to update prev miss_list\n");
		goto err_exit;
	}

	mlx5dr_send_fill_and_append_ste_send_info(new_ste, DR_STE_SIZE, 0, hw_ste,
						  ste_info, send_list, false);

	ste->htbl->ctrl.num_of_collisions++;
	ste->htbl->ctrl.num_of_valid_entries++;

	return new_ste;

err_exit:
	mlx5dr_ste_free(new_ste, matcher, nic_matcher);
free_send_info:
	kfree(ste_info);
	return NULL;
}

static void dr_rule_remove_action_members(struct mlx5dr_rule *rule)
{
	struct mlx5dr_rule_action_member *action_mem;
	struct mlx5dr_rule_action_member *tmp;

	list_for_each_entry_safe(action_mem, tmp, &rule->rule_actions_list, list) {
		list_del(&action_mem->list);
		refcount_dec(&action_mem->action->refcount);
		kvfree(action_mem);
	}
}

static int dr_rule_add_action_members(struct mlx5dr_rule *rule,
				      size_t num_actions,
				      struct mlx5dr_action *actions[])
{
	struct mlx5dr_rule_action_member *action_mem;
	int i;

	for (i = 0; i < num_actions; i++) {
		action_mem = kvzalloc(sizeof(*action_mem), GFP_KERNEL);
		if (!action_mem)
			goto free_action_members;

		action_mem->action = actions[i];
		INIT_LIST_HEAD(&action_mem->list);
		list_add_tail(&action_mem->list, &rule->rule_actions_list);
		refcount_inc(&action_mem->action->refcount);
	}

	return 0;

free_action_members:
	dr_rule_remove_action_members(rule);
	return -ENOMEM;
}

/* While the pointer of ste is no longer valid, like while moving ste to be
 * the first in the miss_list, and to be in the origin table,
 * all rule-members that are attached to this ste should update their ste member
 * to the new pointer
 */
void mlx5dr_rule_update_rule_member(struct mlx5dr_ste *ste,
				    struct mlx5dr_ste *new_ste)
{
	struct mlx5dr_rule_member *rule_mem;

	list_for_each_entry(rule_mem, &ste->rule_list, use_ste_list)
		rule_mem->ste = new_ste;
}

static void dr_rule_clean_rule_members(struct mlx5dr_rule *rule,
				       struct mlx5dr_rule_rx_tx *nic_rule)
{
	struct mlx5dr_rule_member *rule_mem;
	struct mlx5dr_rule_member *tmp_mem;

	if (list_empty(&nic_rule->rule_members_list))
		return;
	list_for_each_entry_safe(rule_mem, tmp_mem, &nic_rule->rule_members_list, list) {
		list_del(&rule_mem->list);
		list_del(&rule_mem->use_ste_list);
		mlx5dr_ste_put(rule_mem->ste, rule->matcher, nic_rule->nic_matcher);
		kvfree(rule_mem);
	}
}

static u16 dr_get_bits_per_mask(u16 byte_mask)
{
	u16 bits = 0;

	while (byte_mask) {
		byte_mask = byte_mask & (byte_mask - 1);
		bits++;
	}

	return bits;
}

static bool dr_rule_need_enlarge_hash(struct mlx5dr_ste_htbl *htbl,
				      struct mlx5dr_domain *dmn,
				      struct mlx5dr_domain_rx_tx *nic_dmn)
{
	struct mlx5dr_ste_htbl_ctrl *ctrl = &htbl->ctrl;
	int threshold;

	if (dmn->info.max_log_sw_icm_sz <= htbl->chunk_size)
		return false;

	if (!mlx5dr_ste_htbl_may_grow(htbl))
		return false;

	if (dr_get_bits_per_mask(htbl->byte_mask) * BITS_PER_BYTE <= htbl->chunk_size)
		return false;

	threshold = mlx5dr_ste_htbl_increase_threshold(htbl);
	if (ctrl->num_of_collisions >= threshold &&
	    (ctrl->num_of_valid_entries - ctrl->num_of_collisions) >= threshold)
		return true;

	return false;
}

static int dr_rule_add_member(struct mlx5dr_rule_rx_tx *nic_rule,
			      struct mlx5dr_ste *ste)
{
	struct mlx5dr_rule_member *rule_mem;

	rule_mem = kvzalloc(sizeof(*rule_mem), GFP_KERNEL);
	if (!rule_mem)
		return -ENOMEM;

	INIT_LIST_HEAD(&rule_mem->list);
	INIT_LIST_HEAD(&rule_mem->use_ste_list);

	rule_mem->ste = ste;
	list_add_tail(&rule_mem->list, &nic_rule->rule_members_list);

	list_add_tail(&rule_mem->use_ste_list, &ste->rule_list);

	return 0;
}

static int dr_rule_handle_action_stes(struct mlx5dr_rule *rule,
				      struct mlx5dr_rule_rx_tx *nic_rule,
				      struct list_head *send_ste_list,
				      struct mlx5dr_ste *last_ste,
				      u8 *hw_ste_arr,
				      u32 new_hw_ste_arr_sz)
{
	struct mlx5dr_matcher_rx_tx *nic_matcher = nic_rule->nic_matcher;
	struct mlx5dr_ste_send_info *ste_info_arr[DR_ACTION_MAX_STES];
	u8 num_of_builders = nic_matcher->num_of_builders;
	struct mlx5dr_matcher *matcher = rule->matcher;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	u8 *curr_hw_ste, *prev_hw_ste;
	struct mlx5dr_ste *action_ste;
	int i, k, ret;

	/* Two cases:
	 * 1. num_of_builders is equal to new_hw_ste_arr_sz, the action in the ste
	 * 2. num_of_builders is less then new_hw_ste_arr_sz, new ste was added
	 *    to support the action.
	 */
	if (num_of_builders == new_hw_ste_arr_sz)
		return 0;

	for (i = num_of_builders, k = 0; i < new_hw_ste_arr_sz; i++, k++) {
		curr_hw_ste = hw_ste_arr + i * DR_STE_SIZE;
		prev_hw_ste = (i == 0) ? curr_hw_ste : hw_ste_arr + ((i - 1) * DR_STE_SIZE);
		action_ste = dr_rule_create_collision_htbl(matcher,
							   nic_matcher,
							   curr_hw_ste);
		if (!action_ste)
			return -ENOMEM;

		mlx5dr_ste_get(action_ste);

		/* While free ste we go over the miss list, so add this ste to the list */
		list_add_tail(&action_ste->miss_list_node,
			      mlx5dr_ste_get_miss_list(action_ste));

		ste_info_arr[k] = kzalloc(sizeof(*ste_info_arr[k]),
					  GFP_KERNEL);
		if (!ste_info_arr[k])
			goto err_exit;

		/* Point current ste to the new action */
		mlx5dr_ste_set_hit_addr_by_next_htbl(dmn->ste_ctx,
						     prev_hw_ste,
						     action_ste->htbl);
		ret = dr_rule_add_member(nic_rule, action_ste);
		if (ret) {
			mlx5dr_dbg(dmn, "Failed adding rule member\n");
			goto free_ste_info;
		}
		mlx5dr_send_fill_and_append_ste_send_info(action_ste, DR_STE_SIZE, 0,
							  curr_hw_ste,
							  ste_info_arr[k],
							  send_ste_list, false);
	}

	return 0;

free_ste_info:
	kfree(ste_info_arr[k]);
err_exit:
	mlx5dr_ste_put(action_ste, matcher, nic_matcher);
	return -ENOMEM;
}

static int dr_rule_handle_empty_entry(struct mlx5dr_matcher *matcher,
				      struct mlx5dr_matcher_rx_tx *nic_matcher,
				      struct mlx5dr_ste_htbl *cur_htbl,
				      struct mlx5dr_ste *ste,
				      u8 ste_location,
				      u8 *hw_ste,
				      struct list_head *miss_list,
				      struct list_head *send_list)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_send_info *ste_info;

	/* Take ref on table, only on first time this ste is used */
	mlx5dr_htbl_get(cur_htbl);

	/* new entry -> new branch */
	list_add_tail(&ste->miss_list_node, miss_list);

	mlx5dr_ste_set_miss_addr(dmn->ste_ctx, hw_ste,
				 nic_matcher->e_anchor->chunk->icm_addr);

	ste->ste_chain_location = ste_location;

	ste_info = kzalloc(sizeof(*ste_info), GFP_KERNEL);
	if (!ste_info)
		goto clean_ste_setting;

	if (mlx5dr_ste_create_next_htbl(matcher,
					nic_matcher,
					ste,
					hw_ste,
					DR_CHUNK_SIZE_1)) {
		mlx5dr_dbg(dmn, "Failed allocating table\n");
		goto clean_ste_info;
	}

	cur_htbl->ctrl.num_of_valid_entries++;

	mlx5dr_send_fill_and_append_ste_send_info(ste, DR_STE_SIZE, 0, hw_ste,
						  ste_info, send_list, false);

	return 0;

clean_ste_info:
	kfree(ste_info);
clean_ste_setting:
	list_del_init(&ste->miss_list_node);
	mlx5dr_htbl_put(cur_htbl);

	return -ENOMEM;
}

static struct mlx5dr_ste *
dr_rule_handle_ste_branch(struct mlx5dr_rule *rule,
			  struct mlx5dr_rule_rx_tx *nic_rule,
			  struct list_head *send_ste_list,
			  struct mlx5dr_ste_htbl *cur_htbl,
			  u8 *hw_ste,
			  u8 ste_location,
			  struct mlx5dr_ste_htbl **put_htbl)
{
	struct mlx5dr_matcher *matcher = rule->matcher;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_matcher_rx_tx *nic_matcher;
	struct mlx5dr_domain_rx_tx *nic_dmn;
	struct mlx5dr_ste_htbl *new_htbl;
	struct mlx5dr_ste *matched_ste;
	struct list_head *miss_list;
	bool skip_rehash = false;
	struct mlx5dr_ste *ste;
	int index;

	nic_matcher = nic_rule->nic_matcher;
	nic_dmn = nic_matcher->nic_tbl->nic_dmn;

again:
	index = mlx5dr_ste_calc_hash_index(hw_ste, cur_htbl);
	miss_list = &cur_htbl->chunk->miss_list[index];
	ste = &cur_htbl->ste_arr[index];

	if (mlx5dr_ste_is_not_used(ste)) {
		if (dr_rule_handle_empty_entry(matcher, nic_matcher, cur_htbl,
					       ste, ste_location,
					       hw_ste, miss_list,
					       send_ste_list))
			return NULL;
	} else {
		/* Hash table index in use, check if this ste is in the miss list */
		matched_ste = dr_rule_find_ste_in_miss_list(miss_list, hw_ste);
		if (matched_ste) {
			/* If it is last STE in the chain, and has the same tag
			 * it means that all the previous stes are the same,
			 * if so, this rule is duplicated.
			 */
			if (!mlx5dr_ste_is_last_in_rule(nic_matcher, ste_location))
				return matched_ste;

			mlx5dr_dbg(dmn, "Duplicate rule inserted\n");
		}

		if (!skip_rehash && dr_rule_need_enlarge_hash(cur_htbl, dmn, nic_dmn)) {
			/* Hash table index in use, try to resize of the hash */
			skip_rehash = true;

			/* Hold the table till we update.
			 * Release in dr_rule_create_rule()
			 */
			*put_htbl = cur_htbl;
			mlx5dr_htbl_get(cur_htbl);

			new_htbl = dr_rule_rehash(rule, nic_rule, cur_htbl,
						  ste_location, send_ste_list);
			if (!new_htbl) {
				mlx5dr_htbl_put(cur_htbl);
				mlx5dr_err(dmn, "Failed creating rehash table, htbl-log_size: %d\n",
					   cur_htbl->chunk_size);
			} else {
				cur_htbl = new_htbl;
			}
			goto again;
		} else {
			/* Hash table index in use, add another collision (miss) */
			ste = dr_rule_handle_collision(matcher,
						       nic_matcher,
						       ste,
						       hw_ste,
						       miss_list,
						       send_ste_list);
			if (!ste) {
				mlx5dr_dbg(dmn, "failed adding collision entry, index: %d\n",
					   index);
				return NULL;
			}
		}
	}
	return ste;
}

static bool dr_rule_cmp_value_to_mask(u8 *mask, u8 *value,
				      u32 s_idx, u32 e_idx)
{
	u32 i;

	for (i = s_idx; i < e_idx; i++) {
		if (value[i] & ~mask[i]) {
			pr_info("Rule parameters contains a value not specified by mask\n");
			return false;
		}
	}
	return true;
}

static bool dr_rule_verify(struct mlx5dr_matcher *matcher,
			   struct mlx5dr_match_parameters *value,
			   struct mlx5dr_match_param *param)
{
	u8 match_criteria = matcher->match_criteria;
	size_t value_size = value->match_sz;
	u8 *mask_p = (u8 *)&matcher->mask;
	u8 *param_p = (u8 *)param;
	u32 s_idx, e_idx;

	if (!value_size ||
	    (value_size > DR_SZ_MATCH_PARAM || (value_size % sizeof(u32)))) {
		mlx5dr_err(matcher->tbl->dmn, "Rule parameters length is incorrect\n");
		return false;
	}

	mlx5dr_ste_copy_param(matcher->match_criteria, param, value);

	if (match_criteria & DR_MATCHER_CRITERIA_OUTER) {
		s_idx = offsetof(struct mlx5dr_match_param, outer);
		e_idx = min(s_idx + sizeof(param->outer), value_size);

		if (!dr_rule_cmp_value_to_mask(mask_p, param_p, s_idx, e_idx)) {
			mlx5dr_err(matcher->tbl->dmn, "Rule outer parameters contains a value not specified by mask\n");
			return false;
		}
	}

	if (match_criteria & DR_MATCHER_CRITERIA_MISC) {
		s_idx = offsetof(struct mlx5dr_match_param, misc);
		e_idx = min(s_idx + sizeof(param->misc), value_size);

		if (!dr_rule_cmp_value_to_mask(mask_p, param_p, s_idx, e_idx)) {
			mlx5dr_err(matcher->tbl->dmn, "Rule misc parameters contains a value not specified by mask\n");
			return false;
		}
	}

	if (match_criteria & DR_MATCHER_CRITERIA_INNER) {
		s_idx = offsetof(struct mlx5dr_match_param, inner);
		e_idx = min(s_idx + sizeof(param->inner), value_size);

		if (!dr_rule_cmp_value_to_mask(mask_p, param_p, s_idx, e_idx)) {
			mlx5dr_err(matcher->tbl->dmn, "Rule inner parameters contains a value not specified by mask\n");
			return false;
		}
	}

	if (match_criteria & DR_MATCHER_CRITERIA_MISC2) {
		s_idx = offsetof(struct mlx5dr_match_param, misc2);
		e_idx = min(s_idx + sizeof(param->misc2), value_size);

		if (!dr_rule_cmp_value_to_mask(mask_p, param_p, s_idx, e_idx)) {
			mlx5dr_err(matcher->tbl->dmn, "Rule misc2 parameters contains a value not specified by mask\n");
			return false;
		}
	}

	if (match_criteria & DR_MATCHER_CRITERIA_MISC3) {
		s_idx = offsetof(struct mlx5dr_match_param, misc3);
		e_idx = min(s_idx + sizeof(param->misc3), value_size);

		if (!dr_rule_cmp_value_to_mask(mask_p, param_p, s_idx, e_idx)) {
			mlx5dr_err(matcher->tbl->dmn, "Rule misc3 parameters contains a value not specified by mask\n");
			return false;
		}
	}

	if (match_criteria & DR_MATCHER_CRITERIA_MISC4) {
		s_idx = offsetof(struct mlx5dr_match_param, misc4);
		e_idx = min(s_idx + sizeof(param->misc4), value_size);

		if (!dr_rule_cmp_value_to_mask(mask_p, param_p, s_idx, e_idx)) {
			mlx5dr_err(matcher->tbl->dmn,
				   "Rule misc4 parameters contains a value not specified by mask\n");
			return false;
		}
	}
	return true;
}

static int dr_rule_destroy_rule_nic(struct mlx5dr_rule *rule,
				    struct mlx5dr_rule_rx_tx *nic_rule)
{
	mlx5dr_domain_nic_lock(nic_rule->nic_matcher->nic_tbl->nic_dmn);
	dr_rule_clean_rule_members(rule, nic_rule);
	mlx5dr_domain_nic_unlock(nic_rule->nic_matcher->nic_tbl->nic_dmn);

	return 0;
}

static int dr_rule_destroy_rule_fdb(struct mlx5dr_rule *rule)
{
	dr_rule_destroy_rule_nic(rule, &rule->rx);
	dr_rule_destroy_rule_nic(rule, &rule->tx);
	return 0;
}

static int dr_rule_destroy_rule(struct mlx5dr_rule *rule)
{
	struct mlx5dr_domain *dmn = rule->matcher->tbl->dmn;

	switch (dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		dr_rule_destroy_rule_nic(rule, &rule->rx);
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		dr_rule_destroy_rule_nic(rule, &rule->tx);
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		dr_rule_destroy_rule_fdb(rule);
		break;
	default:
		return -EINVAL;
	}

	dr_rule_remove_action_members(rule);
	kfree(rule);
	return 0;
}

static enum mlx5dr_ipv dr_rule_get_ipv(struct mlx5dr_match_spec *spec)
{
	if (spec->ip_version == 6 || spec->ethertype == ETH_P_IPV6)
		return DR_RULE_IPV6;

	return DR_RULE_IPV4;
}

static bool dr_rule_skip(enum mlx5dr_domain_type domain,
			 enum mlx5dr_domain_nic_type nic_type,
			 struct mlx5dr_match_param *mask,
			 struct mlx5dr_match_param *value,
			 u32 flow_source)
{
	bool rx = nic_type == DR_DOMAIN_NIC_TYPE_RX;

	if (domain != MLX5DR_DOMAIN_TYPE_FDB)
		return false;

	if (mask->misc.source_port) {
		if (rx && value->misc.source_port != WIRE_PORT)
			return true;

		if (!rx && value->misc.source_port == WIRE_PORT)
			return true;
	}

	if (rx && flow_source == MLX5_FLOW_CONTEXT_FLOW_SOURCE_LOCAL_VPORT)
		return true;

	if (!rx && flow_source == MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK)
		return true;

	return false;
}

static int
dr_rule_create_rule_nic(struct mlx5dr_rule *rule,
			struct mlx5dr_rule_rx_tx *nic_rule,
			struct mlx5dr_match_param *param,
			size_t num_actions,
			struct mlx5dr_action *actions[])
{
	struct mlx5dr_ste_send_info *ste_info, *tmp_ste_info;
	struct mlx5dr_matcher *matcher = rule->matcher;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_matcher_rx_tx *nic_matcher;
	struct mlx5dr_domain_rx_tx *nic_dmn;
	struct mlx5dr_ste_htbl *htbl = NULL;
	struct mlx5dr_ste_htbl *cur_htbl;
	struct mlx5dr_ste *ste = NULL;
	LIST_HEAD(send_ste_list);
	u8 *hw_ste_arr = NULL;
	u32 new_hw_ste_arr_sz;
	int ret, i;

	nic_matcher = nic_rule->nic_matcher;
	nic_dmn = nic_matcher->nic_tbl->nic_dmn;

	INIT_LIST_HEAD(&nic_rule->rule_members_list);

	if (dr_rule_skip(dmn->type, nic_dmn->type, &matcher->mask, param,
			 rule->flow_source))
		return 0;

	hw_ste_arr = kzalloc(DR_RULE_MAX_STE_CHAIN * DR_STE_SIZE, GFP_KERNEL);
	if (!hw_ste_arr)
		return -ENOMEM;

	mlx5dr_domain_nic_lock(nic_dmn);

	ret = mlx5dr_matcher_select_builders(matcher,
					     nic_matcher,
					     dr_rule_get_ipv(&param->outer),
					     dr_rule_get_ipv(&param->inner));
	if (ret)
		goto free_hw_ste;

	/* Set the tag values inside the ste array */
	ret = mlx5dr_ste_build_ste_arr(matcher, nic_matcher, param, hw_ste_arr);
	if (ret)
		goto free_hw_ste;

	/* Set the actions values/addresses inside the ste array */
	ret = mlx5dr_actions_build_ste_arr(matcher, nic_matcher, actions,
					   num_actions, hw_ste_arr,
					   &new_hw_ste_arr_sz);
	if (ret)
		goto free_hw_ste;

	cur_htbl = nic_matcher->s_htbl;

	/* Go over the array of STEs, and build dr_ste accordingly.
	 * The loop is over only the builders which are equal or less to the
	 * number of stes, in case we have actions that lives in other stes.
	 */
	for (i = 0; i < nic_matcher->num_of_builders; i++) {
		/* Calculate CRC and keep new ste entry */
		u8 *cur_hw_ste_ent = hw_ste_arr + (i * DR_STE_SIZE);

		ste = dr_rule_handle_ste_branch(rule,
						nic_rule,
						&send_ste_list,
						cur_htbl,
						cur_hw_ste_ent,
						i + 1,
						&htbl);
		if (!ste) {
			mlx5dr_err(dmn, "Failed creating next branch\n");
			ret = -ENOENT;
			goto free_rule;
		}

		cur_htbl = ste->next_htbl;

		/* Keep all STEs in the rule struct */
		ret = dr_rule_add_member(nic_rule, ste);
		if (ret) {
			mlx5dr_dbg(dmn, "Failed adding rule member index %d\n", i);
			goto free_ste;
		}

		mlx5dr_ste_get(ste);
	}

	/* Connect actions */
	ret = dr_rule_handle_action_stes(rule, nic_rule, &send_ste_list,
					 ste, hw_ste_arr, new_hw_ste_arr_sz);
	if (ret) {
		mlx5dr_dbg(dmn, "Failed apply actions\n");
		goto free_rule;
	}
	ret = dr_rule_send_update_list(&send_ste_list, dmn, true);
	if (ret) {
		mlx5dr_err(dmn, "Failed sending ste!\n");
		goto free_rule;
	}

	if (htbl)
		mlx5dr_htbl_put(htbl);

	mlx5dr_domain_nic_unlock(nic_dmn);

	kfree(hw_ste_arr);

	return 0;

free_ste:
	mlx5dr_ste_put(ste, matcher, nic_matcher);
free_rule:
	dr_rule_clean_rule_members(rule, nic_rule);
	/* Clean all ste_info's */
	list_for_each_entry_safe(ste_info, tmp_ste_info, &send_ste_list, send_list) {
		list_del(&ste_info->send_list);
		kfree(ste_info);
	}
free_hw_ste:
	mlx5dr_domain_nic_unlock(nic_dmn);
	kfree(hw_ste_arr);
	return ret;
}

static int
dr_rule_create_rule_fdb(struct mlx5dr_rule *rule,
			struct mlx5dr_match_param *param,
			size_t num_actions,
			struct mlx5dr_action *actions[])
{
	struct mlx5dr_match_param copy_param = {};
	int ret;

	/* Copy match_param since they will be consumed during the first
	 * nic_rule insertion.
	 */
	memcpy(&copy_param, param, sizeof(struct mlx5dr_match_param));

	ret = dr_rule_create_rule_nic(rule, &rule->rx, param,
				      num_actions, actions);
	if (ret)
		return ret;

	ret = dr_rule_create_rule_nic(rule, &rule->tx, &copy_param,
				      num_actions, actions);
	if (ret)
		goto destroy_rule_nic_rx;

	return 0;

destroy_rule_nic_rx:
	dr_rule_destroy_rule_nic(rule, &rule->rx);
	return ret;
}

static struct mlx5dr_rule *
dr_rule_create_rule(struct mlx5dr_matcher *matcher,
		    struct mlx5dr_match_parameters *value,
		    size_t num_actions,
		    struct mlx5dr_action *actions[],
		    u32 flow_source)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_match_param param = {};
	struct mlx5dr_rule *rule;
	int ret;

	if (!dr_rule_verify(matcher, value, &param))
		return NULL;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return NULL;

	rule->matcher = matcher;
	rule->flow_source = flow_source;
	INIT_LIST_HEAD(&rule->rule_actions_list);

	ret = dr_rule_add_action_members(rule, num_actions, actions);
	if (ret)
		goto free_rule;

	switch (dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		rule->rx.nic_matcher = &matcher->rx;
		ret = dr_rule_create_rule_nic(rule, &rule->rx, &param,
					      num_actions, actions);
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		rule->tx.nic_matcher = &matcher->tx;
		ret = dr_rule_create_rule_nic(rule, &rule->tx, &param,
					      num_actions, actions);
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		rule->rx.nic_matcher = &matcher->rx;
		rule->tx.nic_matcher = &matcher->tx;
		ret = dr_rule_create_rule_fdb(rule, &param,
					      num_actions, actions);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto remove_action_members;

	return rule;

remove_action_members:
	dr_rule_remove_action_members(rule);
free_rule:
	kfree(rule);
	mlx5dr_err(dmn, "Failed creating rule\n");
	return NULL;
}

struct mlx5dr_rule *mlx5dr_rule_create(struct mlx5dr_matcher *matcher,
				       struct mlx5dr_match_parameters *value,
				       size_t num_actions,
				       struct mlx5dr_action *actions[],
				       u32 flow_source)
{
	struct mlx5dr_rule *rule;

	refcount_inc(&matcher->refcount);

	rule = dr_rule_create_rule(matcher, value, num_actions, actions, flow_source);
	if (!rule)
		refcount_dec(&matcher->refcount);

	return rule;
}

int mlx5dr_rule_destroy(struct mlx5dr_rule *rule)
{
	struct mlx5dr_matcher *matcher = rule->matcher;
	int ret;

	ret = dr_rule_destroy_rule(rule);
	if (!ret)
		refcount_dec(&matcher->refcount);

	return ret;
}
