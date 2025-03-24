/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef HWS_ACTION_H_
#define HWS_ACTION_H_

/* Max number of STEs needed for a rule (including match) */
#define MLX5HWS_ACTION_MAX_STE 20

/* Max number of internal subactions of ipv6_ext */
#define MLX5HWS_ACTION_IPV6_EXT_MAX_SA 4

enum mlx5hws_action_stc_idx {
	MLX5HWS_ACTION_STC_IDX_CTRL = 0,
	MLX5HWS_ACTION_STC_IDX_HIT = 1,
	MLX5HWS_ACTION_STC_IDX_DW5 = 2,
	MLX5HWS_ACTION_STC_IDX_DW6 = 3,
	MLX5HWS_ACTION_STC_IDX_DW7 = 4,
	MLX5HWS_ACTION_STC_IDX_MAX = 5,
	/* STC Jumvo STE combo: CTR, Hit */
	MLX5HWS_ACTION_STC_IDX_LAST_JUMBO_STE = 1,
	/* STC combo1: CTR, SINGLE, DOUBLE, Hit */
	MLX5HWS_ACTION_STC_IDX_LAST_COMBO1 = 3,
	/* STC combo2: CTR, 3 x SINGLE, Hit */
	MLX5HWS_ACTION_STC_IDX_LAST_COMBO2 = 4,
	/* STC combo2: CTR, TRIPLE, Hit */
	MLX5HWS_ACTION_STC_IDX_LAST_COMBO3 = 2,
};

enum mlx5hws_action_offset {
	MLX5HWS_ACTION_OFFSET_DW0 = 0,
	MLX5HWS_ACTION_OFFSET_DW5 = 5,
	MLX5HWS_ACTION_OFFSET_DW6 = 6,
	MLX5HWS_ACTION_OFFSET_DW7 = 7,
	MLX5HWS_ACTION_OFFSET_HIT = 3,
	MLX5HWS_ACTION_OFFSET_HIT_LSB = 4,
};

enum {
	MLX5HWS_ACTION_DOUBLE_SIZE = 8,
	MLX5HWS_ACTION_INLINE_DATA_SIZE = 4,
	MLX5HWS_ACTION_HDR_LEN_L2_MACS = 12,
	MLX5HWS_ACTION_HDR_LEN_L2_VLAN = 4,
	MLX5HWS_ACTION_HDR_LEN_L2_ETHER = 2,
	MLX5HWS_ACTION_HDR_LEN_L2 = (MLX5HWS_ACTION_HDR_LEN_L2_MACS +
				     MLX5HWS_ACTION_HDR_LEN_L2_ETHER),
	MLX5HWS_ACTION_HDR_LEN_L2_W_VLAN = (MLX5HWS_ACTION_HDR_LEN_L2 +
					    MLX5HWS_ACTION_HDR_LEN_L2_VLAN),
	MLX5HWS_ACTION_REFORMAT_DATA_SIZE = 64,
	DECAP_L3_NUM_ACTIONS_W_NO_VLAN = 6,
	DECAP_L3_NUM_ACTIONS_W_VLAN = 7,
};

enum mlx5hws_action_setter_flag {
	ASF_SINGLE1 = 1 << 0,
	ASF_SINGLE2 = 1 << 1,
	ASF_SINGLE3 = 1 << 2,
	ASF_DOUBLE = ASF_SINGLE2 | ASF_SINGLE3,
	ASF_TRIPLE = ASF_SINGLE1 | ASF_DOUBLE,
	ASF_INSERT = 1 << 3,
	ASF_REMOVE = 1 << 4,
	ASF_MODIFY = 1 << 5,
	ASF_CTR = 1 << 6,
	ASF_HIT = 1 << 7,
};

struct mlx5hws_action_default_stc {
	struct mlx5hws_pool_chunk nop_ctr;
	struct mlx5hws_pool_chunk nop_dw5;
	struct mlx5hws_pool_chunk nop_dw6;
	struct mlx5hws_pool_chunk nop_dw7;
	struct mlx5hws_pool_chunk default_hit;
	u32 refcount; /* protected by context ctrl lock */
};

struct mlx5hws_action_shared_stc {
	struct mlx5hws_pool_chunk stc_chunk;
	u32 refcount; /* protected by context ctrl lock */
};

struct mlx5hws_actions_apply_data {
	struct mlx5hws_send_engine *queue;
	struct mlx5hws_rule_action *rule_action;
	__be32 *wqe_data;
	struct mlx5hws_wqe_gta_ctrl_seg *wqe_ctrl;
	u32 jump_to_action_stc;
	struct mlx5hws_context_common_res *common_res;
	enum mlx5hws_table_type tbl_type;
	u32 next_direct_idx;
	u8 require_dep;
};

struct mlx5hws_actions_wqe_setter;

typedef void (*mlx5hws_action_setter_fp)(struct mlx5hws_actions_apply_data *apply,
					 struct mlx5hws_actions_wqe_setter *setter);

struct mlx5hws_actions_wqe_setter {
	mlx5hws_action_setter_fp set_single;
	mlx5hws_action_setter_fp set_double;
	mlx5hws_action_setter_fp set_triple;
	mlx5hws_action_setter_fp set_hit;
	mlx5hws_action_setter_fp set_ctr;
	u8 idx_single;
	u8 idx_double;
	u8 idx_triple;
	u8 idx_ctr;
	u8 idx_hit;
	u8 stage_idx;
	u8 flags;
};

struct mlx5hws_action_template {
	struct mlx5hws_actions_wqe_setter setters[MLX5HWS_ACTION_MAX_STE];
	enum mlx5hws_action_type *action_type_arr;
	u8 num_of_action_stes;
	u8 num_actions;
	u8 only_term;
};

struct mlx5hws_action {
	u8 type;
	u8 flags;
	struct mlx5hws_context *ctx;
	union {
		struct {
			struct mlx5hws_pool_chunk stc;
			union {
				struct {
					u32 pat_id;
					u32 arg_id;
					__be64 single_action;
					u32 nope_locations;
					u8 num_of_patterns;
					u8 single_action_type;
					u8 num_of_actions;
					u8 max_num_of_actions;
					u8 require_reparse;
				} modify_header;
				struct {
					u32 arg_id;
					u32 header_size;
					u16 max_hdr_sz;
					u8 num_of_hdrs;
					u8 anchor;
					u8 e_anchor;
					u8 offset;
					bool encap;
					u8 require_reparse;
				} reformat;
				struct {
					u32 obj_id;
					u8 return_reg_id;
				} aso;
				struct {
					u16 vport_num;
					u16 esw_owner_vhca_id;
					bool esw_owner_vhca_id_valid;
				} vport;
				struct {
					u32 obj_id;
				} dest_obj;
				struct {
					struct mlx5hws_cmd_forward_tbl *fw_island;
					size_t num_dest;
					struct mlx5hws_cmd_set_fte_dest *dest_list;
				} dest_array;
				struct {
					struct mlx5hws_cmd_forward_tbl *fw_island;
				} flow_sampler;
				struct {
					u8 type;
					u8 start_anchor;
					u8 end_anchor;
					u8 num_of_words;
					bool decap;
				} insert_hdr;
				struct {
					/* PRM start anchor from which header will be removed */
					u8 anchor;
					/* Header remove offset in bytes, from the start
					 * anchor to the location where remove header starts.
					 */
					u8 offset;
					/* Indicates the removed header size in bytes */
					size_t size;
				} remove_header;
				struct {
					struct mlx5hws_matcher_action_ste *table_ste;
					struct mlx5hws_action *hit_ft_action;
					struct mlx5hws_definer *definer;
				} range;
			};
		};

		struct ibv_flow_action *flow_action;
		u32 obj_id;
		struct ibv_qp *qp;
	};
};

const char *mlx5hws_action_type_to_str(enum mlx5hws_action_type action_type);

int mlx5hws_action_get_default_stc(struct mlx5hws_context *ctx,
				   u8 tbl_type);

void mlx5hws_action_put_default_stc(struct mlx5hws_context *ctx,
				    u8 tbl_type);

void mlx5hws_action_prepare_decap_l3_data(u8 *src, u8 *dst,
					  u16 num_of_actions);

int mlx5hws_action_template_process(struct mlx5hws_action_template *at);

bool mlx5hws_action_check_combo(struct mlx5hws_context *ctx,
				enum mlx5hws_action_type *user_actions,
				enum mlx5hws_table_type table_type);

int mlx5hws_action_alloc_single_stc(struct mlx5hws_context *ctx,
				    struct mlx5hws_cmd_stc_modify_attr *stc_attr,
				    u32 table_type,
				    struct mlx5hws_pool_chunk *stc);

void mlx5hws_action_free_single_stc(struct mlx5hws_context *ctx,
				    u32 table_type,
				    struct mlx5hws_pool_chunk *stc);

static inline void
mlx5hws_action_setter_default_single(struct mlx5hws_actions_apply_data *apply,
				     struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW5] = 0;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW5] =
		htonl(apply->common_res->default_stc->nop_dw5.offset);
}

static inline void
mlx5hws_action_setter_default_double(struct mlx5hws_actions_apply_data *apply,
				     struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = 0;
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = 0;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW6] =
		htonl(apply->common_res->default_stc->nop_dw6.offset);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] =
		htonl(apply->common_res->default_stc->nop_dw7.offset);
}

static inline void
mlx5hws_action_setter_default_ctr(struct mlx5hws_actions_apply_data *apply,
				  struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW0] = 0;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_CTRL] =
		htonl(apply->common_res->default_stc->nop_ctr.offset);
}

static inline void
mlx5hws_action_apply_setter(struct mlx5hws_actions_apply_data *apply,
			    struct mlx5hws_actions_wqe_setter *setter,
			    bool is_jumbo)
{
	u8 num_of_actions;

	/* Set control counter */
	if (setter->set_ctr)
		setter->set_ctr(apply, setter);
	else
		mlx5hws_action_setter_default_ctr(apply, setter);

	if (!is_jumbo) {
		if (unlikely(setter->set_triple)) {
			/* Set triple on match */
			setter->set_triple(apply, setter);
			num_of_actions = MLX5HWS_ACTION_STC_IDX_LAST_COMBO3;
		} else {
			/* Set single and double on match */
			if (setter->set_single)
				setter->set_single(apply, setter);
			else
				mlx5hws_action_setter_default_single(apply, setter);

			if (setter->set_double)
				setter->set_double(apply, setter);
			else
				mlx5hws_action_setter_default_double(apply, setter);

			num_of_actions = setter->set_double ?
				MLX5HWS_ACTION_STC_IDX_LAST_COMBO1 :
				MLX5HWS_ACTION_STC_IDX_LAST_COMBO2;
		}
	} else {
		apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW5] = 0;
		apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = 0;
		apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = 0;
		apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW5] = 0;
		apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW6] = 0;
		apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] = 0;
		num_of_actions = MLX5HWS_ACTION_STC_IDX_LAST_JUMBO_STE;
	}

	/* Set next/final hit action */
	setter->set_hit(apply, setter);

	/* Set number of actions */
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_CTRL] |=
		htonl(num_of_actions << 29);
}

#endif /* HWS_ACTION_H_ */
