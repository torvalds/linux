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
#include "mlx5_core.h"

int mlx5_cmd_update_root_ft(struct mlx5_core_dev *dev,
			    struct mlx5_flow_table *ft)
{
	u32 in[MLX5_ST_SZ_DW(set_flow_table_root_in)];
	u32 out[MLX5_ST_SZ_DW(set_flow_table_root_out)];

	memset(in, 0, sizeof(in));

	MLX5_SET(set_flow_table_root_in, in, opcode,
		 MLX5_CMD_OP_SET_FLOW_TABLE_ROOT);
	MLX5_SET(set_flow_table_root_in, in, table_type, ft->type);
	MLX5_SET(set_flow_table_root_in, in, table_id, ft->id);
	if (ft->vport) {
		MLX5_SET(set_flow_table_root_in, in, vport_number, ft->vport);
		MLX5_SET(set_flow_table_root_in, in, other_vport, 1);
	}

	memset(out, 0, sizeof(out));
	return mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					  sizeof(out));
}

int mlx5_cmd_create_flow_table(struct mlx5_core_dev *dev,
			       u16 vport,
			       enum fs_flow_table_type type, unsigned int level,
			       unsigned int log_size, struct mlx5_flow_table
			       *next_ft, unsigned int *table_id)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)];
	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)];
	int err;

	memset(in, 0, sizeof(in));

	MLX5_SET(create_flow_table_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_TABLE);

	if (next_ft) {
		MLX5_SET(create_flow_table_in, in, table_miss_mode, 1);
		MLX5_SET(create_flow_table_in, in, table_miss_id, next_ft->id);
	}
	MLX5_SET(create_flow_table_in, in, table_type, type);
	MLX5_SET(create_flow_table_in, in, level, level);
	MLX5_SET(create_flow_table_in, in, log_size, log_size);
	if (vport) {
		MLX5_SET(create_flow_table_in, in, vport_number, vport);
		MLX5_SET(create_flow_table_in, in, other_vport, 1);
	}

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					 sizeof(out));

	if (!err)
		*table_id = MLX5_GET(create_flow_table_out, out,
				     table_id);
	return err;
}

int mlx5_cmd_destroy_flow_table(struct mlx5_core_dev *dev,
				struct mlx5_flow_table *ft)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)];
	u32 out[MLX5_ST_SZ_DW(destroy_flow_table_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(destroy_flow_table_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
	MLX5_SET(destroy_flow_table_in, in, table_type, ft->type);
	MLX5_SET(destroy_flow_table_in, in, table_id, ft->id);
	if (ft->vport) {
		MLX5_SET(destroy_flow_table_in, in, vport_number, ft->vport);
		MLX5_SET(destroy_flow_table_in, in, other_vport, 1);
	}

	return mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					  sizeof(out));
}

int mlx5_cmd_modify_flow_table(struct mlx5_core_dev *dev,
			       struct mlx5_flow_table *ft,
			       struct mlx5_flow_table *next_ft)
{
	u32 in[MLX5_ST_SZ_DW(modify_flow_table_in)];
	u32 out[MLX5_ST_SZ_DW(modify_flow_table_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(modify_flow_table_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_FLOW_TABLE);
	MLX5_SET(modify_flow_table_in, in, table_type, ft->type);
	MLX5_SET(modify_flow_table_in, in, table_id, ft->id);
	if (ft->vport) {
		MLX5_SET(modify_flow_table_in, in, vport_number, ft->vport);
		MLX5_SET(modify_flow_table_in, in, other_vport, 1);
	}
	MLX5_SET(modify_flow_table_in, in, modify_field_select,
		 MLX5_MODIFY_FLOW_TABLE_MISS_TABLE_ID);
	if (next_ft) {
		MLX5_SET(modify_flow_table_in, in, table_miss_mode, 1);
		MLX5_SET(modify_flow_table_in, in, table_miss_id, next_ft->id);
	} else {
		MLX5_SET(modify_flow_table_in, in, table_miss_mode, 0);
	}

	return mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					  sizeof(out));
}

int mlx5_cmd_create_flow_group(struct mlx5_core_dev *dev,
			       struct mlx5_flow_table *ft,
			       u32 *in,
			       unsigned int *group_id)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)];
	int err;

	memset(out, 0, sizeof(out));

	MLX5_SET(create_flow_group_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET(create_flow_group_in, in, table_type, ft->type);
	MLX5_SET(create_flow_group_in, in, table_id, ft->id);
	if (ft->vport) {
		MLX5_SET(create_flow_group_in, in, vport_number, ft->vport);
		MLX5_SET(create_flow_group_in, in, other_vport, 1);
	}

	err = mlx5_cmd_exec_check_status(dev, in,
					 inlen, out,
					 sizeof(out));
	if (!err)
		*group_id = MLX5_GET(create_flow_group_out, out,
				     group_id);

	return err;
}

int mlx5_cmd_destroy_flow_group(struct mlx5_core_dev *dev,
				struct mlx5_flow_table *ft,
				unsigned int group_id)
{
	u32 out[MLX5_ST_SZ_DW(destroy_flow_group_out)];
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(destroy_flow_group_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET(destroy_flow_group_in, in, table_type, ft->type);
	MLX5_SET(destroy_flow_group_in, in, table_id, ft->id);
	MLX5_SET(destroy_flow_group_in, in, group_id, group_id);
	if (ft->vport) {
		MLX5_SET(destroy_flow_group_in, in, vport_number, ft->vport);
		MLX5_SET(destroy_flow_group_in, in, other_vport, 1);
	}

	return mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					  sizeof(out));
}

static int mlx5_cmd_set_fte(struct mlx5_core_dev *dev,
			    int opmod, int modify_mask,
			    struct mlx5_flow_table *ft,
			    unsigned group_id,
			    struct fs_fte *fte)
{
	unsigned int inlen = MLX5_ST_SZ_BYTES(set_fte_in) +
		fte->dests_size * MLX5_ST_SZ_BYTES(dest_format_struct);
	u32 out[MLX5_ST_SZ_DW(set_fte_out)];
	struct mlx5_flow_rule *dst;
	void *in_flow_context;
	void *in_match_value;
	void *in_dests;
	u32 *in;
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in) {
		mlx5_core_warn(dev, "failed to allocate inbox\n");
		return -ENOMEM;
	}

	MLX5_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	MLX5_SET(set_fte_in, in, op_mod, opmod);
	MLX5_SET(set_fte_in, in, modify_enable_mask, modify_mask);
	MLX5_SET(set_fte_in, in, table_type, ft->type);
	MLX5_SET(set_fte_in, in, table_id,   ft->id);
	MLX5_SET(set_fte_in, in, flow_index, fte->index);
	if (ft->vport) {
		MLX5_SET(set_fte_in, in, vport_number, ft->vport);
		MLX5_SET(set_fte_in, in, other_vport, 1);
	}

	in_flow_context = MLX5_ADDR_OF(set_fte_in, in, flow_context);
	MLX5_SET(flow_context, in_flow_context, group_id, group_id);
	MLX5_SET(flow_context, in_flow_context, flow_tag, fte->flow_tag);
	MLX5_SET(flow_context, in_flow_context, action, fte->action);
	in_match_value = MLX5_ADDR_OF(flow_context, in_flow_context,
				      match_value);
	memcpy(in_match_value, &fte->val, MLX5_ST_SZ_BYTES(fte_match_param));

	in_dests = MLX5_ADDR_OF(flow_context, in_flow_context, destination);
	if (fte->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		int list_size = 0;

		list_for_each_entry(dst, &fte->node.children, node.list) {
			unsigned int id;

			if (dst->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			MLX5_SET(dest_format_struct, in_dests, destination_type,
				 dst->dest_attr.type);
			if (dst->dest_attr.type ==
			    MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE) {
				id = dst->dest_attr.ft->id;
			} else {
				id = dst->dest_attr.tir_num;
			}
			MLX5_SET(dest_format_struct, in_dests, destination_id, id);
			in_dests += MLX5_ST_SZ_BYTES(dest_format_struct);
			list_size++;
		}

		MLX5_SET(flow_context, in_flow_context, destination_list_size,
			 list_size);
	}

	if (fte->action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		int list_size = 0;

		list_for_each_entry(dst, &fte->node.children, node.list) {
			if (dst->dest_attr.type !=
			    MLX5_FLOW_DESTINATION_TYPE_COUNTER)
				continue;

			MLX5_SET(flow_counter_list, in_dests, flow_counter_id,
				 dst->dest_attr.counter->id);
			in_dests += MLX5_ST_SZ_BYTES(dest_format_struct);
			list_size++;
		}

		MLX5_SET(flow_context, in_flow_context, flow_counter_list_size,
			 list_size);
	}

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(dev, in, inlen, out,
					 sizeof(out));
	kvfree(in);

	return err;
}

int mlx5_cmd_create_fte(struct mlx5_core_dev *dev,
			struct mlx5_flow_table *ft,
			unsigned group_id,
			struct fs_fte *fte)
{
	return	mlx5_cmd_set_fte(dev, 0, 0, ft, group_id, fte);
}

int mlx5_cmd_update_fte(struct mlx5_core_dev *dev,
			struct mlx5_flow_table *ft,
			unsigned group_id,
			int modify_mask,
			struct fs_fte *fte)
{
	int opmod;
	int atomic_mod_cap = MLX5_CAP_FLOWTABLE(dev,
						flow_table_properties_nic_receive.
						flow_modify_en);
	if (!atomic_mod_cap)
		return -ENOTSUPP;
	opmod = 1;

	return	mlx5_cmd_set_fte(dev, opmod, modify_mask, ft, group_id, fte);
}

int mlx5_cmd_delete_fte(struct mlx5_core_dev *dev,
			struct mlx5_flow_table *ft,
			unsigned int index)
{
	u32 out[MLX5_ST_SZ_DW(delete_fte_out)];
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)];
	int err;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(delete_fte_in, in, opcode, MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
	MLX5_SET(delete_fte_in, in, table_type, ft->type);
	MLX5_SET(delete_fte_in, in, table_id, ft->id);
	MLX5_SET(delete_fte_in, in, flow_index, index);
	if (ft->vport) {
		MLX5_SET(delete_fte_in, in, vport_number, ft->vport);
		MLX5_SET(delete_fte_in, in, other_vport, 1);
	}

	err =  mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, sizeof(out));

	return err;
}

int mlx5_cmd_fc_alloc(struct mlx5_core_dev *dev, u16 *id)
{
	u32 in[MLX5_ST_SZ_DW(alloc_flow_counter_in)];
	u32 out[MLX5_ST_SZ_DW(alloc_flow_counter_out)];
	int err;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(alloc_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_FLOW_COUNTER);

	err = mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					 sizeof(out));
	if (err)
		return err;

	*id = MLX5_GET(alloc_flow_counter_out, out, flow_counter_id);

	return 0;
}

int mlx5_cmd_fc_free(struct mlx5_core_dev *dev, u16 id)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_flow_counter_in)];
	u32 out[MLX5_ST_SZ_DW(dealloc_flow_counter_out)];

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(dealloc_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_FLOW_COUNTER);
	MLX5_SET(dealloc_flow_counter_in, in, flow_counter_id, id);

	return mlx5_cmd_exec_check_status(dev, in, sizeof(in), out,
					  sizeof(out));
}

int mlx5_cmd_fc_query(struct mlx5_core_dev *dev, u16 id,
		      u64 *packets, u64 *bytes)
{
	u32 out[MLX5_ST_SZ_BYTES(query_flow_counter_out) +
		MLX5_ST_SZ_BYTES(traffic_counter)];
	u32 in[MLX5_ST_SZ_DW(query_flow_counter_in)];
	void *stats;
	int err = 0;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(query_flow_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_FLOW_COUNTER);
	MLX5_SET(query_flow_counter_in, in, op_mod, 0);
	MLX5_SET(query_flow_counter_in, in, flow_counter_id, id);

	err = mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	stats = MLX5_ADDR_OF(query_flow_counter_out, out, flow_statistics);
	*packets = MLX5_GET64(traffic_counter, stats, packets);
	*bytes = MLX5_GET64(traffic_counter, stats, octets);

	return 0;
}
