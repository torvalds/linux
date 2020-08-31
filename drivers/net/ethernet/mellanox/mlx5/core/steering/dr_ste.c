// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/types.h>
#include <linux/crc32.h>
#include "dr_types.h"

#define DR_STE_CRC_POLY 0xEDB88320L
#define STE_IPV4 0x1
#define STE_IPV6 0x2
#define STE_TCP 0x1
#define STE_UDP 0x2
#define STE_SPI 0x3
#define IP_VERSION_IPV4 0x4
#define IP_VERSION_IPV6 0x6
#define STE_SVLAN 0x1
#define STE_CVLAN 0x2

#define DR_STE_ENABLE_FLOW_TAG BIT(31)

/* Set to STE a specific value using DR_STE_SET */
#define DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, value) do { \
	if ((spec)->s_fname) { \
		MLX5_SET(ste_##lookup_type, tag, t_fname, value); \
		(spec)->s_fname = 0; \
	} \
} while (0)

/* Set to STE spec->s_fname to tag->t_fname */
#define DR_STE_SET_TAG(lookup_type, tag, t_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, spec->s_fname)

/* Set to STE -1 to bit_mask->bm_fname and set spec->s_fname as used */
#define DR_STE_SET_MASK(lookup_type, bit_mask, bm_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, bit_mask, bm_fname, spec, s_fname, -1)

/* Set to STE spec->s_fname to bit_mask->bm_fname and set spec->s_fname as used */
#define DR_STE_SET_MASK_V(lookup_type, bit_mask, bm_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, bit_mask, bm_fname, spec, s_fname, (spec)->s_fname)

#define DR_STE_SET_TCP_FLAGS(lookup_type, tag, spec) do { \
	MLX5_SET(ste_##lookup_type, tag, tcp_ns, !!((spec)->tcp_flags & (1 << 8))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_cwr, !!((spec)->tcp_flags & (1 << 7))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_ece, !!((spec)->tcp_flags & (1 << 6))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_urg, !!((spec)->tcp_flags & (1 << 5))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_ack, !!((spec)->tcp_flags & (1 << 4))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_psh, !!((spec)->tcp_flags & (1 << 3))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_rst, !!((spec)->tcp_flags & (1 << 2))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_syn, !!((spec)->tcp_flags & (1 << 1))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_fin, !!((spec)->tcp_flags & (1 << 0))); \
} while (0)

#define DR_STE_SET_MPLS_MASK(lookup_type, mask, in_out, bit_mask) do { \
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_label, mask, \
			  in_out##_first_mpls_label);\
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_s_bos, mask, \
			  in_out##_first_mpls_s_bos); \
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_exp, mask, \
			  in_out##_first_mpls_exp); \
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_ttl, mask, \
			  in_out##_first_mpls_ttl); \
} while (0)

#define DR_STE_SET_MPLS_TAG(lookup_type, mask, in_out, tag) do { \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_label, mask, \
		       in_out##_first_mpls_label);\
	DR_STE_SET_TAG(lookup_type, tag, mpls0_s_bos, mask, \
		       in_out##_first_mpls_s_bos); \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_exp, mask, \
		       in_out##_first_mpls_exp); \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_ttl, mask, \
		       in_out##_first_mpls_ttl); \
} while (0)

#define DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(_misc) (\
	(_misc)->outer_first_mpls_over_gre_label || \
	(_misc)->outer_first_mpls_over_gre_exp || \
	(_misc)->outer_first_mpls_over_gre_s_bos || \
	(_misc)->outer_first_mpls_over_gre_ttl)
#define DR_STE_IS_OUTER_MPLS_OVER_UDP_SET(_misc) (\
	(_misc)->outer_first_mpls_over_udp_label || \
	(_misc)->outer_first_mpls_over_udp_exp || \
	(_misc)->outer_first_mpls_over_udp_s_bos || \
	(_misc)->outer_first_mpls_over_udp_ttl)

#define DR_STE_CALC_LU_TYPE(lookup_type, rx, inner) \
	((inner) ? MLX5DR_STE_LU_TYPE_##lookup_type##_I : \
		   (rx) ? MLX5DR_STE_LU_TYPE_##lookup_type##_D : \
			  MLX5DR_STE_LU_TYPE_##lookup_type##_O)

enum dr_ste_tunl_action {
	DR_STE_TUNL_ACTION_NONE		= 0,
	DR_STE_TUNL_ACTION_ENABLE	= 1,
	DR_STE_TUNL_ACTION_DECAP	= 2,
	DR_STE_TUNL_ACTION_L3_DECAP	= 3,
	DR_STE_TUNL_ACTION_POP_VLAN	= 4,
};

enum dr_ste_action_type {
	DR_STE_ACTION_TYPE_PUSH_VLAN	= 1,
	DR_STE_ACTION_TYPE_ENCAP_L3	= 3,
	DR_STE_ACTION_TYPE_ENCAP	= 4,
};

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

static u16 dr_ste_conv_bit_to_byte_mask(u8 *bit_mask)
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

void mlx5dr_ste_set_bit_mask(u8 *hw_ste_p, u8 *bit_mask)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;

	memcpy(hw_ste->mask, bit_mask, DR_STE_SIZE_MASK);
}

void mlx5dr_ste_rx_set_flow_tag(u8 *hw_ste_p, u32 flow_tag)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, qp_list_pointer,
		 DR_STE_ENABLE_FLOW_TAG | flow_tag);
}

void mlx5dr_ste_set_counter_id(u8 *hw_ste_p, u32 ctr_id)
{
	/* This can be used for both rx_steering_mult and for sx_transmit */
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, counter_trigger_15_0, ctr_id);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, counter_trigger_23_16, ctr_id >> 16);
}

void mlx5dr_ste_set_go_back_bit(u8 *hw_ste_p)
{
	MLX5_SET(ste_sx_transmit, hw_ste_p, go_back, 1);
}

void mlx5dr_ste_set_tx_push_vlan(u8 *hw_ste_p, u32 vlan_hdr,
				 bool go_back)
{
	MLX5_SET(ste_sx_transmit, hw_ste_p, action_type,
		 DR_STE_ACTION_TYPE_PUSH_VLAN);
	MLX5_SET(ste_sx_transmit, hw_ste_p, encap_pointer_vlan_data, vlan_hdr);
	/* Due to HW limitation we need to set this bit, otherwise reforamt +
	 * push vlan will not work.
	 */
	if (go_back)
		mlx5dr_ste_set_go_back_bit(hw_ste_p);
}

void mlx5dr_ste_set_tx_encap(void *hw_ste_p, u32 reformat_id, int size, bool encap_l3)
{
	MLX5_SET(ste_sx_transmit, hw_ste_p, action_type,
		 encap_l3 ? DR_STE_ACTION_TYPE_ENCAP_L3 : DR_STE_ACTION_TYPE_ENCAP);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_sx_transmit, hw_ste_p, action_description, size / 2);
	MLX5_SET(ste_sx_transmit, hw_ste_p, encap_pointer_vlan_data, reformat_id);
}

void mlx5dr_ste_set_rx_decap(u8 *hw_ste_p)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, tunneling_action,
		 DR_STE_TUNL_ACTION_DECAP);
}

void mlx5dr_ste_set_rx_pop_vlan(u8 *hw_ste_p)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, tunneling_action,
		 DR_STE_TUNL_ACTION_POP_VLAN);
}

void mlx5dr_ste_set_rx_decap_l3(u8 *hw_ste_p, bool vlan)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, tunneling_action,
		 DR_STE_TUNL_ACTION_L3_DECAP);
	MLX5_SET(ste_modify_packet, hw_ste_p, action_description, vlan ? 1 : 0);
}

void mlx5dr_ste_set_entry_type(u8 *hw_ste_p, u8 entry_type)
{
	MLX5_SET(ste_general, hw_ste_p, entry_type, entry_type);
}

u8 mlx5dr_ste_get_entry_type(u8 *hw_ste_p)
{
	return MLX5_GET(ste_general, hw_ste_p, entry_type);
}

void mlx5dr_ste_set_rewrite_actions(u8 *hw_ste_p, u16 num_of_actions,
				    u32 re_write_index)
{
	MLX5_SET(ste_modify_packet, hw_ste_p, number_of_re_write_actions,
		 num_of_actions);
	MLX5_SET(ste_modify_packet, hw_ste_p, header_re_write_actions_pointer,
		 re_write_index);
}

void mlx5dr_ste_set_hit_gvmi(u8 *hw_ste_p, u16 gvmi)
{
	MLX5_SET(ste_general, hw_ste_p, next_table_base_63_48, gvmi);
}

void mlx5dr_ste_init(u8 *hw_ste_p, u8 lu_type, u8 entry_type,
		     u16 gvmi)
{
	MLX5_SET(ste_general, hw_ste_p, entry_type, entry_type);
	MLX5_SET(ste_general, hw_ste_p, entry_sub_type, lu_type);
	MLX5_SET(ste_general, hw_ste_p, next_lu_type, MLX5DR_STE_LU_TYPE_DONT_CARE);

	/* Set GVMI once, this is the same for RX/TX
	 * bits 63_48 of next table base / miss address encode the next GVMI
	 */
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, gvmi, gvmi);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, next_table_base_63_48, gvmi);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, miss_address_63_48, gvmi);
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

u64 mlx5dr_ste_get_miss_addr(u8 *hw_ste)
{
	u64 index =
		(MLX5_GET(ste_rx_steering_mult, hw_ste, miss_address_31_6) |
		 MLX5_GET(ste_rx_steering_mult, hw_ste, miss_address_39_32) << 26);

	return index << 6;
}

void mlx5dr_ste_set_hit_addr(u8 *hw_ste, u64 icm_addr, u32 ht_size)
{
	u64 index = (icm_addr >> 5) | ht_size;

	MLX5_SET(ste_general, hw_ste, next_table_base_39_32_size, index >> 27);
	MLX5_SET(ste_general, hw_ste, next_table_base_31_5_size, index);
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

static void dr_ste_always_hit_htbl(struct mlx5dr_ste *ste,
				   struct mlx5dr_ste_htbl *next_htbl)
{
	struct mlx5dr_icm_chunk *chunk = next_htbl->chunk;
	u8 *hw_ste = ste->hw_ste;

	MLX5_SET(ste_general, hw_ste, byte_mask, next_htbl->byte_mask);
	MLX5_SET(ste_general, hw_ste, next_lu_type, next_htbl->lu_type);
	mlx5dr_ste_set_hit_addr(hw_ste, chunk->icm_addr, chunk->num_of_entries);

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
dr_ste_remove_head_ste(struct mlx5dr_ste *ste,
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
	mlx5dr_ste_always_miss_addr(&tmp_ste, miss_addr);
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
dr_ste_replace_head_ste(struct mlx5dr_ste *ste, struct mlx5dr_ste *next_ste,
			struct mlx5dr_ste_send_info *ste_info_head,
			struct list_head *send_ste_list,
			struct mlx5dr_ste_htbl *stats_tbl)

{
	struct mlx5dr_ste_htbl *next_miss_htbl;

	next_miss_htbl = next_ste->htbl;

	/* Remove from the miss_list the next_ste before copy */
	list_del_init(&next_ste->miss_list_node);

	/* All rule-members that use next_ste should know about that */
	mlx5dr_rule_update_rule_member(next_ste, ste);

	/* Move data from next into ste */
	dr_ste_replace(ste, next_ste);

	/* Del the htbl that contains the next_ste.
	 * The origin htbl stay with the same number of entries.
	 */
	mlx5dr_htbl_put(next_miss_htbl);

	mlx5dr_send_fill_and_append_ste_send_info(ste, DR_STE_SIZE_REDUCED,
						  0, ste->hw_ste,
						  ste_info_head,
						  send_ste_list,
						  true /* Copy data */);

	stats_tbl->ctrl.num_of_collisions--;
	stats_tbl->ctrl.num_of_valid_entries--;
}

/* Free ste that is located in the middle of the miss list:
 * |__| -->|_prev_ste_|->|_ste_|-->|_next_ste_|
 */
static void dr_ste_remove_middle_ste(struct mlx5dr_ste *ste,
				     struct mlx5dr_ste_send_info *ste_info,
				     struct list_head *send_ste_list,
				     struct mlx5dr_ste_htbl *stats_tbl)
{
	struct mlx5dr_ste *prev_ste;
	u64 miss_addr;

	prev_ste = list_prev_entry(ste, miss_list_node);
	if (WARN_ON(!prev_ste))
		return;

	miss_addr = mlx5dr_ste_get_miss_addr(ste->hw_ste);
	mlx5dr_ste_set_miss_addr(prev_ste->hw_ste, miss_addr);

	mlx5dr_send_fill_and_append_ste_send_info(prev_ste, DR_STE_SIZE_REDUCED, 0,
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
			dr_ste_remove_head_ste(ste, nic_matcher,
					       &ste_info_head,
					       &send_ste_list,
					       stats_tbl);
		} else {
			/* First but not only entry in the list */
			dr_ste_replace_head_ste(ste, next_ste, &ste_info_head,
						&send_ste_list, stats_tbl);
			put_on_origin_table = false;
		}
	} else { /* Ste in the middle of the list */
		dr_ste_remove_middle_ste(ste, &ste_info_head, &send_ste_list, stats_tbl);
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

void mlx5dr_ste_set_hit_addr_by_next_htbl(u8 *hw_ste,
					  struct mlx5dr_ste_htbl *next_htbl)
{
	struct mlx5dr_icm_chunk *chunk = next_htbl->chunk;

	mlx5dr_ste_set_hit_addr(hw_ste, chunk->icm_addr, chunk->num_of_entries);
}

void mlx5dr_ste_set_miss_addr(u8 *hw_ste_p, u64 miss_addr)
{
	u64 index = miss_addr >> 6;

	/* Miss address for TX and RX STEs located in the same offsets */
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, miss_address_39_32, index >> 26);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, miss_address_31_6, index);
}

void mlx5dr_ste_always_miss_addr(struct mlx5dr_ste *ste, u64 miss_addr)
{
	u8 *hw_ste = ste->hw_ste;

	MLX5_SET(ste_rx_steering_mult, hw_ste, next_lu_type, MLX5DR_STE_LU_TYPE_DONT_CARE);
	mlx5dr_ste_set_miss_addr(hw_ste, miss_addr);
	dr_ste_set_always_miss((struct dr_hw_ste_format *)ste->hw_ste);
}

/* Init one ste as a pattern for ste data array */
void mlx5dr_ste_set_formatted_ste(u16 gvmi,
				  struct mlx5dr_domain_rx_tx *nic_dmn,
				  struct mlx5dr_ste_htbl *htbl,
				  u8 *formatted_ste,
				  struct mlx5dr_htbl_connect_info *connect_info)
{
	struct mlx5dr_ste ste = {};

	mlx5dr_ste_init(formatted_ste, htbl->lu_type, nic_dmn->ste_type, gvmi);
	ste.hw_ste = formatted_ste;

	if (connect_info->type == CONNECT_HIT)
		dr_ste_always_hit_htbl(&ste, connect_info->hit_next_htbl);
	else
		mlx5dr_ste_always_miss_addr(&ste, connect_info->miss_icm_addr);
}

int mlx5dr_ste_htbl_init_and_postsend(struct mlx5dr_domain *dmn,
				      struct mlx5dr_domain_rx_tx *nic_dmn,
				      struct mlx5dr_ste_htbl *htbl,
				      struct mlx5dr_htbl_connect_info *connect_info,
				      bool update_hw_ste)
{
	u8 formatted_ste[DR_STE_SIZE] = {};

	mlx5dr_ste_set_formatted_ste(dmn->info.caps.gvmi,
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
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)cur_hw_ste;
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_matcher->nic_tbl->nic_dmn;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_htbl_connect_info info;
	struct mlx5dr_ste_htbl *next_htbl;

	if (!mlx5dr_ste_is_last_in_rule(nic_matcher, ste->ste_chain_location)) {
		u8 next_lu_type;
		u16 byte_mask;

		next_lu_type = MLX5_GET(ste_general, hw_ste, next_lu_type);
		byte_mask = MLX5_GET(ste_general, hw_ste, byte_mask);

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

		mlx5dr_ste_set_hit_addr_by_next_htbl(cur_hw_ste, next_htbl);
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
					      u8 lu_type, u16 byte_mask)
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
	struct mlx5dr_ste_build *sb;
	int ret, i;

	ret = mlx5dr_ste_build_pre_check(dmn, matcher->match_criteria,
					 &matcher->mask, value);
	if (ret)
		return ret;

	sb = nic_matcher->ste_builder;
	for (i = 0; i < nic_matcher->num_of_builders; i++) {
		mlx5dr_ste_init(ste_arr,
				sb->lu_type,
				nic_dmn->ste_type,
				dmn->info.caps.gvmi);

		mlx5dr_ste_set_bit_mask(ste_arr, sb->bit_mask);

		ret = sb->ste_build_tag_func(value, sb, ste_arr);
		if (ret)
			return ret;

		/* Connect the STEs */
		if (i < (nic_matcher->num_of_builders - 1)) {
			/* Need the next builder for these fields,
			 * not relevant for the last ste in the chain.
			 */
			sb++;
			MLX5_SET(ste_general, ste_arr, next_lu_type, sb->lu_type);
			MLX5_SET(ste_general, ste_arr, byte_mask, sb->byte_mask);
		}
		ste_arr += DR_STE_SIZE;
	}
	return 0;
}

static void dr_ste_build_eth_l2_src_des_bit_mask(struct mlx5dr_match_param *value,
						 bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, dmac_15_0, mask, dmac_15_0);

	if (mask->smac_47_16 || mask->smac_15_0) {
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, smac_47_32,
			 mask->smac_47_16 >> 16);
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, smac_31_0,
			 mask->smac_47_16 << 16 | mask->smac_15_0);
		mask->smac_47_16 = 0;
		mask->smac_15_0 = 0;
	}

	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK(eth_l2_src_dst, bit_mask, l3_type, mask, ip_version);

	if (mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
	} else if (mask->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, first_vlan_qualifier, -1);
		mask->svlan_tag = 0;
	}
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
}

static int dr_ste_build_eth_l2_src_des_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *hw_ste_p)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l2_src_dst, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, dmac_15_0, spec, dmac_15_0);

	if (spec->smac_47_16 || spec->smac_15_0) {
		MLX5_SET(ste_eth_l2_src_dst, tag, smac_47_32,
			 spec->smac_47_16 >> 16);
		MLX5_SET(ste_eth_l2_src_dst, tag, smac_31_0,
			 spec->smac_47_16 << 16 | spec->smac_15_0);
		spec->smac_47_16 = 0;
		spec->smac_15_0 = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			MLX5_SET(ste_eth_l2_src_dst, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			MLX5_SET(ste_eth_l2_src_dst, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			pr_info("Unsupported ip_version value\n");
			return -EINVAL;
		}
	}

	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_priority, spec, first_prio);

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}
	return 0;
}

void mlx5dr_ste_build_eth_l2_src_des(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask,
				     bool inner, bool rx)
{
	dr_ste_build_eth_l2_src_des_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL2_SRC_DST, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l2_src_des_tag;
}

static void dr_ste_build_eth_l3_ipv6_dst_bit_mask(struct mlx5dr_match_param *value,
						  bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv6_dst, bit_mask, dst_ip_127_96, mask, dst_ip_127_96);
	DR_STE_SET_MASK_V(eth_l3_ipv6_dst, bit_mask, dst_ip_95_64, mask, dst_ip_95_64);
	DR_STE_SET_MASK_V(eth_l3_ipv6_dst, bit_mask, dst_ip_63_32, mask, dst_ip_63_32);
	DR_STE_SET_MASK_V(eth_l3_ipv6_dst, bit_mask, dst_ip_31_0, mask, dst_ip_31_0);
}

static int dr_ste_build_eth_l3_ipv6_dst_tag(struct mlx5dr_match_param *value,
					    struct mlx5dr_ste_build *sb,
					    u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_127_96, spec, dst_ip_127_96);
	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_95_64, spec, dst_ip_95_64);
	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_63_32, spec, dst_ip_63_32);
	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_31_0, spec, dst_ip_31_0);

	return 0;
}

void mlx5dr_ste_build_eth_l3_ipv6_dst(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx)
{
	dr_ste_build_eth_l3_ipv6_dst_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV6_DST, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l3_ipv6_dst_tag;
}

static void dr_ste_build_eth_l3_ipv6_src_bit_mask(struct mlx5dr_match_param *value,
						  bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv6_src, bit_mask, src_ip_127_96, mask, src_ip_127_96);
	DR_STE_SET_MASK_V(eth_l3_ipv6_src, bit_mask, src_ip_95_64, mask, src_ip_95_64);
	DR_STE_SET_MASK_V(eth_l3_ipv6_src, bit_mask, src_ip_63_32, mask, src_ip_63_32);
	DR_STE_SET_MASK_V(eth_l3_ipv6_src, bit_mask, src_ip_31_0, mask, src_ip_31_0);
}

static int dr_ste_build_eth_l3_ipv6_src_tag(struct mlx5dr_match_param *value,
					    struct mlx5dr_ste_build *sb,
					    u8 *hw_ste_p)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_127_96, spec, src_ip_127_96);
	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_95_64, spec, src_ip_95_64);
	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_63_32, spec, src_ip_63_32);
	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_31_0, spec, src_ip_31_0);

	return 0;
}

void mlx5dr_ste_build_eth_l3_ipv6_src(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx)
{
	dr_ste_build_eth_l3_ipv6_src_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV6_SRC, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l3_ipv6_src_tag;
}

static void dr_ste_build_eth_l3_ipv4_5_tuple_bit_mask(struct mlx5dr_match_param *value,
						      bool inner,
						      u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  destination_address, mask, dst_ip_31_0);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  source_address, mask, src_ip_31_0);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  destination_port, mask, tcp_dport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  destination_port, mask, udp_dport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  source_port, mask, tcp_sport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  source_port, mask, udp_sport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  protocol, mask, ip_protocol);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  dscp, mask, ip_dscp);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask,
			  ecn, mask, ip_ecn);

	if (mask->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple, bit_mask, mask);
		mask->tcp_flags = 0;
	}
}

static int dr_ste_build_eth_l3_ipv4_5_tuple_tag(struct mlx5dr_match_param *value,
						struct mlx5dr_ste_build *sb,
						u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_address, spec, dst_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_address, spec, src_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, ecn, spec, ip_ecn);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

void mlx5dr_ste_build_eth_l3_ipv4_5_tuple(struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask,
					  bool inner, bool rx)
{
	dr_ste_build_eth_l3_ipv4_5_tuple_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV4_5_TUPLE, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l3_ipv4_5_tuple_tag;
}

static void
dr_ste_build_eth_l2_src_or_dst_bit_mask(struct mlx5dr_match_param *value,
					bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_MASK(eth_l2_src, bit_mask, l3_type, mask, ip_version);

	if (mask->svlan_tag || mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}

	if (inner) {
		if (misc_mask->inner_second_cvlan_tag ||
		    misc_mask->inner_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, bit_mask, second_vlan_qualifier, -1);
			misc_mask->inner_second_cvlan_tag = 0;
			misc_mask->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_MASK_V(eth_l2_src, bit_mask,
				  second_vlan_id, misc_mask, inner_second_vid);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask,
				  second_cfi, misc_mask, inner_second_cfi);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask,
				  second_priority, misc_mask, inner_second_prio);
	} else {
		if (misc_mask->outer_second_cvlan_tag ||
		    misc_mask->outer_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, bit_mask, second_vlan_qualifier, -1);
			misc_mask->outer_second_cvlan_tag = 0;
			misc_mask->outer_second_svlan_tag = 0;
		}

		DR_STE_SET_MASK_V(eth_l2_src, bit_mask,
				  second_vlan_id, misc_mask, outer_second_vid);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask,
				  second_cfi, misc_mask, outer_second_cfi);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask,
				  second_priority, misc_mask, outer_second_prio);
	}
}

static int dr_ste_build_eth_l2_src_or_dst_tag(struct mlx5dr_match_param *value,
					      bool inner, u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_spec *spec = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc_spec = &value->misc;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l2_src, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_src, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_src, tag, l3_ethertype, spec, ethertype);

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			MLX5_SET(ste_eth_l2_src, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			MLX5_SET(ste_eth_l2_src, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			pr_info("Unsupported ip_version value\n");
			return -EINVAL;
		}
	}

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_src, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (inner) {
		if (misc_spec->inner_second_cvlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->inner_second_cvlan_tag = 0;
		} else if (misc_spec->inner_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src, tag, second_vlan_id, misc_spec, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src, tag, second_cfi, misc_spec, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, tag, second_priority, misc_spec, inner_second_prio);
	} else {
		if (misc_spec->outer_second_cvlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->outer_second_cvlan_tag = 0;
		} else if (misc_spec->outer_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->outer_second_svlan_tag = 0;
		}
		DR_STE_SET_TAG(eth_l2_src, tag, second_vlan_id, misc_spec, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src, tag, second_cfi, misc_spec, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, tag, second_priority, misc_spec, outer_second_prio);
	}

	return 0;
}

static void dr_ste_build_eth_l2_src_bit_mask(struct mlx5dr_match_param *value,
					     bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, smac_15_0, mask, smac_15_0);

	dr_ste_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_build_eth_l2_src_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l2_src, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src, tag, smac_15_0, spec, smac_15_0);

	return dr_ste_build_eth_l2_src_or_dst_tag(value, sb->inner, hw_ste_p);
}

void mlx5dr_ste_build_eth_l2_src(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	dr_ste_build_eth_l2_src_bit_mask(mask, inner, sb->bit_mask);
	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL2_SRC, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l2_src_tag;
}

static void dr_ste_build_eth_l2_dst_bit_mask(struct mlx5dr_match_param *value,
					     bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_dst, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_dst, bit_mask, dmac_15_0, mask, dmac_15_0);

	dr_ste_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_build_eth_l2_dst_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l2_dst, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst, tag, dmac_15_0, spec, dmac_15_0);

	return dr_ste_build_eth_l2_src_or_dst_tag(value, sb->inner, hw_ste_p);
}

void mlx5dr_ste_build_eth_l2_dst(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	dr_ste_build_eth_l2_dst_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL2_DST, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l2_dst_tag;
}

static void dr_ste_build_eth_l2_tnl_bit_mask(struct mlx5dr_match_param *value,
					     bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, dmac_15_0, mask, dmac_15_0);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_MASK(eth_l2_tnl, bit_mask, l3_type, mask, ip_version);

	if (misc->vxlan_vni) {
		MLX5_SET(ste_eth_l2_tnl, bit_mask,
			 l2_tunneling_network_id, (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (mask->svlan_tag || mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_tnl, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}
}

static int dr_ste_build_eth_l2_tnl_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *hw_ste_p)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc *misc = &value->misc;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l2_tnl, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl, tag, dmac_15_0, spec, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl, tag, l3_ethertype, spec, ethertype);

	if (misc->vxlan_vni) {
		MLX5_SET(ste_eth_l2_tnl, tag, l2_tunneling_network_id,
			 (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_tnl, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_tnl, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			MLX5_SET(ste_eth_l2_tnl, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			MLX5_SET(ste_eth_l2_tnl, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

void mlx5dr_ste_build_eth_l2_tnl(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask, bool inner, bool rx)
{
	dr_ste_build_eth_l2_tnl_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_ETHL2_TUNNELING_I;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l2_tnl_tag;
}

static void dr_ste_build_eth_l3_ipv4_misc_bit_mask(struct mlx5dr_match_param *value,
						   bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv4_misc, bit_mask, time_to_live, mask, ttl_hoplimit);
}

static int dr_ste_build_eth_l3_ipv4_misc_tag(struct mlx5dr_match_param *value,
					     struct mlx5dr_ste_build *sb,
					     u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l3_ipv4_misc, tag, time_to_live, spec, ttl_hoplimit);

	return 0;
}

void mlx5dr_ste_build_eth_l3_ipv4_misc(struct mlx5dr_ste_build *sb,
				       struct mlx5dr_match_param *mask,
				       bool inner, bool rx)
{
	dr_ste_build_eth_l3_ipv4_misc_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV4_MISC, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l3_ipv4_misc_tag;
}

static void dr_ste_build_ipv6_l3_l4_bit_mask(struct mlx5dr_match_param *value,
					     bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l4, bit_mask, dst_port, mask, tcp_dport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, src_port, mask, tcp_sport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, dst_port, mask, udp_dport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, src_port, mask, udp_sport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, protocol, mask, ip_protocol);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, dscp, mask, ip_dscp);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, ecn, mask, ip_ecn);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, ipv6_hop_limit, mask, ttl_hoplimit);

	if (mask->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4, bit_mask, mask);
		mask->tcp_flags = 0;
	}
}

static int dr_ste_build_ipv6_l3_l4_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *hw_ste_p)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(eth_l4, tag, dst_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l4, tag, src_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l4, tag, dst_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l4, tag, src_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l4, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l4, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l4, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l4, tag, ecn, spec, ip_ecn);
	DR_STE_SET_TAG(eth_l4, tag, ipv6_hop_limit, spec, ttl_hoplimit);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

void mlx5dr_ste_build_ipv6_l3_l4(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	dr_ste_build_ipv6_l3_l4_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL4, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_ipv6_l3_l4_tag;
}

static int dr_ste_build_empty_always_hit_tag(struct mlx5dr_match_param *value,
					     struct mlx5dr_ste_build *sb,
					     u8 *hw_ste_p)
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

static void dr_ste_build_mpls_bit_mask(struct mlx5dr_match_param *value,
				       bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_misc2 *misc2_mask = &value->misc2;

	if (inner)
		DR_STE_SET_MPLS_MASK(mpls, misc2_mask, inner, bit_mask);
	else
		DR_STE_SET_MPLS_MASK(mpls, misc2_mask, outer, bit_mask);
}

static int dr_ste_build_mpls_tag(struct mlx5dr_match_param *value,
				 struct mlx5dr_ste_build *sb,
				 u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc2 *misc2_mask = &value->misc2;
	u8 *tag = hw_ste->tag;

	if (sb->inner)
		DR_STE_SET_MPLS_TAG(mpls, misc2_mask, inner, tag);
	else
		DR_STE_SET_MPLS_TAG(mpls, misc2_mask, outer, tag);

	return 0;
}

void mlx5dr_ste_build_mpls(struct mlx5dr_ste_build *sb,
			   struct mlx5dr_match_param *mask,
			   bool inner, bool rx)
{
	dr_ste_build_mpls_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(MPLS_FIRST, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_mpls_tag;
}

static void dr_ste_build_gre_bit_mask(struct mlx5dr_match_param *value,
				      bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK_V(gre, bit_mask, gre_protocol, misc_mask, gre_protocol);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_k_present, misc_mask, gre_k_present);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_key_h, misc_mask, gre_key_h);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_key_l, misc_mask, gre_key_l);

	DR_STE_SET_MASK_V(gre, bit_mask, gre_c_present, misc_mask, gre_c_present);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_s_present, misc_mask, gre_s_present);
}

static int dr_ste_build_gre_tag(struct mlx5dr_match_param *value,
				struct mlx5dr_ste_build *sb,
				u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct  mlx5dr_match_misc *misc = &value->misc;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(gre, tag, gre_protocol, misc, gre_protocol);

	DR_STE_SET_TAG(gre, tag, gre_k_present, misc, gre_k_present);
	DR_STE_SET_TAG(gre, tag, gre_key_h, misc, gre_key_h);
	DR_STE_SET_TAG(gre, tag, gre_key_l, misc, gre_key_l);

	DR_STE_SET_TAG(gre, tag, gre_c_present, misc, gre_c_present);

	DR_STE_SET_TAG(gre, tag, gre_s_present, misc, gre_s_present);

	return 0;
}

void mlx5dr_ste_build_gre(struct mlx5dr_ste_build *sb,
			  struct mlx5dr_match_param *mask, bool inner, bool rx)
{
	dr_ste_build_gre_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_GRE;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_gre_tag;
}

static void dr_ste_build_flex_parser_0_bit_mask(struct mlx5dr_match_param *value,
						bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_misc2 *misc_2_mask = &value->misc2;

	if (DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(misc_2_mask)) {
		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_label,
				  misc_2_mask, outer_first_mpls_over_gre_label);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_exp,
				  misc_2_mask, outer_first_mpls_over_gre_exp);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_s_bos,
				  misc_2_mask, outer_first_mpls_over_gre_s_bos);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_ttl,
				  misc_2_mask, outer_first_mpls_over_gre_ttl);
	} else {
		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_label,
				  misc_2_mask, outer_first_mpls_over_udp_label);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_exp,
				  misc_2_mask, outer_first_mpls_over_udp_exp);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_s_bos,
				  misc_2_mask, outer_first_mpls_over_udp_s_bos);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_ttl,
				  misc_2_mask, outer_first_mpls_over_udp_ttl);
	}
}

static int dr_ste_build_flex_parser_0_tag(struct mlx5dr_match_param *value,
					  struct mlx5dr_ste_build *sb,
					  u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc2 *misc_2_mask = &value->misc2;
	u8 *tag = hw_ste->tag;

	if (DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(misc_2_mask)) {
		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_label,
			       misc_2_mask, outer_first_mpls_over_gre_label);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_exp,
			       misc_2_mask, outer_first_mpls_over_gre_exp);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_s_bos,
			       misc_2_mask, outer_first_mpls_over_gre_s_bos);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_ttl,
			       misc_2_mask, outer_first_mpls_over_gre_ttl);
	} else {
		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_label,
			       misc_2_mask, outer_first_mpls_over_udp_label);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_exp,
			       misc_2_mask, outer_first_mpls_over_udp_exp);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_s_bos,
			       misc_2_mask, outer_first_mpls_over_udp_s_bos);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_ttl,
			       misc_2_mask, outer_first_mpls_over_udp_ttl);
	}
	return 0;
}

void mlx5dr_ste_build_flex_parser_0(struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask,
				    bool inner, bool rx)
{
	dr_ste_build_flex_parser_0_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_FLEX_PARSER_0;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_flex_parser_0_tag;
}

#define ICMP_TYPE_OFFSET_FIRST_DW		24
#define ICMP_CODE_OFFSET_FIRST_DW		16
#define ICMP_HEADER_DATA_OFFSET_SECOND_DW	0

static int dr_ste_build_flex_parser_1_bit_mask(struct mlx5dr_match_param *mask,
					       struct mlx5dr_cmd_caps *caps,
					       u8 *bit_mask)
{
	struct mlx5dr_match_misc3 *misc_3_mask = &mask->misc3;
	bool is_ipv4_mask = DR_MASK_IS_FLEX_PARSER_ICMPV4_SET(misc_3_mask);
	u32 icmp_header_data_mask;
	u32 icmp_type_mask;
	u32 icmp_code_mask;
	int dw0_location;
	int dw1_location;

	if (is_ipv4_mask) {
		icmp_header_data_mask	= misc_3_mask->icmpv4_header_data;
		icmp_type_mask		= misc_3_mask->icmpv4_type;
		icmp_code_mask		= misc_3_mask->icmpv4_code;
		dw0_location		= caps->flex_parser_id_icmp_dw0;
		dw1_location		= caps->flex_parser_id_icmp_dw1;
	} else {
		icmp_header_data_mask	= misc_3_mask->icmpv6_header_data;
		icmp_type_mask		= misc_3_mask->icmpv6_type;
		icmp_code_mask		= misc_3_mask->icmpv6_code;
		dw0_location		= caps->flex_parser_id_icmpv6_dw0;
		dw1_location		= caps->flex_parser_id_icmpv6_dw1;
	}

	switch (dw0_location) {
	case 4:
		if (icmp_type_mask) {
			MLX5_SET(ste_flex_parser_1, bit_mask, flex_parser_4,
				 (icmp_type_mask << ICMP_TYPE_OFFSET_FIRST_DW));
			if (is_ipv4_mask)
				misc_3_mask->icmpv4_type = 0;
			else
				misc_3_mask->icmpv6_type = 0;
		}
		if (icmp_code_mask) {
			u32 cur_val = MLX5_GET(ste_flex_parser_1, bit_mask,
					       flex_parser_4);
			MLX5_SET(ste_flex_parser_1, bit_mask, flex_parser_4,
				 cur_val | (icmp_code_mask << ICMP_CODE_OFFSET_FIRST_DW));
			if (is_ipv4_mask)
				misc_3_mask->icmpv4_code = 0;
			else
				misc_3_mask->icmpv6_code = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	switch (dw1_location) {
	case 5:
		if (icmp_header_data_mask) {
			MLX5_SET(ste_flex_parser_1, bit_mask, flex_parser_5,
				 (icmp_header_data_mask << ICMP_HEADER_DATA_OFFSET_SECOND_DW));
			if (is_ipv4_mask)
				misc_3_mask->icmpv4_header_data = 0;
			else
				misc_3_mask->icmpv6_header_data = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dr_ste_build_flex_parser_1_tag(struct mlx5dr_match_param *value,
					  struct mlx5dr_ste_build *sb,
					  u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc3 *misc_3 = &value->misc3;
	u8 *tag = hw_ste->tag;
	u32 icmp_header_data;
	int dw0_location;
	int dw1_location;
	u32 icmp_type;
	u32 icmp_code;
	bool is_ipv4;

	is_ipv4 = DR_MASK_IS_FLEX_PARSER_ICMPV4_SET(misc_3);
	if (is_ipv4) {
		icmp_header_data	= misc_3->icmpv4_header_data;
		icmp_type		= misc_3->icmpv4_type;
		icmp_code		= misc_3->icmpv4_code;
		dw0_location		= sb->caps->flex_parser_id_icmp_dw0;
		dw1_location		= sb->caps->flex_parser_id_icmp_dw1;
	} else {
		icmp_header_data	= misc_3->icmpv6_header_data;
		icmp_type		= misc_3->icmpv6_type;
		icmp_code		= misc_3->icmpv6_code;
		dw0_location		= sb->caps->flex_parser_id_icmpv6_dw0;
		dw1_location		= sb->caps->flex_parser_id_icmpv6_dw1;
	}

	switch (dw0_location) {
	case 4:
		if (icmp_type) {
			MLX5_SET(ste_flex_parser_1, tag, flex_parser_4,
				 (icmp_type << ICMP_TYPE_OFFSET_FIRST_DW));
			if (is_ipv4)
				misc_3->icmpv4_type = 0;
			else
				misc_3->icmpv6_type = 0;
		}

		if (icmp_code) {
			u32 cur_val = MLX5_GET(ste_flex_parser_1, tag,
					       flex_parser_4);
			MLX5_SET(ste_flex_parser_1, tag, flex_parser_4,
				 cur_val | (icmp_code << ICMP_CODE_OFFSET_FIRST_DW));
			if (is_ipv4)
				misc_3->icmpv4_code = 0;
			else
				misc_3->icmpv6_code = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	switch (dw1_location) {
	case 5:
		if (icmp_header_data) {
			MLX5_SET(ste_flex_parser_1, tag, flex_parser_5,
				 (icmp_header_data << ICMP_HEADER_DATA_OFFSET_SECOND_DW));
			if (is_ipv4)
				misc_3->icmpv4_header_data = 0;
			else
				misc_3->icmpv6_header_data = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int mlx5dr_ste_build_flex_parser_1(struct mlx5dr_ste_build *sb,
				   struct mlx5dr_match_param *mask,
				   struct mlx5dr_cmd_caps *caps,
				   bool inner, bool rx)
{
	int ret;

	ret = dr_ste_build_flex_parser_1_bit_mask(mask, caps, sb->bit_mask);
	if (ret)
		return ret;

	sb->rx = rx;
	sb->inner = inner;
	sb->caps = caps;
	sb->lu_type = MLX5DR_STE_LU_TYPE_FLEX_PARSER_1;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_flex_parser_1_tag;

	return 0;
}

static void dr_ste_build_general_purpose_bit_mask(struct mlx5dr_match_param *value,
						  bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_misc2 *misc_2_mask = &value->misc2;

	DR_STE_SET_MASK_V(general_purpose, bit_mask,
			  general_purpose_lookup_field, misc_2_mask,
			  metadata_reg_a);
}

static int dr_ste_build_general_purpose_tag(struct mlx5dr_match_param *value,
					    struct mlx5dr_ste_build *sb,
					    u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc2 *misc_2_mask = &value->misc2;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(general_purpose, tag, general_purpose_lookup_field,
		       misc_2_mask, metadata_reg_a);

	return 0;
}

void mlx5dr_ste_build_general_purpose(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask,
				      bool inner, bool rx)
{
	dr_ste_build_general_purpose_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_GENERAL_PURPOSE;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_general_purpose_tag;
}

static void dr_ste_build_eth_l4_misc_bit_mask(struct mlx5dr_match_param *value,
					      bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_misc3 *misc_3_mask = &value->misc3;

	if (inner) {
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, seq_num, misc_3_mask,
				  inner_tcp_seq_num);
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, ack_num, misc_3_mask,
				  inner_tcp_ack_num);
	} else {
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, seq_num, misc_3_mask,
				  outer_tcp_seq_num);
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, ack_num, misc_3_mask,
				  outer_tcp_ack_num);
	}
}

static int dr_ste_build_eth_l4_misc_tag(struct mlx5dr_match_param *value,
					struct mlx5dr_ste_build *sb,
					u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;
	u8 *tag = hw_ste->tag;

	if (sb->inner) {
		DR_STE_SET_TAG(eth_l4_misc, tag, seq_num, misc3, inner_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc, tag, ack_num, misc3, inner_tcp_ack_num);
	} else {
		DR_STE_SET_TAG(eth_l4_misc, tag, seq_num, misc3, outer_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc, tag, ack_num, misc3, outer_tcp_ack_num);
	}

	return 0;
}

void mlx5dr_ste_build_eth_l4_misc(struct mlx5dr_ste_build *sb,
				  struct mlx5dr_match_param *mask,
				  bool inner, bool rx)
{
	dr_ste_build_eth_l4_misc_bit_mask(mask, inner, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL4_MISC, rx, inner);
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_eth_l4_misc_tag;
}

static void
dr_ste_build_flex_parser_tnl_vxlan_gpe_bit_mask(struct mlx5dr_match_param *value,
						bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_misc3 *misc_3_mask = &value->misc3;

	DR_STE_SET_MASK_V(flex_parser_tnl_vxlan_gpe, bit_mask,
			  outer_vxlan_gpe_flags,
			  misc_3_mask, outer_vxlan_gpe_flags);
	DR_STE_SET_MASK_V(flex_parser_tnl_vxlan_gpe, bit_mask,
			  outer_vxlan_gpe_next_protocol,
			  misc_3_mask, outer_vxlan_gpe_next_protocol);
	DR_STE_SET_MASK_V(flex_parser_tnl_vxlan_gpe, bit_mask,
			  outer_vxlan_gpe_vni,
			  misc_3_mask, outer_vxlan_gpe_vni);
}

static int
dr_ste_build_flex_parser_tnl_vxlan_gpe_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(flex_parser_tnl_vxlan_gpe, tag,
		       outer_vxlan_gpe_flags, misc3,
		       outer_vxlan_gpe_flags);
	DR_STE_SET_TAG(flex_parser_tnl_vxlan_gpe, tag,
		       outer_vxlan_gpe_next_protocol, misc3,
		       outer_vxlan_gpe_next_protocol);
	DR_STE_SET_TAG(flex_parser_tnl_vxlan_gpe, tag,
		       outer_vxlan_gpe_vni, misc3,
		       outer_vxlan_gpe_vni);

	return 0;
}

void mlx5dr_ste_build_flex_parser_tnl_vxlan_gpe(struct mlx5dr_ste_build *sb,
						struct mlx5dr_match_param *mask,
						bool inner, bool rx)
{
	dr_ste_build_flex_parser_tnl_vxlan_gpe_bit_mask(mask, inner,
							sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_flex_parser_tnl_vxlan_gpe_tag;
}

static void
dr_ste_build_flex_parser_tnl_geneve_bit_mask(struct mlx5dr_match_param *value,
					     u8 *bit_mask)
{
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK_V(flex_parser_tnl_geneve, bit_mask,
			  geneve_protocol_type,
			  misc_mask, geneve_protocol_type);
	DR_STE_SET_MASK_V(flex_parser_tnl_geneve, bit_mask,
			  geneve_oam,
			  misc_mask, geneve_oam);
	DR_STE_SET_MASK_V(flex_parser_tnl_geneve, bit_mask,
			  geneve_opt_len,
			  misc_mask, geneve_opt_len);
	DR_STE_SET_MASK_V(flex_parser_tnl_geneve, bit_mask,
			  geneve_vni,
			  misc_mask, geneve_vni);
}

static int
dr_ste_build_flex_parser_tnl_geneve_tag(struct mlx5dr_match_param *value,
					struct mlx5dr_ste_build *sb,
					u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc *misc = &value->misc;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_protocol_type, misc, geneve_protocol_type);
	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_oam, misc, geneve_oam);
	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_opt_len, misc, geneve_opt_len);
	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_vni, misc, geneve_vni);

	return 0;
}

void mlx5dr_ste_build_flex_parser_tnl_geneve(struct mlx5dr_ste_build *sb,
					     struct mlx5dr_match_param *mask,
					     bool inner, bool rx)
{
	dr_ste_build_flex_parser_tnl_geneve_bit_mask(mask, sb->bit_mask);
	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_flex_parser_tnl_geneve_tag;
}

static void dr_ste_build_register_0_bit_mask(struct mlx5dr_match_param *value,
					     u8 *bit_mask)
{
	struct mlx5dr_match_misc2 *misc_2_mask = &value->misc2;

	DR_STE_SET_MASK_V(register_0, bit_mask, register_0_h,
			  misc_2_mask, metadata_reg_c_0);
	DR_STE_SET_MASK_V(register_0, bit_mask, register_0_l,
			  misc_2_mask, metadata_reg_c_1);
	DR_STE_SET_MASK_V(register_0, bit_mask, register_1_h,
			  misc_2_mask, metadata_reg_c_2);
	DR_STE_SET_MASK_V(register_0, bit_mask, register_1_l,
			  misc_2_mask, metadata_reg_c_3);
}

static int dr_ste_build_register_0_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(register_0, tag, register_0_h, misc2, metadata_reg_c_0);
	DR_STE_SET_TAG(register_0, tag, register_0_l, misc2, metadata_reg_c_1);
	DR_STE_SET_TAG(register_0, tag, register_1_h, misc2, metadata_reg_c_2);
	DR_STE_SET_TAG(register_0, tag, register_1_l, misc2, metadata_reg_c_3);

	return 0;
}

void mlx5dr_ste_build_register_0(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	dr_ste_build_register_0_bit_mask(mask, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_STEERING_REGISTERS_0;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_register_0_tag;
}

static void dr_ste_build_register_1_bit_mask(struct mlx5dr_match_param *value,
					     u8 *bit_mask)
{
	struct mlx5dr_match_misc2 *misc_2_mask = &value->misc2;

	DR_STE_SET_MASK_V(register_1, bit_mask, register_2_h,
			  misc_2_mask, metadata_reg_c_4);
	DR_STE_SET_MASK_V(register_1, bit_mask, register_2_l,
			  misc_2_mask, metadata_reg_c_5);
	DR_STE_SET_MASK_V(register_1, bit_mask, register_3_h,
			  misc_2_mask, metadata_reg_c_6);
	DR_STE_SET_MASK_V(register_1, bit_mask, register_3_l,
			  misc_2_mask, metadata_reg_c_7);
}

static int dr_ste_build_register_1_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;
	u8 *tag = hw_ste->tag;

	DR_STE_SET_TAG(register_1, tag, register_2_h, misc2, metadata_reg_c_4);
	DR_STE_SET_TAG(register_1, tag, register_2_l, misc2, metadata_reg_c_5);
	DR_STE_SET_TAG(register_1, tag, register_3_h, misc2, metadata_reg_c_6);
	DR_STE_SET_TAG(register_1, tag, register_3_l, misc2, metadata_reg_c_7);

	return 0;
}

void mlx5dr_ste_build_register_1(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask,
				 bool inner, bool rx)
{
	dr_ste_build_register_1_bit_mask(mask, sb->bit_mask);

	sb->rx = rx;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_STEERING_REGISTERS_1;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_register_1_tag;
}

static void dr_ste_build_src_gvmi_qpn_bit_mask(struct mlx5dr_match_param *value,
					       u8 *bit_mask)
{
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK(src_gvmi_qp, bit_mask, source_gvmi, misc_mask, source_port);
	DR_STE_SET_MASK(src_gvmi_qp, bit_mask, source_qp, misc_mask, source_sqn);
	misc_mask->source_eswitch_owner_vhca_id = 0;
}

static int dr_ste_build_src_gvmi_qpn_tag(struct mlx5dr_match_param *value,
					 struct mlx5dr_ste_build *sb,
					 u8 *hw_ste_p)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	struct mlx5dr_match_misc *misc = &value->misc;
	struct mlx5dr_cmd_vport_cap *vport_cap;
	struct mlx5dr_domain *dmn = sb->dmn;
	struct mlx5dr_cmd_caps *caps;
	u8 *bit_mask = sb->bit_mask;
	u8 *tag = hw_ste->tag;
	bool source_gvmi_set;

	DR_STE_SET_TAG(src_gvmi_qp, tag, source_qp, misc, source_sqn);

	if (sb->vhca_id_valid) {
		/* Find port GVMI based on the eswitch_owner_vhca_id */
		if (misc->source_eswitch_owner_vhca_id == dmn->info.caps.gvmi)
			caps = &dmn->info.caps;
		else if (dmn->peer_dmn && (misc->source_eswitch_owner_vhca_id ==
					   dmn->peer_dmn->info.caps.gvmi))
			caps = &dmn->peer_dmn->info.caps;
		else
			return -EINVAL;
	} else {
		caps = &dmn->info.caps;
	}

	vport_cap = mlx5dr_get_vport_cap(caps, misc->source_port);
	if (!vport_cap)
		return -EINVAL;

	source_gvmi_set = MLX5_GET(ste_src_gvmi_qp, bit_mask, source_gvmi);
	if (vport_cap->vport_gvmi && source_gvmi_set)
		MLX5_SET(ste_src_gvmi_qp, tag, source_gvmi, vport_cap->vport_gvmi);

	misc->source_eswitch_owner_vhca_id = 0;
	misc->source_port = 0;

	return 0;
}

void mlx5dr_ste_build_src_gvmi_qpn(struct mlx5dr_ste_build *sb,
				   struct mlx5dr_match_param *mask,
				   struct mlx5dr_domain *dmn,
				   bool inner, bool rx)
{
	/* Set vhca_id_valid before we reset source_eswitch_owner_vhca_id */
	sb->vhca_id_valid = mask->misc.source_eswitch_owner_vhca_id;

	dr_ste_build_src_gvmi_qpn_bit_mask(mask, sb->bit_mask);

	sb->rx = rx;
	sb->dmn = dmn;
	sb->inner = inner;
	sb->lu_type = MLX5DR_STE_LU_TYPE_SRC_GVMI_AND_QP;
	sb->byte_mask = dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_build_src_gvmi_qpn_tag;
}
