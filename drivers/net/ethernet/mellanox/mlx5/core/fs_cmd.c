/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>
#include <linux/mlx5/mlx5_ifc.h>

#include "fs_core.h"
#include "fs_cmd.h"
#include "fs_ft_pool.h"
#include "mlx5_core.h"
#include "eswitch.h"

static int mlx5_cmd_stub_update_root_ft(struct mlx5_flow_root_namespace *ns,
					struct mlx5_flow_table *ft,
					u32 underlay_qpn,
					bool disconnect)
{
	return 0;
}

static int mlx5_cmd_stub_create_flow_table(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft,
					   unsigned int size,
					   struct mlx5_flow_table *next_ft)
{
	ft->max_fte = size ? roundup_pow_of_two(size) : 1;

	return 0;
}

static int mlx5_cmd_stub_destroy_flow_table(struct mlx5_flow_root_namespace *ns,
					    struct mlx5_flow_table *ft)
{
	return 0;
}

static int mlx5_cmd_stub_modify_flow_table(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft,
					   struct mlx5_flow_table *next_ft)
{
	return 0;
}

static int mlx5_cmd_stub_create_flow_group(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_flow_table *ft,
					   u32 *in,
					   struct mlx5_flow_group *fg)
{
	return 0;
}

static int mlx5_cmd_stub_destroy_flow_group(struct mlx5_flow_root_namespace *ns,
					    struct mlx5_flow_table *ft,
					    struct mlx5_flow_group *fg)
{
	return 0;
}

static int mlx5_cmd_stub_create_fte(struct mlx5_flow_root_namespace *ns,
				    struct mlx5_flow_table *ft,
				    struct mlx5_flow_group *group,
				    struct fs_fte *fte)
{
	return 0;
}

static int mlx5_cmd_stub_update_fte(struct mlx5_flow_root_namespace *ns,
				    struct mlx5_flow_table *ft,
				    struct mlx5_flow_group *group,
				    int modify_mask,
				    struct fs_fte *fte)
{
	return -EOPNOTSUPP;
}

static int mlx5_cmd_stub_delete_fte(struct mlx5_flow_root_namespace *ns,
				    struct mlx5_flow_table *ft,
				    struct fs_fte *fte)
{
	return 0;
}

static int mlx5_cmd_stub_packet_reformat_alloc(struct mlx5_flow_root_namespace *ns,
					       struct mlx5_pkt_reformat_params *params,
					       enum mlx5_flow_namespace_type namespace,
					       struct mlx5_pkt_reformat *pkt_reformat)
{
	return 0;
}

static void mlx5_cmd_stub_packet_reformat_dealloc(struct mlx5_flow_root_namespace *ns,
						  struct mlx5_pkt_reformat *pkt_reformat)
{
}

static int mlx5_cmd_stub_modify_header_alloc(struct mlx5_flow_root_namespace *ns,
					     u8 namespace, u8 num_actions,
					     void *modify_actions,
					     struct mlx5_modify_hdr *modify_hdr)
{
	return 0;
}

static void mlx5_cmd_stub_modify_header_dealloc(struct mlx5_flow_root_namespace *ns,
						struct mlx5_modify_hdr *modify_hdr)
{
}

static int mlx5_cmd_stub_set_peer(struct mlx5_flow_root_namespace *ns,
				  struct mlx5_flow_root_namespace *peer_ns)
{
	return 0;
}

static int mlx5_cmd_stub_create_ns(struct mlx5_flow_root_namespace *ns)
{
	return 0;
}

static int mlx5_cmd_stub_destroy_ns(struct mlx5_flow_root_namespace *ns)
{
	return 0;
}

static int mlx5_cmd_set_slave_root_fdb(struct mlx5_core_dev *master,
				       struct mlx5_core_dev *slave,
				       bool ft_id_valid,
				       u32 ft_id)
{
	u32 out[MLX5_ST_SZ_DW(set_flow_table_root_out)] = {};
	u32 in[MLX5_ST_SZ_DW(set_flow_table_root_in)] = {};
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_namespace *ns;

	MLX5_SET(set_flow_table_root_in, in, opcode,
		 MLX5_CMD_OP_SET_FLOW_TABLE_ROOT);
	MLX5_SET(set_flow_table_root_in, in, table_type,
		 FS_FT_FDB);
	if (ft_id_valid) {
		MLX5_SET(set_flow_table_root_in, in,
			 table_eswitch_owner_vhca_id_valid, 1);
		MLX5_SET(set_flow_table_root_in, in,
			 table_eswitch_owner_vhca_id,
			 MLX5_CAP_GEN(master, vhca_id));
		MLX5_SET(set_flow_table_root_in, in, table_id,
			 ft_id);
	} else {
		ns = mlx5_get_flow_namespace(slave,
					     MLX5_FLOW_NAMESPACE_FDB);
		root = find_root(&ns->node);
		MLX5_SET(set_flow_table_root_in, in, table_id,
			 root->root_ft->id);
	}

	return mlx5_cmd_exec(slave, in, sizeof(in), out, sizeof(out));
}

static int
mlx5_cmd_stub_destroy_match_definer(struct mlx5_flow_root_namespace *ns,
				    int definer_id)
{
	return 0;
}

static int
mlx5_cmd_stub_create_match_definer(struct mlx5_flow_root_namespace *ns,
				   u16 format_id, u32 *match_mask)
{
	return 0;
}

static int mlx5_cmd_update_root_ft(struct mlx5_flow_root_namespace *ns,
				   struct mlx5_flow_table *ft, u32 underlay_qpn,
				   bool disconnect)
{
	u32 in[MLX5_ST_SZ_DW(set_flow_table_root_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;
	int err;

	if ((MLX5_CAP_GEN(dev, port_type) == MLX5_CAP_PORT_TYPE_IB) &&
	    underlay_qpn == 0)
		return 0;

	if (ft->type == FS_FT_FDB &&
	    mlx5_lag_is_shared_fdb(dev) &&
	    !mlx5_lag_is_master(dev))
		return 0;

	MLX5_SET(set_flow_table_root_in, in, opcode,
		 MLX5_CMD_OP_SET_FLOW_TABLE_ROOT);
	MLX5_SET(set_flow_table_root_in, in, table_type, ft->type);

	if (disconnect)
		MLX5_SET(set_flow_table_root_in, in, op_mod, 1);
	else
		MLX5_SET(set_flow_table_root_in, in, table_id, ft->id);

	MLX5_SET(set_flow_table_root_in, in, underlay_qpn, underlay_qpn);
	MLX5_SET(set_flow_table_root_in, in, vport_number, ft->vport);
	MLX5_SET(set_flow_table_root_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));

	err = mlx5_cmd_exec_in(dev, set_flow_table_root, in);
	if (!err &&
	    ft->type == FS_FT_FDB &&
	    mlx5_lag_is_shared_fdb(dev) &&
	    mlx5_lag_is_master(dev)) {
		err = mlx5_cmd_set_slave_root_fdb(dev,
						  mlx5_lag_get_peer_mdev(dev),
						  !disconnect, (!disconnect) ?
						  ft->id : 0);
		if (err && !disconnect) {
			MLX5_SET(set_flow_table_root_in, in, op_mod, 0);
			MLX5_SET(set_flow_table_root_in, in, table_id,
				 ns->root_ft->id);
			mlx5_cmd_exec_in(dev, set_flow_table_root, in);
		}
	}

	return err;
}

static int mlx5_cmd_create_flow_table(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      unsigned int size,
				      struct mlx5_flow_table *next_ft)
{
	int en_encap = !!(ft->flags & MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT);
	int en_decap = !!(ft->flags & MLX5_FLOW_TABLE_TUNNEL_EN_DECAP);
	int term = !!(ft->flags & MLX5_FLOW_TABLE_TERMINATION);
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)] = {};
	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;
	int err;

	if (size != POOL_NEXT_SIZE)
		size = roundup_pow_of_two(size);
	size = mlx5_ft_pool_get_avail_sz(dev, ft->type, size);
	if (!size)
		return -ENOSPC;

	MLX5_SET(create_flow_table_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_TABLE);

	MLX5_SET(create_flow_table_in, in, table_type, ft->type);
	MLX5_SET(create_flow_table_in, in, flow_table_context.level, ft->level);
	MLX5_SET(create_flow_table_in, in, flow_table_context.log_size, size ? ilog2(size) : 0);
	MLX5_SET(create_flow_table_in, in, vport_number, ft->vport);
	MLX5_SET(create_flow_table_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));

	MLX5_SET(create_flow_table_in, in, flow_table_context.decap_en,
		 en_decap);
	MLX5_SET(create_flow_table_in, in, flow_table_context.reformat_en,
		 en_encap);
	MLX5_SET(create_flow_table_in, in, flow_table_context.termination_table,
		 term);

	switch (ft->op_mod) {
	case FS_FT_OP_MOD_NORMAL:
		if (next_ft) {
			MLX5_SET(create_flow_table_in, in,
				 flow_table_context.table_miss_action,
				 MLX5_FLOW_TABLE_MISS_ACTION_FWD);
			MLX5_SET(create_flow_table_in, in,
				 flow_table_context.table_miss_id, next_ft->id);
		} else {
			MLX5_SET(create_flow_table_in, in,
				 flow_table_context.table_miss_action,
				 ft->def_miss_action);
		}
		break;

	case FS_FT_OP_MOD_LAG_DEMUX:
		MLX5_SET(create_flow_table_in, in, op_mod, 0x1);
		if (next_ft)
			MLX5_SET(create_flow_table_in, in,
				 flow_table_context.lag_master_next_table_id,
				 next_ft->id);
		break;
	}

	err = mlx5_cmd_exec_inout(dev, create_flow_table, in, out);
	if (!err) {
		ft->id = MLX5_GET(create_flow_table_out, out,
				  table_id);
		ft->max_fte = size;
	} else {
		mlx5_ft_pool_put_sz(ns->dev, size);
	}

	return err;
}

static int mlx5_cmd_destroy_flow_table(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;
	int err;

	MLX5_SET(destroy_flow_table_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
	MLX5_SET(destroy_flow_table_in, in, table_type, ft->type);
	MLX5_SET(destroy_flow_table_in, in, table_id, ft->id);
	MLX5_SET(destroy_flow_table_in, in, vport_number, ft->vport);
	MLX5_SET(destroy_flow_table_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));

	err = mlx5_cmd_exec_in(dev, destroy_flow_table, in);
	if (!err)
		mlx5_ft_pool_put_sz(ns->dev, ft->max_fte);

	return err;
}

static int mlx5_cmd_modify_flow_table(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      struct mlx5_flow_table *next_ft)
{
	u32 in[MLX5_ST_SZ_DW(modify_flow_table_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;

	MLX5_SET(modify_flow_table_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_FLOW_TABLE);
	MLX5_SET(modify_flow_table_in, in, table_type, ft->type);
	MLX5_SET(modify_flow_table_in, in, table_id, ft->id);

	if (ft->op_mod == FS_FT_OP_MOD_LAG_DEMUX) {
		MLX5_SET(modify_flow_table_in, in, modify_field_select,
			 MLX5_MODIFY_FLOW_TABLE_LAG_NEXT_TABLE_ID);
		if (next_ft) {
			MLX5_SET(modify_flow_table_in, in,
				 flow_table_context.lag_master_next_table_id, next_ft->id);
		} else {
			MLX5_SET(modify_flow_table_in, in,
				 flow_table_context.lag_master_next_table_id, 0);
		}
	} else {
		MLX5_SET(modify_flow_table_in, in, vport_number, ft->vport);
		MLX5_SET(modify_flow_table_in, in, other_vport,
			 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));
		MLX5_SET(modify_flow_table_in, in, modify_field_select,
			 MLX5_MODIFY_FLOW_TABLE_MISS_TABLE_ID);
		if (next_ft) {
			MLX5_SET(modify_flow_table_in, in,
				 flow_table_context.table_miss_action,
				 MLX5_FLOW_TABLE_MISS_ACTION_FWD);
			MLX5_SET(modify_flow_table_in, in,
				 flow_table_context.table_miss_id,
				 next_ft->id);
		} else {
			MLX5_SET(modify_flow_table_in, in,
				 flow_table_context.table_miss_action,
				 ft->def_miss_action);
		}
	}

	return mlx5_cmd_exec_in(dev, modify_flow_table, in);
}

static int mlx5_cmd_create_flow_group(struct mlx5_flow_root_namespace *ns,
				      struct mlx5_flow_table *ft,
				      u32 *in,
				      struct mlx5_flow_group *fg)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)] = {};
	struct mlx5_core_dev *dev = ns->dev;
	int err;

	MLX5_SET(create_flow_group_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET(create_flow_group_in, in, table_type, ft->type);
	MLX5_SET(create_flow_group_in, in, table_id, ft->id);
	if (ft->vport) {
		MLX5_SET(create_flow_group_in, in, vport_number, ft->vport);
		MLX5_SET(create_flow_group_in, in, other_vport, 1);
	}

	MLX5_SET(create_flow_group_in, in, vport_number, ft->vport);
	MLX5_SET(create_flow_group_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));
	err = mlx5_cmd_exec_inout(dev, create_flow_group, in, out);
	if (!err)
		fg->id = MLX5_GET(create_flow_group_out, out,
				  group_id);
	return err;
}

static int mlx5_cmd_destroy_flow_group(struct mlx5_flow_root_namespace *ns,
				       struct mlx5_flow_table *ft,
				       struct mlx5_flow_group *fg)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;

	MLX5_SET(destroy_flow_group_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET(destroy_flow_group_in, in, table_type, ft->type);
	MLX5_SET(destroy_flow_group_in, in, table_id, ft->id);
	MLX5_SET(destroy_flow_group_in, in, group_id, fg->id);
	MLX5_SET(destroy_flow_group_in, in, vport_number, ft->vport);
	MLX5_SET(destroy_flow_group_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));
	return mlx5_cmd_exec_in(dev, destroy_flow_group, in);
}

static int mlx5_set_extended_dest(struct mlx5_core_dev *dev,
				  struct fs_fte *fte, bool *extended_dest)
{
	int fw_log_max_fdb_encap_uplink =
		MLX5_CAP_ESW(dev, log_max_fdb_encap_uplink);
	int num_fwd_destinations = 0;
	struct mlx5_flow_rule *dst;
	int num_encap = 0;

	*extended_dest = false;
	if (!(fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return 0;

	list_for_each_entry(dst, &fte->node.children, node.list) {
		if (dst->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
			continue;
		if (dst->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_VPORT &&
		    dst->dest_attr.vport.flags & MLX5_FLOW_DEST_VPORT_REFORMAT_ID)
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
static int mlx5_cmd_set_fte(struct mlx5_core_dev *dev,
			    int opmod, int modify_mask,
			    struct mlx5_flow_table *ft,
			    unsigned group_id,
			    struct fs_fte *fte)
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)] = {0};
	bool extended_dest = false;
	struct mlx5_flow_rule *dst;
	void *in_flow_context, *vlan;
	void *in_match_value;
	unsigned int inlen;
	int dst_cnt_size;
	void *in_dests;
	u32 *in;
	int err;

	if (mlx5_set_extended_dest(dev, fte, &extended_dest))
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
	MLX5_SET(set_fte_in, in, table_id,   ft->id);
	MLX5_SET(set_fte_in, in, flow_index, fte->index);
	MLX5_SET(set_fte_in, in, ignore_flow_level,
		 !!(fte->action.flags & FLOW_ACT_IGNORE_FLOW_LEVEL));

	MLX5_SET(set_fte_in, in, vport_number, ft->vport);
	MLX5_SET(set_fte_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));

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

	MLX5_SET(flow_context, in_flow_context, ipsec_obj_id, fte->action.ipsec_obj_id);

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
	memcpy(in_match_value, &fte->val, sizeof(fte->val));

	in_dests = MLX5_ADDR_OF(flow_context, in_flow_context, destination);
	if (fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		int list_size = 0;

		list_for_each_entry(dst, &fte->node.children, node.list) {
			unsigned int id, type = dst->dest_attr.type;

			if (type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			switch (type) {
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM:
				id = dst->dest_attr.ft_num;
				type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE:
				id = dst->dest_attr.ft->id;
				break;
			case MLX5_FLOW_DESTINATION_TYPE_VPORT:
				id = dst->dest_attr.vport.num;
				MLX5_SET(dest_format_struct, in_dests,
					 destination_eswitch_owner_vhca_id_valid,
					 !!(dst->dest_attr.vport.flags &
					    MLX5_FLOW_DEST_VPORT_VHCA_ID));
				MLX5_SET(dest_format_struct, in_dests,
					 destination_eswitch_owner_vhca_id,
					 dst->dest_attr.vport.vhca_id);
				if (extended_dest &&
				    dst->dest_attr.vport.pkt_reformat) {
					MLX5_SET(dest_format_struct, in_dests,
						 packet_reformat,
						 !!(dst->dest_attr.vport.flags &
						    MLX5_FLOW_DEST_VPORT_REFORMAT_ID));
					MLX5_SET(extended_dest_format, in_dests,
						 packet_reformat_id,
						 dst->dest_attr.vport.pkt_reformat->id);
				}
				break;
			case MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER:
				id = dst->dest_attr.sampler_id;
				break;
			default:
				id = dst->dest_attr.tir_num;
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

		list_for_each_entry(dst, &fte->node.children, node.list) {
			if (dst->dest_attr.type !=
			    MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			MLX5_SET(flow_counter_list, in_dests, flow_counter_id,
				 dst->dest_attr.counter_id);
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

static int mlx5_cmd_create_fte(struct mlx5_flow_root_namespace *ns,
			       struct mlx5_flow_table *ft,
			       struct mlx5_flow_group *group,
			       struct fs_fte *fte)
{
	struct mlx5_core_dev *dev = ns->dev;
	unsigned int group_id = group->id;

	return mlx5_cmd_set_fte(dev, 0, 0, ft, group_id, fte);
}

static int mlx5_cmd_update_fte(struct mlx5_flow_root_namespace *ns,
			       struct mlx5_flow_table *ft,
			       struct mlx5_flow_group *fg,
			       int modify_mask,
			       struct fs_fte *fte)
{
	int opmod;
	struct mlx5_core_dev *dev = ns->dev;
	int atomic_mod_cap = MLX5_CAP_FLOWTABLE(dev,
						flow_table_properties_nic_receive.
						flow_modify_en);
	if (!atomic_mod_cap)
		return -EOPNOTSUPP;
	opmod = 1;

	return	mlx5_cmd_set_fte(dev, opmod, modify_mask, ft, fg->id, fte);
}

static int mlx5_cmd_delete_fte(struct mlx5_flow_root_namespace *ns,
			       struct mlx5_flow_table *ft,
			       struct fs_fte *fte)
{
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;

	MLX5_SET(delete_fte_in, in, opcode, MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
	MLX5_SET(delete_fte_in, in, table_type, ft->type);
	MLX5_SET(delete_fte_in, in, table_id, ft->id);
	MLX5_SET(delete_fte_in, in, flow_index, fte->index);
	MLX5_SET(delete_fte_in, in, vport_number, ft->vport);
	MLX5_SET(delete_fte_in, in, other_vport,
		 !!(ft->flags & MLX5_FLOW_TABLE_OTHER_VPORT));

	return mlx5_cmd_exec_in(dev, delete_fte, in);
}

int mlx5_cmd_fc_bulk_alloc(struct mlx5_core_dev *dev,
			   enum mlx5_fc_bulk_alloc_bitmask alloc_bitmask,
			   u32 *id)
{
	u32 out[MLX5_ST_SZ_DW(alloc_flow_counter_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_flow_counter_in)] = {};
	int err;

	MLX5_SET(alloc_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_FLOW_COUNTER);
	MLX5_SET(alloc_flow_counter_in, in, flow_counter_bulk, alloc_bitmask);

	err = mlx5_cmd_exec_inout(dev, alloc_flow_counter, in, out);
	if (!err)
		*id = MLX5_GET(alloc_flow_counter_out, out, flow_counter_id);
	return err;
}

int mlx5_cmd_fc_alloc(struct mlx5_core_dev *dev, u32 *id)
{
	return mlx5_cmd_fc_bulk_alloc(dev, 0, id);
}

int mlx5_cmd_fc_free(struct mlx5_core_dev *dev, u32 id)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_flow_counter_in)] = {};

	MLX5_SET(dealloc_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_FLOW_COUNTER);
	MLX5_SET(dealloc_flow_counter_in, in, flow_counter_id, id);
	return mlx5_cmd_exec_in(dev, dealloc_flow_counter, in);
}

int mlx5_cmd_fc_query(struct mlx5_core_dev *dev, u32 id,
		      u64 *packets, u64 *bytes)
{
	u32 out[MLX5_ST_SZ_BYTES(query_flow_counter_out) +
		MLX5_ST_SZ_BYTES(traffic_counter)] = {};
	u32 in[MLX5_ST_SZ_DW(query_flow_counter_in)] = {};
	void *stats;
	int err = 0;

	MLX5_SET(query_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_FLOW_COUNTER);
	MLX5_SET(query_flow_counter_in, in, op_mod, 0);
	MLX5_SET(query_flow_counter_in, in, flow_counter_id, id);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	stats = MLX5_ADDR_OF(query_flow_counter_out, out, flow_statistics);
	*packets = MLX5_GET64(traffic_counter, stats, packets);
	*bytes = MLX5_GET64(traffic_counter, stats, octets);
	return 0;
}

int mlx5_cmd_fc_get_bulk_query_out_len(int bulk_len)
{
	return MLX5_ST_SZ_BYTES(query_flow_counter_out) +
		MLX5_ST_SZ_BYTES(traffic_counter) * bulk_len;
}

int mlx5_cmd_fc_bulk_query(struct mlx5_core_dev *dev, u32 base_id, int bulk_len,
			   u32 *out)
{
	int outlen = mlx5_cmd_fc_get_bulk_query_out_len(bulk_len);
	u32 in[MLX5_ST_SZ_DW(query_flow_counter_in)] = {};

	MLX5_SET(query_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_FLOW_COUNTER);
	MLX5_SET(query_flow_counter_in, in, flow_counter_id, base_id);
	MLX5_SET(query_flow_counter_in, in, num_of_counters, bulk_len);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

static int mlx5_cmd_packet_reformat_alloc(struct mlx5_flow_root_namespace *ns,
					  struct mlx5_pkt_reformat_params *params,
					  enum mlx5_flow_namespace_type namespace,
					  struct mlx5_pkt_reformat *pkt_reformat)
{
	u32 out[MLX5_ST_SZ_DW(alloc_packet_reformat_context_out)] = {};
	struct mlx5_core_dev *dev = ns->dev;
	void *packet_reformat_context_in;
	int max_encap_size;
	void *reformat;
	int inlen;
	int err;
	u32 *in;

	if (namespace == MLX5_FLOW_NAMESPACE_FDB)
		max_encap_size = MLX5_CAP_ESW(dev, max_encap_header_size);
	else
		max_encap_size = MLX5_CAP_FLOWTABLE(dev, max_encap_header_size);

	if (params->size > max_encap_size) {
		mlx5_core_warn(dev, "encap size %zd too big, max supported is %d\n",
			       params->size, max_encap_size);
		return -EINVAL;
	}

	in = kzalloc(MLX5_ST_SZ_BYTES(alloc_packet_reformat_context_in) +
		     params->size, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	packet_reformat_context_in = MLX5_ADDR_OF(alloc_packet_reformat_context_in,
						  in, packet_reformat_context);
	reformat = MLX5_ADDR_OF(packet_reformat_context_in,
				packet_reformat_context_in,
				reformat_data);
	inlen = reformat - (void *)in + params->size;

	MLX5_SET(alloc_packet_reformat_context_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT);
	MLX5_SET(packet_reformat_context_in, packet_reformat_context_in,
		 reformat_data_size, params->size);
	MLX5_SET(packet_reformat_context_in, packet_reformat_context_in,
		 reformat_type, params->type);
	MLX5_SET(packet_reformat_context_in, packet_reformat_context_in,
		 reformat_param_0, params->param_0);
	MLX5_SET(packet_reformat_context_in, packet_reformat_context_in,
		 reformat_param_1, params->param_1);
	if (params->data && params->size)
		memcpy(reformat, params->data, params->size);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));

	pkt_reformat->id = MLX5_GET(alloc_packet_reformat_context_out,
				    out, packet_reformat_id);
	kfree(in);
	return err;
}

static void mlx5_cmd_packet_reformat_dealloc(struct mlx5_flow_root_namespace *ns,
					     struct mlx5_pkt_reformat *pkt_reformat)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_packet_reformat_context_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;

	MLX5_SET(dealloc_packet_reformat_context_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT);
	MLX5_SET(dealloc_packet_reformat_context_in, in, packet_reformat_id,
		 pkt_reformat->id);

	mlx5_cmd_exec_in(dev, dealloc_packet_reformat_context, in);
}

static int mlx5_cmd_modify_header_alloc(struct mlx5_flow_root_namespace *ns,
					u8 namespace, u8 num_actions,
					void *modify_actions,
					struct mlx5_modify_hdr *modify_hdr)
{
	u32 out[MLX5_ST_SZ_DW(alloc_modify_header_context_out)] = {};
	int max_actions, actions_size, inlen, err;
	struct mlx5_core_dev *dev = ns->dev;
	void *actions_in;
	u8 table_type;
	u32 *in;

	switch (namespace) {
	case MLX5_FLOW_NAMESPACE_FDB:
		max_actions = MLX5_CAP_ESW_FLOWTABLE_FDB(dev, max_modify_header_actions);
		table_type = FS_FT_FDB;
		break;
	case MLX5_FLOW_NAMESPACE_KERNEL:
	case MLX5_FLOW_NAMESPACE_BYPASS:
		max_actions = MLX5_CAP_FLOWTABLE_NIC_RX(dev, max_modify_header_actions);
		table_type = FS_FT_NIC_RX;
		break;
	case MLX5_FLOW_NAMESPACE_EGRESS:
#ifdef CONFIG_MLX5_IPSEC
	case MLX5_FLOW_NAMESPACE_EGRESS_KERNEL:
#endif
		max_actions = MLX5_CAP_FLOWTABLE_NIC_TX(dev, max_modify_header_actions);
		table_type = FS_FT_NIC_TX;
		break;
	case MLX5_FLOW_NAMESPACE_ESW_INGRESS:
		max_actions = MLX5_CAP_ESW_INGRESS_ACL(dev, max_modify_header_actions);
		table_type = FS_FT_ESW_INGRESS_ACL;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_TX:
		max_actions = MLX5_CAP_FLOWTABLE_RDMA_TX(dev, max_modify_header_actions);
		table_type = FS_FT_RDMA_TX;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (num_actions > max_actions) {
		mlx5_core_warn(dev, "too many modify header actions %d, max supported %d\n",
			       num_actions, max_actions);
		return -EOPNOTSUPP;
	}

	actions_size = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto) * num_actions;
	inlen = MLX5_ST_SZ_BYTES(alloc_modify_header_context_in) + actions_size;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(alloc_modify_header_context_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT);
	MLX5_SET(alloc_modify_header_context_in, in, table_type, table_type);
	MLX5_SET(alloc_modify_header_context_in, in, num_of_actions, num_actions);

	actions_in = MLX5_ADDR_OF(alloc_modify_header_context_in, in, actions);
	memcpy(actions_in, modify_actions, actions_size);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));

	modify_hdr->id = MLX5_GET(alloc_modify_header_context_out, out, modify_header_id);
	kfree(in);
	return err;
}

static void mlx5_cmd_modify_header_dealloc(struct mlx5_flow_root_namespace *ns,
					   struct mlx5_modify_hdr *modify_hdr)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_modify_header_context_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;

	MLX5_SET(dealloc_modify_header_context_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_MODIFY_HEADER_CONTEXT);
	MLX5_SET(dealloc_modify_header_context_in, in, modify_header_id,
		 modify_hdr->id);

	mlx5_cmd_exec_in(dev, dealloc_modify_header_context, in);
}

static int mlx5_cmd_destroy_match_definer(struct mlx5_flow_root_namespace *ns,
					  int definer_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_MATCH_DEFINER);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, definer_id);

	return mlx5_cmd_exec(ns->dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_create_match_definer(struct mlx5_flow_root_namespace *ns,
					 u16 format_id, u32 *match_mask)
{
	u32 out[MLX5_ST_SZ_DW(create_match_definer_out)] = {};
	u32 in[MLX5_ST_SZ_DW(create_match_definer_in)] = {};
	struct mlx5_core_dev *dev = ns->dev;
	void *ptr;
	int err;

	MLX5_SET(create_match_definer_in, in, general_obj_in_cmd_hdr.opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(create_match_definer_in, in, general_obj_in_cmd_hdr.obj_type,
		 MLX5_OBJ_TYPE_MATCH_DEFINER);

	ptr = MLX5_ADDR_OF(create_match_definer_in, in, obj_context);
	MLX5_SET(match_definer, ptr, format_id, format_id);

	ptr = MLX5_ADDR_OF(match_definer, ptr, match_mask);
	memcpy(ptr, match_mask, MLX5_FLD_SZ_BYTES(match_definer, match_mask));

	err = mlx5_cmd_exec_inout(dev, create_match_definer, in, out);
	return err ? err : MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
}

static const struct mlx5_flow_cmds mlx5_flow_cmds = {
	.create_flow_table = mlx5_cmd_create_flow_table,
	.destroy_flow_table = mlx5_cmd_destroy_flow_table,
	.modify_flow_table = mlx5_cmd_modify_flow_table,
	.create_flow_group = mlx5_cmd_create_flow_group,
	.destroy_flow_group = mlx5_cmd_destroy_flow_group,
	.create_fte = mlx5_cmd_create_fte,
	.update_fte = mlx5_cmd_update_fte,
	.delete_fte = mlx5_cmd_delete_fte,
	.update_root_ft = mlx5_cmd_update_root_ft,
	.packet_reformat_alloc = mlx5_cmd_packet_reformat_alloc,
	.packet_reformat_dealloc = mlx5_cmd_packet_reformat_dealloc,
	.modify_header_alloc = mlx5_cmd_modify_header_alloc,
	.modify_header_dealloc = mlx5_cmd_modify_header_dealloc,
	.create_match_definer = mlx5_cmd_create_match_definer,
	.destroy_match_definer = mlx5_cmd_destroy_match_definer,
	.set_peer = mlx5_cmd_stub_set_peer,
	.create_ns = mlx5_cmd_stub_create_ns,
	.destroy_ns = mlx5_cmd_stub_destroy_ns,
};

static const struct mlx5_flow_cmds mlx5_flow_cmd_stubs = {
	.create_flow_table = mlx5_cmd_stub_create_flow_table,
	.destroy_flow_table = mlx5_cmd_stub_destroy_flow_table,
	.modify_flow_table = mlx5_cmd_stub_modify_flow_table,
	.create_flow_group = mlx5_cmd_stub_create_flow_group,
	.destroy_flow_group = mlx5_cmd_stub_destroy_flow_group,
	.create_fte = mlx5_cmd_stub_create_fte,
	.update_fte = mlx5_cmd_stub_update_fte,
	.delete_fte = mlx5_cmd_stub_delete_fte,
	.update_root_ft = mlx5_cmd_stub_update_root_ft,
	.packet_reformat_alloc = mlx5_cmd_stub_packet_reformat_alloc,
	.packet_reformat_dealloc = mlx5_cmd_stub_packet_reformat_dealloc,
	.modify_header_alloc = mlx5_cmd_stub_modify_header_alloc,
	.modify_header_dealloc = mlx5_cmd_stub_modify_header_dealloc,
	.create_match_definer = mlx5_cmd_stub_create_match_definer,
	.destroy_match_definer = mlx5_cmd_stub_destroy_match_definer,
	.set_peer = mlx5_cmd_stub_set_peer,
	.create_ns = mlx5_cmd_stub_create_ns,
	.destroy_ns = mlx5_cmd_stub_destroy_ns,
};

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_fw_cmds(void)
{
	return &mlx5_flow_cmds;
}

static const struct mlx5_flow_cmds *mlx5_fs_cmd_get_stub_cmds(void)
{
	return &mlx5_flow_cmd_stubs;
}

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_default(enum fs_flow_table_type type)
{
	switch (type) {
	case FS_FT_NIC_RX:
	case FS_FT_ESW_EGRESS_ACL:
	case FS_FT_ESW_INGRESS_ACL:
	case FS_FT_FDB:
	case FS_FT_SNIFFER_RX:
	case FS_FT_SNIFFER_TX:
	case FS_FT_NIC_TX:
	case FS_FT_RDMA_RX:
	case FS_FT_RDMA_TX:
	case FS_FT_PORT_SEL:
		return mlx5_fs_cmd_get_fw_cmds();
	default:
		return mlx5_fs_cmd_get_stub_cmds();
	}
}
