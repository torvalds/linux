// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/types.h>
#include <linux/crc32.h>
#include "dr_ste.h"

struct dr_hw_ste_format {
	u8 ctrl[DR_STE_SIZE_CTRL];
	u8 tag[DR_STE_SIZE_TAG];
	u8 mask[DR_STE_SIZE_MASK];
};

static u32 dr_ste_crc32_calc(const void *input_data, size_t length)
{
	u32 crc = crc32(0, input_data, length);

	return (__force u32)htonl(crc);
}

bool mlx5dr_ste_supp_ttl_cs_recalc(struct mlx5dr_cmd_caps *caps)
{
	return caps->sw_format_ver > MLX5_STEERING_FORMAT_CONNECTX_5;
}

u32 mlx5dr_ste_calc_hash_index(u8 *hw_ste_p, struct mlx5dr_ste_htbl *htbl)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	u8 masked[DR_STE_SIZE_TAG] = {};
	u32 crc32, index;
	u16 bit;
	int i;

	/* Don't calculate CRC if the result is predicted */
	if (htbl->chunk->num_of_entries == 1 || htbl->byte_mask == 0)
		return 0;

	/* Mask tag using byte mask, bit per byte */
	bit = 1 << (DR_STE_SIZE_TAG - 1);
	for (i = 0; i < DR_STE_SIZE_TAG; i++) {
		if (htbl->byte_mask & bit)
			masked[i] = hw_ste->tag[i];

		bit = bit >> 1;
	}

	crc32 = dr_ste_crc32_calc(masked, DR_STE_SIZE_TAG);
	index = crc32 & (htbl->chunk->num_of_entries - 1);

	return index;
}

u16 mlx5dr_ste_conv_bit_to_byte_mask(u8 *bit_mask)
{
	u16 byte_mask = 0;
	int i;

	for (i = 0; i < DR_STE_SIZE_MASK; i++) {
		byte_mask = byte_mask << 1;
		if (bit_mask[i] == 0xff)
			byte_mask |= 1;
	}
	return byte_mask;
}

static u8 *dr_ste_get_tag(u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;

	return hw_ste->tag;
}

void mlx5dr_ste_set_bit_mask(u8 *hw_ste_p, u8 *bit_mask)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;

	memcpy(hw_ste->mask, bit_mask, DR_STE_SIZE_MASK);
}

static void dr_ste_set_always_hit(struct dr_hw_ste_format *hw_ste)
{
	memset(&hw_ste->tag, 0, sizeof(hw_ste->tag));
	memset(&hw_ste->mask, 0, sizeof(hw_ste->mask));
}

static void dr_ste_set_always_miss(struct dr_hw_ste_format *hw_ste)
{
	hw_ste->tag[0] = 0xdc;
	hw_ste->mask[0] = 0;
}

void mlx5dr_ste_set_miss_addr(struct mlx5dr_ste_ctx *ste_ctx,
			      u8 *hw_ste_p, u64 miss_addr)
{
	ste_ctx->set_miss_addr(hw_ste_p, miss_addr);
}

static void dr_ste_always_miss_addr(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste *ste, u64 miss_addr)
{
	u8 *hw_ste_p = ste->hw_ste;

	ste_ctx->set_next_lu_type(hw_ste_p, MLX5DR_STE_LU_TYPE_DONT_CARE);
	ste_ctx->set_miss_addr(hw_ste_p, miss_addr);
	dr_ste_set_always_miss((struct dr_hw_ste_format *)ste->hw_ste);
}

void mlx5dr_ste_set_hit_addr(struct mlx5dr_ste_ctx *ste_ctx,
			     u8 *hw_ste, u64 icm_addr, u32 ht_size)
{
	ste_ctx->set_hit_addr(hw_ste, icm_addr, ht_size);
}

u64 mlx5dr_ste_get_icm_addr(struct mlx5dr_ste *ste)
{
	u32 index = ste - ste->htbl->ste_arr;

	return ste->htbl->chunk->icm_addr + DR_STE_SIZE * index;
}

u64 mlx5dr_ste_get_mr_addr(struct mlx5dr_ste *ste)
{
	u32 index = ste - ste->htbl->ste_arr;

	return ste->htbl->chunk->mr_addr + DR_STE_SIZE * index;
}

struct list_head *mlx5dr_ste_get_miss_list(struct mlx5dr_ste *ste)
{
	u32 index = ste - ste->htbl->ste_arr;

	return &ste->htbl->miss_list[index];
}

static void dr_ste_always_hit_htbl(struct mlx5dr_ste_ctx *ste_ctx,
				   struct mlx5dr_ste *ste,
				   struct mlx5dr_ste_htbl *next_htbl)
{
	struct mlx5dr_icm_chunk *chunk = next_htbl->chunk;
	u8 *hw_ste = ste->hw_ste;

	ste_ctx->set_byte_mask(hw_ste, next_htbl->byte_mask);
	ste_ctx->set_next_lu_type(hw_ste, next_htbl->lu_type);
	ste_ctx->set_hit_addr(hw_ste, chunk->icm_addr, chunk->num_of_entries);

	dr_ste_set_always_hit((struct dr_hw_ste_format *)ste->hw_ste);
}

bool mlx5dr_ste_is_last_in_rule(struct mlx5dr_matcher_rx_tx *nic_matcher,
				u8 ste_location)
{
	return ste_location == nic_matcher->num_of_builders;
}

/* Replace relevant fields, except of:
 * htbl - keep the origin htbl
 * miss_list + list - already took the src from the list.
 * icm_addr/mr_addr - depends on the hosting table.
 *
 * Before:
 * | a | -> | b | -> | c | ->
 *
 * After:
 * | a | -> | c | ->
 * While the data that was in b copied to a.
 */
static void dr_ste_replace(struct mlx5dr_ste *dst, struct mlx5dr_ste *src)
{
	memcpy(dst->hw_ste, src->hw_ste, DR_STE_SIZE_REDUCED);
	dst->next_htbl = src->next_htbl;
	if (dst->next_htbl)
		dst->next_htbl->pointing_ste = dst;

	dst->refcount = src->refcount;

	INIT_LIST_HEAD(&dst->rule_list);
	list_splice_tail_init(&src->rule_list, &dst->rule_list);
}

/* Free ste which is the head and the only one in miss_list */
static void
dr_ste_remove_head_ste(struct mlx5dr_ste_ctx *ste_ctx,
		       struct mlx5dr_ste *ste,
		       struct mlx5dr_matcher_rx_tx *nic_matcher,
		       struct mlx5dr_ste_send_info *ste_info_head,
		       struct list_head *send_ste_list,
		       struct mlx5dr_ste_htbl *stats_tbl)
{
	u8 tmp_data_ste[DR_STE_SIZE] = {};
	struct mlx5dr_ste tmp_ste = {};
	u64 miss_addr;

	tmp_ste.hw_ste = tmp_data_ste;

	/* Use temp ste because dr_ste_always_miss_addr
	 * touches bit_mask area which doesn't exist at ste->hw_ste.
	 */
	memcpy(tmp_ste.hw_ste, ste->hw_ste, DR_STE_SIZE_REDUCED);
	miss_addr = nic_matcher->e_anchor->chunk->icm_addr;
	dr_ste_always_miss_addr(ste_ctx, &tmp_ste, miss_addr);
	memcpy(ste->hw_ste, tmp_ste.hw_ste, DR_STE_SIZE_REDUCED);

	list_del_init(&ste->miss_list_node);

	/* Write full STE size in order to have "always_miss" */
	mlx5dr_send_fill_and_append_ste_send_info(ste, DR_STE_SIZE,
						  0, tmp_data_ste,
						  ste_info_head,
						  send_ste_list,
						  true /* Copy data */);

	stats_tbl->ctrl.num_of_valid_entries--;
}

/* Free ste which is the head but NOT the only one in miss_list:
 * |_ste_| --> |_next_ste_| -->|__| -->|__| -->/0
 */
static void
dr_ste_replace_head_ste(struct mlx5dr_matcher_rx_tx *nic_matcher,
			struct mlx5dr_ste *ste,
			struct mlx5dr_ste *next_ste,
			struct mlx5dr_ste_send_info *ste_info_head,
			struct list_head *send_ste_list,
			struct mlx5dr_ste_htbl *stats_tbl)

{
	struct mlx5dr_ste_htbl *next_miss_htbl;
	u8 hw_ste[DR_STE_SIZE] = {};
	int sb_idx;

	next_miss_htbl = next_ste->htbl;

	/* Remove from the miss_list the next_ste before copy */
	list_del_init(&next_ste->miss_list_node);

	/* All rule-members that use next_ste should know about that */
	mlx5dr_rule_update_rule_member(next_ste, ste);

	/* Move data from next into ste */
	dr_ste_replace(ste, next_ste);

	/* Copy all 64 hw_ste bytes */
	memcpy(hw_ste, ste->hw_ste, DR_STE_SIZE_REDUCED);
	sb_idx = ste->ste_chain_location - 1;
	mlx5dr_ste_set_bit_mask(hw_ste,
				nic_matcher->ste_builder[sb_idx].bit_mask);

	/* Del the htbl that contains the next_ste.
	 * The origin htbl stay with the same number of entries.
	 */
	mlx5dr_htbl_put(next_miss_htbl);

	mlx5dr_send_fill_and_append_ste_send_info(ste, DR_STE_SIZE,
						  0, hw_ste,
						  ste_info_head,
						  send_ste_list,
						  true /* Copy data */);

	stats_tbl->ctrl.num_of_collisions--;
	stats_tbl->ctrl.num_of_valid_entries--;
}

/* Free ste that is located in the middle of the miss list:
 * |__| -->|_prev_ste_|->|_ste_|-->|_next_ste_|
 */
static void dr_ste_remove_middle_ste(struct mlx5dr_ste_ctx *ste_ctx,
				     struct mlx5dr_ste *ste,
				     struct mlx5dr_ste_send_info *ste_info,
				     struct list_head *send_ste_list,
				     struct mlx5dr_ste_htbl *stats_tbl)
{
	struct mlx5dr_ste *prev_ste;
	u64 miss_addr;

	prev_ste = list_prev_entry(ste, miss_list_node);
	if (WARN_ON(!prev_ste))
		return;

	miss_addr = ste_ctx->get_miss_addr(ste->hw_ste);
	ste_ctx->set_miss_addr(prev_ste->hw_ste, miss_addr);

	mlx5dr_send_fill_and_append_ste_send_info(prev_ste, DR_STE_SIZE_CTRL, 0,
						  prev_ste->hw_ste, ste_info,
						  send_ste_list, true /* Copy data*/);

	list_del_init(&ste->miss_list_node);

	stats_tbl->ctrl.num_of_valid_entries--;
	stats_tbl->ctrl.num_of_collisions--;
}

void mlx5dr_ste_free(struct mlx5dr_ste *ste,
		     struct mlx5dr_matcher *matcher,
		     struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	struct mlx5dr_ste_send_info *cur_ste_info, *tmp_ste_info;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_ctx *ste_ctx = dmn->ste_ctx;
	struct mlx5dr_ste_send_info ste_info_head;
	struct mlx5dr_ste *next_ste, *first_ste;
	bool put_on_origin_table = true;
	struct mlx5dr_ste_htbl *stats_tbl;
	LIST_HEAD(send_ste_list);

	first_ste = list_first_entry(mlx5dr_ste_get_miss_list(ste),
				     struct mlx5dr_ste, miss_list_node);
	stats_tbl = first_ste->htbl;

	/* Two options:
	 * 1. ste is head:
	 *	a. head ste is the only ste in the miss list
	 *	b. head ste is not the only ste in the miss-list
	 * 2. ste is not head
	 */
	if (first_ste == ste) { /* Ste is the head */
		struct mlx5dr_ste *last_ste;

		last_ste = list_last_entry(mlx5dr_ste_get_miss_list(ste),
					   struct mlx5dr_ste, miss_list_node);
		if (last_ste == first_ste)
			next_ste = NULL;
		else
			next_ste = list_next_entry(ste, miss_list_node);

		if (!next_ste) {
			/* One and only entry in the list */
			dr_ste_remove_head_ste(ste_ctx, ste,
					       nic_matcher,
					       &ste_info_head,
					       &send_ste_list,
					       stats_tbl);
		} else {
			/* First but not only entry in the list */
			dr_ste_replace_head_ste(nic_matcher, ste,
						next_ste, &ste_info_head,
						&send_ste_list, stats_tbl);
			put_on_origin_table = false;
		}
	} else { /* Ste in the middle of the list */
		dr_ste_remove_middle_ste(ste_ctx, ste,
					 &ste_info_head, &send_ste_list,
					 stats_tbl);
	}

	/* Update HW */
	list_for_each_entry_safe(cur_ste_info, tmp_ste_info,
				 &send_ste_list, send_list) {
		list_del(&cur_ste_info->send_list);
		mlx5dr_send_postsend_ste(dmn, cur_ste_info->ste,
					 cur_ste_info->data, cur_ste_info->size,
					 cur_ste_info->offset);
	}

	if (put_on_origin_table)
		mlx5dr_htbl_put(ste->htbl);
}

bool mlx5dr_ste_equal_tag(void *src, void *dst)
{
	struct dr_hw_ste_format *s_hw_ste = (struct dr_hw_ste_format *)src;
	struct dr_hw_ste_format *d_hw_ste = (struct dr_hw_ste_format *)dst;

	return !memcmp(s_hw_ste->tag, d_hw_ste->tag, DR_STE_SIZE_TAG);
}

void mlx5dr_ste_set_hit_addr_by_next_htbl(struct mlx5dr_ste_ctx *ste_ctx,
					  u8 *hw_ste,
					  struct mlx5dr_ste_htbl *next_htbl)
{
	struct mlx5dr_icm_chunk *chunk = next_htbl->chunk;

	ste_ctx->set_hit_addr(hw_ste, chunk->icm_addr, chunk->num_of_entries);
}

void mlx5dr_ste_prepare_for_postsend(struct mlx5dr_ste_ctx *ste_ctx,
				     u8 *hw_ste_p, u32 ste_size)
{
	if (ste_ctx->prepare_for_postsend)
		ste_ctx->prepare_for_postsend(hw_ste_p, ste_size);
}

/* Init one ste as a pattern for ste data array */
void mlx5dr_ste_set_formatted_ste(struct mlx5dr_ste_ctx *ste_ctx,
				  u16 gvmi,
				  struct mlx5dr_domain_rx_tx *nic_dmn,
				  struct mlx5dr_ste_htbl *htbl,
				  u8 *formatted_ste,
				  struct mlx5dr_htbl_connect_info *connect_info)
{
	struct mlx5dr_ste ste = {};

	ste_ctx->ste_init(formatted_ste, htbl->lu_type, nic_dmn->ste_type, gvmi);
	ste.hw_ste = formatted_ste;

	if (connect_info->type == CONNECT_HIT)
		dr_ste_always_hit_htbl(ste_ctx, &ste, connect_info->hit_next_htbl);
	else
		dr_ste_always_miss_addr(ste_ctx, &ste, connect_info->miss_icm_addr);
}

int mlx5dr_ste_htbl_init_and_postsend(struct mlx5dr_domain *dmn,
				      struct mlx5dr_domain_rx_tx *nic_dmn,
				      struct mlx5dr_ste_htbl *htbl,
				      struct mlx5dr_htbl_connect_info *connect_info,
				      bool update_hw_ste)
{
	u8 formatted_ste[DR_STE_SIZE] = {};

	mlx5dr_ste_set_formatted_ste(dmn->ste_ctx,
				     dmn->info.caps.gvmi,
				     nic_dmn,
				     htbl,
				     formatted_ste,
				     connect_info);

	return mlx5dr_send_postsend_formatted_htbl(dmn, htbl, formatted_ste, update_hw_ste);
}

int mlx5dr_ste_create_next_htbl(struct mlx5dr_matcher *matcher,
				struct mlx5dr_matcher_rx_tx *nic_matcher,
				struct mlx5dr_ste *ste,
				u8 *cur_hw_ste,
				enum mlx5dr_icm_chunk_size log_table_size)
{
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_matcher->nic_tbl->nic_dmn;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_ctx *ste_ctx = dmn->ste_ctx;
	struct mlx5dr_htbl_connect_info info;
	struct mlx5dr_ste_htbl *next_htbl;

	if (!mlx5dr_ste_is_last_in_rule(nic_matcher, ste->ste_chain_location)) {
		u16 next_lu_type;
		u16 byte_mask;

		next_lu_type = ste_ctx->get_next_lu_type(cur_hw_ste);
		byte_mask = ste_ctx->get_byte_mask(cur_hw_ste);

		next_htbl = mlx5dr_ste_htbl_alloc(dmn->ste_icm_pool,
						  log_table_size,
						  next_lu_type,
						  byte_mask);
		if (!next_htbl) {
			mlx5dr_dbg(dmn, "Failed allocating table\n");
			return -ENOMEM;
		}

		/* Write new table to HW */
		info.type = CONNECT_MISS;
		info.miss_icm_addr = nic_matcher->e_anchor->chunk->icm_addr;
		if (mlx5dr_ste_htbl_init_and_postsend(dmn, nic_dmn, next_htbl,
						      &info, false)) {
			mlx5dr_info(dmn, "Failed writing table to HW\n");
			goto free_table;
		}

		mlx5dr_ste_set_hit_addr_by_next_htbl(ste_ctx,
						     cur_hw_ste, next_htbl);
		ste->next_htbl = next_htbl;
		next_htbl->pointing_ste = ste;
	}

	return 0;

free_table:
	mlx5dr_ste_htbl_free(next_htbl);
	return -ENOENT;
}

static void dr_ste_set_ctrl(struct mlx5dr_ste_htbl *htbl)
{
	struct mlx5dr_ste_htbl_ctrl *ctrl = &htbl->ctrl;
	int num_of_entries;

	htbl->ctrl.may_grow = true;

	if (htbl->chunk_size == DR_CHUNK_SIZE_MAX - 1 || !htbl->byte_mask)
		htbl->ctrl.may_grow = false;

	/* Threshold is 50%, one is added to table of size 1 */
	num_of_entries = mlx5dr_icm_pool_chunk_size_to_entries(htbl->chunk_size);
	ctrl->increase_threshold = (num_of_entries + 1) / 2;
}

struct mlx5dr_ste_htbl *mlx5dr_ste_htbl_alloc(struct mlx5dr_icm_pool *pool,
					      enum mlx5dr_icm_chunk_size chunk_size,
					      u16 lu_type, u16 byte_mask)
{
	struct mlx5dr_icm_chunk *chunk;
	struct mlx5dr_ste_htbl *htbl;
	int i;

	htbl = kzalloc(sizeof(*htbl), GFP_KERNEL);
	if (!htbl)
		return NULL;

	chunk = mlx5dr_icm_alloc_chunk(pool, chunk_size);
	if (!chunk)
		goto out_free_htbl;

	htbl->chunk = chunk;
	htbl->lu_type = lu_type;
	htbl->byte_mask = byte_mask;
	htbl->ste_arr = chunk->ste_arr;
	htbl->hw_ste_arr = chunk->hw_ste_arr;
	htbl->miss_list = chunk->miss_list;
	htbl->refcount = 0;

	for (i = 0; i < chunk->num_of_entries; i++) {
		struct mlx5dr_ste *ste = &htbl->ste_arr[i];

		ste->hw_ste = htbl->hw_ste_arr + i * DR_STE_SIZE_REDUCED;
		ste->htbl = htbl;
		ste->refcount = 0;
		INIT_LIST_HEAD(&ste->miss_list_node);
		INIT_LIST_HEAD(&htbl->miss_list[i]);
		INIT_LIST_HEAD(&ste->rule_list);
	}

	htbl->chunk_size = chunk_size;
	dr_ste_set_ctrl(htbl);
	return htbl;

out_free_htbl:
	kfree(htbl);
	return NULL;
}

int mlx5dr_ste_htbl_free(struct mlx5dr_ste_htbl *htbl)
{
	if (htbl->refcount)
		return -EBUSY;

	mlx5dr_icm_free_chunk(htbl->chunk);
	kfree(htbl);
	return 0;
}

void mlx5dr_ste_set_actions_tx(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_domain *dmn,
			       u8 *action_type_set,
			       u8 *hw_ste_arr,
			       struct mlx5dr_ste_actions_attr *attr,
			       u32 *added_stes)
{
	ste_ctx->set_actions_tx(dmn, action_type_set, hw_ste_arr,
				attr, added_stes);
}

void mlx5dr_ste_set_actions_rx(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_domain *dmn,
			       u8 *action_type_set,
			       u8 *hw_ste_arr,
			       struct mlx5dr_ste_actions_attr *attr,
			       u32 *added_stes)
{
	ste_ctx->set_actions_rx(dmn, action_type_set, hw_ste_arr,
				attr, added_stes);
}

const struct mlx5dr_ste_action_modify_field *
mlx5dr_ste_conv_modify_hdr_sw_field(struct mlx5dr_ste_ctx *ste_ctx, u16 sw_field)
{
	const struct mlx5dr_ste_action_modify_field *hw_field;

	if (sw_field >= ste_ctx->modify_field_arr_sz)
		return NULL;

	hw_field = &ste_ctx->modify_field_arr[sw_field];
	if (!hw_field->end && !hw_field->start)
		return NULL;

	return hw_field;
}

void mlx5dr_ste_set_action_set(struct mlx5dr_ste_ctx *ste_ctx,
			       __be64 *hw_action,
			       u8 hw_field,
			       u8 shifter,
			       u8 length,
			       u32 data)
{
	ste_ctx->set_action_set((u8 *)hw_action,
				hw_field, shifter, length, data);
}

void mlx5dr_ste_set_action_add(struct mlx5dr_ste_ctx *ste_ctx,
			       __be64 *hw_action,
			       u8 hw_field,
			       u8 shifter,
			       u8 length,
			       u32 data)
{
	ste_ctx->set_action_add((u8 *)hw_action,
				hw_field, shifter, length, data);
}

void mlx5dr_ste_set_action_copy(struct mlx5dr_ste_ctx *ste_ctx,
				__be64 *hw_action,
				u8 dst_hw_field,
				u8 dst_shifter,
				u8 dst_len,
				u8 src_hw_field,
				u8 src_shifter)
{
	ste_ctx->set_action_copy((u8 *)hw_action,
				 dst_hw_field, dst_shifter, dst_len,
				 src_hw_field, src_shifter);
}

int mlx5dr_ste_set_action_decap_l3_list(struct mlx5dr_ste_ctx *ste_ctx,
					void *data, u32 data_sz,
					u8 *hw_action, u32 hw_action_sz,
					u16 *used_hw_action_num)
{
	/* Only Ethernet frame is supported, with VLAN (18) or without (14) */
	if (data_sz != HDR_LEN_L2 && data_sz != HDR_LEN_L2_W_VLAN)
		return -EINVAL;

	return ste_ctx->set_action_decap_l3_list(data, data_sz,
						 hw_action, hw_action_sz,
						 used_hw_action_num);
}

int mlx5dr_ste_build_pre_check(struct mlx5dr_domain *dmn,
			       u8 match_criteria,
			       struct mlx5dr_match_param *mask,
			       struct mlx5dr_match_param *value)
{
	if (!value && (match_criteria & DR_MATCHER_CRITERIA_MISC)) {
		if (mask->misc.source_port && mask->misc.source_port != 0xffff) {
			mlx5dr_err(dmn,
				   "Partial mask source_port is not supported\n");
			return -EINVAL;
		}
		if (mask->misc.source_eswitch_owner_vhca_id &&
		    mask->misc.source_eswitch_owner_vhca_id != 0xffff) {
			mlx5dr_err(dmn,
				   "Partial mask source_eswitch_owner_vhca_id is not supported\n");
			return -EINVAL;
		}
	}

	return 0;
}

int mlx5dr_ste_build_ste_arr(struct mlx5dr_matcher *matcher,
			     struct mlx5dr_matcher_rx_tx *nic_matcher,
			     struct mlx5dr_match_param *value,
			     u8 *ste_arr)
{
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_matcher->nic_tbl->nic_dmn;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_ctx *ste_ctx = dmn->ste_ctx;
	struct mlx5dr_ste_build *sb;
	int ret, i;

	ret = mlx5dr_ste_build_pre_check(dmn, matcher->match_criteria,
					 &matcher->mask, value);
	if (ret)
		return ret;

	sb = nic_matcher->ste_builder;
	for (i = 0; i < nic_matcher->num_of_builders; i++) {
		ste_ctx->ste_init(ste_arr,
				  sb->lu_type,
				  nic_dmn->ste_type,
				  dmn->info.caps.gvmi);

		mlx5dr_ste_set_bit_mask(ste_arr, sb->bit_mask);

		ret = sb->ste_build_tag_func(value, sb, dr_ste_get_tag(ste_arr));
		if (ret)
			return ret;

		/* Connect the STEs */
		if (i < (nic_matcher->num_of_builders - 1)) {
			/* Need the next builder for these fields,
			 * not relevant for the last ste in the chain.
			 */
			sb++;
			ste_ctx->set_next_lu_type(ste_arr, sb->lu_type);
			ste_ctx->set_byte_mask(ste_arr, sb->byte_mask);
		}
		ste_arr += DR_STE_SIZE;
	}
	return 0;
}

static void dr_ste_copy_mask_misc(char *mask, struct mlx5dr_match_misc *spec)
{
	spec->gre_c_present = MLX5_GET(fte_match_set_misc, mask, gre_c_present);
	spec->gre_k_present = MLX5_GET(fte_match_set_misc, mask, gre_k_present);
	spec->gre_s_present = MLX5_GET(fte_match_set_misc, mask, gre_s_present);
	spec->source_vhca_port = MLX5_GET(fte_match_set_misc, mask, source_vhca_port);
	spec->source_sqn = MLX5_GET(fte_match_set_misc, mask, source_sqn);

	spec->source_port = MLX5_GET(fte_match_set_misc, mask, source_port);
	spec->source_eswitch_owner_vhca_id = MLX5_GET(fte_match_set_misc, mask,
						      source_eswitch_owner_vhca_id);

	spec->outer_second_prio = MLX5_GET(fte_match_set_misc, mask, outer_second_prio);
	spec->outer_second_cfi = MLX5_GET(fte_match_set_misc, mask, outer_second_cfi);
	spec->outer_second_vid = MLX5_GET(fte_match_set_misc, mask, outer_second_vid);
	spec->inner_second_prio = MLX5_GET(fte_match_set_misc, mask, inner_second_prio);
	spec->inner_second_cfi = MLX5_GET(fte_match_set_misc, mask, inner_second_cfi);
	spec->inner_second_vid = MLX5_GET(fte_match_set_misc, mask, inner_second_vid);

	spec->outer_second_cvlan_tag =
		MLX5_GET(fte_match_set_misc, mask, outer_second_cvlan_tag);
	spec->inner_second_cvlan_tag =
		MLX5_GET(fte_match_set_misc, mask, inner_second_cvlan_tag);
	spec->outer_second_svlan_tag =
		MLX5_GET(fte_match_set_misc, mask, outer_second_svlan_tag);
	spec->inner_second_svlan_tag =
		MLX5_GET(fte_match_set_misc, mask, inner_second_svlan_tag);

	spec->gre_protocol = MLX5_GET(fte_match_set_misc, mask, gre_protocol);

	spec->gre_key_h = MLX5_GET(fte_match_set_misc, mask, gre_key.nvgre.hi);
	spec->gre_key_l = MLX5_GET(fte_match_set_misc, mask, gre_key.nvgre.lo);

	spec->vxlan_vni = MLX5_GET(fte_match_set_misc, mask, vxlan_vni);

	spec->geneve_vni = MLX5_GET(fte_match_set_misc, mask, geneve_vni);
	spec->geneve_oam = MLX5_GET(fte_match_set_misc, mask, geneve_oam);

	spec->outer_ipv6_flow_label =
		MLX5_GET(fte_match_set_misc, mask, outer_ipv6_flow_label);

	spec->inner_ipv6_flow_label =
		MLX5_GET(fte_match_set_misc, mask, inner_ipv6_flow_label);

	spec->geneve_opt_len = MLX5_GET(fte_match_set_misc, mask, geneve_opt_len);
	spec->geneve_protocol_type =
		MLX5_GET(fte_match_set_misc, mask, geneve_protocol_type);

	spec->bth_dst_qp = MLX5_GET(fte_match_set_misc, mask, bth_dst_qp);
}

static void dr_ste_copy_mask_spec(char *mask, struct mlx5dr_match_spec *spec)
{
	__be32 raw_ip[4];

	spec->smac_47_16 = MLX5_GET(fte_match_set_lyr_2_4, mask, smac_47_16);

	spec->smac_15_0 = MLX5_GET(fte_match_set_lyr_2_4, mask, smac_15_0);
	spec->ethertype = MLX5_GET(fte_match_set_lyr_2_4, mask, ethertype);

	spec->dmac_47_16 = MLX5_GET(fte_match_set_lyr_2_4, mask, dmac_47_16);

	spec->dmac_15_0 = MLX5_GET(fte_match_set_lyr_2_4, mask, dmac_15_0);
	spec->first_prio = MLX5_GET(fte_match_set_lyr_2_4, mask, first_prio);
	spec->first_cfi = MLX5_GET(fte_match_set_lyr_2_4, mask, first_cfi);
	spec->first_vid = MLX5_GET(fte_match_set_lyr_2_4, mask, first_vid);

	spec->ip_protocol = MLX5_GET(fte_match_set_lyr_2_4, mask, ip_protocol);
	spec->ip_dscp = MLX5_GET(fte_match_set_lyr_2_4, mask, ip_dscp);
	spec->ip_ecn = MLX5_GET(fte_match_set_lyr_2_4, mask, ip_ecn);
	spec->cvlan_tag = MLX5_GET(fte_match_set_lyr_2_4, mask, cvlan_tag);
	spec->svlan_tag = MLX5_GET(fte_match_set_lyr_2_4, mask, svlan_tag);
	spec->frag = MLX5_GET(fte_match_set_lyr_2_4, mask, frag);
	spec->ip_version = MLX5_GET(fte_match_set_lyr_2_4, mask, ip_version);
	spec->tcp_flags = MLX5_GET(fte_match_set_lyr_2_4, mask, tcp_flags);
	spec->tcp_sport = MLX5_GET(fte_match_set_lyr_2_4, mask, tcp_sport);
	spec->tcp_dport = MLX5_GET(fte_match_set_lyr_2_4, mask, tcp_dport);

	spec->ttl_hoplimit = MLX5_GET(fte_match_set_lyr_2_4, mask, ttl_hoplimit);

	spec->udp_sport = MLX5_GET(fte_match_set_lyr_2_4, mask, udp_sport);
	spec->udp_dport = MLX5_GET(fte_match_set_lyr_2_4, mask, udp_dport);

	memcpy(raw_ip, MLX5_ADDR_OF(fte_match_set_lyr_2_4, mask,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
				    sizeof(raw_ip));

	spec->src_ip_127_96 = be32_to_cpu(raw_ip[0]);
	spec->src_ip_95_64 = be32_to_cpu(raw_ip[1]);
	spec->src_ip_63_32 = be32_to_cpu(raw_ip[2]);
	spec->src_ip_31_0 = be32_to_cpu(raw_ip[3]);

	memcpy(raw_ip, MLX5_ADDR_OF(fte_match_set_lyr_2_4, mask,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
				    sizeof(raw_ip));

	spec->dst_ip_127_96 = be32_to_cpu(raw_ip[0]);
	spec->dst_ip_95_64 = be32_to_cpu(raw_ip[1]);
	spec->dst_ip_63_32 = be32_to_cpu(raw_ip[2]);
	spec->dst_ip_31_0 = be32_to_cpu(raw_ip[3]);
}

static void dr_ste_copy_mask_misc2(char *mask, struct mlx5dr_match_misc2 *spec)
{
	spec->outer_first_mpls_label =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls.mpls_label);
	spec->outer_first_mpls_exp =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls.mpls_exp);
	spec->outer_first_mpls_s_bos =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls.mpls_s_bos);
	spec->outer_first_mpls_ttl =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls.mpls_ttl);
	spec->inner_first_mpls_label =
		MLX5_GET(fte_match_set_misc2, mask, inner_first_mpls.mpls_label);
	spec->inner_first_mpls_exp =
		MLX5_GET(fte_match_set_misc2, mask, inner_first_mpls.mpls_exp);
	spec->inner_first_mpls_s_bos =
		MLX5_GET(fte_match_set_misc2, mask, inner_first_mpls.mpls_s_bos);
	spec->inner_first_mpls_ttl =
		MLX5_GET(fte_match_set_misc2, mask, inner_first_mpls.mpls_ttl);
	spec->outer_first_mpls_over_gre_label =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_gre.mpls_label);
	spec->outer_first_mpls_over_gre_exp =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_gre.mpls_exp);
	spec->outer_first_mpls_over_gre_s_bos =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_gre.mpls_s_bos);
	spec->outer_first_mpls_over_gre_ttl =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_gre.mpls_ttl);
	spec->outer_first_mpls_over_udp_label =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_udp.mpls_label);
	spec->outer_first_mpls_over_udp_exp =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_udp.mpls_exp);
	spec->outer_first_mpls_over_udp_s_bos =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_udp.mpls_s_bos);
	spec->outer_first_mpls_over_udp_ttl =
		MLX5_GET(fte_match_set_misc2, mask, outer_first_mpls_over_udp.mpls_ttl);
	spec->metadata_reg_c_7 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_7);
	spec->metadata_reg_c_6 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_6);
	spec->metadata_reg_c_5 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_5);
	spec->metadata_reg_c_4 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_4);
	spec->metadata_reg_c_3 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_3);
	spec->metadata_reg_c_2 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_2);
	spec->metadata_reg_c_1 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_1);
	spec->metadata_reg_c_0 = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_c_0);
	spec->metadata_reg_a = MLX5_GET(fte_match_set_misc2, mask, metadata_reg_a);
}

static void dr_ste_copy_mask_misc3(char *mask, struct mlx5dr_match_misc3 *spec)
{
	spec->inner_tcp_seq_num = MLX5_GET(fte_match_set_misc3, mask, inner_tcp_seq_num);
	spec->outer_tcp_seq_num = MLX5_GET(fte_match_set_misc3, mask, outer_tcp_seq_num);
	spec->inner_tcp_ack_num = MLX5_GET(fte_match_set_misc3, mask, inner_tcp_ack_num);
	spec->outer_tcp_ack_num = MLX5_GET(fte_match_set_misc3, mask, outer_tcp_ack_num);
	spec->outer_vxlan_gpe_vni =
		MLX5_GET(fte_match_set_misc3, mask, outer_vxlan_gpe_vni);
	spec->outer_vxlan_gpe_next_protocol =
		MLX5_GET(fte_match_set_misc3, mask, outer_vxlan_gpe_next_protocol);
	spec->outer_vxlan_gpe_flags =
		MLX5_GET(fte_match_set_misc3, mask, outer_vxlan_gpe_flags);
	spec->icmpv4_header_data = MLX5_GET(fte_match_set_misc3, mask, icmp_header_data);
	spec->icmpv6_header_data =
		MLX5_GET(fte_match_set_misc3, mask, icmpv6_header_data);
	spec->icmpv4_type = MLX5_GET(fte_match_set_misc3, mask, icmp_type);
	spec->icmpv4_code = MLX5_GET(fte_match_set_misc3, mask, icmp_code);
	spec->icmpv6_type = MLX5_GET(fte_match_set_misc3, mask, icmpv6_type);
	spec->icmpv6_code = MLX5_GET(fte_match_set_misc3, mask, icmpv6_code);
	spec->geneve_tlv_option_0_data =
		MLX5_GET(fte_match_set_misc3, mask, geneve_tlv_option_0_data);
	spec->gtpu_msg_flags = MLX5_GET(fte_match_set_misc3, mask, gtpu_msg_flags);
	spec->gtpu_msg_type = MLX5_GET(fte_match_set_misc3, mask, gtpu_msg_type);
	spec->gtpu_teid = MLX5_GET(fte_match_set_misc3, mask, gtpu_teid);
	spec->gtpu_dw_0 = MLX5_GET(fte_match_set_misc3, mask, gtpu_dw_0);
	spec->gtpu_dw_2 = MLX5_GET(fte_match_set_misc3, mask, gtpu_dw_2);
	spec->gtpu_first_ext_dw_0 =
		MLX5_GET(fte_match_set_misc3, mask, gtpu_first_ext_dw_0);
}

static void dr_ste_copy_mask_misc4(char *mask, struct mlx5dr_match_misc4 *spec)
{
	spec->prog_sample_field_id_0 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_id_0);
	spec->prog_sample_field_value_0 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_value_0);
	spec->prog_sample_field_id_1 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_id_1);
	spec->prog_sample_field_value_1 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_value_1);
	spec->prog_sample_field_id_2 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_id_2);
	spec->prog_sample_field_value_2 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_value_2);
	spec->prog_sample_field_id_3 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_id_3);
	spec->prog_sample_field_value_3 =
		MLX5_GET(fte_match_set_misc4, mask, prog_sample_field_value_3);
}

void mlx5dr_ste_copy_param(u8 match_criteria,
			   struct mlx5dr_match_param *set_param,
			   struct mlx5dr_match_parameters *mask)
{
	u8 tail_param[MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4)] = {};
	u8 *data = (u8 *)mask->match_buf;
	size_t param_location;
	void *buff;

	if (match_criteria & DR_MATCHER_CRITERIA_OUTER) {
		if (mask->match_sz < sizeof(struct mlx5dr_match_spec)) {
			memcpy(tail_param, data, mask->match_sz);
			buff = tail_param;
		} else {
			buff = mask->match_buf;
		}
		dr_ste_copy_mask_spec(buff, &set_param->outer);
	}
	param_location = sizeof(struct mlx5dr_match_spec);

	if (match_criteria & DR_MATCHER_CRITERIA_MISC) {
		if (mask->match_sz < param_location +
		    sizeof(struct mlx5dr_match_misc)) {
			memcpy(tail_param, data + param_location,
			       mask->match_sz - param_location);
			buff = tail_param;
		} else {
			buff = data + param_location;
		}
		dr_ste_copy_mask_misc(buff, &set_param->misc);
	}
	param_location += sizeof(struct mlx5dr_match_misc);

	if (match_criteria & DR_MATCHER_CRITERIA_INNER) {
		if (mask->match_sz < param_location +
		    sizeof(struct mlx5dr_match_spec)) {
			memcpy(tail_param, data + param_location,
			       mask->match_sz - param_location);
			buff = tail_param;
		} else {
			buff = data + param_location;
		}
		dr_ste_copy_mask_spec(buff, &set_param->inner);
	}
	param_location += sizeof(struct mlx5dr_match_spec);

	if (match_criteria & DR_MATCHER_CRITERIA_MISC2) {
		if (mask->match_sz < param_location +
		    sizeof(struct mlx5dr_match_misc2)) {
			memcpy(tail_param, data + param_location,
			       mask->match_sz - param_location);
			buff = tail_param;
		} else {
			buff = data + param_location;
		}
		dr_ste_copy_mask_misc2(buff, &set_param->misc2);
	}

	param_location += sizeof(struct mlx5dr_match_misc2);

	if (match_criteria & DR_MATCHER_CRITERIA_MISC3) {
		if (mask->match_sz < param_location +
		    sizeof(struct mlx5dr_match_misc3)) {
			memcpy(tail_param, data + param_location,
			       mask->match_sz - param_location);
			buff = tail_param;
		} else {
			buff = data + param_location;
		}
		dr_ste_copy_mask_misc3(buff, &set_param->misc3);
	}

	param_location += sizeof(struct mlx5dr_match_misc3);

	if (match_criteria & DR_MATCHER_CRITERIA_MISC4) {
		if (mask->match_sz < param_location +
		    sizeof(struct mlx5dr_match_misc4)) {
			memcpy(tail_param, data + param_location,
			       mask->match_sz - param_location);
			buff = tail_param;
		} else {
			buff = data + param_location;
		}
		dr_ste_copy_mask_misc4(buff, &set_param->misc4);
	}
}

void mlx5dr_ste_build_eth_l2_src_dst(struct mlx5dr_ste_ctx *ste_ctx,
				     struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask,
				     bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l2_src_dst_init(sb, mask);
}

void mlx5dr_ste_build_eth_l3_ipv6_dst(struct mlx5dr_ste_ctx *ste_ctx,
				      struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l3_ipv6_dst_init(sb, mask);
}

void mlx5dr_ste_build_eth_l3_ipv6_src(struct mlx5dr_ste_ctx *ste_ctx,
				      struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l3_ipv6_src_init(sb, mask);
}

void mlx5dr_ste_build_eth_l3_ipv4_5_tuple(struct mlx5dr_ste_ctx *ste_ctx,
					  struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask,
					  bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l3_ipv4_5_tuple_init(sb, mask);
}

void mlx5dr_ste_build_eth_l2_src(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l2_src_init(sb, mask);
}

void mlx5dr_ste_build_eth_l2_dst(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l2_dst_init(sb, mask);
}

void mlx5dr_ste_build_eth_l2_tnl(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask, bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l2_tnl_init(sb, mask);
}

void mlx5dr_ste_build_eth_l3_ipv4_misc(struct mlx5dr_ste_ctx *ste_ctx,
				       struct mlx5dr_ste_build *sb,
				       struct mlx5dr_match_param *mask,
				       bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l3_ipv4_misc_init(sb, mask);
}

void mlx5dr_ste_build_eth_ipv6_l3_l4(struct mlx5dr_ste_ctx *ste_ctx,
				     struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask,
				     bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_ipv6_l3_l4_init(sb, mask);
}

static int dr_ste_build_empty_always_hit_tag(struct mlx5dr_match_param *value,
					     struct mlx5dr_ste_build *sb,
					     u8 *tag)
{
	return 0;
}

void mlx5dr_ste_build_empty_always_hit(struct mlx5dr_ste_build *sb, bool rx)
{
	sb->rx = rx;
	sb->lu_type = MLX5DR_STE_LU_TYPE_DONT_CARE;
	sb->byte_mask = 0;
	sb->ste_build_tag_func = &dr_ste_build_empty_always_hit_tag;
}

void mlx5dr_ste_build_mpls(struct mlx5dr_ste_ctx *ste_ctx,
			   struct mlx5dr_ste_build *sb,
			   struct mlx5dr_match_param *mask,
			   bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_mpls_init(sb, mask);
}

void mlx5dr_ste_build_tnl_gre(struct mlx5dr_ste_ctx *ste_ctx,
			      struct mlx5dr_ste_build *sb,
			      struct mlx5dr_match_param *mask,
			      bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_tnl_gre_init(sb, mask);
}

void mlx5dr_ste_build_tnl_mpls_over_gre(struct mlx5dr_ste_ctx *ste_ctx,
					struct mlx5dr_ste_build *sb,
					struct mlx5dr_match_param *mask,
					struct mlx5dr_cmd_caps *caps,
					bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	sb->caps = caps;
	return ste_ctx->build_tnl_mpls_over_gre_init(sb, mask);
}

void mlx5dr_ste_build_tnl_mpls_over_udp(struct mlx5dr_ste_ctx *ste_ctx,
					struct mlx5dr_ste_build *sb,
					struct mlx5dr_match_param *mask,
					struct mlx5dr_cmd_caps *caps,
					bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	sb->caps = caps;
	return ste_ctx->build_tnl_mpls_over_udp_init(sb, mask);
}

void mlx5dr_ste_build_icmp(struct mlx5dr_ste_ctx *ste_ctx,
			   struct mlx5dr_ste_build *sb,
			   struct mlx5dr_match_param *mask,
			   struct mlx5dr_cmd_caps *caps,
			   bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	sb->caps = caps;
	ste_ctx->build_icmp_init(sb, mask);
}

void mlx5dr_ste_build_general_purpose(struct mlx5dr_ste_ctx *ste_ctx,
				      struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_general_purpose_init(sb, mask);
}

void mlx5dr_ste_build_eth_l4_misc(struct mlx5dr_ste_ctx *ste_ctx,
				  struct mlx5dr_ste_build *sb,
				  struct mlx5dr_match_param *mask,
				  bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_eth_l4_misc_init(sb, mask);
}

void mlx5dr_ste_build_tnl_vxlan_gpe(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_tnl_vxlan_gpe_init(sb, mask);
}

void mlx5dr_ste_build_tnl_geneve(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_tnl_geneve_init(sb, mask);
}

void mlx5dr_ste_build_tnl_geneve_tlv_opt(struct mlx5dr_ste_ctx *ste_ctx,
					 struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask,
					 struct mlx5dr_cmd_caps *caps,
					 bool inner, bool rx)
{
	sb->rx = rx;
	sb->caps = caps;
	sb->inner = inner;
	ste_ctx->build_tnl_geneve_tlv_opt_init(sb, mask);
}

void mlx5dr_ste_build_tnl_gtpu(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_ste_build *sb,
			       struct mlx5dr_match_param *mask,
			       bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_tnl_gtpu_init(sb, mask);
}

void mlx5dr_ste_build_tnl_gtpu_flex_parser_0(struct mlx5dr_ste_ctx *ste_ctx,
					     struct mlx5dr_ste_build *sb,
					     struct mlx5dr_match_param *mask,
					     struct mlx5dr_cmd_caps *caps,
					     bool inner, bool rx)
{
	sb->rx = rx;
	sb->caps = caps;
	sb->inner = inner;
	ste_ctx->build_tnl_gtpu_flex_parser_0_init(sb, mask);
}

void mlx5dr_ste_build_tnl_gtpu_flex_parser_1(struct mlx5dr_ste_ctx *ste_ctx,
					     struct mlx5dr_ste_build *sb,
					     struct mlx5dr_match_param *mask,
					     struct mlx5dr_cmd_caps *caps,
					     bool inner, bool rx)
{
	sb->rx = rx;
	sb->caps = caps;
	sb->inner = inner;
	ste_ctx->build_tnl_gtpu_flex_parser_1_init(sb, mask);
}

void mlx5dr_ste_build_register_0(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_register_0_init(sb, mask);
}

void mlx5dr_ste_build_register_1(struct mlx5dr_ste_ctx *ste_ctx,
				 struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_register_1_init(sb, mask);
}

void mlx5dr_ste_build_src_gvmi_qpn(struct mlx5dr_ste_ctx *ste_ctx,
				   struct mlx5dr_ste_build *sb,
				   struct mlx5dr_match_param *mask,
				   struct mlx5dr_domain *dmn,
				   bool inner, bool rx)
{
	/* Set vhca_id_valid before we reset source_eswitch_owner_vhca_id */
	sb->vhca_id_valid = mask->misc.source_eswitch_owner_vhca_id;

	sb->rx = rx;
	sb->dmn = dmn;
	sb->inner = inner;
	ste_ctx->build_src_gvmi_qpn_init(sb, mask);
}

void mlx5dr_ste_build_flex_parser_0(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_flex_parser_0_init(sb, mask);
}

void mlx5dr_ste_build_flex_parser_1(struct mlx5dr_ste_ctx *ste_ctx,
				    struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx)
{
	sb->rx = rx;
	sb->inner = inner;
	ste_ctx->build_flex_parser_1_init(sb, mask);
}

static struct mlx5dr_ste_ctx *mlx5dr_ste_ctx_arr[] = {
	[MLX5_STEERING_FORMAT_CONNECTX_5] = &ste_ctx_v0,
	[MLX5_STEERING_FORMAT_CONNECTX_6DX] = &ste_ctx_v1,
};

struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx(u8 version)
{
	if (version > MLX5_STEERING_FORMAT_CONNECTX_6DX)
		return NULL;

	return mlx5dr_ste_ctx_arr[version];
}
