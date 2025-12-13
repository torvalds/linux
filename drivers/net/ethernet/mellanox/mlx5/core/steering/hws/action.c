// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

#define MLX5HWS_ACTION_METER_INIT_COLOR_OFFSET 1

/* Header removal size limited to 128B (64 words) */
#define MLX5HWS_ACTION_REMOVE_HEADER_MAX_SIZE 128

/* This is the longest supported action sequence for FDB table:
 * DECAP, POP_VLAN, MODIFY, CTR, ASO, PUSH_VLAN, MODIFY, ENCAP, Term.
 */
static const u32 action_order_arr[MLX5HWS_ACTION_TYP_MAX] = {
	BIT(MLX5HWS_ACTION_TYP_REMOVE_HEADER) |
	BIT(MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2) |
	BIT(MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2),
	BIT(MLX5HWS_ACTION_TYP_POP_VLAN),
	BIT(MLX5HWS_ACTION_TYP_POP_VLAN),
	BIT(MLX5HWS_ACTION_TYP_MODIFY_HDR),
	BIT(MLX5HWS_ACTION_TYP_PUSH_VLAN),
	BIT(MLX5HWS_ACTION_TYP_PUSH_VLAN),
	BIT(MLX5HWS_ACTION_TYP_INSERT_HEADER) |
	BIT(MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2) |
	BIT(MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3),
	BIT(MLX5HWS_ACTION_TYP_CTR),
	BIT(MLX5HWS_ACTION_TYP_TAG),
	BIT(MLX5HWS_ACTION_TYP_ASO_METER),
	BIT(MLX5HWS_ACTION_TYP_MODIFY_HDR),
	BIT(MLX5HWS_ACTION_TYP_TBL) |
	BIT(MLX5HWS_ACTION_TYP_VPORT) |
	BIT(MLX5HWS_ACTION_TYP_DROP) |
	BIT(MLX5HWS_ACTION_TYP_SAMPLER) |
	BIT(MLX5HWS_ACTION_TYP_RANGE) |
	BIT(MLX5HWS_ACTION_TYP_DEST_ARRAY),
	BIT(MLX5HWS_ACTION_TYP_LAST),
};

static const char * const mlx5hws_action_type_str[] = {
	[MLX5HWS_ACTION_TYP_LAST] = "LAST",
	[MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2] = "TNL_L2_TO_L2",
	[MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2] = "L2_TO_TNL_L2",
	[MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2] = "TNL_L3_TO_L2",
	[MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3] = "L2_TO_TNL_L3",
	[MLX5HWS_ACTION_TYP_DROP] = "DROP",
	[MLX5HWS_ACTION_TYP_TBL] = "TBL",
	[MLX5HWS_ACTION_TYP_CTR] = "CTR",
	[MLX5HWS_ACTION_TYP_TAG] = "TAG",
	[MLX5HWS_ACTION_TYP_MODIFY_HDR] = "MODIFY_HDR",
	[MLX5HWS_ACTION_TYP_VPORT] = "VPORT",
	[MLX5HWS_ACTION_TYP_MISS] = "DEFAULT_MISS",
	[MLX5HWS_ACTION_TYP_POP_VLAN] = "POP_VLAN",
	[MLX5HWS_ACTION_TYP_PUSH_VLAN] = "PUSH_VLAN",
	[MLX5HWS_ACTION_TYP_ASO_METER] = "ASO_METER",
	[MLX5HWS_ACTION_TYP_DEST_ARRAY] = "DEST_ARRAY",
	[MLX5HWS_ACTION_TYP_INSERT_HEADER] = "INSERT_HEADER",
	[MLX5HWS_ACTION_TYP_REMOVE_HEADER] = "REMOVE_HEADER",
	[MLX5HWS_ACTION_TYP_SAMPLER] = "SAMPLER",
	[MLX5HWS_ACTION_TYP_RANGE] = "RANGE",
};

static_assert(ARRAY_SIZE(mlx5hws_action_type_str) == MLX5HWS_ACTION_TYP_MAX,
	      "Missing mlx5hws_action_type_str");

const char *mlx5hws_action_type_to_str(enum mlx5hws_action_type action_type)
{
	return mlx5hws_action_type_str[action_type];
}

enum mlx5hws_action_type mlx5hws_action_get_type(struct mlx5hws_action *action)
{
	return action->type;
}

struct mlx5_core_dev *mlx5hws_action_get_dev(struct mlx5hws_action *action)
{
	return action->ctx->mdev;
}

static int hws_action_get_shared_stc_nic(struct mlx5hws_context *ctx,
					 enum mlx5hws_context_shared_stc_type stc_type,
					 u8 tbl_type)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = {0};
	struct mlx5hws_action_shared_stc *shared_stc;
	int ret;

	mutex_lock(&ctx->ctrl_lock);
	if (ctx->common_res.shared_stc[stc_type]) {
		ctx->common_res.shared_stc[stc_type]->refcount++;
		mutex_unlock(&ctx->ctrl_lock);
		return 0;
	}

	shared_stc = kzalloc(sizeof(*shared_stc), GFP_KERNEL);
	if (!shared_stc) {
		ret = -ENOMEM;
		goto unlock_and_out;
	}
	switch (stc_type) {
	case MLX5HWS_CONTEXT_SHARED_STC_DECAP_L3:
		stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_HEADER_REMOVE;
		stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_DW5;
		stc_attr.reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;
		stc_attr.remove_header.decap = 0;
		stc_attr.remove_header.start_anchor = MLX5_HEADER_ANCHOR_PACKET_START;
		stc_attr.remove_header.end_anchor = MLX5_HEADER_ANCHOR_IPV6_IPV4;
		break;
	case MLX5HWS_CONTEXT_SHARED_STC_DOUBLE_POP:
		stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_REMOVE_WORDS;
		stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_DW5;
		stc_attr.reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;
		stc_attr.remove_words.start_anchor = MLX5_HEADER_ANCHOR_FIRST_VLAN_START;
		stc_attr.remove_words.num_of_words = MLX5HWS_ACTION_HDR_LEN_L2_VLAN;
		break;
	default:
		mlx5hws_err(ctx, "No such stc_type: %d\n", stc_type);
		pr_warn("HWS: Invalid stc_type: %d\n", stc_type);
		ret = -EINVAL;
		goto free_shared_stc;
	}

	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl_type,
					      &shared_stc->stc_chunk);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate shared decap l2 STC\n");
		goto free_shared_stc;
	}

	ctx->common_res.shared_stc[stc_type] = shared_stc;
	ctx->common_res.shared_stc[stc_type]->refcount = 1;

	mutex_unlock(&ctx->ctrl_lock);

	return 0;

free_shared_stc:
	kfree(shared_stc);
unlock_and_out:
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}

static int hws_action_get_shared_stc(struct mlx5hws_action *action,
				     enum mlx5hws_context_shared_stc_type stc_type)
{
	struct mlx5hws_context *ctx = action->ctx;
	int ret;

	if (stc_type >= MLX5HWS_CONTEXT_SHARED_STC_MAX) {
		pr_warn("HWS: Invalid shared stc_type: %d\n", stc_type);
		return -EINVAL;
	}

	if (unlikely(!(action->flags & MLX5HWS_ACTION_FLAG_HWS_FDB))) {
		pr_warn("HWS: Invalid action->flags: %d\n", action->flags);
		return -EINVAL;
	}

	ret = hws_action_get_shared_stc_nic(ctx, stc_type, MLX5HWS_TABLE_TYPE_FDB);
	if (ret) {
		mlx5hws_err(ctx,
			    "Failed to allocate memory for FDB shared STCs (type: %d)\n",
			    stc_type);
		return ret;
	}

	return 0;
}

static void hws_action_put_shared_stc(struct mlx5hws_action *action,
				      enum mlx5hws_context_shared_stc_type stc_type)
{
	enum mlx5hws_table_type tbl_type = MLX5HWS_TABLE_TYPE_FDB;
	struct mlx5hws_action_shared_stc *shared_stc;
	struct mlx5hws_context *ctx = action->ctx;

	if (stc_type >= MLX5HWS_CONTEXT_SHARED_STC_MAX) {
		pr_warn("HWS: Invalid shared stc_type: %d\n", stc_type);
		return;
	}

	mutex_lock(&ctx->ctrl_lock);
	if (--ctx->common_res.shared_stc[stc_type]->refcount) {
		mutex_unlock(&ctx->ctrl_lock);
		return;
	}

	shared_stc = ctx->common_res.shared_stc[stc_type];

	mlx5hws_action_free_single_stc(ctx, tbl_type, &shared_stc->stc_chunk);
	kfree(shared_stc);
	ctx->common_res.shared_stc[stc_type] = NULL;
	mutex_unlock(&ctx->ctrl_lock);
}

static void hws_action_print_combo(struct mlx5hws_context *ctx,
				   enum mlx5hws_action_type *user_actions)
{
	mlx5hws_err(ctx, "Invalid action_type sequence");
	while (*user_actions != MLX5HWS_ACTION_TYP_LAST) {
		mlx5hws_err(ctx, " %s", mlx5hws_action_type_to_str(*user_actions));
		user_actions++;
	}
	mlx5hws_err(ctx, "\n");
}

bool mlx5hws_action_check_combo(struct mlx5hws_context *ctx,
				enum mlx5hws_action_type *user_actions,
				enum mlx5hws_table_type table_type)
{
	const u32 *order_arr = action_order_arr;
	bool valid_combo;
	u8 order_idx = 0;
	u8 user_idx = 0;

	if (table_type >= MLX5HWS_TABLE_TYPE_MAX) {
		mlx5hws_err(ctx, "Invalid table_type %d", table_type);
		return false;
	}

	while (order_arr[order_idx] != BIT(MLX5HWS_ACTION_TYP_LAST)) {
		/* User action order validated move to next user action */
		if (BIT(user_actions[user_idx]) & order_arr[order_idx])
			user_idx++;

		/* Iterate to the next supported action in the order */
		order_idx++;
	}

	/* Combination is valid if all user action were processed */
	valid_combo = user_actions[user_idx] == MLX5HWS_ACTION_TYP_LAST;
	if (!valid_combo)
		hws_action_print_combo(ctx, user_actions);

	return valid_combo;
}

static bool
hws_action_fixup_stc_attr(struct mlx5hws_context *ctx,
			  struct mlx5hws_cmd_stc_modify_attr *stc_attr,
			  struct mlx5hws_cmd_stc_modify_attr *fixup_stc_attr,
			  enum mlx5hws_table_type table_type,
			  bool is_mirror)
{
	struct mlx5hws_pool *pool;
	bool use_fixup = false;
	u32 fw_tbl_type;
	u32 base_id;

	fw_tbl_type = mlx5hws_table_get_res_fw_ft_type(table_type, is_mirror);

	switch (stc_attr->action_type) {
	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_STE_TABLE:
		if (is_mirror && stc_attr->ste_table.ignore_tx) {
			fixup_stc_attr->action_type = MLX5_IFC_STC_ACTION_TYPE_DROP;
			fixup_stc_attr->action_offset = MLX5HWS_ACTION_OFFSET_HIT;
			fixup_stc_attr->stc_offset = stc_attr->stc_offset;
			use_fixup = true;
			break;
		}
		pool = stc_attr->ste_table.ste_pool;
		if (!is_mirror)
			base_id = mlx5hws_pool_get_base_id(pool);
		else
			base_id = mlx5hws_pool_get_base_mirror_id(pool);

		*fixup_stc_attr = *stc_attr;
		fixup_stc_attr->ste_table.ste_obj_id = base_id;
		use_fixup = true;
		break;

	case MLX5_IFC_STC_ACTION_TYPE_TAG:
		if (fw_tbl_type == FS_FT_FDB_TX) {
			fixup_stc_attr->action_type = MLX5_IFC_STC_ACTION_TYPE_NOP;
			fixup_stc_attr->action_offset = MLX5HWS_ACTION_OFFSET_DW5;
			fixup_stc_attr->stc_offset = stc_attr->stc_offset;
			use_fixup = true;
		}
		break;

	case MLX5_IFC_STC_ACTION_TYPE_ALLOW:
		if (fw_tbl_type == FS_FT_FDB_TX || fw_tbl_type == FS_FT_FDB_RX) {
			fixup_stc_attr->action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_VPORT;
			fixup_stc_attr->action_offset = stc_attr->action_offset;
			fixup_stc_attr->stc_offset = stc_attr->stc_offset;
			fixup_stc_attr->vport.esw_owner_vhca_id = ctx->caps->vhca_id;
			fixup_stc_attr->vport.vport_num = ctx->caps->eswitch_manager_vport_number;
			fixup_stc_attr->vport.eswitch_owner_vhca_id_valid =
				ctx->caps->merged_eswitch;
			use_fixup = true;
		}
		break;

	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_VPORT:
		if (stc_attr->vport.vport_num != MLX5_VPORT_UPLINK)
			break;

		if (fw_tbl_type == FS_FT_FDB_TX || fw_tbl_type == FS_FT_FDB_RX) {
			/* The FW doesn't allow to go to wire in the TX/RX by JUMP_TO_VPORT */
			fixup_stc_attr->action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_UPLINK;
			fixup_stc_attr->action_offset = stc_attr->action_offset;
			fixup_stc_attr->stc_offset = stc_attr->stc_offset;
			fixup_stc_attr->vport.vport_num = 0;
			fixup_stc_attr->vport.esw_owner_vhca_id = stc_attr->vport.esw_owner_vhca_id;
			fixup_stc_attr->vport.eswitch_owner_vhca_id_valid =
				stc_attr->vport.eswitch_owner_vhca_id_valid;
		}
		use_fixup = true;
		break;

	default:
		break;
	}

	return use_fixup;
}

int mlx5hws_action_alloc_single_stc(struct mlx5hws_context *ctx,
				    struct mlx5hws_cmd_stc_modify_attr *stc_attr,
				    u32 table_type,
				    struct mlx5hws_pool_chunk *stc)
__must_hold(&ctx->ctrl_lock)
{
	struct mlx5hws_cmd_stc_modify_attr cleanup_stc_attr = {0};
	struct mlx5hws_cmd_stc_modify_attr fixup_stc_attr = {0};
	struct mlx5hws_pool *stc_pool = ctx->stc_pool;
	bool use_fixup;
	u32 obj_0_id;
	int ret;

	ret = mlx5hws_pool_chunk_alloc(stc_pool, stc);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate single action STC\n");
		return ret;
	}

	stc_attr->stc_offset = stc->offset;

	/* Dynamic reparse not supported, overwrite and use default */
	if (!mlx5hws_context_cap_dynamic_reparse(ctx))
		stc_attr->reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;

	obj_0_id = mlx5hws_pool_get_base_id(stc_pool);

	/* According to table/action limitation change the stc_attr */
	use_fixup = hws_action_fixup_stc_attr(ctx, stc_attr, &fixup_stc_attr, table_type, false);
	ret = mlx5hws_cmd_stc_modify(ctx->mdev, obj_0_id,
				     use_fixup ? &fixup_stc_attr : stc_attr);
	if (ret) {
		mlx5hws_err(ctx, "Failed to modify STC action_type %d tbl_type %d\n",
			    stc_attr->action_type, table_type);
		goto free_chunk;
	}

	/* Modify the FDB peer */
	if (table_type == MLX5HWS_TABLE_TYPE_FDB) {
		u32 obj_1_id;

		obj_1_id = mlx5hws_pool_get_base_mirror_id(stc_pool);

		use_fixup = hws_action_fixup_stc_attr(ctx, stc_attr,
						      &fixup_stc_attr,
						      table_type, true);
		ret = mlx5hws_cmd_stc_modify(ctx->mdev, obj_1_id,
					     use_fixup ? &fixup_stc_attr : stc_attr);
		if (ret) {
			mlx5hws_err(ctx,
				    "Failed to modify peer STC action_type %d tbl_type %d\n",
				    stc_attr->action_type, table_type);
			goto clean_obj_0;
		}
	}

	return 0;

clean_obj_0:
	cleanup_stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_DROP;
	cleanup_stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_HIT;
	cleanup_stc_attr.stc_offset = stc->offset;
	mlx5hws_cmd_stc_modify(ctx->mdev, obj_0_id, &cleanup_stc_attr);
free_chunk:
	mlx5hws_pool_chunk_free(stc_pool, stc);
	return ret;
}

void mlx5hws_action_free_single_stc(struct mlx5hws_context *ctx,
				    u32 table_type,
				    struct mlx5hws_pool_chunk *stc)
__must_hold(&ctx->ctrl_lock)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = {0};
	struct mlx5hws_pool *stc_pool = ctx->stc_pool;
	u32 obj_id;

	/* Modify the STC not to point to an object */
	stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_DROP;
	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_HIT;
	stc_attr.stc_offset = stc->offset;
	obj_id = mlx5hws_pool_get_base_id(stc_pool);
	mlx5hws_cmd_stc_modify(ctx->mdev, obj_id, &stc_attr);

	if (table_type == MLX5HWS_TABLE_TYPE_FDB) {
		obj_id = mlx5hws_pool_get_base_mirror_id(stc_pool);
		mlx5hws_cmd_stc_modify(ctx->mdev, obj_id, &stc_attr);
	}

	mlx5hws_pool_chunk_free(stc_pool, stc);
}

static u32 hws_action_get_mh_stc_type(struct mlx5hws_context *ctx,
				      __be64 pattern)
{
	u8 action_type = MLX5_GET(set_action_in, &pattern, action_type);

	switch (action_type) {
	case MLX5_MODIFICATION_TYPE_SET:
		return MLX5_IFC_STC_ACTION_TYPE_SET;
	case MLX5_MODIFICATION_TYPE_ADD:
		return MLX5_IFC_STC_ACTION_TYPE_ADD;
	case MLX5_MODIFICATION_TYPE_COPY:
		return MLX5_IFC_STC_ACTION_TYPE_COPY;
	case MLX5_MODIFICATION_TYPE_ADD_FIELD:
		return MLX5_IFC_STC_ACTION_TYPE_ADD_FIELD;
	default:
		mlx5hws_err(ctx, "Unsupported action type: 0x%x\n", action_type);
		return MLX5_IFC_STC_ACTION_TYPE_NOP;
	}
}

static void hws_action_fill_stc_attr(struct mlx5hws_action *action,
				     u32 obj_id,
				     struct mlx5hws_cmd_stc_modify_attr *attr)
{
	attr->reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;

	switch (action->type) {
	case MLX5HWS_ACTION_TYP_TAG:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_TAG;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW5;
		break;
	case MLX5HWS_ACTION_TYP_DROP:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_DROP;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_HIT;
		break;
	case MLX5HWS_ACTION_TYP_MISS:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_ALLOW;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_HIT;
		break;
	case MLX5HWS_ACTION_TYP_CTR:
		attr->id = obj_id;
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_COUNTER;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW0;
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2:
	case MLX5HWS_ACTION_TYP_MODIFY_HDR:
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW6;
		attr->reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;
		if (action->modify_header.require_reparse)
			attr->reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;

		if (action->modify_header.num_of_actions == 1) {
			attr->modify_action.data = action->modify_header.single_action;
			attr->action_type = hws_action_get_mh_stc_type(action->ctx,
								       attr->modify_action.data);

			if (attr->action_type == MLX5_IFC_STC_ACTION_TYPE_ADD ||
			    attr->action_type == MLX5_IFC_STC_ACTION_TYPE_SET)
				MLX5_SET(set_action_in, &attr->modify_action.data, data, 0);
		} else {
			attr->action_type = MLX5_IFC_STC_ACTION_TYPE_ACC_MODIFY_LIST;
			attr->modify_header.arg_id = action->modify_header.arg_id;
			attr->modify_header.pattern_id = action->modify_header.pat_id;
		}
		break;
	case MLX5HWS_ACTION_TYP_TBL:
	case MLX5HWS_ACTION_TYP_DEST_ARRAY:
	case MLX5HWS_ACTION_TYP_SAMPLER:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_FT;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_HIT;
		attr->dest_table_id = obj_id;
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_HEADER_REMOVE;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW5;
		attr->reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;
		attr->remove_header.decap = 1;
		attr->remove_header.start_anchor = MLX5_HEADER_ANCHOR_PACKET_START;
		attr->remove_header.end_anchor = MLX5_HEADER_ANCHOR_INNER_MAC;
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2:
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3:
	case MLX5HWS_ACTION_TYP_INSERT_HEADER:
		attr->reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;
		if (!action->reformat.require_reparse)
			attr->reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;

		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_HEADER_INSERT;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW6;
		attr->insert_header.encap = action->reformat.encap;
		attr->insert_header.insert_anchor = action->reformat.anchor;
		attr->insert_header.arg_id = action->reformat.arg_id;
		attr->insert_header.header_size = action->reformat.header_size;
		attr->insert_header.insert_offset = action->reformat.offset;
		break;
	case MLX5HWS_ACTION_TYP_ASO_METER:
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW6;
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_ASO;
		attr->aso.aso_type = ASO_OPC_MOD_POLICER;
		attr->aso.devx_obj_id = obj_id;
		attr->aso.return_reg_id = action->aso.return_reg_id;
		break;
	case MLX5HWS_ACTION_TYP_VPORT:
		attr->action_offset = MLX5HWS_ACTION_OFFSET_HIT;
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_VPORT;
		attr->vport.vport_num = action->vport.vport_num;
		attr->vport.esw_owner_vhca_id =	action->vport.esw_owner_vhca_id;
		attr->vport.eswitch_owner_vhca_id_valid = action->vport.esw_owner_vhca_id_valid;
		break;
	case MLX5HWS_ACTION_TYP_POP_VLAN:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_REMOVE_WORDS;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW5;
		attr->reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;
		attr->remove_words.start_anchor = MLX5_HEADER_ANCHOR_FIRST_VLAN_START;
		attr->remove_words.num_of_words = MLX5HWS_ACTION_HDR_LEN_L2_VLAN / 2;
		break;
	case MLX5HWS_ACTION_TYP_PUSH_VLAN:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_HEADER_INSERT;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW6;
		attr->reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;
		attr->insert_header.encap = 0;
		attr->insert_header.is_inline = 1;
		attr->insert_header.insert_anchor = MLX5_HEADER_ANCHOR_PACKET_START;
		attr->insert_header.insert_offset = MLX5HWS_ACTION_HDR_LEN_L2_MACS;
		attr->insert_header.header_size = MLX5HWS_ACTION_HDR_LEN_L2_VLAN;
		break;
	case MLX5HWS_ACTION_TYP_REMOVE_HEADER:
		attr->action_type = MLX5_IFC_STC_ACTION_TYPE_REMOVE_WORDS;
		attr->remove_header.decap = 0; /* the mode we support decap is 0 */
		attr->remove_words.start_anchor = action->remove_header.anchor;
		/* the size is in already in words */
		attr->remove_words.num_of_words = action->remove_header.size;
		attr->action_offset = MLX5HWS_ACTION_OFFSET_DW5;
		attr->reparse_mode = MLX5_IFC_STC_REPARSE_ALWAYS;
		break;
	default:
		mlx5hws_err(action->ctx, "Invalid action type %d\n", action->type);
	}
}

static int
hws_action_create_stcs(struct mlx5hws_action *action, u32 obj_id)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = {0};
	struct mlx5hws_context *ctx = action->ctx;
	int ret;

	hws_action_fill_stc_attr(action, obj_id, &stc_attr);

	/* Block unsupported parallel obj modify over the same base */
	mutex_lock(&ctx->ctrl_lock);

	/* Allocate STC for FDB */
	if (action->flags & MLX5HWS_ACTION_FLAG_HWS_FDB) {
		ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr,
						      MLX5HWS_TABLE_TYPE_FDB,
						      &action->stc);
		if (ret)
			goto out_err;
	}

	mutex_unlock(&ctx->ctrl_lock);

	return 0;

out_err:
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}

static void
hws_action_destroy_stcs(struct mlx5hws_action *action)
{
	struct mlx5hws_context *ctx = action->ctx;

	/* Block unsupported parallel obj modify over the same base */
	mutex_lock(&ctx->ctrl_lock);

	if (action->flags & MLX5HWS_ACTION_FLAG_HWS_FDB)
		mlx5hws_action_free_single_stc(ctx, MLX5HWS_TABLE_TYPE_FDB,
					       &action->stc);

	mutex_unlock(&ctx->ctrl_lock);
}

static bool hws_action_is_flag_hws_fdb(u32 flags)
{
	return flags & MLX5HWS_ACTION_FLAG_HWS_FDB;
}

static bool
hws_action_validate_hws_action(struct mlx5hws_context *ctx, u32 flags)
{
	if (!(ctx->flags & MLX5HWS_CONTEXT_FLAG_HWS_SUPPORT)) {
		mlx5hws_err(ctx, "Cannot create HWS action since HWS is not supported\n");
		return false;
	}

	if ((flags & MLX5HWS_ACTION_FLAG_HWS_FDB) && !ctx->caps->eswitch_manager) {
		mlx5hws_err(ctx, "Cannot create HWS action for FDB for non-eswitch-manager\n");
		return false;
	}

	return true;
}

static struct mlx5hws_action *
hws_action_create_generic_bulk(struct mlx5hws_context *ctx,
			       u32 flags,
			       enum mlx5hws_action_type action_type,
			       u8 bulk_sz)
{
	struct mlx5hws_action *action;
	int i;

	if (!hws_action_is_flag_hws_fdb(flags)) {
		mlx5hws_err(ctx,
			    "Action (type: %d) flags must specify only HWS FDB\n", action_type);
		return NULL;
	}

	if (!hws_action_validate_hws_action(ctx, flags))
		return NULL;

	action = kcalloc(bulk_sz, sizeof(*action), GFP_KERNEL);
	if (!action)
		return NULL;

	for (i = 0; i < bulk_sz; i++) {
		action[i].ctx = ctx;
		action[i].flags = flags;
		action[i].type = action_type;
	}

	return action;
}

static struct mlx5hws_action *
hws_action_create_generic(struct mlx5hws_context *ctx,
			  u32 flags,
			  enum mlx5hws_action_type action_type)
{
	return hws_action_create_generic_bulk(ctx, flags, action_type, 1);
}

struct mlx5hws_action *
mlx5hws_action_create_dest_table_num(struct mlx5hws_context *ctx,
				     u32 table_id,
				     u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_TBL);
	if (!action)
		return NULL;

	ret = hws_action_create_stcs(action, table_id);
	if (ret)
		goto free_action;

	action->dest_obj.obj_id = table_id;

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_dest_table(struct mlx5hws_context *ctx,
				 struct mlx5hws_table *tbl,
				 u32 flags)
{
	return mlx5hws_action_create_dest_table_num(ctx, tbl->ft_id, flags);
}

struct mlx5hws_action *
mlx5hws_action_create_dest_drop(struct mlx5hws_context *ctx, u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_DROP);
	if (!action)
		return NULL;

	ret = hws_action_create_stcs(action, 0);
	if (ret)
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_default_miss(struct mlx5hws_context *ctx, u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_MISS);
	if (!action)
		return NULL;

	ret = hws_action_create_stcs(action, 0);
	if (ret)
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_tag(struct mlx5hws_context *ctx, u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_TAG);
	if (!action)
		return NULL;

	ret = hws_action_create_stcs(action, 0);
	if (ret)
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

static struct mlx5hws_action *
hws_action_create_aso(struct mlx5hws_context *ctx,
		      enum mlx5hws_action_type action_type,
		      u32 obj_id,
		      u8 return_reg_id,
		      u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, action_type);
	if (!action)
		return NULL;

	action->aso.obj_id = obj_id;
	action->aso.return_reg_id = return_reg_id;

	ret = hws_action_create_stcs(action, obj_id);
	if (ret)
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_aso_meter(struct mlx5hws_context *ctx,
				u32 obj_id,
				u8 return_reg_id,
				u32 flags)
{
	return hws_action_create_aso(ctx, MLX5HWS_ACTION_TYP_ASO_METER,
				     obj_id, return_reg_id, flags);
}

struct mlx5hws_action *
mlx5hws_action_create_counter(struct mlx5hws_context *ctx,
			      u32 obj_id,
			      u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_CTR);
	if (!action)
		return NULL;

	ret = hws_action_create_stcs(action, obj_id);
	if (ret)
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_dest_vport(struct mlx5hws_context *ctx,
				 u16 vport_num,
				 bool vhca_id_valid,
				 u16 vhca_id,
				 u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	if (!(flags & MLX5HWS_ACTION_FLAG_HWS_FDB)) {
		mlx5hws_err(ctx, "Vport action is supported for FDB only\n");
		return NULL;
	}

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_VPORT);
	if (!action)
		return NULL;

	if (!ctx->caps->merged_eswitch && vhca_id_valid && vhca_id != ctx->caps->vhca_id) {
		mlx5hws_err(ctx, "Non merged eswitch cannot send to other vhca\n");
		goto free_action;
	}

	action->vport.vport_num = vport_num;
	action->vport.esw_owner_vhca_id_valid = vhca_id_valid;

	if (vhca_id_valid)
		action->vport.esw_owner_vhca_id = vhca_id;

	ret = hws_action_create_stcs(action, 0);
	if (ret) {
		mlx5hws_err(ctx, "Failed creating stc for vport %d\n", vport_num);
		goto free_action;
	}

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_push_vlan(struct mlx5hws_context *ctx, u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_PUSH_VLAN);
	if (!action)
		return NULL;

	ret = hws_action_create_stcs(action, 0);
	if (ret) {
		mlx5hws_err(ctx, "Failed creating stc for push vlan\n");
		goto free_action;
	}

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_pop_vlan(struct mlx5hws_context *ctx, u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_POP_VLAN);
	if (!action)
		return NULL;

	ret = hws_action_get_shared_stc(action, MLX5HWS_CONTEXT_SHARED_STC_DOUBLE_POP);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create remove stc for reformat\n");
		goto free_action;
	}

	ret = hws_action_create_stcs(action, 0);
	if (ret) {
		mlx5hws_err(ctx, "Failed creating stc for pop vlan\n");
		goto free_shared;
	}

	return action;

free_shared:
	hws_action_put_shared_stc(action, MLX5HWS_CONTEXT_SHARED_STC_DOUBLE_POP);
free_action:
	kfree(action);
	return NULL;
}

static int
hws_action_handle_insert_with_ptr(struct mlx5hws_action *action,
				  u8 num_of_hdrs,
				  struct mlx5hws_action_reformat_header *hdrs,
				  u32 log_bulk_sz)
{
	size_t max_sz = 0;
	u32 arg_id;
	int ret, i;

	for (i = 0; i < num_of_hdrs; i++) {
		if (hdrs[i].sz % W_SIZE != 0) {
			mlx5hws_err(action->ctx,
				    "Header data size should be in WORD granularity\n");
			return -EINVAL;
		}
		max_sz = max(hdrs[i].sz, max_sz);
	}

	/* Allocate single shared arg object for all headers */
	ret = mlx5hws_arg_create(action->ctx,
				 hdrs->data,
				 max_sz,
				 log_bulk_sz,
				 action->flags & MLX5HWS_ACTION_FLAG_SHARED,
				 &arg_id);
	if (ret)
		return ret;

	for (i = 0; i < num_of_hdrs; i++) {
		action[i].reformat.arg_id = arg_id;
		action[i].reformat.header_size = hdrs[i].sz;
		action[i].reformat.num_of_hdrs = num_of_hdrs;
		action[i].reformat.max_hdr_sz = max_sz;
		action[i].reformat.require_reparse = true;

		if (action[i].type == MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2 ||
		    action[i].type == MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3) {
			action[i].reformat.anchor = MLX5_HEADER_ANCHOR_PACKET_START;
			action[i].reformat.offset = 0;
			action[i].reformat.encap = 1;
		}

		ret = hws_action_create_stcs(&action[i], 0);
		if (ret) {
			mlx5hws_err(action->ctx, "Failed to create stc for reformat\n");
			goto free_stc;
		}
	}

	return 0;

free_stc:
	while (i--)
		hws_action_destroy_stcs(&action[i]);

	mlx5hws_arg_destroy(action->ctx, arg_id);
	return ret;
}

static int
hws_action_handle_l2_to_tunnel_l3(struct mlx5hws_action *action,
				  u8 num_of_hdrs,
				  struct mlx5hws_action_reformat_header *hdrs,
				  u32 log_bulk_sz)
{
	int ret;

	/* The action is remove-l2-header + insert-l3-header */
	ret = hws_action_get_shared_stc(action, MLX5HWS_CONTEXT_SHARED_STC_DECAP_L3);
	if (ret) {
		mlx5hws_err(action->ctx, "Failed to create remove stc for reformat\n");
		return ret;
	}

	/* Reuse the insert with pointer for the L2L3 header */
	ret = hws_action_handle_insert_with_ptr(action,
						num_of_hdrs,
						hdrs,
						log_bulk_sz);
	if (ret)
		goto put_shared_stc;

	return 0;

put_shared_stc:
	hws_action_put_shared_stc(action, MLX5HWS_CONTEXT_SHARED_STC_DECAP_L3);
	return ret;
}

static void hws_action_prepare_decap_l3_actions(size_t data_sz,
						u8 *mh_data,
						int *num_of_actions)
{
	int actions;
	u32 i;

	/* Remove L2L3 outer headers */
	MLX5_SET(stc_ste_param_remove, mh_data, action_type,
		 MLX5_MODIFICATION_TYPE_REMOVE);
	MLX5_SET(stc_ste_param_remove, mh_data, decap, 0x1);
	MLX5_SET(stc_ste_param_remove, mh_data, remove_start_anchor,
		 MLX5_HEADER_ANCHOR_PACKET_START);
	MLX5_SET(stc_ste_param_remove, mh_data, remove_end_anchor,
		 MLX5_HEADER_ANCHOR_INNER_IPV6_IPV4);
	mh_data += MLX5HWS_ACTION_DOUBLE_SIZE; /* Assume every action is 2 dw */
	actions = 1;

	/* Add the new header using inline action 4Byte at a time, the header
	 * is added in reversed order to the beginning of the packet to avoid
	 * incorrect parsing by the HW. Since header is 14B or 18B an extra
	 * two bytes are padded and later removed.
	 */
	for (i = 0; i < data_sz / MLX5HWS_ACTION_INLINE_DATA_SIZE + 1; i++) {
		MLX5_SET(stc_ste_param_insert, mh_data, action_type,
			 MLX5_MODIFICATION_TYPE_INSERT);
		MLX5_SET(stc_ste_param_insert, mh_data, inline_data, 0x1);
		MLX5_SET(stc_ste_param_insert, mh_data, insert_anchor,
			 MLX5_HEADER_ANCHOR_PACKET_START);
		MLX5_SET(stc_ste_param_insert, mh_data, insert_size, 2);
		mh_data += MLX5HWS_ACTION_DOUBLE_SIZE;
		actions++;
	}

	/* Remove first 2 extra bytes */
	MLX5_SET(stc_ste_param_remove_words, mh_data, action_type,
		 MLX5_MODIFICATION_TYPE_REMOVE_WORDS);
	MLX5_SET(stc_ste_param_remove_words, mh_data, remove_start_anchor,
		 MLX5_HEADER_ANCHOR_PACKET_START);
	/* The hardware expects here size in words (2 bytes) */
	MLX5_SET(stc_ste_param_remove_words, mh_data, remove_size, 1);
	actions++;

	*num_of_actions = actions;
}

static int
hws_action_handle_tunnel_l3_to_l2(struct mlx5hws_action *action,
				  u8 num_of_hdrs,
				  struct mlx5hws_action_reformat_header *hdrs,
				  u32 log_bulk_sz)
{
	u8 mh_data[MLX5HWS_ACTION_REFORMAT_DATA_SIZE] = {0};
	struct mlx5hws_context *ctx = action->ctx;
	u32 arg_id, pat_id;
	int num_of_actions;
	int mh_data_size;
	int ret, i;

	for (i = 0; i < num_of_hdrs; i++) {
		if (hdrs[i].sz != MLX5HWS_ACTION_HDR_LEN_L2 &&
		    hdrs[i].sz != MLX5HWS_ACTION_HDR_LEN_L2_W_VLAN) {
			mlx5hws_err(ctx, "Data size is not supported for decap-l3\n");
			return -EINVAL;
		}
	}

	/* Create a full modify header action list in case shared */
	hws_action_prepare_decap_l3_actions(hdrs->sz, mh_data, &num_of_actions);
	if (action->flags & MLX5HWS_ACTION_FLAG_SHARED)
		mlx5hws_action_prepare_decap_l3_data(hdrs->data, mh_data, num_of_actions);

	/* All DecapL3 cases require the same max arg size */
	ret = mlx5hws_arg_create_modify_header_arg(ctx,
						   (__be64 *)mh_data,
						   num_of_actions,
						   log_bulk_sz,
						   action->flags & MLX5HWS_ACTION_FLAG_SHARED,
						   &arg_id);
	if (ret)
		return ret;

	for (i = 0; i < num_of_hdrs; i++) {
		memset(mh_data, 0, MLX5HWS_ACTION_REFORMAT_DATA_SIZE);
		hws_action_prepare_decap_l3_actions(hdrs[i].sz, mh_data, &num_of_actions);
		mh_data_size = num_of_actions * MLX5HWS_MODIFY_ACTION_SIZE;

		ret = mlx5hws_pat_get_pattern(ctx, (__be64 *)mh_data, mh_data_size, &pat_id);
		if (ret) {
			mlx5hws_err(ctx, "Failed to allocate pattern for DecapL3\n");
			goto free_stc_and_pat;
		}

		action[i].modify_header.max_num_of_actions = num_of_actions;
		action[i].modify_header.num_of_actions = num_of_actions;
		action[i].modify_header.num_of_patterns = num_of_hdrs;
		action[i].modify_header.arg_id = arg_id;
		action[i].modify_header.pat_id = pat_id;
		action[i].modify_header.require_reparse =
			mlx5hws_pat_require_reparse((__be64 *)mh_data, num_of_actions);

		ret = hws_action_create_stcs(&action[i], 0);
		if (ret) {
			mlx5hws_pat_put_pattern(ctx, pat_id);
			goto free_stc_and_pat;
		}
	}

	return 0;

free_stc_and_pat:
	while (i--) {
		hws_action_destroy_stcs(&action[i]);
		mlx5hws_pat_put_pattern(ctx, action[i].modify_header.pat_id);
	}

	mlx5hws_arg_destroy(action->ctx, arg_id);
	return ret;
}

static int
hws_action_create_reformat_hws(struct mlx5hws_action *action,
			       u8 num_of_hdrs,
			       struct mlx5hws_action_reformat_header *hdrs,
			       u32 bulk_size)
{
	int ret;

	switch (action->type) {
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2:
		ret = hws_action_create_stcs(action, 0);
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2:
		ret = hws_action_handle_insert_with_ptr(action, num_of_hdrs, hdrs, bulk_size);
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3:
		ret = hws_action_handle_l2_to_tunnel_l3(action, num_of_hdrs, hdrs, bulk_size);
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2:
		ret = hws_action_handle_tunnel_l3_to_l2(action, num_of_hdrs, hdrs, bulk_size);
		break;
	default:
		mlx5hws_err(action->ctx, "Invalid HWS reformat action type\n");
		return -EINVAL;
	}

	return ret;
}

struct mlx5hws_action *
mlx5hws_action_create_reformat(struct mlx5hws_context *ctx,
			       enum mlx5hws_action_type reformat_type,
			       u8 num_of_hdrs,
			       struct mlx5hws_action_reformat_header *hdrs,
			       u32 log_bulk_size,
			       u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	if (!num_of_hdrs) {
		mlx5hws_err(ctx, "Reformat num_of_hdrs cannot be zero\n");
		return NULL;
	}

	action = hws_action_create_generic_bulk(ctx, flags, reformat_type, num_of_hdrs);
	if (!action)
		return NULL;

	if ((flags & MLX5HWS_ACTION_FLAG_SHARED) && (log_bulk_size || num_of_hdrs > 1)) {
		mlx5hws_err(ctx, "Reformat flags don't fit HWS (flags: 0x%x)\n", flags);
		goto free_action;
	}

	ret = hws_action_create_reformat_hws(action, num_of_hdrs, hdrs, log_bulk_size);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create HWS reformat action\n");
		goto free_action;
	}

	return action;

free_action:
	kfree(action);
	return NULL;
}

static int
hws_action_create_modify_header_hws(struct mlx5hws_action *action,
				    u8 num_of_patterns,
				    struct mlx5hws_action_mh_pattern *pattern,
				    u32 log_bulk_size)
{
	u16 num_actions, max_mh_actions = 0, hw_max_actions;
	struct mlx5hws_context *ctx = action->ctx;
	int i, ret, size_in_bytes;
	u32 pat_id, arg_id = 0;
	__be64 *new_pattern;
	size_t pat_max_sz;

	pat_max_sz = MLX5HWS_ARG_CHUNK_SIZE_MAX * MLX5HWS_ARG_DATA_SIZE;
	hw_max_actions = pat_max_sz / MLX5HWS_MODIFY_ACTION_SIZE;
	size_in_bytes = pat_max_sz * sizeof(__be64);
	new_pattern = kcalloc(num_of_patterns, size_in_bytes, GFP_KERNEL);
	if (!new_pattern)
		return -ENOMEM;

	/* Calculate maximum number of mh actions for shared arg allocation */
	for (i = 0; i < num_of_patterns; i++) {
		size_t new_num_actions;
		size_t cur_num_actions;
		u32 nop_locations;

		cur_num_actions = pattern[i].sz / MLX5HWS_MODIFY_ACTION_SIZE;

		ret = mlx5hws_pat_calc_nop(pattern[i].data, cur_num_actions,
					   hw_max_actions, &new_num_actions,
					   &nop_locations,
					   &new_pattern[i * pat_max_sz]);
		if (ret) {
			mlx5hws_err(ctx, "Too many actions after nop insertion\n");
			goto free_new_pat;
		}

		action[i].modify_header.nop_locations = nop_locations;
		action[i].modify_header.num_of_actions = new_num_actions;

		max_mh_actions = max(max_mh_actions, new_num_actions);
	}

	if (mlx5hws_arg_get_arg_log_size(max_mh_actions) >= MLX5HWS_ARG_CHUNK_SIZE_MAX) {
		mlx5hws_err(ctx, "Num of actions (%d) bigger than allowed\n",
			    max_mh_actions);
		ret = -EINVAL;
		goto free_new_pat;
	}

	/* Allocate single shared arg for all patterns based on the max size */
	if (max_mh_actions > 1) {
		ret = mlx5hws_arg_create_modify_header_arg(ctx,
							   pattern->data,
							   max_mh_actions,
							   log_bulk_size,
							   action->flags &
							   MLX5HWS_ACTION_FLAG_SHARED,
							   &arg_id);
		if (ret)
			goto free_new_pat;
	}

	for (i = 0; i < num_of_patterns; i++) {
		if (!mlx5hws_pat_verify_actions(ctx, pattern[i].data, pattern[i].sz)) {
			mlx5hws_err(ctx, "Fail to verify pattern modify actions\n");
			ret = -EINVAL;
			goto free_stc_and_pat;
		}
		num_actions = pattern[i].sz / MLX5HWS_MODIFY_ACTION_SIZE;
		action[i].modify_header.num_of_patterns = num_of_patterns;
		action[i].modify_header.max_num_of_actions = max_mh_actions;

		action[i].modify_header.require_reparse =
			mlx5hws_pat_require_reparse(pattern[i].data, num_actions);

		if (num_actions == 1) {
			pat_id = 0;
			/* Optimize single modify action to be used inline */
			action[i].modify_header.single_action = pattern[i].data[0];
			action[i].modify_header.single_action_type =
				MLX5_GET(set_action_in, pattern[i].data, action_type);
		} else {
			/* Multiple modify actions require a pattern */
			if (unlikely(action[i].modify_header.nop_locations)) {
				size_t pattern_sz;

				pattern_sz = action[i].modify_header.num_of_actions *
					     MLX5HWS_MODIFY_ACTION_SIZE;
				ret =
				mlx5hws_pat_get_pattern(ctx,
							&new_pattern[i * pat_max_sz],
							pattern_sz, &pat_id);
			} else {
				ret = mlx5hws_pat_get_pattern(ctx,
							      pattern[i].data,
							      pattern[i].sz,
							      &pat_id);
			}
			if (ret) {
				mlx5hws_err(ctx,
					    "Failed to allocate pattern for modify header\n");
				goto free_stc_and_pat;
			}

			action[i].modify_header.arg_id = arg_id;
			action[i].modify_header.pat_id = pat_id;
		}
		/* Allocate STC for each action representing a header */
		ret = hws_action_create_stcs(&action[i], 0);
		if (ret) {
			if (pat_id)
				mlx5hws_pat_put_pattern(ctx, pat_id);
			goto free_stc_and_pat;
		}
	}

	kfree(new_pattern);
	return 0;

free_stc_and_pat:
	while (i--) {
		hws_action_destroy_stcs(&action[i]);
		if (action[i].modify_header.pat_id)
			mlx5hws_pat_put_pattern(ctx, action[i].modify_header.pat_id);
	}

	if (arg_id)
		mlx5hws_arg_destroy(ctx, arg_id);
free_new_pat:
	kfree(new_pattern);
	return ret;
}

struct mlx5hws_action *
mlx5hws_action_create_modify_header(struct mlx5hws_context *ctx,
				    u8 num_of_patterns,
				    struct mlx5hws_action_mh_pattern *patterns,
				    u32 log_bulk_size,
				    u32 flags)
{
	struct mlx5hws_action *action;
	int ret;

	if (!num_of_patterns) {
		mlx5hws_err(ctx, "Invalid number of patterns\n");
		return NULL;
	}
	action = hws_action_create_generic_bulk(ctx, flags,
						MLX5HWS_ACTION_TYP_MODIFY_HDR,
						num_of_patterns);
	if (!action)
		return NULL;

	if ((flags & MLX5HWS_ACTION_FLAG_SHARED) && (log_bulk_size || num_of_patterns > 1)) {
		mlx5hws_err(ctx, "Action cannot be shared with requested pattern or size\n");
		goto free_action;
	}

	ret = hws_action_create_modify_header_hws(action,
						  num_of_patterns,
						  patterns,
						  log_bulk_size);
	if (ret)
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_dest_array(struct mlx5hws_context *ctx, size_t num_dest,
				 struct mlx5hws_action_dest_attr *dests,
				 u32 flags)
{
	struct mlx5hws_cmd_set_fte_dest *dest_list = NULL;
	struct mlx5hws_cmd_ft_create_attr ft_attr = {0};
	struct mlx5hws_cmd_set_fte_attr fte_attr = {0};
	struct mlx5hws_cmd_forward_tbl *fw_island;
	struct mlx5hws_action *action;
	int ret, last_dest_idx = -1;
	u32 i;

	if (num_dest <= 1) {
		mlx5hws_err(ctx, "Action must have multiple dests\n");
		return NULL;
	}

	if (flags == (MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED)) {
		ft_attr.type = FS_FT_FDB;
		ft_attr.level = ctx->caps->fdb_ft.max_level - 1;
	} else {
		mlx5hws_err(ctx, "Action flags not supported\n");
		return NULL;
	}

	dest_list = kcalloc(num_dest, sizeof(*dest_list), GFP_KERNEL);
	if (!dest_list)
		return NULL;

	for (i = 0; i < num_dest; i++) {
		enum mlx5hws_action_type action_type = dests[i].dest->type;
		struct mlx5hws_action *reformat_action = dests[i].reformat;

		switch (action_type) {
		case MLX5HWS_ACTION_TYP_TBL:
			dest_list[i].destination_type =
				MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			dest_list[i].destination_id = dests[i].dest->dest_obj.obj_id;
			fte_attr.action_flags |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			fte_attr.ignore_flow_level = 1;
			if (dests[i].is_wire_ft)
				last_dest_idx = i;
			break;
		case MLX5HWS_ACTION_TYP_VPORT:
			dest_list[i].destination_type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
			dest_list[i].destination_id = dests[i].dest->vport.vport_num;
			fte_attr.action_flags |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			if (ctx->caps->merged_eswitch) {
				dest_list[i].ext_flags |=
					MLX5HWS_CMD_EXT_DEST_ESW_OWNER_VHCA_ID;
				dest_list[i].esw_owner_vhca_id =
					dests[i].dest->vport.esw_owner_vhca_id;
			}
			break;
		default:
			mlx5hws_err(ctx, "Unsupported action in dest_array\n");
			goto free_dest_list;
		}

		if (reformat_action) {
			mlx5hws_err(ctx, "dest_array with reformat action - unsupported\n");
			goto free_dest_list;
		}
	}

	if (last_dest_idx != -1)
		swap(dest_list[last_dest_idx], dest_list[num_dest - 1]);

	fte_attr.dests_num = num_dest;
	fte_attr.dests = dest_list;

	fw_island = mlx5hws_cmd_forward_tbl_create(ctx->mdev, &ft_attr, &fte_attr);
	if (!fw_island)
		goto free_dest_list;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_DEST_ARRAY);
	if (!action)
		goto destroy_fw_island;

	ret = hws_action_create_stcs(action, fw_island->ft_id);
	if (ret)
		goto free_action;

	action->dest_array.fw_island = fw_island;
	action->dest_array.num_dest = num_dest;
	action->dest_array.dest_list = dest_list;

	return action;

free_action:
	kfree(action);
destroy_fw_island:
	mlx5hws_cmd_forward_tbl_destroy(ctx->mdev, fw_island);
free_dest_list:
	for (i = 0; i < num_dest; i++) {
		if (dest_list[i].ext_reformat_id)
			mlx5hws_cmd_packet_reformat_destroy(ctx->mdev,
							    dest_list[i].ext_reformat_id);
	}
	kfree(dest_list);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_insert_header(struct mlx5hws_context *ctx,
				    u8 num_of_hdrs,
				    struct mlx5hws_action_insert_header *hdrs,
				    u32 log_bulk_size,
				    u32 flags)
{
	struct mlx5hws_action_reformat_header *reformat_hdrs;
	struct mlx5hws_action *action;
	int ret;
	int i;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_INSERT_HEADER);
	if (!action)
		return NULL;

	reformat_hdrs = kcalloc(num_of_hdrs, sizeof(*reformat_hdrs), GFP_KERNEL);
	if (!reformat_hdrs)
		goto free_action;

	for (i = 0; i < num_of_hdrs; i++) {
		if (hdrs[i].offset % W_SIZE != 0) {
			mlx5hws_err(ctx, "Header offset should be in WORD granularity\n");
			goto free_reformat_hdrs;
		}

		action[i].reformat.anchor = hdrs[i].anchor;
		action[i].reformat.encap = hdrs[i].encap;
		action[i].reformat.offset = hdrs[i].offset;

		reformat_hdrs[i].sz = hdrs[i].hdr.sz;
		reformat_hdrs[i].data = hdrs[i].hdr.data;
	}

	ret = hws_action_handle_insert_with_ptr(action, num_of_hdrs,
						reformat_hdrs, log_bulk_size);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create HWS reformat action\n");
		goto free_reformat_hdrs;
	}

	kfree(reformat_hdrs);

	return action;

free_reformat_hdrs:
	kfree(reformat_hdrs);
free_action:
	kfree(action);
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_remove_header(struct mlx5hws_context *ctx,
				    struct mlx5hws_action_remove_header_attr *attr,
				    u32 flags)
{
	struct mlx5hws_action *action;

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_REMOVE_HEADER);
	if (!action)
		return NULL;

	/* support only remove anchor with size */
	if (attr->size % W_SIZE != 0) {
		mlx5hws_err(ctx,
			    "Invalid size, HW supports header remove in WORD granularity\n");
		goto free_action;
	}

	if (attr->size > MLX5HWS_ACTION_REMOVE_HEADER_MAX_SIZE) {
		mlx5hws_err(ctx, "Header removal size limited to %u bytes\n",
			    MLX5HWS_ACTION_REMOVE_HEADER_MAX_SIZE);
		goto free_action;
	}

	action->remove_header.anchor = attr->anchor;
	action->remove_header.size = attr->size / W_SIZE;

	if (hws_action_create_stcs(action, 0))
		goto free_action;

	return action;

free_action:
	kfree(action);
	return NULL;
}

static struct mlx5hws_definer *
hws_action_create_dest_match_range_definer(struct mlx5hws_context *ctx)
{
	struct mlx5hws_definer *definer;
	__be32 *tag;
	int ret;

	definer = kzalloc(sizeof(*definer), GFP_KERNEL);
	if (!definer)
		return NULL;

	definer->dw_selector[0] = MLX5_IFC_DEFINER_FORMAT_OFFSET_OUTER_ETH_PKT_LEN / 4;
	/* Set DW0 tag mask */
	tag = (__force __be32 *)definer->mask.jumbo;
	tag[MLX5HWS_RULE_JUMBO_MATCH_TAG_OFFSET_DW0] = htonl(0xffffUL << 16);

	mutex_lock(&ctx->ctrl_lock);

	ret = mlx5hws_definer_get_obj(ctx, definer);
	if (ret < 0) {
		mutex_unlock(&ctx->ctrl_lock);
		kfree(definer);
		return NULL;
	}

	mutex_unlock(&ctx->ctrl_lock);
	definer->obj_id = ret;

	return definer;
}

static struct mlx5hws_range_action_table *
hws_action_create_dest_match_range_table(struct mlx5hws_context *ctx,
					 struct mlx5hws_definer *definer,
					 u32 miss_ft_id)
{
	struct mlx5hws_cmd_rtc_create_attr rtc_attr = {0};
	struct mlx5hws_range_action_table *table_ste;
	struct mlx5hws_pool_attr pool_attr = {0};
	struct mlx5hws_pool *ste_pool, *stc_pool;
	u32 *rtc_0_id, *rtc_1_id;
	u32 obj_id;
	int ret;

	/* Check if STE range is supported */
	if (!IS_BIT_SET(ctx->caps->supp_ste_format_gen_wqe, MLX5_IFC_RTC_STE_FORMAT_RANGE)) {
		mlx5hws_err(ctx, "Range STE format not supported\n");
		return NULL;
	}

	table_ste = kzalloc(sizeof(*table_ste), GFP_KERNEL);
	if (!table_ste)
		return NULL;

	mutex_lock(&ctx->ctrl_lock);

	pool_attr.table_type = MLX5HWS_TABLE_TYPE_FDB;
	pool_attr.pool_type = MLX5HWS_POOL_TYPE_STE;
	pool_attr.alloc_log_sz = 1;
	table_ste->pool = mlx5hws_pool_create(ctx, &pool_attr);
	if (!table_ste->pool) {
		mlx5hws_err(ctx, "Failed to allocate memory ste pool\n");
		goto free_ste;
	}

	/* Allocate RTC */
	rtc_0_id = &table_ste->rtc_0_id;
	rtc_1_id = &table_ste->rtc_1_id;
	ste_pool = table_ste->pool;

	rtc_attr.log_size = 0;
	rtc_attr.log_depth = 0;
	rtc_attr.miss_ft_id = miss_ft_id;
	rtc_attr.num_hash_definer = 1;
	rtc_attr.update_index_mode = MLX5_IFC_RTC_STE_UPDATE_MODE_BY_HASH;
	rtc_attr.access_index_mode = MLX5_IFC_RTC_STE_ACCESS_MODE_BY_HASH;
	rtc_attr.match_definer_0 = ctx->caps->trivial_match_definer;
	rtc_attr.fw_gen_wqe = true;
	rtc_attr.is_scnd_range = true;

	obj_id = mlx5hws_pool_get_base_id(ste_pool);

	rtc_attr.pd = ctx->pd_num;
	rtc_attr.ste_base = obj_id;
	rtc_attr.reparse_mode = mlx5hws_context_get_reparse_mode(ctx);
	rtc_attr.table_type = mlx5hws_table_get_res_fw_ft_type(MLX5HWS_TABLE_TYPE_FDB, false);

	/* STC is a single resource (obj_id), use any STC for the ID */
	stc_pool = ctx->stc_pool;
	obj_id = mlx5hws_pool_get_base_id(stc_pool);
	rtc_attr.stc_base = obj_id;

	ret = mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr, rtc_0_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create RTC");
		goto pool_destroy;
	}

	/* Create mirror RTC */
	obj_id = mlx5hws_pool_get_base_mirror_id(ste_pool);
	rtc_attr.ste_base = obj_id;
	rtc_attr.table_type = mlx5hws_table_get_res_fw_ft_type(MLX5HWS_TABLE_TYPE_FDB, true);

	obj_id = mlx5hws_pool_get_base_mirror_id(stc_pool);
	rtc_attr.stc_base = obj_id;

	ret = mlx5hws_cmd_rtc_create(ctx->mdev, &rtc_attr, rtc_1_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed to create mirror RTC");
		goto destroy_rtc_0;
	}

	mutex_unlock(&ctx->ctrl_lock);

	return table_ste;

destroy_rtc_0:
	mlx5hws_cmd_rtc_destroy(ctx->mdev, *rtc_0_id);
pool_destroy:
	mlx5hws_pool_destroy(table_ste->pool);
free_ste:
	mutex_unlock(&ctx->ctrl_lock);
	kfree(table_ste);
	return NULL;
}

static void hws_action_destroy_dest_match_range_table(
	struct mlx5hws_context *ctx,
	struct mlx5hws_range_action_table *table_ste)
{
	mutex_lock(&ctx->ctrl_lock);

	mlx5hws_cmd_rtc_destroy(ctx->mdev, table_ste->rtc_1_id);
	mlx5hws_cmd_rtc_destroy(ctx->mdev, table_ste->rtc_0_id);
	mlx5hws_pool_destroy(table_ste->pool);
	kfree(table_ste);

	mutex_unlock(&ctx->ctrl_lock);
}

static int hws_action_create_dest_match_range_fill_table(
	struct mlx5hws_context *ctx,
	struct mlx5hws_range_action_table *table_ste,
	struct mlx5hws_action *hit_ft_action,
	struct mlx5hws_definer *range_definer, u32 min, u32 max)
{
	struct mlx5hws_wqe_gta_data_seg_ste match_wqe_data = {0};
	struct mlx5hws_wqe_gta_data_seg_ste range_wqe_data = {0};
	struct mlx5hws_wqe_gta_ctrl_seg wqe_ctrl = {0};
	u32 no_use, used_rtc_0_id, used_rtc_1_id, ret;
	struct mlx5hws_context_common_res *common_res;
	struct mlx5hws_send_ste_attr ste_attr = {0};
	struct mlx5hws_send_engine *queue;
	__be32 *wqe_data_arr;

	mutex_lock(&ctx->ctrl_lock);

	/* Get the control queue */
	queue = &ctx->send_queue[ctx->queues - 1];
	if (unlikely(mlx5hws_send_engine_err(queue))) {
		ret = -EIO;
		goto error;
	}

	/* Init default send STE attributes */
	ste_attr.gta_opcode = MLX5HWS_WQE_GTA_OP_ACTIVATE;
	ste_attr.send_attr.opmod = MLX5HWS_WQE_GTA_OPMOD_STE;
	ste_attr.send_attr.opcode = MLX5HWS_WQE_OPCODE_TBL_ACCESS;
	ste_attr.send_attr.len = MLX5HWS_WQE_SZ_GTA_CTRL + MLX5HWS_WQE_SZ_GTA_DATA;
	ste_attr.send_attr.user_data = &no_use;
	ste_attr.send_attr.rule = NULL;
	ste_attr.send_attr.fence = 1;
	ste_attr.send_attr.notify_hw = true;
	ste_attr.rtc_0 = table_ste->rtc_0_id;
	ste_attr.rtc_1 = table_ste->rtc_1_id;
	ste_attr.used_id_rtc_0 = &used_rtc_0_id;
	ste_attr.used_id_rtc_1 = &used_rtc_1_id;

	common_res = &ctx->common_res;

	/* init an empty match STE which will always hit */
	ste_attr.wqe_ctrl = &wqe_ctrl;
	ste_attr.wqe_data = &match_wqe_data;
	ste_attr.send_attr.match_definer_id = ctx->caps->trivial_match_definer;

	/* Fill WQE control data */
	wqe_ctrl.stc_ix[MLX5HWS_ACTION_STC_IDX_CTRL] =
		htonl(common_res->default_stc->nop_ctr.offset);
	wqe_ctrl.stc_ix[MLX5HWS_ACTION_STC_IDX_DW5] =
		htonl(common_res->default_stc->nop_dw5.offset);
	wqe_ctrl.stc_ix[MLX5HWS_ACTION_STC_IDX_DW6] =
		htonl(common_res->default_stc->nop_dw6.offset);
	wqe_ctrl.stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] =
		htonl(common_res->default_stc->nop_dw7.offset);
	wqe_ctrl.stc_ix[MLX5HWS_ACTION_STC_IDX_CTRL] |=
		htonl(MLX5HWS_ACTION_STC_IDX_LAST_COMBO2 << 29);
	wqe_ctrl.stc_ix[MLX5HWS_ACTION_STC_IDX_HIT] =
		htonl(hit_ft_action->stc.offset);

	wqe_data_arr = (__force __be32 *)&range_wqe_data;

	ste_attr.range_wqe_data = &range_wqe_data;
	ste_attr.send_attr.len += MLX5HWS_WQE_SZ_GTA_DATA;
	ste_attr.send_attr.range_definer_id = mlx5hws_definer_get_id(range_definer);

	/* Fill range matching fields,
	 * min/max_value_2 corresponds to match_dw_0 in its definer,
	 * min_value_2 sets in DW0 in the STE and max_value_2 sets in DW1 in the STE.
	 */
	wqe_data_arr[MLX5HWS_MATCHER_OFFSET_TAG_DW0] = htonl(min << 16);
	wqe_data_arr[MLX5HWS_MATCHER_OFFSET_TAG_DW1] = htonl(max << 16);

	/* Send WQEs to FW */
	mlx5hws_send_stes_fw(ctx, queue, &ste_attr);

	/* Poll for completion */
	ret = mlx5hws_send_queue_action(ctx, ctx->queues - 1,
					MLX5HWS_SEND_QUEUE_ACTION_DRAIN_SYNC);
	if (ret) {
		mlx5hws_err(ctx, "Failed to drain control queue");
		goto error;
	}

	mutex_unlock(&ctx->ctrl_lock);

	return 0;

error:
	mutex_unlock(&ctx->ctrl_lock);
	return ret;
}

struct mlx5hws_action *
mlx5hws_action_create_dest_match_range(struct mlx5hws_context *ctx,
				       u32 field,
				       struct mlx5_flow_table *hit_ft,
				       struct mlx5_flow_table *miss_ft,
				       u32 min, u32 max, u32 flags)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = {0};
	struct mlx5hws_range_action_table *table_ste;
	struct mlx5hws_action *hit_ft_action;
	struct mlx5hws_definer *definer;
	struct mlx5hws_action *action;
	u32 miss_ft_id = miss_ft->id;
	u32 hit_ft_id = hit_ft->id;
	int ret;

	if (field != MLX5_FLOW_DEST_RANGE_FIELD_PKT_LEN ||
	    min > 0xffff || max > 0xffff) {
		mlx5hws_err(ctx, "Invalid match range parameters\n");
		return NULL;
	}

	action = hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_RANGE);
	if (!action)
		return NULL;

	definer = hws_action_create_dest_match_range_definer(ctx);
	if (!definer)
		goto free_action;

	table_ste = hws_action_create_dest_match_range_table(ctx, definer, miss_ft_id);
	if (!table_ste)
		goto destroy_definer;

	hit_ft_action = mlx5hws_action_create_dest_table_num(ctx, hit_ft_id, flags);
	if (!hit_ft_action)
		goto destroy_table_ste;

	ret = hws_action_create_dest_match_range_fill_table(ctx, table_ste,
							    hit_ft_action,
							    definer, min, max);
	if (ret)
		goto destroy_hit_ft_action;

	action->range.table_ste = table_ste;
	action->range.definer = definer;
	action->range.hit_ft_action = hit_ft_action;

	/* Allocate STC for jumps to STE */
	mutex_lock(&ctx->ctrl_lock);
	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_HIT;
	stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_STE_TABLE;
	stc_attr.reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;
	stc_attr.ste_table.ste_pool = table_ste->pool;
	stc_attr.ste_table.match_definer_id = ctx->caps->trivial_match_definer;

	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, MLX5HWS_TABLE_TYPE_FDB,
					      &action->stc);
	if (ret)
		goto error_unlock;

	mutex_unlock(&ctx->ctrl_lock);

	return action;

error_unlock:
	mutex_unlock(&ctx->ctrl_lock);
destroy_hit_ft_action:
	mlx5hws_action_destroy(hit_ft_action);
destroy_table_ste:
	hws_action_destroy_dest_match_range_table(ctx, table_ste);
destroy_definer:
	mlx5hws_definer_free(ctx, definer);
free_action:
	kfree(action);
	mlx5hws_err(ctx, "Failed to create action dest match range");
	return NULL;
}

struct mlx5hws_action *
mlx5hws_action_create_last(struct mlx5hws_context *ctx, u32 flags)
{
	return hws_action_create_generic(ctx, flags, MLX5HWS_ACTION_TYP_LAST);
}

struct mlx5hws_action *
mlx5hws_action_create_flow_sampler(struct mlx5hws_context *ctx,
				   u32 sampler_id, u32 flags)
{
	struct mlx5hws_cmd_ft_create_attr ft_attr = {0};
	struct mlx5hws_cmd_set_fte_attr fte_attr = {0};
	struct mlx5hws_cmd_forward_tbl *fw_island;
	struct mlx5hws_cmd_set_fte_dest dest;
	struct mlx5hws_action *action;
	int ret;

	if (flags != (MLX5HWS_ACTION_FLAG_HWS_FDB | MLX5HWS_ACTION_FLAG_SHARED)) {
		mlx5hws_err(ctx, "Unsupported flags for flow sampler\n");
		return NULL;
	}

	ft_attr.type = FS_FT_FDB;
	ft_attr.level = ctx->caps->fdb_ft.max_level - 1;

	dest.destination_type = MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER;
	dest.destination_id = sampler_id;

	fte_attr.dests_num = 1;
	fte_attr.dests = &dest;
	fte_attr.action_flags = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	fte_attr.ignore_flow_level = 1;

	fw_island = mlx5hws_cmd_forward_tbl_create(ctx->mdev, &ft_attr, &fte_attr);
	if (!fw_island)
		return NULL;

	action = hws_action_create_generic(ctx, flags,
					   MLX5HWS_ACTION_TYP_SAMPLER);
	if (!action)
		goto destroy_fw_island;

	ret = hws_action_create_stcs(action, fw_island->ft_id);
	if (ret)
		goto free_action;

	action->flow_sampler.fw_island = fw_island;

	return action;

free_action:
	kfree(action);
destroy_fw_island:
	mlx5hws_cmd_forward_tbl_destroy(ctx->mdev, fw_island);
	return NULL;
}

static void hws_action_destroy_hws(struct mlx5hws_action *action)
{
	u32 ext_reformat_id;
	bool shared_arg;
	u32 obj_id;
	u32 i;

	switch (action->type) {
	case MLX5HWS_ACTION_TYP_MISS:
	case MLX5HWS_ACTION_TYP_TAG:
	case MLX5HWS_ACTION_TYP_DROP:
	case MLX5HWS_ACTION_TYP_CTR:
	case MLX5HWS_ACTION_TYP_TBL:
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2:
	case MLX5HWS_ACTION_TYP_ASO_METER:
	case MLX5HWS_ACTION_TYP_PUSH_VLAN:
	case MLX5HWS_ACTION_TYP_REMOVE_HEADER:
	case MLX5HWS_ACTION_TYP_VPORT:
		hws_action_destroy_stcs(action);
		break;
	case MLX5HWS_ACTION_TYP_POP_VLAN:
		hws_action_destroy_stcs(action);
		hws_action_put_shared_stc(action, MLX5HWS_CONTEXT_SHARED_STC_DOUBLE_POP);
		break;
	case MLX5HWS_ACTION_TYP_DEST_ARRAY:
		hws_action_destroy_stcs(action);
		mlx5hws_cmd_forward_tbl_destroy(action->ctx->mdev, action->dest_array.fw_island);
		for (i = 0; i < action->dest_array.num_dest; i++) {
			ext_reformat_id = action->dest_array.dest_list[i].ext_reformat_id;
			if (ext_reformat_id)
				mlx5hws_cmd_packet_reformat_destroy(action->ctx->mdev,
								    ext_reformat_id);
		}
		kfree(action->dest_array.dest_list);
		break;
	case MLX5HWS_ACTION_TYP_SAMPLER:
		hws_action_destroy_stcs(action);
		mlx5hws_cmd_forward_tbl_destroy(action->ctx->mdev,
						action->flow_sampler.fw_island);
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2:
	case MLX5HWS_ACTION_TYP_MODIFY_HDR:
		shared_arg = false;
		for (i = 0; i < action->modify_header.num_of_patterns; i++) {
			hws_action_destroy_stcs(&action[i]);
			if (action[i].modify_header.num_of_actions > 1) {
				mlx5hws_pat_put_pattern(action[i].ctx,
							action[i].modify_header.pat_id);
				/* Save shared arg object to be freed after */
				obj_id = action[i].modify_header.arg_id;
				shared_arg = true;
			}
		}
		if (shared_arg)
			mlx5hws_arg_destroy(action->ctx, obj_id);
		break;
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3:
		hws_action_put_shared_stc(action, MLX5HWS_CONTEXT_SHARED_STC_DECAP_L3);
		for (i = 0; i < action->reformat.num_of_hdrs; i++)
			hws_action_destroy_stcs(&action[i]);
		mlx5hws_arg_destroy(action->ctx, action->reformat.arg_id);
		break;
	case MLX5HWS_ACTION_TYP_INSERT_HEADER:
	case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2:
		for (i = 0; i < action->reformat.num_of_hdrs; i++)
			hws_action_destroy_stcs(&action[i]);
		mlx5hws_arg_destroy(action->ctx, action->reformat.arg_id);
		break;
	case MLX5HWS_ACTION_TYP_RANGE:
		hws_action_destroy_stcs(action);
		hws_action_destroy_dest_match_range_table(action->ctx, action->range.table_ste);
		mlx5hws_definer_free(action->ctx, action->range.definer);
		mlx5hws_action_destroy(action->range.hit_ft_action);
		break;
	case MLX5HWS_ACTION_TYP_LAST:
		break;
	default:
		pr_warn("HWS: Invalid action type: %d\n", action->type);
	}
}

int mlx5hws_action_destroy(struct mlx5hws_action *action)
{
	hws_action_destroy_hws(action);

	kfree(action);
	return 0;
}

int mlx5hws_action_get_default_stc(struct mlx5hws_context *ctx, u8 tbl_type)
__must_hold(&ctx->ctrl_lock)
{
	struct mlx5hws_cmd_stc_modify_attr stc_attr = {0};
	struct mlx5hws_action_default_stc *default_stc;
	int ret;

	if (ctx->common_res.default_stc) {
		ctx->common_res.default_stc->refcount++;
		return 0;
	}

	default_stc = kzalloc(sizeof(*default_stc), GFP_KERNEL);
	if (!default_stc)
		return -ENOMEM;

	stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_NOP;
	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_DW0;
	stc_attr.reparse_mode = MLX5_IFC_STC_REPARSE_IGNORE;
	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl_type,
					      &default_stc->nop_ctr);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate default counter STC\n");
		goto free_default_stc;
	}

	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_DW5;
	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl_type,
					      &default_stc->nop_dw5);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate default NOP DW5 STC\n");
		goto free_nop_ctr;
	}

	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_DW6;
	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl_type,
					      &default_stc->nop_dw6);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate default NOP DW6 STC\n");
		goto free_nop_dw5;
	}

	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_DW7;
	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl_type,
					      &default_stc->nop_dw7);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate default NOP DW7 STC\n");
		goto free_nop_dw6;
	}

	stc_attr.action_offset = MLX5HWS_ACTION_OFFSET_HIT;
	stc_attr.action_type = MLX5_IFC_STC_ACTION_TYPE_ALLOW;

	ret = mlx5hws_action_alloc_single_stc(ctx, &stc_attr, tbl_type,
					      &default_stc->default_hit);
	if (ret) {
		mlx5hws_err(ctx, "Failed to allocate default allow STC\n");
		goto free_nop_dw7;
	}

	ctx->common_res.default_stc = default_stc;
	ctx->common_res.default_stc->refcount++;

	return 0;

free_nop_dw7:
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_dw7);
free_nop_dw6:
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_dw6);
free_nop_dw5:
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_dw5);
free_nop_ctr:
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_ctr);
free_default_stc:
	kfree(default_stc);
	return ret;
}

void mlx5hws_action_put_default_stc(struct mlx5hws_context *ctx, u8 tbl_type)
__must_hold(&ctx->ctrl_lock)
{
	struct mlx5hws_action_default_stc *default_stc;

	default_stc = ctx->common_res.default_stc;
	if (--default_stc->refcount)
		return;

	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->default_hit);
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_dw7);
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_dw6);
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_dw5);
	mlx5hws_action_free_single_stc(ctx, tbl_type, &default_stc->nop_ctr);
	kfree(default_stc);
	ctx->common_res.default_stc = NULL;
}

static void hws_action_modify_write(struct mlx5hws_send_engine *queue,
				    u32 arg_idx,
				    u8 *arg_data,
				    u16 num_of_actions,
				    u32 nop_locations)
{
	u8 *new_arg_data = NULL;
	int i, j;

	if (unlikely(nop_locations)) {
		new_arg_data = kcalloc(num_of_actions,
				       MLX5HWS_MODIFY_ACTION_SIZE, GFP_KERNEL);
		if (unlikely(!new_arg_data))
			return;

		for (i = 0, j = 0; j < num_of_actions; i++, j++) {
			if (BIT(i) & nop_locations)
				j++;
			memcpy(&new_arg_data[j * MLX5HWS_MODIFY_ACTION_SIZE],
			       &arg_data[i * MLX5HWS_MODIFY_ACTION_SIZE],
			       MLX5HWS_MODIFY_ACTION_SIZE);
		}
	}

	mlx5hws_arg_write(queue, NULL, arg_idx,
			  new_arg_data ? new_arg_data : arg_data,
			  num_of_actions * MLX5HWS_MODIFY_ACTION_SIZE);

	kfree(new_arg_data);
}

void mlx5hws_action_prepare_decap_l3_data(u8 *src, u8 *dst, u16 num_of_actions)
{
	u8 *e_src;
	int i;

	/* num_of_actions = remove l3l2 + 4/5 inserts + remove extra 2 bytes
	 * copy from end of src to the start of dst.
	 * move to the end, 2 is the leftover from 14B or 18B
	 */
	if (num_of_actions == DECAP_L3_NUM_ACTIONS_W_NO_VLAN)
		e_src = src + MLX5HWS_ACTION_HDR_LEN_L2;
	else
		e_src = src + MLX5HWS_ACTION_HDR_LEN_L2_W_VLAN;

	/* Move dst over the first remove action + zero data */
	dst += MLX5HWS_ACTION_DOUBLE_SIZE;
	/* Move dst over the first insert ctrl action */
	dst += MLX5HWS_ACTION_DOUBLE_SIZE / 2;
	/* Actions:
	 * no vlan: r_h-insert_4b-insert_4b-insert_4b-insert_4b-remove_2b.
	 * with vlan: r_h-insert_4b-insert_4b-insert_4b-insert_4b-insert_4b-remove_2b.
	 * the loop is without the last insertion.
	 */
	for (i = 0; i < num_of_actions - 3; i++) {
		e_src -= MLX5HWS_ACTION_INLINE_DATA_SIZE;
		memcpy(dst, e_src, MLX5HWS_ACTION_INLINE_DATA_SIZE); /* data */
		dst += MLX5HWS_ACTION_DOUBLE_SIZE;
	}
	/* Copy the last 2 bytes after a gap of 2 bytes which will be removed */
	e_src -= MLX5HWS_ACTION_INLINE_DATA_SIZE / 2;
	dst += MLX5HWS_ACTION_INLINE_DATA_SIZE / 2;
	memcpy(dst, e_src, 2);
}

static int
hws_action_get_shared_stc_offset(struct mlx5hws_context_common_res *common_res,
				 enum mlx5hws_context_shared_stc_type stc_type)
{
	return common_res->shared_stc[stc_type]->stc_chunk.offset;
}

static struct mlx5hws_actions_wqe_setter *
hws_action_setter_find_first(struct mlx5hws_actions_wqe_setter *setter,
			     u8 req_flags)
{
	/* Use a new setter if requested flags are taken */
	while (setter->flags & req_flags)
		setter++;

	/* Use current setter in required flags are not used */
	return setter;
}

static void
hws_action_apply_stc(struct mlx5hws_actions_apply_data *apply,
		     enum mlx5hws_action_stc_idx stc_idx,
		     u8 action_idx)
{
	struct mlx5hws_action *action = apply->rule_action[action_idx].action;

	apply->wqe_ctrl->stc_ix[stc_idx] = htonl(action->stc.offset);
}

static void
hws_action_setter_push_vlan(struct mlx5hws_actions_apply_data *apply,
			    struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;

	rule_action = &apply->rule_action[setter->idx_double];
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = 0;
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = rule_action->push_vlan.vlan_hdr;

	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_DW6, setter->idx_double);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] = 0;
}

static void
hws_action_setter_modify_header(struct mlx5hws_actions_apply_data *apply,
				struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;
	struct mlx5hws_action *action;
	u32 arg_sz, arg_idx;
	u8 *single_action;
	u8 max_actions;
	__be32 stc_idx;

	rule_action = &apply->rule_action[setter->idx_double];
	action = rule_action->action;

	stc_idx = htonl(action->stc.offset);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW6] = stc_idx;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] = 0;

	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = 0;

	if (action->modify_header.num_of_actions == 1) {
		if (action->modify_header.single_action_type ==
		    MLX5_MODIFICATION_TYPE_COPY ||
		    action->modify_header.single_action_type ==
		    MLX5_MODIFICATION_TYPE_ADD_FIELD) {
			apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = 0;
			return;
		}

		if (action->flags & MLX5HWS_ACTION_FLAG_SHARED)
			single_action = (u8 *)&action->modify_header.single_action;
		else
			single_action = rule_action->modify_header.data;

		apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] =
			*(__be32 *)MLX5_ADDR_OF(set_action_in, single_action, data);
		return;
	}

	/* Argument offset multiple with number of args per these actions */
	max_actions = action->modify_header.max_num_of_actions;
	arg_sz = mlx5hws_arg_get_arg_size(max_actions);
	arg_idx = rule_action->modify_header.offset * arg_sz;

	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = htonl(arg_idx);

	if (!(action->flags & MLX5HWS_ACTION_FLAG_SHARED)) {
		apply->require_dep = 1;
		hws_action_modify_write(apply->queue,
					action->modify_header.arg_id + arg_idx,
					rule_action->modify_header.data,
					action->modify_header.num_of_actions,
					action->modify_header.nop_locations);
	}
}

static void
hws_action_setter_insert_ptr(struct mlx5hws_actions_apply_data *apply,
			     struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;
	struct mlx5hws_action *action;
	u32 arg_idx, arg_sz;
	__be32 stc_idx;

	rule_action = &apply->rule_action[setter->idx_double];
	action = rule_action->action + rule_action->reformat.hdr_idx;

	/* Argument offset multiple on args required for header size */
	arg_sz = mlx5hws_arg_data_size_to_arg_size(action->reformat.max_hdr_sz);
	arg_idx = rule_action->reformat.offset * arg_sz;

	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = 0;
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = htonl(arg_idx);

	stc_idx = htonl(action->stc.offset);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW6] = stc_idx;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] = 0;

	if (!(action->flags & MLX5HWS_ACTION_FLAG_SHARED)) {
		apply->require_dep = 1;
		mlx5hws_arg_write(apply->queue, NULL,
				  action->reformat.arg_id + arg_idx,
				  rule_action->reformat.data,
				  action->reformat.header_size);
	}
}

static void
hws_action_setter_tnl_l3_to_l2(struct mlx5hws_actions_apply_data *apply,
			       struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;
	struct mlx5hws_action *action;
	u32 arg_sz, arg_idx;
	__be32 stc_idx;

	rule_action = &apply->rule_action[setter->idx_double];
	action = rule_action->action + rule_action->reformat.hdr_idx;

	/* Argument offset multiple on args required for num of actions */
	arg_sz = mlx5hws_arg_get_arg_size(action->modify_header.max_num_of_actions);
	arg_idx = rule_action->reformat.offset * arg_sz;

	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = 0;
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = htonl(arg_idx);

	stc_idx = htonl(action->stc.offset);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW6] = stc_idx;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] = 0;

	if (!(action->flags & MLX5HWS_ACTION_FLAG_SHARED)) {
		apply->require_dep = 1;
		mlx5hws_arg_decapl3_write(apply->queue,
					  action->modify_header.arg_id + arg_idx,
					  rule_action->reformat.data,
					  action->modify_header.num_of_actions);
	}
}

static void
hws_action_setter_aso(struct mlx5hws_actions_apply_data *apply,
		      struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;
	u32 exe_aso_ctrl;
	u32 offset;

	rule_action = &apply->rule_action[setter->idx_double];

	switch (rule_action->action->type) {
	case MLX5HWS_ACTION_TYP_ASO_METER:
		/* exe_aso_ctrl format:
		 * [STC only and reserved bits 29b][init_color 2b][meter_id 1b]
		 */
		offset = rule_action->aso_meter.offset / MLX5_ASO_METER_NUM_PER_OBJ;
		exe_aso_ctrl = rule_action->aso_meter.offset % MLX5_ASO_METER_NUM_PER_OBJ;
		exe_aso_ctrl |= rule_action->aso_meter.init_color <<
				MLX5HWS_ACTION_METER_INIT_COLOR_OFFSET;
		break;
	default:
		mlx5hws_err(rule_action->action->ctx,
			    "Unsupported ASO action type: %d\n", rule_action->action->type);
		return;
	}

	/* aso_object_offset format: [24B] */
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW6] = htonl(offset);
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW7] = htonl(exe_aso_ctrl);

	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_DW6, setter->idx_double);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW7] = 0;
}

static void
hws_action_setter_tag(struct mlx5hws_actions_apply_data *apply,
		      struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;

	rule_action = &apply->rule_action[setter->idx_single];
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW5] = htonl(rule_action->tag.value);
	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_DW5, setter->idx_single);
}

static void
hws_action_setter_ctrl_ctr(struct mlx5hws_actions_apply_data *apply,
			   struct mlx5hws_actions_wqe_setter *setter)
{
	struct mlx5hws_rule_action *rule_action;

	rule_action = &apply->rule_action[setter->idx_ctr];
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW0] = htonl(rule_action->counter.offset);
	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_CTRL, setter->idx_ctr);
}

static void
hws_action_setter_single(struct mlx5hws_actions_apply_data *apply,
			 struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW5] = 0;
	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_DW5, setter->idx_single);
}

static void
hws_action_setter_single_double_pop(struct mlx5hws_actions_apply_data *apply,
				    struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW5] = 0;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW5] =
		htonl(hws_action_get_shared_stc_offset(apply->common_res,
						       MLX5HWS_CONTEXT_SHARED_STC_DOUBLE_POP));
}

static void
hws_action_setter_hit(struct mlx5hws_actions_apply_data *apply,
		      struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_HIT_LSB] = 0;
	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_HIT, setter->idx_hit);
}

static void
hws_action_setter_default_hit(struct mlx5hws_actions_apply_data *apply,
			      struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_HIT_LSB] = 0;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_HIT] =
		htonl(apply->common_res->default_stc->default_hit.offset);
}

static void
hws_action_setter_hit_next_action(struct mlx5hws_actions_apply_data *apply,
				  struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_HIT_LSB] = htonl(apply->next_direct_idx << 6);
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_HIT] = htonl(apply->jump_to_action_stc);
}

static void
hws_action_setter_common_decap(struct mlx5hws_actions_apply_data *apply,
			       struct mlx5hws_actions_wqe_setter *setter)
{
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_DW5] = 0;
	apply->wqe_ctrl->stc_ix[MLX5HWS_ACTION_STC_IDX_DW5] =
		htonl(hws_action_get_shared_stc_offset(apply->common_res,
						       MLX5HWS_CONTEXT_SHARED_STC_DECAP_L3));
}

static void
hws_action_setter_range(struct mlx5hws_actions_apply_data *apply,
			struct mlx5hws_actions_wqe_setter *setter)
{
	/* Always jump to index zero */
	apply->wqe_data[MLX5HWS_ACTION_OFFSET_HIT_LSB] = 0;
	hws_action_apply_stc(apply, MLX5HWS_ACTION_STC_IDX_HIT, setter->idx_hit);
}

int mlx5hws_action_template_process(struct mlx5hws_action_template *at)
{
	struct mlx5hws_actions_wqe_setter *start_setter = at->setters + 1;
	enum mlx5hws_action_type *action_type = at->action_type_arr;
	struct mlx5hws_actions_wqe_setter *setter = at->setters;
	struct mlx5hws_actions_wqe_setter *pop_setter = NULL;
	struct mlx5hws_actions_wqe_setter *last_setter;
	int i;

	/* Note: Given action combination must be valid */

	/* Check if action were already processed */
	if (at->num_of_action_stes)
		return 0;

	for (i = 0; i < MLX5HWS_ACTION_MAX_STE; i++)
		setter[i].set_hit = &hws_action_setter_hit_next_action;

	/* The same action template setters can be used with jumbo or match
	 * STE, to support both cases we reserve the first setter for cases
	 * with jumbo STE to allow jump to the first action STE.
	 * This extra setter can be reduced in some cases on rule creation.
	 */
	setter = start_setter;
	last_setter = start_setter;

	for (i = 0; i < at->num_actions; i++) {
		switch (action_type[i]) {
		case MLX5HWS_ACTION_TYP_DROP:
		case MLX5HWS_ACTION_TYP_TBL:
		case MLX5HWS_ACTION_TYP_DEST_ARRAY:
		case MLX5HWS_ACTION_TYP_SAMPLER:
		case MLX5HWS_ACTION_TYP_VPORT:
		case MLX5HWS_ACTION_TYP_MISS:
			/* Hit action */
			last_setter->flags |= ASF_HIT;
			last_setter->set_hit = &hws_action_setter_hit;
			last_setter->idx_hit = i;
			break;

		case MLX5HWS_ACTION_TYP_RANGE:
			last_setter->flags |= ASF_HIT;
			last_setter->set_hit = &hws_action_setter_range;
			last_setter->idx_hit = i;
			break;

		case MLX5HWS_ACTION_TYP_POP_VLAN:
			/* Single remove header to header */
			if (pop_setter) {
				/* We have 2 pops, use the shared */
				pop_setter->set_single = &hws_action_setter_single_double_pop;
				break;
			}
			setter = hws_action_setter_find_first(last_setter,
							      ASF_SINGLE1 | ASF_MODIFY |
							      ASF_INSERT);
			setter->flags |= ASF_SINGLE1 | ASF_REMOVE;
			setter->set_single = &hws_action_setter_single;
			setter->idx_single = i;
			pop_setter = setter;
			break;

		case MLX5HWS_ACTION_TYP_PUSH_VLAN:
			/* Double insert inline */
			setter = hws_action_setter_find_first(last_setter, ASF_DOUBLE | ASF_REMOVE);
			setter->flags |= ASF_DOUBLE | ASF_INSERT;
			setter->set_double = &hws_action_setter_push_vlan;
			setter->idx_double = i;
			break;

		case MLX5HWS_ACTION_TYP_MODIFY_HDR:
			/* Double modify header list */
			setter = hws_action_setter_find_first(last_setter, ASF_DOUBLE | ASF_REMOVE);
			setter->flags |= ASF_DOUBLE | ASF_MODIFY;
			setter->set_double = &hws_action_setter_modify_header;
			setter->idx_double = i;
			break;

		case MLX5HWS_ACTION_TYP_ASO_METER:
			/* Double ASO action */
			setter = hws_action_setter_find_first(last_setter, ASF_DOUBLE);
			setter->flags |= ASF_DOUBLE;
			setter->set_double = &hws_action_setter_aso;
			setter->idx_double = i;
			break;

		case MLX5HWS_ACTION_TYP_REMOVE_HEADER:
		case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2:
			/* Single remove header to header */
			setter = hws_action_setter_find_first(last_setter,
							      ASF_SINGLE1 | ASF_MODIFY);
			setter->flags |= ASF_SINGLE1 | ASF_REMOVE;
			setter->set_single = &hws_action_setter_single;
			setter->idx_single = i;
			break;

		case MLX5HWS_ACTION_TYP_INSERT_HEADER:
		case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2:
			/* Double insert header with pointer */
			setter = hws_action_setter_find_first(last_setter, ASF_DOUBLE | ASF_REMOVE);
			setter->flags |= ASF_DOUBLE | ASF_INSERT;
			setter->set_double = &hws_action_setter_insert_ptr;
			setter->idx_double = i;
			break;

		case MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3:
			/* Single remove + Double insert header with pointer */
			setter = hws_action_setter_find_first(last_setter,
							      ASF_SINGLE1 | ASF_DOUBLE);
			setter->flags |= ASF_SINGLE1 | ASF_DOUBLE;
			setter->set_double = &hws_action_setter_insert_ptr;
			setter->idx_double = i;
			setter->set_single = &hws_action_setter_common_decap;
			setter->idx_single = i;
			break;

		case MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2:
			/* Double modify header list with remove and push inline */
			setter = hws_action_setter_find_first(last_setter, ASF_DOUBLE | ASF_REMOVE);
			setter->flags |= ASF_DOUBLE | ASF_MODIFY | ASF_INSERT;
			setter->set_double = &hws_action_setter_tnl_l3_to_l2;
			setter->idx_double = i;
			break;

		case MLX5HWS_ACTION_TYP_TAG:
			/* Single TAG action, search for any room from the start */
			setter = hws_action_setter_find_first(start_setter, ASF_SINGLE1);
			setter->flags |= ASF_SINGLE1;
			setter->set_single = &hws_action_setter_tag;
			setter->idx_single = i;
			break;

		case MLX5HWS_ACTION_TYP_CTR:
			/* Control counter action
			 * TODO: Current counter executed first. Support is needed
			 *	 for single ation counter action which is done last.
			 *	 Example: Decap + CTR
			 */
			setter = hws_action_setter_find_first(start_setter, ASF_CTR);
			setter->flags |= ASF_CTR;
			setter->set_ctr = &hws_action_setter_ctrl_ctr;
			setter->idx_ctr = i;
			break;
		default:
			pr_warn("HWS: Invalid action type in processingaction template: action_type[%d]=%d\n",
				i, action_type[i]);
			return -EOPNOTSUPP;
		}

		last_setter = max(setter, last_setter);
	}

	/* Set default hit on the last STE if no hit action provided */
	if (!(last_setter->flags & ASF_HIT))
		last_setter->set_hit = &hws_action_setter_default_hit;

	at->num_of_action_stes = last_setter - start_setter + 1;

	/* Check if action template doesn't require any action DWs */
	at->only_term = (at->num_of_action_stes == 1) &&
		!(last_setter->flags & ~(ASF_CTR | ASF_HIT));

	return 0;
}

struct mlx5hws_action_template *
mlx5hws_action_template_create(enum mlx5hws_action_type action_type[])
{
	struct mlx5hws_action_template *at;
	u8 num_actions = 0;
	int i;

	at = kzalloc(sizeof(*at), GFP_KERNEL);
	if (!at)
		return NULL;

	while (action_type[num_actions++] != MLX5HWS_ACTION_TYP_LAST)
		;

	at->num_actions = num_actions - 1;
	at->action_type_arr = kcalloc(num_actions, sizeof(*action_type), GFP_KERNEL);
	if (!at->action_type_arr)
		goto free_at;

	for (i = 0; i < num_actions; i++)
		at->action_type_arr[i] = action_type[i];

	return at;

free_at:
	kfree(at);
	return NULL;
}

int mlx5hws_action_template_destroy(struct mlx5hws_action_template *at)
{
	kfree(at->action_type_arr);
	kfree(at);
	return 0;
}
