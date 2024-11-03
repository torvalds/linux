// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

static enum mlx5_ifc_flow_destination_type
hws_cmd_dest_type_to_ifc_dest_type(enum mlx5_flow_destination_type type)
{
	switch (type) {
	case MLX5_FLOW_DESTINATION_TYPE_VPORT:
		return MLX5_IFC_FLOW_DESTINATION_TYPE_VPORT;
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
		return MLX5_IFC_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	case MLX5_FLOW_DESTINATION_TYPE_TIR:
		return MLX5_IFC_FLOW_DESTINATION_TYPE_TIR;
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER:
		return MLX5_IFC_FLOW_DESTINATION_TYPE_FLOW_SAMPLER;
	case MLX5_FLOW_DESTINATION_TYPE_UPLINK:
		return MLX5_IFC_FLOW_DESTINATION_TYPE_UPLINK;
	case MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE:
		return MLX5_IFC_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	case MLX5_FLOW_DESTINATION_TYPE_NONE:
	case MLX5_FLOW_DESTINATION_TYPE_PORT:
	case MLX5_FLOW_DESTINATION_TYPE_COUNTER:
	case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM:
	case MLX5_FLOW_DESTINATION_TYPE_RANGE:
	default:
		pr_warn("HWS: unknown flow dest type %d\n", type);
		return 0;
	}
};

static int hws_cmd_general_obj_destroy(struct mlx5_core_dev *mdev,
				       u32 object_type,
				       u32 object_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, object_type);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, object_id);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5hws_cmd_flow_table_create(struct mlx5_core_dev *mdev,
				  struct mlx5hws_cmd_ft_create_attr *ft_attr,
				  u32 *table_id)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)] = {0};
	void *ft_ctx;
	int ret;

	MLX5_SET(create_flow_table_in, in, opcode, MLX5_CMD_OP_CREATE_FLOW_TABLE);
	MLX5_SET(create_flow_table_in, in, table_type, ft_attr->type);

	ft_ctx = MLX5_ADDR_OF(create_flow_table_in, in, flow_table_context);
	MLX5_SET(flow_table_context, ft_ctx, level, ft_attr->level);
	MLX5_SET(flow_table_context, ft_ctx, rtc_valid, ft_attr->rtc_valid);
	MLX5_SET(flow_table_context, ft_ctx, reformat_en, ft_attr->reformat_en);
	MLX5_SET(flow_table_context, ft_ctx, decap_en, ft_attr->decap_en);

	ret = mlx5_cmd_exec_inout(mdev, create_flow_table, in, out);
	if (ret)
		return ret;

	*table_id = MLX5_GET(create_flow_table_out, out, table_id);

	return 0;
}

int mlx5hws_cmd_flow_table_modify(struct mlx5_core_dev *mdev,
				  struct mlx5hws_cmd_ft_modify_attr *ft_attr,
				  u32 table_id)
{
	u32 in[MLX5_ST_SZ_DW(modify_flow_table_in)] = {0};
	void *ft_ctx;

	MLX5_SET(modify_flow_table_in, in, opcode, MLX5_CMD_OP_MODIFY_FLOW_TABLE);
	MLX5_SET(modify_flow_table_in, in, table_type, ft_attr->type);
	MLX5_SET(modify_flow_table_in, in, modify_field_select, ft_attr->modify_fs);
	MLX5_SET(modify_flow_table_in, in, table_id, table_id);

	ft_ctx = MLX5_ADDR_OF(modify_flow_table_in, in, flow_table_context);

	MLX5_SET(flow_table_context, ft_ctx, table_miss_action, ft_attr->table_miss_action);
	MLX5_SET(flow_table_context, ft_ctx, table_miss_id, ft_attr->table_miss_id);
	MLX5_SET(flow_table_context, ft_ctx, hws.rtc_id_0, ft_attr->rtc_id_0);
	MLX5_SET(flow_table_context, ft_ctx, hws.rtc_id_1, ft_attr->rtc_id_1);

	return mlx5_cmd_exec_in(mdev, modify_flow_table, in);
}

int mlx5hws_cmd_flow_table_query(struct mlx5_core_dev *mdev,
				 u32 table_id,
				 struct mlx5hws_cmd_ft_query_attr *ft_attr,
				 u64 *icm_addr_0, u64 *icm_addr_1)
{
	u32 out[MLX5_ST_SZ_DW(query_flow_table_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(query_flow_table_in)] = {0};
	void *ft_ctx;
	int ret;

	MLX5_SET(query_flow_table_in, in, opcode, MLX5_CMD_OP_QUERY_FLOW_TABLE);
	MLX5_SET(query_flow_table_in, in, table_type, ft_attr->type);
	MLX5_SET(query_flow_table_in, in, table_id, table_id);

	ret = mlx5_cmd_exec_inout(mdev, query_flow_table, in, out);
	if (ret)
		return ret;

	ft_ctx = MLX5_ADDR_OF(query_flow_table_out, out, flow_table_context);
	*icm_addr_0 = MLX5_GET64(flow_table_context, ft_ctx, sws.sw_owner_icm_root_0);
	*icm_addr_1 = MLX5_GET64(flow_table_context, ft_ctx, sws.sw_owner_icm_root_1);

	return ret;
}

int mlx5hws_cmd_flow_table_destroy(struct mlx5_core_dev *mdev,
				   u8 fw_ft_type, u32 table_id)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)] = {0};

	MLX5_SET(destroy_flow_table_in, in, opcode, MLX5_CMD_OP_DESTROY_FLOW_TABLE);
	MLX5_SET(destroy_flow_table_in, in, table_type, fw_ft_type);
	MLX5_SET(destroy_flow_table_in, in, table_id, table_id);

	return mlx5_cmd_exec_in(mdev, destroy_flow_table, in);
}

void mlx5hws_cmd_alias_flow_table_destroy(struct mlx5_core_dev *mdev,
					  u32 table_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_FT_ALIAS, table_id);
}

static int hws_cmd_flow_group_create(struct mlx5_core_dev *mdev,
				     struct mlx5hws_cmd_fg_attr *fg_attr,
				     u32 *group_id)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)] = {0};
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u32 *in;
	int ret;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_flow_group_in, in, opcode, MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET(create_flow_group_in, in, table_type, fg_attr->table_type);
	MLX5_SET(create_flow_group_in, in, table_id, fg_attr->table_id);

	ret = mlx5_cmd_exec_inout(mdev, create_flow_group, in, out);
	if (ret)
		goto out;

	*group_id = MLX5_GET(create_flow_group_out, out, group_id);

out:
	kvfree(in);
	return ret;
}

static int hws_cmd_flow_group_destroy(struct mlx5_core_dev *mdev,
				      u32 ft_id, u32 fg_id, u8 ft_type)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)] = {};

	MLX5_SET(destroy_flow_group_in, in, opcode, MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET(destroy_flow_group_in, in, table_type, ft_type);
	MLX5_SET(destroy_flow_group_in, in, table_id, ft_id);
	MLX5_SET(destroy_flow_group_in, in, group_id, fg_id);

	return mlx5_cmd_exec_in(mdev, destroy_flow_group, in);
}

int mlx5hws_cmd_set_fte(struct mlx5_core_dev *mdev,
			u32 table_type,
			u32 table_id,
			u32 group_id,
			struct mlx5hws_cmd_set_fte_attr *fte_attr)
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)] = {0};
	void *in_flow_context;
	u32 dest_entry_sz;
	u32 total_dest_sz;
	u32 action_flags;
	u8 *in_dests;
	u32 inlen;
	u32 *in;
	int ret;
	u32 i;

	dest_entry_sz = fte_attr->extended_dest ?
			MLX5_ST_SZ_BYTES(extended_dest_format) :
			MLX5_ST_SZ_BYTES(dest_format);
	total_dest_sz = dest_entry_sz * fte_attr->dests_num;
	inlen = align((MLX5_ST_SZ_BYTES(set_fte_in) + total_dest_sz), DW_SIZE);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	MLX5_SET(set_fte_in, in, table_type, table_type);
	MLX5_SET(set_fte_in, in, table_id, table_id);

	in_flow_context = MLX5_ADDR_OF(set_fte_in, in, flow_context);
	MLX5_SET(flow_context, in_flow_context, group_id, group_id);
	MLX5_SET(flow_context, in_flow_context, flow_source, fte_attr->flow_source);
	MLX5_SET(flow_context, in_flow_context, extended_destination, fte_attr->extended_dest);
	MLX5_SET(set_fte_in, in, ignore_flow_level, fte_attr->ignore_flow_level);

	action_flags = fte_attr->action_flags;
	MLX5_SET(flow_context, in_flow_context, action, action_flags);

	if (action_flags & MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT) {
		MLX5_SET(flow_context, in_flow_context,
			 packet_reformat_id, fte_attr->packet_reformat_id);
	}

	if (action_flags & (MLX5_FLOW_CONTEXT_ACTION_DECRYPT | MLX5_FLOW_CONTEXT_ACTION_ENCRYPT)) {
		MLX5_SET(flow_context, in_flow_context,
			 encrypt_decrypt_type, fte_attr->encrypt_decrypt_type);
		MLX5_SET(flow_context, in_flow_context,
			 encrypt_decrypt_obj_id, fte_attr->encrypt_decrypt_obj_id);
	}

	if (action_flags & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		in_dests = (u8 *)MLX5_ADDR_OF(flow_context, in_flow_context, destination);

		for (i = 0; i < fte_attr->dests_num; i++) {
			struct mlx5hws_cmd_set_fte_dest *dest = &fte_attr->dests[i];
			enum mlx5_ifc_flow_destination_type ifc_dest_type =
				hws_cmd_dest_type_to_ifc_dest_type(dest->destination_type);

			switch (dest->destination_type) {
			case MLX5_FLOW_DESTINATION_TYPE_VPORT:
				if (dest->ext_flags & MLX5HWS_CMD_EXT_DEST_ESW_OWNER_VHCA_ID) {
					MLX5_SET(dest_format, in_dests,
						 destination_eswitch_owner_vhca_id_valid, 1);
					MLX5_SET(dest_format, in_dests,
						 destination_eswitch_owner_vhca_id,
						 dest->esw_owner_vhca_id);
				}
				fallthrough;
			case MLX5_FLOW_DESTINATION_TYPE_TIR:
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
				MLX5_SET(dest_format, in_dests, destination_type, ifc_dest_type);
				MLX5_SET(dest_format, in_dests, destination_id,
					 dest->destination_id);
				if (dest->ext_flags & MLX5HWS_CMD_EXT_DEST_REFORMAT) {
					MLX5_SET(dest_format, in_dests, packet_reformat, 1);
					MLX5_SET(extended_dest_format, in_dests, packet_reformat_id,
						 dest->ext_reformat_id);
				}
				break;
			default:
				ret = -EOPNOTSUPP;
				goto out;
			}

			in_dests = in_dests + dest_entry_sz;
		}
		MLX5_SET(flow_context, in_flow_context, destination_list_size, fte_attr->dests_num);
	}

	ret = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	if (ret)
		mlx5_core_err(mdev, "Failed creating FLOW_TABLE_ENTRY\n");

out:
	kfree(in);
	return ret;
}

int mlx5hws_cmd_delete_fte(struct mlx5_core_dev *mdev,
			   u32 table_type,
			   u32 table_id)
{
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)] = {};

	MLX5_SET(delete_fte_in, in, opcode, MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
	MLX5_SET(delete_fte_in, in, table_type, table_type);
	MLX5_SET(delete_fte_in, in, table_id, table_id);

	return mlx5_cmd_exec_in(mdev, delete_fte, in);
}

struct mlx5hws_cmd_forward_tbl *
mlx5hws_cmd_forward_tbl_create(struct mlx5_core_dev *mdev,
			       struct mlx5hws_cmd_ft_create_attr *ft_attr,
			       struct mlx5hws_cmd_set_fte_attr *fte_attr)
{
	struct mlx5hws_cmd_fg_attr fg_attr = {0};
	struct mlx5hws_cmd_forward_tbl *tbl;
	int ret;

	tbl = kzalloc(sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		return NULL;

	ret = mlx5hws_cmd_flow_table_create(mdev, ft_attr, &tbl->ft_id);
	if (ret) {
		mlx5_core_err(mdev, "Failed to create FT\n");
		goto free_tbl;
	}

	fg_attr.table_id = tbl->ft_id;
	fg_attr.table_type = ft_attr->type;

	ret = hws_cmd_flow_group_create(mdev, &fg_attr, &tbl->fg_id);
	if (ret) {
		mlx5_core_err(mdev, "Failed to create FG\n");
		goto free_ft;
	}

	ret = mlx5hws_cmd_set_fte(mdev, ft_attr->type,
				  tbl->ft_id, tbl->fg_id, fte_attr);
	if (ret) {
		mlx5_core_err(mdev, "Failed to create FTE\n");
		goto free_fg;
	}

	tbl->type = ft_attr->type;
	return tbl;

free_fg:
	hws_cmd_flow_group_destroy(mdev, tbl->ft_id, tbl->fg_id, ft_attr->type);
free_ft:
	mlx5hws_cmd_flow_table_destroy(mdev, ft_attr->type, tbl->ft_id);
free_tbl:
	kfree(tbl);
	return NULL;
}

void mlx5hws_cmd_forward_tbl_destroy(struct mlx5_core_dev *mdev,
				     struct mlx5hws_cmd_forward_tbl *tbl)
{
	mlx5hws_cmd_delete_fte(mdev, tbl->type, tbl->ft_id);
	hws_cmd_flow_group_destroy(mdev, tbl->ft_id, tbl->fg_id, tbl->type);
	mlx5hws_cmd_flow_table_destroy(mdev, tbl->type, tbl->ft_id);
	kfree(tbl);
}

void mlx5hws_cmd_set_attr_connect_miss_tbl(struct mlx5hws_context *ctx,
					   u32 fw_ft_type,
					   enum mlx5hws_table_type type,
					   struct mlx5hws_cmd_ft_modify_attr *ft_attr)
{
	u32 default_miss_tbl;

	if (type != MLX5HWS_TABLE_TYPE_FDB)
		return;

	ft_attr->modify_fs = MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION;
	ft_attr->type = fw_ft_type;
	ft_attr->table_miss_action = MLX5_IFC_MODIFY_FLOW_TABLE_MISS_ACTION_GOTO_TBL;

	default_miss_tbl = ctx->common_res[type].default_miss->ft_id;
	if (!default_miss_tbl) {
		pr_warn("HWS: no flow table ID for default miss\n");
		return;
	}

	ft_attr->table_miss_id = default_miss_tbl;
}

int mlx5hws_cmd_rtc_create(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_rtc_create_attr *rtc_attr,
			   u32 *rtc_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_rtc_in)] = {0};
	void *attr;
	int ret;

	attr = MLX5_ADDR_OF(create_rtc_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, MLX5_OBJ_TYPE_RTC);

	attr = MLX5_ADDR_OF(create_rtc_in, in, rtc);
	MLX5_SET(rtc, attr, ste_format_0, rtc_attr->is_frst_jumbo ?
		 MLX5_IFC_RTC_STE_FORMAT_11DW :
		 MLX5_IFC_RTC_STE_FORMAT_8DW);

	if (rtc_attr->is_scnd_range) {
		MLX5_SET(rtc, attr, ste_format_1, MLX5_IFC_RTC_STE_FORMAT_RANGE);
		MLX5_SET(rtc, attr, num_match_ste, 2);
	}

	MLX5_SET(rtc, attr, pd, rtc_attr->pd);
	MLX5_SET(rtc, attr, update_method, rtc_attr->fw_gen_wqe);
	MLX5_SET(rtc, attr, update_index_mode, rtc_attr->update_index_mode);
	MLX5_SET(rtc, attr, access_index_mode, rtc_attr->access_index_mode);
	MLX5_SET(rtc, attr, num_hash_definer, rtc_attr->num_hash_definer);
	MLX5_SET(rtc, attr, log_depth, rtc_attr->log_depth);
	MLX5_SET(rtc, attr, log_hash_size, rtc_attr->log_size);
	MLX5_SET(rtc, attr, table_type, rtc_attr->table_type);
	MLX5_SET(rtc, attr, num_hash_definer, rtc_attr->num_hash_definer);
	MLX5_SET(rtc, attr, match_definer_0, rtc_attr->match_definer_0);
	MLX5_SET(rtc, attr, match_definer_1, rtc_attr->match_definer_1);
	MLX5_SET(rtc, attr, stc_id, rtc_attr->stc_base);
	MLX5_SET(rtc, attr, ste_table_base_id, rtc_attr->ste_base);
	MLX5_SET(rtc, attr, ste_table_offset, rtc_attr->ste_offset);
	MLX5_SET(rtc, attr, miss_flow_table_id, rtc_attr->miss_ft_id);
	MLX5_SET(rtc, attr, reparse_mode, rtc_attr->reparse_mode);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create RTC\n");
		goto out;
	}

	*rtc_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

void mlx5hws_cmd_rtc_destroy(struct mlx5_core_dev *mdev, u32 rtc_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_RTC, rtc_id);
}

int mlx5hws_cmd_stc_create(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_stc_create_attr *stc_attr,
			   u32 *stc_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_stc_in)] = {0};
	void *attr;
	int ret;

	attr = MLX5_ADDR_OF(create_stc_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, MLX5_OBJ_TYPE_STC);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, op_param.create.log_obj_range, stc_attr->log_obj_range);

	attr = MLX5_ADDR_OF(create_stc_in, in, stc);
	MLX5_SET(stc, attr, table_type, stc_attr->table_type);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create STC\n");
		goto out;
	}

	*stc_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

void mlx5hws_cmd_stc_destroy(struct mlx5_core_dev *mdev, u32 stc_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_STC, stc_id);
}

static int
hws_cmd_stc_modify_set_stc_param(struct mlx5_core_dev *mdev,
				 struct mlx5hws_cmd_stc_modify_attr *stc_attr,
				 void *stc_param)
{
	switch (stc_attr->action_type) {
	case MLX5_IFC_STC_ACTION_TYPE_COUNTER:
		MLX5_SET(stc_ste_param_flow_counter, stc_param, flow_counter_id, stc_attr->id);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_TIR:
		MLX5_SET(stc_ste_param_tir, stc_param, tirn, stc_attr->dest_tir_num);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_FT:
		MLX5_SET(stc_ste_param_table, stc_param, table_id, stc_attr->dest_table_id);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_ACC_MODIFY_LIST:
		MLX5_SET(stc_ste_param_header_modify_list, stc_param,
			 header_modify_pattern_id, stc_attr->modify_header.pattern_id);
		MLX5_SET(stc_ste_param_header_modify_list, stc_param,
			 header_modify_argument_id, stc_attr->modify_header.arg_id);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_HEADER_REMOVE:
		MLX5_SET(stc_ste_param_remove, stc_param, action_type,
			 MLX5_MODIFICATION_TYPE_REMOVE);
		MLX5_SET(stc_ste_param_remove, stc_param, decap,
			 stc_attr->remove_header.decap);
		MLX5_SET(stc_ste_param_remove, stc_param, remove_start_anchor,
			 stc_attr->remove_header.start_anchor);
		MLX5_SET(stc_ste_param_remove, stc_param, remove_end_anchor,
			 stc_attr->remove_header.end_anchor);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_HEADER_INSERT:
		MLX5_SET(stc_ste_param_insert, stc_param, action_type,
			 MLX5_MODIFICATION_TYPE_INSERT);
		MLX5_SET(stc_ste_param_insert, stc_param, encap,
			 stc_attr->insert_header.encap);
		MLX5_SET(stc_ste_param_insert, stc_param, inline_data,
			 stc_attr->insert_header.is_inline);
		MLX5_SET(stc_ste_param_insert, stc_param, insert_anchor,
			 stc_attr->insert_header.insert_anchor);
		/* HW gets the next 2 sizes in words */
		MLX5_SET(stc_ste_param_insert, stc_param, insert_size,
			 stc_attr->insert_header.header_size / W_SIZE);
		MLX5_SET(stc_ste_param_insert, stc_param, insert_offset,
			 stc_attr->insert_header.insert_offset / W_SIZE);
		MLX5_SET(stc_ste_param_insert, stc_param, insert_argument,
			 stc_attr->insert_header.arg_id);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_COPY:
	case MLX5_IFC_STC_ACTION_TYPE_SET:
	case MLX5_IFC_STC_ACTION_TYPE_ADD:
	case MLX5_IFC_STC_ACTION_TYPE_ADD_FIELD:
		*(__be64 *)stc_param = stc_attr->modify_action.data;
		break;
	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_VPORT:
	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_UPLINK:
		MLX5_SET(stc_ste_param_vport, stc_param, vport_number,
			 stc_attr->vport.vport_num);
		MLX5_SET(stc_ste_param_vport, stc_param, eswitch_owner_vhca_id,
			 stc_attr->vport.esw_owner_vhca_id);
		MLX5_SET(stc_ste_param_vport, stc_param, eswitch_owner_vhca_id_valid,
			 stc_attr->vport.eswitch_owner_vhca_id_valid);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_DROP:
	case MLX5_IFC_STC_ACTION_TYPE_NOP:
	case MLX5_IFC_STC_ACTION_TYPE_TAG:
	case MLX5_IFC_STC_ACTION_TYPE_ALLOW:
		break;
	case MLX5_IFC_STC_ACTION_TYPE_ASO:
		MLX5_SET(stc_ste_param_execute_aso, stc_param, aso_object_id,
			 stc_attr->aso.devx_obj_id);
		MLX5_SET(stc_ste_param_execute_aso, stc_param, return_reg_id,
			 stc_attr->aso.return_reg_id);
		MLX5_SET(stc_ste_param_execute_aso, stc_param, aso_type,
			 stc_attr->aso.aso_type);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_JUMP_TO_STE_TABLE:
		MLX5_SET(stc_ste_param_ste_table, stc_param, ste_obj_id,
			 stc_attr->ste_table.ste_obj_id);
		MLX5_SET(stc_ste_param_ste_table, stc_param, match_definer_id,
			 stc_attr->ste_table.match_definer_id);
		MLX5_SET(stc_ste_param_ste_table, stc_param, log_hash_size,
			 stc_attr->ste_table.log_hash_size);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_REMOVE_WORDS:
		MLX5_SET(stc_ste_param_remove_words, stc_param, action_type,
			 MLX5_MODIFICATION_TYPE_REMOVE_WORDS);
		MLX5_SET(stc_ste_param_remove_words, stc_param, remove_start_anchor,
			 stc_attr->remove_words.start_anchor);
		MLX5_SET(stc_ste_param_remove_words, stc_param,
			 remove_size, stc_attr->remove_words.num_of_words);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_CRYPTO_IPSEC_ENCRYPTION:
		MLX5_SET(stc_ste_param_ipsec_encrypt, stc_param, ipsec_object_id,
			 stc_attr->id);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_CRYPTO_IPSEC_DECRYPTION:
		MLX5_SET(stc_ste_param_ipsec_decrypt, stc_param, ipsec_object_id,
			 stc_attr->id);
		break;
	case MLX5_IFC_STC_ACTION_TYPE_TRAILER:
		MLX5_SET(stc_ste_param_trailer, stc_param, command,
			 stc_attr->reformat_trailer.op);
		MLX5_SET(stc_ste_param_trailer, stc_param, type,
			 stc_attr->reformat_trailer.type);
		MLX5_SET(stc_ste_param_trailer, stc_param, length,
			 stc_attr->reformat_trailer.size);
		break;
	default:
		mlx5_core_err(mdev, "Not supported type %d\n", stc_attr->action_type);
		return -EINVAL;
	}
	return 0;
}

int mlx5hws_cmd_stc_modify(struct mlx5_core_dev *mdev,
			   u32 stc_id,
			   struct mlx5hws_cmd_stc_modify_attr *stc_attr)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_stc_in)] = {0};
	void *stc_param;
	void *attr;
	int ret;

	attr = MLX5_ADDR_OF(create_stc_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, MLX5_OBJ_TYPE_STC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, stc_id);
	MLX5_SET(general_obj_in_cmd_hdr, in,
		 op_param.query.obj_offset, stc_attr->stc_offset);

	attr = MLX5_ADDR_OF(create_stc_in, in, stc);
	MLX5_SET(stc, attr, ste_action_offset, stc_attr->action_offset);
	MLX5_SET(stc, attr, action_type, stc_attr->action_type);
	MLX5_SET(stc, attr, reparse_mode, stc_attr->reparse_mode);
	MLX5_SET64(stc, attr, modify_field_select,
		   MLX5_IFC_MODIFY_STC_FIELD_SELECT_NEW_STC);

	/* Set destination TIRN, TAG, FT ID, STE ID */
	stc_param = MLX5_ADDR_OF(stc, attr, stc_param);
	ret = hws_cmd_stc_modify_set_stc_param(mdev, stc_attr, stc_param);
	if (ret)
		return ret;

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret)
		mlx5_core_err(mdev, "Failed to modify STC FW action_type %d\n",
			      stc_attr->action_type);

	return ret;
}

int mlx5hws_cmd_arg_create(struct mlx5_core_dev *mdev,
			   u16 log_obj_range,
			   u32 pd,
			   u32 *arg_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_arg_in)] = {0};
	void *attr;
	int ret;

	attr = MLX5_ADDR_OF(create_arg_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, MLX5_OBJ_TYPE_HEADER_MODIFY_ARGUMENT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, op_param.create.log_obj_range, log_obj_range);

	attr = MLX5_ADDR_OF(create_arg_in, in, arg);
	MLX5_SET(arg, attr, access_pd, pd);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create ARG\n");
		goto out;
	}

	*arg_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

void mlx5hws_cmd_arg_destroy(struct mlx5_core_dev *mdev,
			     u32 arg_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_HEADER_MODIFY_ARGUMENT, arg_id);
}

int mlx5hws_cmd_header_modify_pattern_create(struct mlx5_core_dev *mdev,
					     u32 pattern_length,
					     u8 *actions,
					     u32 *ptrn_id)
{
	u32 in[MLX5_ST_SZ_DW(create_header_modify_pattern_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	int num_of_actions;
	u64 *pattern_data;
	void *pattern;
	void *attr;
	int ret;
	int i;

	if (pattern_length > MLX5_MAX_ACTIONS_DATA_IN_HEADER_MODIFY) {
		mlx5_core_err(mdev, "Pattern length %d exceeds limit %d\n",
			      pattern_length, MLX5_MAX_ACTIONS_DATA_IN_HEADER_MODIFY);
		return -EINVAL;
	}

	attr = MLX5_ADDR_OF(create_header_modify_pattern_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, MLX5_OBJ_TYPE_MODIFY_HDR_PATTERN);

	pattern = MLX5_ADDR_OF(create_header_modify_pattern_in, in, pattern);
	/* Pattern_length is in ddwords */
	MLX5_SET(header_modify_pattern_in, pattern, pattern_length, pattern_length / (2 * DW_SIZE));

	pattern_data = (u64 *)MLX5_ADDR_OF(header_modify_pattern_in, pattern, pattern_data);
	memcpy(pattern_data, actions, pattern_length);

	num_of_actions = pattern_length / MLX5HWS_MODIFY_ACTION_SIZE;
	for (i = 0; i < num_of_actions; i++) {
		int type;

		type = MLX5_GET(set_action_in, &pattern_data[i], action_type);
		if (type != MLX5_MODIFICATION_TYPE_COPY &&
		    type != MLX5_MODIFICATION_TYPE_ADD_FIELD)
			/* Action typ-copy use all bytes for control */
			MLX5_SET(set_action_in, &pattern_data[i], data, 0);
	}

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create header_modify_pattern\n");
		goto out;
	}

	*ptrn_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

void mlx5hws_cmd_header_modify_pattern_destroy(struct mlx5_core_dev *mdev,
					       u32 ptrn_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_MODIFY_HDR_PATTERN, ptrn_id);
}

int mlx5hws_cmd_ste_create(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_ste_create_attr *ste_attr,
			   u32 *ste_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_ste_in)] = {0};
	void *attr;
	int ret;

	attr = MLX5_ADDR_OF(create_ste_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, MLX5_OBJ_TYPE_STE);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, op_param.create.log_obj_range, ste_attr->log_obj_range);

	attr = MLX5_ADDR_OF(create_ste_in, in, ste);
	MLX5_SET(ste, attr, table_type, ste_attr->table_type);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create STE\n");
		goto out;
	}

	*ste_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

void mlx5hws_cmd_ste_destroy(struct mlx5_core_dev *mdev, u32 ste_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_STE, ste_id);
}

int mlx5hws_cmd_definer_create(struct mlx5_core_dev *mdev,
			       struct mlx5hws_cmd_definer_create_attr *def_attr,
			       u32 *definer_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_definer_in)] = {0};
	void *ptr;
	int ret;

	MLX5_SET(general_obj_in_cmd_hdr,
		 in, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 in, obj_type, MLX5_OBJ_TYPE_MATCH_DEFINER);

	ptr = MLX5_ADDR_OF(create_definer_in, in, definer);
	MLX5_SET(definer, ptr, format_id, MLX5_IFC_DEFINER_FORMAT_ID_SELECT);

	MLX5_SET(definer, ptr, format_select_dw0, def_attr->dw_selector[0]);
	MLX5_SET(definer, ptr, format_select_dw1, def_attr->dw_selector[1]);
	MLX5_SET(definer, ptr, format_select_dw2, def_attr->dw_selector[2]);
	MLX5_SET(definer, ptr, format_select_dw3, def_attr->dw_selector[3]);
	MLX5_SET(definer, ptr, format_select_dw4, def_attr->dw_selector[4]);
	MLX5_SET(definer, ptr, format_select_dw5, def_attr->dw_selector[5]);
	MLX5_SET(definer, ptr, format_select_dw6, def_attr->dw_selector[6]);
	MLX5_SET(definer, ptr, format_select_dw7, def_attr->dw_selector[7]);
	MLX5_SET(definer, ptr, format_select_dw8, def_attr->dw_selector[8]);

	MLX5_SET(definer, ptr, format_select_byte0, def_attr->byte_selector[0]);
	MLX5_SET(definer, ptr, format_select_byte1, def_attr->byte_selector[1]);
	MLX5_SET(definer, ptr, format_select_byte2, def_attr->byte_selector[2]);
	MLX5_SET(definer, ptr, format_select_byte3, def_attr->byte_selector[3]);
	MLX5_SET(definer, ptr, format_select_byte4, def_attr->byte_selector[4]);
	MLX5_SET(definer, ptr, format_select_byte5, def_attr->byte_selector[5]);
	MLX5_SET(definer, ptr, format_select_byte6, def_attr->byte_selector[6]);
	MLX5_SET(definer, ptr, format_select_byte7, def_attr->byte_selector[7]);

	ptr = MLX5_ADDR_OF(definer, ptr, match_mask);
	memcpy(ptr, def_attr->match_mask, MLX5_FLD_SZ_BYTES(definer, match_mask));

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create Definer\n");
		goto out;
	}

	*definer_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

void mlx5hws_cmd_definer_destroy(struct mlx5_core_dev *mdev,
				 u32 definer_id)
{
	hws_cmd_general_obj_destroy(mdev, MLX5_OBJ_TYPE_MATCH_DEFINER, definer_id);
}

int mlx5hws_cmd_packet_reformat_create(struct mlx5_core_dev *mdev,
				       struct mlx5hws_cmd_packet_reformat_create_attr *attr,
				       u32 *reformat_id)
{
	u32 out[MLX5_ST_SZ_DW(alloc_packet_reformat_out)] = {0};
	size_t insz, cmd_data_sz, cmd_total_sz;
	void *prctx;
	void *pdata;
	void *in;
	int ret;

	cmd_total_sz = MLX5_ST_SZ_BYTES(alloc_packet_reformat_context_in);
	cmd_total_sz += MLX5_ST_SZ_BYTES(packet_reformat_context_in);
	cmd_data_sz = MLX5_FLD_SZ_BYTES(packet_reformat_context_in, reformat_data);
	insz = align(cmd_total_sz + attr->data_sz - cmd_data_sz, DW_SIZE);
	in = kzalloc(insz, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(alloc_packet_reformat_context_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT);

	prctx = MLX5_ADDR_OF(alloc_packet_reformat_context_in, in,
			     packet_reformat_context);
	pdata = MLX5_ADDR_OF(packet_reformat_context_in, prctx, reformat_data);

	MLX5_SET(packet_reformat_context_in, prctx, reformat_type, attr->type);
	MLX5_SET(packet_reformat_context_in, prctx, reformat_param_0, attr->reformat_param_0);
	MLX5_SET(packet_reformat_context_in, prctx, reformat_data_size, attr->data_sz);
	memcpy(pdata, attr->data, attr->data_sz);

	ret = mlx5_cmd_exec(mdev, in, insz, out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create packet reformat\n");
		goto out;
	}

	*reformat_id = MLX5_GET(alloc_packet_reformat_out, out, packet_reformat_id);
out:
	kfree(in);
	return ret;
}

int mlx5hws_cmd_packet_reformat_destroy(struct mlx5_core_dev *mdev,
					u32 reformat_id)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_packet_reformat_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(dealloc_packet_reformat_in)] = {0};
	int ret;

	MLX5_SET(dealloc_packet_reformat_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT);
	MLX5_SET(dealloc_packet_reformat_in, in,
		 packet_reformat_id, reformat_id);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret)
		mlx5_core_err(mdev, "Failed to destroy packet_reformat\n");

	return ret;
}

int mlx5hws_cmd_sq_modify_rdy(struct mlx5_core_dev *mdev, u32 sqn)
{
	u32 out[MLX5_ST_SZ_DW(modify_sq_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(modify_sq_in)] = {0};
	void *sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);
	int ret;

	MLX5_SET(modify_sq_in, in, opcode, MLX5_CMD_OP_MODIFY_SQ);
	MLX5_SET(modify_sq_in, in, sqn, sqn);
	MLX5_SET(modify_sq_in, in, sq_state, MLX5_SQC_STATE_RST);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RDY);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret)
		mlx5_core_err(mdev, "Failed to modify SQ\n");

	return ret;
}

int mlx5hws_cmd_allow_other_vhca_access(struct mlx5_core_dev *mdev,
					struct mlx5hws_cmd_allow_other_vhca_access_attr *attr)
{
	u32 out[MLX5_ST_SZ_DW(allow_other_vhca_access_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(allow_other_vhca_access_in)] = {0};
	void *key;
	int ret;

	MLX5_SET(allow_other_vhca_access_in,
		 in, opcode, MLX5_CMD_OP_ALLOW_OTHER_VHCA_ACCESS);
	MLX5_SET(allow_other_vhca_access_in,
		 in, object_type_to_be_accessed, attr->obj_type);
	MLX5_SET(allow_other_vhca_access_in,
		 in, object_id_to_be_accessed, attr->obj_id);

	key = MLX5_ADDR_OF(allow_other_vhca_access_in, in, access_key);
	memcpy(key, attr->access_key, sizeof(attr->access_key));

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret)
		mlx5_core_err(mdev, "Failed to execute ALLOW_OTHER_VHCA_ACCESS command\n");

	return ret;
}

int mlx5hws_cmd_alias_obj_create(struct mlx5_core_dev *mdev,
				 struct mlx5hws_cmd_alias_obj_create_attr *alias_attr,
				 u32 *obj_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_alias_obj_in)] = {0};
	void *attr;
	void *key;
	int ret;

	attr = MLX5_ADDR_OF(create_alias_obj_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr,
		 attr, obj_type, alias_attr->obj_type);
	MLX5_SET(general_obj_in_cmd_hdr, attr, op_param.create.alias_object, 1);

	attr = MLX5_ADDR_OF(create_alias_obj_in, in, alias_ctx);
	MLX5_SET(alias_context, attr, vhca_id_to_be_accessed, alias_attr->vhca_id);
	MLX5_SET(alias_context, attr, object_id_to_be_accessed, alias_attr->obj_id);

	key = MLX5_ADDR_OF(alias_context, attr, access_key);
	memcpy(key, alias_attr->access_key, sizeof(alias_attr->access_key));

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to create ALIAS OBJ\n");
		goto out;
	}

	*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	return ret;
}

int mlx5hws_cmd_alias_obj_destroy(struct mlx5_core_dev *mdev,
				  u16 obj_type,
				  u32 obj_id)
{
	return hws_cmd_general_obj_destroy(mdev, obj_type, obj_id);
}

int mlx5hws_cmd_generate_wqe(struct mlx5_core_dev *mdev,
			     struct mlx5hws_cmd_generate_wqe_attr *attr,
			     struct mlx5_cqe64 *ret_cqe)
{
	u32 out[MLX5_ST_SZ_DW(generate_wqe_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(generate_wqe_in)] = {0};
	u8 status;
	void *ptr;
	int ret;

	MLX5_SET(generate_wqe_in, in, opcode, MLX5_CMD_OP_GENERATE_WQE);
	MLX5_SET(generate_wqe_in, in, pdn, attr->pdn);

	ptr = MLX5_ADDR_OF(generate_wqe_in, in, wqe_ctrl);
	memcpy(ptr, attr->wqe_ctrl, MLX5_FLD_SZ_BYTES(generate_wqe_in, wqe_ctrl));

	ptr = MLX5_ADDR_OF(generate_wqe_in, in, wqe_gta_ctrl);
	memcpy(ptr, attr->gta_ctrl, MLX5_FLD_SZ_BYTES(generate_wqe_in, wqe_gta_ctrl));

	ptr = MLX5_ADDR_OF(generate_wqe_in, in, wqe_gta_data_0);
	memcpy(ptr, attr->gta_data_0, MLX5_FLD_SZ_BYTES(generate_wqe_in, wqe_gta_data_0));

	if (attr->gta_data_1) {
		ptr = MLX5_ADDR_OF(generate_wqe_in, in, wqe_gta_data_1);
		memcpy(ptr, attr->gta_data_1, MLX5_FLD_SZ_BYTES(generate_wqe_in, wqe_gta_data_1));
	}

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		mlx5_core_err(mdev, "Failed to write GTA WQE using FW\n");
		return ret;
	}

	status = MLX5_GET(generate_wqe_out, out, status);
	if (status) {
		mlx5_core_err(mdev, "Invalid FW CQE status %d\n", status);
		return -EINVAL;
	}

	ptr = MLX5_ADDR_OF(generate_wqe_out, out, cqe_data);
	memcpy(ret_cqe, ptr, sizeof(*ret_cqe));

	return ret;
}

int mlx5hws_cmd_query_caps(struct mlx5_core_dev *mdev,
			   struct mlx5hws_cmd_query_caps *caps)
{
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {0};
	u32 out_size;
	u32 *out;
	int ret;

	out_size = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	out = kzalloc(out_size, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE | HCA_CAP_OPMOD_GET_CUR);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
	if (ret) {
		mlx5_core_err(mdev, "Failed to query device caps\n");
		goto out;
	}

	caps->wqe_based_update =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap.wqe_based_flow_table_update_cap);

	caps->eswitch_manager = MLX5_GET(query_hca_cap_out, out,
					 capability.cmd_hca_cap.eswitch_manager);

	caps->flex_protocols = MLX5_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.flex_parser_protocols);

	if (caps->flex_protocols & MLX5_FLEX_PARSER_GENEVE_TLV_OPTION_0_ENABLED)
		caps->flex_parser_id_geneve_tlv_option_0 =
			MLX5_GET(query_hca_cap_out, out,
				 capability.cmd_hca_cap.flex_parser_id_geneve_tlv_option_0);

	if (caps->flex_protocols & MLX5_FLEX_PARSER_MPLS_OVER_GRE_ENABLED)
		caps->flex_parser_id_mpls_over_gre =
			MLX5_GET(query_hca_cap_out, out,
				 capability.cmd_hca_cap.flex_parser_id_outer_first_mpls_over_gre);

	if (caps->flex_protocols & MLX5_FLEX_PARSER_MPLS_OVER_UDP_ENABLED)
		caps->flex_parser_id_mpls_over_udp =
			MLX5_GET(query_hca_cap_out, out,
				 capability.cmd_hca_cap.flex_parser_id_outer_first_mpls_over_udp_label);

	caps->log_header_modify_argument_granularity =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap.log_header_modify_argument_granularity);

	caps->log_header_modify_argument_granularity -=
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap.log_header_modify_argument_granularity_offset);

	caps->log_header_modify_argument_max_alloc =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap.log_header_modify_argument_max_alloc);

	caps->definer_format_sup =
		MLX5_GET64(query_hca_cap_out, out,
			   capability.cmd_hca_cap.match_definer_format_supported);

	caps->vhca_id = MLX5_GET(query_hca_cap_out, out,
				 capability.cmd_hca_cap.vhca_id);

	caps->sq_ts_format = MLX5_GET(query_hca_cap_out, out,
				      capability.cmd_hca_cap.sq_ts_format);

	caps->ipsec_offload = MLX5_GET(query_hca_cap_out, out,
				       capability.cmd_hca_cap.ipsec_offload);

	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_GET_HCA_CAP_OP_MOD_GENERAL_DEVICE_2 | HCA_CAP_OPMOD_GET_CUR);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
	if (ret) {
		mlx5_core_err(mdev, "Failed to query device caps 2\n");
		goto out;
	}

	caps->full_dw_jumbo_support =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.format_select_dw_8_6_ext);

	caps->format_select_gtpu_dw_0 =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.format_select_dw_gtpu_dw_0);

	caps->format_select_gtpu_dw_1 =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.format_select_dw_gtpu_dw_1);

	caps->format_select_gtpu_dw_2 =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.format_select_dw_gtpu_dw_2);

	caps->format_select_gtpu_ext_dw_0 =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.format_select_dw_gtpu_first_ext_dw_0);

	caps->supp_type_gen_wqe =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.generate_wqe_type);

	caps->flow_table_hash_type =
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap_2.flow_table_hash_type);

	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_GET_HCA_CAP_OP_MOD_NIC_FLOW_TABLE | HCA_CAP_OPMOD_GET_CUR);

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
	if (ret) {
		mlx5_core_err(mdev, "Failed to query flow table caps\n");
		goto out;
	}

	caps->nic_ft.max_level =
		MLX5_GET(query_hca_cap_out, out,
			 capability.flow_table_nic_cap.flow_table_properties_nic_receive.max_ft_level);

	caps->nic_ft.reparse =
		MLX5_GET(query_hca_cap_out, out,
			 capability.flow_table_nic_cap.flow_table_properties_nic_receive.reparse);

	caps->nic_ft.ignore_flow_level_rtc_valid =
		MLX5_GET(query_hca_cap_out, out,
			 capability.flow_table_nic_cap.flow_table_properties_nic_receive.ignore_flow_level_rtc_valid);

	caps->flex_parser_ok_bits_supp =
		MLX5_GET(query_hca_cap_out, out,
			 capability.flow_table_nic_cap.flow_table_properties_nic_receive.ft_field_support.geneve_tlv_option_0_exist);

	if (caps->wqe_based_update) {
		MLX5_SET(query_hca_cap_in, in, op_mod,
			 MLX5_GET_HCA_CAP_OP_MOD_WQE_BASED_FLOW_TABLE | HCA_CAP_OPMOD_GET_CUR);

		ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
		if (ret) {
			mlx5_core_err(mdev, "Failed to query WQE based FT caps\n");
			goto out;
		}

		caps->rtc_reparse_mode =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.rtc_reparse_mode);

		caps->ste_format =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.ste_format);

		caps->rtc_index_mode =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.rtc_index_mode);

		caps->rtc_log_depth_max =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.rtc_log_depth_max);

		caps->ste_alloc_log_max =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.ste_alloc_log_max);

		caps->ste_alloc_log_gran =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.ste_alloc_log_granularity);

		caps->trivial_match_definer =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.trivial_match_definer);

		caps->stc_alloc_log_max =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.stc_alloc_log_max);

		caps->stc_alloc_log_gran =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.stc_alloc_log_granularity);

		caps->rtc_hash_split_table =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.rtc_hash_split_table);

		caps->rtc_linear_lookup_table =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.rtc_linear_lookup_table);

		caps->access_index_mode =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.access_index_mode);

		caps->linear_match_definer =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.linear_match_definer_reg_c3);

		caps->rtc_max_hash_def_gen_wqe =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.rtc_max_num_hash_definer_gen_wqe);

		caps->supp_ste_format_gen_wqe =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.ste_format_gen_wqe);

		caps->fdb_tir_stc =
			MLX5_GET(query_hca_cap_out, out,
				 capability.wqe_based_flow_table_cap.fdb_jump_to_tir_stc);
	}

	if (caps->eswitch_manager) {
		MLX5_SET(query_hca_cap_in, in, op_mod,
			 MLX5_GET_HCA_CAP_OP_MOD_ESW_FLOW_TABLE | HCA_CAP_OPMOD_GET_CUR);

		ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
		if (ret) {
			mlx5_core_err(mdev, "Failed to query flow table esw caps\n");
			goto out;
		}

		caps->fdb_ft.max_level =
			MLX5_GET(query_hca_cap_out, out,
				 capability.flow_table_nic_cap.flow_table_properties_nic_receive.max_ft_level);

		caps->fdb_ft.reparse =
			MLX5_GET(query_hca_cap_out, out,
				 capability.flow_table_nic_cap.flow_table_properties_nic_receive.reparse);

		MLX5_SET(query_hca_cap_in, in, op_mod,
			 MLX5_SET_HCA_CAP_OP_MOD_ESW | HCA_CAP_OPMOD_GET_CUR);

		ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
		if (ret) {
			mlx5_core_err(mdev, "Failed to query eswitch capabilities\n");
			goto out;
		}

		if (MLX5_GET(query_hca_cap_out, out,
			     capability.esw_cap.esw_manager_vport_number_valid))
			caps->eswitch_manager_vport_number =
				MLX5_GET(query_hca_cap_out, out,
					 capability.esw_cap.esw_manager_vport_number);

		caps->merged_eswitch = MLX5_GET(query_hca_cap_out, out,
						capability.esw_cap.merged_eswitch);
	}

	ret = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
	if (ret) {
		mlx5_core_err(mdev, "Failed to query device attributes\n");
		goto out;
	}

	snprintf(caps->fw_ver, sizeof(caps->fw_ver), "%d.%d.%d",
		 fw_rev_maj(mdev), fw_rev_min(mdev), fw_rev_sub(mdev));

	caps->is_ecpf = mlx5_core_is_ecpf_esw_manager(mdev);

out:
	kfree(out);
	return ret;
}

int mlx5hws_cmd_query_gvmi(struct mlx5_core_dev *mdev, bool other_function,
			   u16 vport_number, u16 *gvmi)
{
	bool ec_vf_func = other_function ? mlx5_core_is_ec_vf_vport(mdev, vport_number) : false;
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	int out_size;
	void *out;
	int err;

	out_size = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	out = kzalloc(out_size, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, other_function, other_function);
	MLX5_SET(query_hca_cap_in, in, function_id,
		 mlx5_vport_to_func_id(mdev, vport_number, ec_vf_func));
	MLX5_SET(query_hca_cap_in, in, ec_vf_function, ec_vf_func);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1 | HCA_CAP_OPMOD_GET_CUR);

	err = mlx5_cmd_exec_inout(mdev, query_hca_cap, in, out);
	if (err) {
		kfree(out);
		return err;
	}

	*gvmi = MLX5_GET(query_hca_cap_out, out, capability.cmd_hca_cap.vhca_id);

	kfree(out);

	return 0;
}
