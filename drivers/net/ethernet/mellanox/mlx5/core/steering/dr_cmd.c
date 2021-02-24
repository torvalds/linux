// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

int mlx5dr_cmd_query_esw_vport_context(struct mlx5_core_dev *mdev,
				       bool other_vport,
				       u16 vport_number,
				       u64 *icm_address_rx,
				       u64 *icm_address_tx)
{
	u32 out[MLX5_ST_SZ_DW(query_esw_vport_context_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_esw_vport_context_in)] = {};
	int err;

	MLX5_SET(query_esw_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT);
	MLX5_SET(query_esw_vport_context_in, in, other_vport, other_vport);
	MLX5_SET(query_esw_vport_context_in, in, vport_number, vport_number);

	err = mlx5_cmd_exec_inout(mdev, query_esw_vport_context, in, out);
	if (err)
		return err;

	*icm_address_rx =
		MLX5_GET64(query_esw_vport_context_out, out,
			   esw_vport_context.sw_steering_vport_icm_address_rx);
	*icm_address_tx =
		MLX5_GET64(query_esw_vport_context_out, out,
			   esw_vport_context.sw_steering_vport_icm_address_tx);
	return 0;
}

int mlx5dr_cmd_query_gvmi(struct mlx5_core_dev *mdev, bool other_vport,
			  u16 vport_number, u16 *gvmi)
{
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	int out_size;
	void *out;
	int err;

	out_size = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	out = kzalloc(out_size, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, other_function, other_vport);
	MLX5_SET(query_hca_cap_in, in, function_id, vport_number);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1 |
		 HCA_CAP_OPMOD_GET_CUR);

	err = mlx5_cmd_exec_inout(mdev, query_hca_cap, in, out);
	if (err) {
		kfree(out);
		return err;
	}

	*gvmi = MLX5_GET(query_hca_cap_out, out, capability.cmd_hca_cap.vhca_id);

	kfree(out);
	return 0;
}

int mlx5dr_cmd_query_esw_caps(struct mlx5_core_dev *mdev,
			      struct mlx5dr_esw_caps *caps)
{
	caps->drop_icm_address_rx =
		MLX5_CAP64_ESW_FLOWTABLE(mdev,
					 sw_steering_fdb_action_drop_icm_address_rx);
	caps->drop_icm_address_tx =
		MLX5_CAP64_ESW_FLOWTABLE(mdev,
					 sw_steering_fdb_action_drop_icm_address_tx);
	caps->uplink_icm_address_rx =
		MLX5_CAP64_ESW_FLOWTABLE(mdev,
					 sw_steering_uplink_icm_address_rx);
	caps->uplink_icm_address_tx =
		MLX5_CAP64_ESW_FLOWTABLE(mdev,
					 sw_steering_uplink_icm_address_tx);
	caps->sw_owner_v2 = MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, sw_owner_v2);
	if (!caps->sw_owner_v2)
		caps->sw_owner = MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, sw_owner);

	return 0;
}

int mlx5dr_cmd_query_device(struct mlx5_core_dev *mdev,
			    struct mlx5dr_cmd_caps *caps)
{
	caps->prio_tag_required	= MLX5_CAP_GEN(mdev, prio_tag_required);
	caps->eswitch_manager	= MLX5_CAP_GEN(mdev, eswitch_manager);
	caps->gvmi		= MLX5_CAP_GEN(mdev, vhca_id);
	caps->flex_protocols	= MLX5_CAP_GEN(mdev, flex_parser_protocols);
	caps->sw_format_ver	= MLX5_CAP_GEN(mdev, steering_format_version);

	if (caps->flex_protocols & MLX5_FLEX_PARSER_ICMP_V4_ENABLED) {
		caps->flex_parser_id_icmp_dw0 = MLX5_CAP_GEN(mdev, flex_parser_id_icmp_dw0);
		caps->flex_parser_id_icmp_dw1 = MLX5_CAP_GEN(mdev, flex_parser_id_icmp_dw1);
	}

	if (caps->flex_protocols & MLX5_FLEX_PARSER_ICMP_V6_ENABLED) {
		caps->flex_parser_id_icmpv6_dw0 =
			MLX5_CAP_GEN(mdev, flex_parser_id_icmpv6_dw0);
		caps->flex_parser_id_icmpv6_dw1 =
			MLX5_CAP_GEN(mdev, flex_parser_id_icmpv6_dw1);
	}

	caps->nic_rx_drop_address =
		MLX5_CAP64_FLOWTABLE(mdev, sw_steering_nic_rx_action_drop_icm_address);
	caps->nic_tx_drop_address =
		MLX5_CAP64_FLOWTABLE(mdev, sw_steering_nic_tx_action_drop_icm_address);
	caps->nic_tx_allow_address =
		MLX5_CAP64_FLOWTABLE(mdev, sw_steering_nic_tx_action_allow_icm_address);

	caps->rx_sw_owner_v2 = MLX5_CAP_FLOWTABLE_NIC_RX(mdev, sw_owner_v2);
	caps->tx_sw_owner_v2 = MLX5_CAP_FLOWTABLE_NIC_TX(mdev, sw_owner_v2);

	if (!caps->rx_sw_owner_v2)
		caps->rx_sw_owner = MLX5_CAP_FLOWTABLE_NIC_RX(mdev, sw_owner);
	if (!caps->tx_sw_owner_v2)
		caps->tx_sw_owner = MLX5_CAP_FLOWTABLE_NIC_TX(mdev, sw_owner);

	caps->max_ft_level = MLX5_CAP_FLOWTABLE_NIC_RX(mdev, max_ft_level);

	caps->log_icm_size = MLX5_CAP_DEV_MEM(mdev, log_steering_sw_icm_size);
	caps->hdr_modify_icm_addr =
		MLX5_CAP64_DEV_MEM(mdev, header_modify_sw_icm_start_address);

	caps->roce_min_src_udp = MLX5_CAP_ROCE(mdev, r_roce_min_src_udp_port);

	return 0;
}

int mlx5dr_cmd_query_flow_table(struct mlx5_core_dev *dev,
				enum fs_flow_table_type type,
				u32 table_id,
				struct mlx5dr_cmd_query_flow_table_details *output)
{
	u32 out[MLX5_ST_SZ_DW(query_flow_table_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_flow_table_in)] = {};
	int err;

	MLX5_SET(query_flow_table_in, in, opcode,
		 MLX5_CMD_OP_QUERY_FLOW_TABLE);

	MLX5_SET(query_flow_table_in, in, table_type, type);
	MLX5_SET(query_flow_table_in, in, table_id, table_id);

	err = mlx5_cmd_exec_inout(dev, query_flow_table, in, out);
	if (err)
		return err;

	output->status = MLX5_GET(query_flow_table_out, out, status);
	output->level = MLX5_GET(query_flow_table_out, out, flow_table_context.level);

	output->sw_owner_icm_root_1 = MLX5_GET64(query_flow_table_out, out,
						 flow_table_context.sw_owner_icm_root_1);
	output->sw_owner_icm_root_0 = MLX5_GET64(query_flow_table_out, out,
						 flow_table_context.sw_owner_icm_root_0);

	return 0;
}

int mlx5dr_cmd_sync_steering(struct mlx5_core_dev *mdev)
{
	u32 in[MLX5_ST_SZ_DW(sync_steering_in)] = {};

	MLX5_SET(sync_steering_in, in, opcode, MLX5_CMD_OP_SYNC_STEERING);

	return mlx5_cmd_exec_in(mdev, sync_steering, in);
}

int mlx5dr_cmd_set_fte_modify_and_vport(struct mlx5_core_dev *mdev,
					u32 table_type,
					u32 table_id,
					u32 group_id,
					u32 modify_header_id,
					u32 vport_id)
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)] = {};
	void *in_flow_context;
	unsigned int inlen;
	void *in_dests;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(set_fte_in) +
		1 * MLX5_ST_SZ_BYTES(dest_format_struct); /* One destination only */

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	MLX5_SET(set_fte_in, in, table_type, table_type);
	MLX5_SET(set_fte_in, in, table_id, table_id);

	in_flow_context = MLX5_ADDR_OF(set_fte_in, in, flow_context);
	MLX5_SET(flow_context, in_flow_context, group_id, group_id);
	MLX5_SET(flow_context, in_flow_context, modify_header_id, modify_header_id);
	MLX5_SET(flow_context, in_flow_context, destination_list_size, 1);
	MLX5_SET(flow_context, in_flow_context, action,
		 MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
		 MLX5_FLOW_CONTEXT_ACTION_MOD_HDR);

	in_dests = MLX5_ADDR_OF(flow_context, in_flow_context, destination);
	MLX5_SET(dest_format_struct, in_dests, destination_type,
		 MLX5_FLOW_DESTINATION_TYPE_VPORT);
	MLX5_SET(dest_format_struct, in_dests, destination_id, vport_id);

	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	kvfree(in);

	return err;
}

int mlx5dr_cmd_del_flow_table_entry(struct mlx5_core_dev *mdev,
				    u32 table_type,
				    u32 table_id)
{
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)] = {};

	MLX5_SET(delete_fte_in, in, opcode, MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
	MLX5_SET(delete_fte_in, in, table_type, table_type);
	MLX5_SET(delete_fte_in, in, table_id, table_id);

	return mlx5_cmd_exec_in(mdev, delete_fte, in);
}

int mlx5dr_cmd_alloc_modify_header(struct mlx5_core_dev *mdev,
				   u32 table_type,
				   u8 num_of_actions,
				   u64 *actions,
				   u32 *modify_header_id)
{
	u32 out[MLX5_ST_SZ_DW(alloc_modify_header_context_out)] = {};
	void *p_actions;
	u32 inlen;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(alloc_modify_header_context_in) +
		 num_of_actions * sizeof(u64);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(alloc_modify_header_context_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT);
	MLX5_SET(alloc_modify_header_context_in, in, table_type, table_type);
	MLX5_SET(alloc_modify_header_context_in, in, num_of_actions, num_of_actions);
	p_actions = MLX5_ADDR_OF(alloc_modify_header_context_in, in, actions);
	memcpy(p_actions, actions, num_of_actions * sizeof(u64));

	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	if (err)
		goto out;

	*modify_header_id = MLX5_GET(alloc_modify_header_context_out, out,
				     modify_header_id);
out:
	kvfree(in);
	return err;
}

int mlx5dr_cmd_dealloc_modify_header(struct mlx5_core_dev *mdev,
				     u32 modify_header_id)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_modify_header_context_in)] = {};

	MLX5_SET(dealloc_modify_header_context_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_MODIFY_HEADER_CONTEXT);
	MLX5_SET(dealloc_modify_header_context_in, in, modify_header_id,
		 modify_header_id);

	return mlx5_cmd_exec_in(mdev, dealloc_modify_header_context, in);
}

int mlx5dr_cmd_create_empty_flow_group(struct mlx5_core_dev *mdev,
				       u32 table_type,
				       u32 table_id,
				       u32 *group_id)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)] = {};
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u32 *in;
	int err;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_flow_group_in, in, opcode, MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET(create_flow_group_in, in, table_type, table_type);
	MLX5_SET(create_flow_group_in, in, table_id, table_id);

	err = mlx5_cmd_exec_inout(mdev, create_flow_group, in, out);
	if (err)
		goto out;

	*group_id = MLX5_GET(create_flow_group_out, out, group_id);

out:
	kfree(in);
	return err;
}

int mlx5dr_cmd_destroy_flow_group(struct mlx5_core_dev *mdev,
				  u32 table_type,
				  u32 table_id,
				  u32 group_id)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)] = {};

	MLX5_SET(destroy_flow_group_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET(destroy_flow_group_in, in, table_type, table_type);
	MLX5_SET(destroy_flow_group_in, in, table_id, table_id);
	MLX5_SET(destroy_flow_group_in, in, group_id, group_id);

	return mlx5_cmd_exec_in(mdev, destroy_flow_group, in);
}

int mlx5dr_cmd_create_flow_table(struct mlx5_core_dev *mdev,
				 struct mlx5dr_cmd_create_flow_table_attr *attr,
				 u64 *fdb_rx_icm_addr,
				 u32 *table_id)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)] = {};
	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)] = {};
	void *ft_mdev;
	int err;

	MLX5_SET(create_flow_table_in, in, opcode, MLX5_CMD_OP_CREATE_FLOW_TABLE);
	MLX5_SET(create_flow_table_in, in, table_type, attr->table_type);

	ft_mdev = MLX5_ADDR_OF(create_flow_table_in, in, flow_table_context);
	MLX5_SET(flow_table_context, ft_mdev, termination_table, attr->term_tbl);
	MLX5_SET(flow_table_context, ft_mdev, sw_owner, attr->sw_owner);
	MLX5_SET(flow_table_context, ft_mdev, level, attr->level);

	if (attr->sw_owner) {
		/* icm_addr_0 used for FDB RX / NIC TX / NIC_RX
		 * icm_addr_1 used for FDB TX
		 */
		if (attr->table_type == MLX5_FLOW_TABLE_TYPE_NIC_RX) {
			MLX5_SET64(flow_table_context, ft_mdev,
				   sw_owner_icm_root_0, attr->icm_addr_rx);
		} else if (attr->table_type == MLX5_FLOW_TABLE_TYPE_NIC_TX) {
			MLX5_SET64(flow_table_context, ft_mdev,
				   sw_owner_icm_root_0, attr->icm_addr_tx);
		} else if (attr->table_type == MLX5_FLOW_TABLE_TYPE_FDB) {
			MLX5_SET64(flow_table_context, ft_mdev,
				   sw_owner_icm_root_0, attr->icm_addr_rx);
			MLX5_SET64(flow_table_context, ft_mdev,
				   sw_owner_icm_root_1, attr->icm_addr_tx);
		}
	}

	MLX5_SET(create_flow_table_in, in, flow_table_context.decap_en,
		 attr->decap_en);
	MLX5_SET(create_flow_table_in, in, flow_table_context.reformat_en,
		 attr->reformat_en);

	err = mlx5_cmd_exec_inout(mdev, create_flow_table, in, out);
	if (err)
		return err;

	*table_id = MLX5_GET(create_flow_table_out, out, table_id);
	if (!attr->sw_owner && attr->table_type == MLX5_FLOW_TABLE_TYPE_FDB &&
	    fdb_rx_icm_addr)
		*fdb_rx_icm_addr =
		(u64)MLX5_GET(create_flow_table_out, out, icm_address_31_0) |
		(u64)MLX5_GET(create_flow_table_out, out, icm_address_39_32) << 32 |
		(u64)MLX5_GET(create_flow_table_out, out, icm_address_63_40) << 40;

	return 0;
}

int mlx5dr_cmd_destroy_flow_table(struct mlx5_core_dev *mdev,
				  u32 table_id,
				  u32 table_type)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)] = {};

	MLX5_SET(destroy_flow_table_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
	MLX5_SET(destroy_flow_table_in, in, table_type, table_type);
	MLX5_SET(destroy_flow_table_in, in, table_id, table_id);

	return mlx5_cmd_exec_in(mdev, destroy_flow_table, in);
}

int mlx5dr_cmd_create_reformat_ctx(struct mlx5_core_dev *mdev,
				   enum mlx5_reformat_ctx_type rt,
				   size_t reformat_size,
				   void *reformat_data,
				   u32 *reformat_id)
{
	u32 out[MLX5_ST_SZ_DW(alloc_packet_reformat_context_out)] = {};
	size_t inlen, cmd_data_sz, cmd_total_sz;
	void *prctx;
	void *pdata;
	void *in;
	int err;

	cmd_total_sz = MLX5_ST_SZ_BYTES(alloc_packet_reformat_context_in);
	cmd_data_sz = MLX5_FLD_SZ_BYTES(alloc_packet_reformat_context_in,
					packet_reformat_context.reformat_data);
	inlen = ALIGN(cmd_total_sz + reformat_size - cmd_data_sz, 4);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(alloc_packet_reformat_context_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT);

	prctx = MLX5_ADDR_OF(alloc_packet_reformat_context_in, in, packet_reformat_context);
	pdata = MLX5_ADDR_OF(packet_reformat_context_in, prctx, reformat_data);

	MLX5_SET(packet_reformat_context_in, prctx, reformat_type, rt);
	MLX5_SET(packet_reformat_context_in, prctx, reformat_data_size, reformat_size);
	memcpy(pdata, reformat_data, reformat_size);

	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	if (err)
		return err;

	*reformat_id = MLX5_GET(alloc_packet_reformat_context_out, out, packet_reformat_id);
	kvfree(in);

	return err;
}

void mlx5dr_cmd_destroy_reformat_ctx(struct mlx5_core_dev *mdev,
				     u32 reformat_id)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_packet_reformat_context_in)] = {};

	MLX5_SET(dealloc_packet_reformat_context_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT);
	MLX5_SET(dealloc_packet_reformat_context_in, in, packet_reformat_id,
		 reformat_id);

	mlx5_cmd_exec_in(mdev, dealloc_packet_reformat_context, in);
}

int mlx5dr_cmd_query_gid(struct mlx5_core_dev *mdev, u8 vhca_port_num,
			 u16 index, struct mlx5dr_cmd_gid_attr *attr)
{
	u32 out[MLX5_ST_SZ_DW(query_roce_address_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_roce_address_in)] = {};
	int err;

	MLX5_SET(query_roce_address_in, in, opcode,
		 MLX5_CMD_OP_QUERY_ROCE_ADDRESS);

	MLX5_SET(query_roce_address_in, in, roce_address_index, index);
	MLX5_SET(query_roce_address_in, in, vhca_port_num, vhca_port_num);

	err = mlx5_cmd_exec_inout(mdev, query_roce_address, in, out);
	if (err)
		return err;

	memcpy(&attr->gid,
	       MLX5_ADDR_OF(query_roce_address_out,
			    out, roce_address.source_l3_address),
	       sizeof(attr->gid));
	memcpy(attr->mac,
	       MLX5_ADDR_OF(query_roce_address_out, out,
			    roce_address.source_mac_47_32),
	       sizeof(attr->mac));

	if (MLX5_GET(query_roce_address_out, out,
		     roce_address.roce_version) == MLX5_ROCE_VERSION_2)
		attr->roce_ver = MLX5_ROCE_VERSION_2;
	else
		attr->roce_ver = MLX5_ROCE_VERSION_1;

	return 0;
}

static int mlx5dr_cmd_set_extended_dest(struct mlx5_core_dev *dev,
					struct mlx5dr_cmd_fte_info *fte,
					bool *extended_dest)
{
	int fw_log_max_fdb_encap_uplink = MLX5_CAP_ESW(dev, log_max_fdb_encap_uplink);
	int num_fwd_destinations = 0;
	int num_encap = 0;
	int i;

	*extended_dest = false;
	if (!(fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return 0;
	for (i = 0; i < fte->dests_size; i++) {
		if (fte->dest_arr[i].type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
			continue;
		if (fte->dest_arr[i].type == MLX5_FLOW_DESTINATION_TYPE_VPORT &&
		    fte->dest_arr[i].vport.flags & MLX5_FLOW_DEST_VPORT_REFORMAT_ID)
			num_encap++;
		num_fwd_destinations++;
	}

	if (num_fwd_destinations > 1 && num_encap > 0)
		*extended_dest = true;

	if (*extended_dest && !fw_log_max_fdb_encap_uplink) {
		mlx5_core_warn(dev, "FW does not support extended destination");
		return -EOPNOTSUPP;
	}
	if (num_encap > (1 << fw_log_max_fdb_encap_uplink)) {
		mlx5_core_warn(dev, "FW does not support more than %d encaps",
			       1 << fw_log_max_fdb_encap_uplink);
		return -EOPNOTSUPP;
	}

	return 0;
}

int mlx5dr_cmd_set_fte(struct mlx5_core_dev *dev,
		       int opmod, int modify_mask,
		       struct mlx5dr_cmd_ft_info *ft,
		       u32 group_id,
		       struct mlx5dr_cmd_fte_info *fte)
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)] = {};
	void *in_flow_context, *vlan;
	bool extended_dest = false;
	void *in_match_value;
	unsigned int inlen;
	int dst_cnt_size;
	void *in_dests;
	u32 *in;
	int err;
	int i;

	if (mlx5dr_cmd_set_extended_dest(dev, fte, &extended_dest))
		return -EOPNOTSUPP;

	if (!extended_dest)
		dst_cnt_size = MLX5_ST_SZ_BYTES(dest_format_struct);
	else
		dst_cnt_size = MLX5_ST_SZ_BYTES(extended_dest_format);

	inlen = MLX5_ST_SZ_BYTES(set_fte_in) + fte->dests_size * dst_cnt_size;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	MLX5_SET(set_fte_in, in, op_mod, opmod);
	MLX5_SET(set_fte_in, in, modify_enable_mask, modify_mask);
	MLX5_SET(set_fte_in, in, table_type, ft->type);
	MLX5_SET(set_fte_in, in, table_id, ft->id);
	MLX5_SET(set_fte_in, in, flow_index, fte->index);
	if (ft->vport) {
		MLX5_SET(set_fte_in, in, vport_number, ft->vport);
		MLX5_SET(set_fte_in, in, other_vport, 1);
	}

	in_flow_context = MLX5_ADDR_OF(set_fte_in, in, flow_context);
	MLX5_SET(flow_context, in_flow_context, group_id, group_id);

	MLX5_SET(flow_context, in_flow_context, flow_tag,
		 fte->flow_context.flow_tag);
	MLX5_SET(flow_context, in_flow_context, flow_source,
		 fte->flow_context.flow_source);

	MLX5_SET(flow_context, in_flow_context, extended_destination,
		 extended_dest);
	if (extended_dest) {
		u32 action;

		action = fte->action.action &
			~MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
		MLX5_SET(flow_context, in_flow_context, action, action);
	} else {
		MLX5_SET(flow_context, in_flow_context, action,
			 fte->action.action);
		if (fte->action.pkt_reformat)
			MLX5_SET(flow_context, in_flow_context, packet_reformat_id,
				 fte->action.pkt_reformat->id);
	}
	if (fte->action.modify_hdr)
		MLX5_SET(flow_context, in_flow_context, modify_header_id,
			 fte->action.modify_hdr->id);

	vlan = MLX5_ADDR_OF(flow_context, in_flow_context, push_vlan);

	MLX5_SET(vlan, vlan, ethtype, fte->action.vlan[0].ethtype);
	MLX5_SET(vlan, vlan, vid, fte->action.vlan[0].vid);
	MLX5_SET(vlan, vlan, prio, fte->action.vlan[0].prio);

	vlan = MLX5_ADDR_OF(flow_context, in_flow_context, push_vlan_2);

	MLX5_SET(vlan, vlan, ethtype, fte->action.vlan[1].ethtype);
	MLX5_SET(vlan, vlan, vid, fte->action.vlan[1].vid);
	MLX5_SET(vlan, vlan, prio, fte->action.vlan[1].prio);

	in_match_value = MLX5_ADDR_OF(flow_context, in_flow_context,
				      match_value);
	memcpy(in_match_value, fte->val, sizeof(u32) * MLX5_ST_SZ_DW_MATCH_PARAM);

	in_dests = MLX5_ADDR_OF(flow_context, in_flow_context, destination);
	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		int list_size = 0;

		for (i = 0; i < fte->dests_size; i++) {
			unsigned int id, type = fte->dest_arr[i].type;

			if (type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			switch (type) {
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM:
				id = fte->dest_arr[i].ft_num;
				type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
				id = fte->dest_arr[i].ft_id;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_VPORT:
				id = fte->dest_arr[i].vport.num;
				MLX5_SET(dest_format_struct, in_dests,
					 destination_eswitch_owner_vhca_id_valid,
					 !!(fte->dest_arr[i].vport.flags &
					    MLX5_FLOW_DEST_VPORT_VHCA_ID));
				MLX5_SET(dest_format_struct, in_dests,
					 destination_eswitch_owner_vhca_id,
					 fte->dest_arr[i].vport.vhca_id);
				if (extended_dest && (fte->dest_arr[i].vport.flags &
						    MLX5_FLOW_DEST_VPORT_REFORMAT_ID)) {
					MLX5_SET(dest_format_struct, in_dests,
						 packet_reformat,
						 !!(fte->dest_arr[i].vport.flags &
						    MLX5_FLOW_DEST_VPORT_REFORMAT_ID));
					MLX5_SET(extended_dest_format, in_dests,
						 packet_reformat_id,
						 fte->dest_arr[i].vport.reformat_id);
				}
				break;
			default:
				id = fte->dest_arr[i].tir_num;
			}

			MLX5_SET(dest_format_struct, in_dests, destination_type,
				 type);
			MLX5_SET(dest_format_struct, in_dests, destination_id, id);
			in_dests += dst_cnt_size;
			list_size++;
		}

		MLX5_SET(flow_context, in_flow_context, destination_list_size,
			 list_size);
	}

	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		int max_list_size = BIT(MLX5_CAP_FLOWTABLE_TYPE(dev,
					log_max_flow_counter,
					ft->type));
		int list_size = 0;

		for (i = 0; i < fte->dests_size; i++) {
			if (fte->dest_arr[i].type !=
			    MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			MLX5_SET(flow_counter_list, in_dests, flow_counter_id,
				 fte->dest_arr[i].counter_id);
			in_dests += dst_cnt_size;
			list_size++;
		}
		if (list_size > max_list_size) {
			err = -EINVAL;
			goto err_out;
		}

		MLX5_SET(flow_context, in_flow_context, flow_counter_list_size,
			 list_size);
	}

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
err_out:
	kvfree(in);
	return err;
}
