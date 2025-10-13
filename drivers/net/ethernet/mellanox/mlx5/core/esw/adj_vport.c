// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "fs_core.h"
#include "eswitch.h"

enum {
	MLX5_ADJ_VPORT_DISCONNECT = 0x0,
	MLX5_ADJ_VPORT_CONNECT = 0x1,
};

static int mlx5_esw_adj_vport_modify(struct mlx5_core_dev *dev,
				     u16 vport, bool connect)
{
	u32 in[MLX5_ST_SZ_DW(modify_vport_state_in)] = {};

	MLX5_SET(modify_vport_state_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_VPORT_STATE);
	MLX5_SET(modify_vport_state_in, in, op_mod,
		 MLX5_VPORT_STATE_OP_MOD_ESW_VPORT);
	MLX5_SET(modify_vport_state_in, in, other_vport, 1);
	MLX5_SET(modify_vport_state_in, in, vport_number, vport);
	MLX5_SET(modify_vport_state_in, in, ingress_connect_valid, 1);
	MLX5_SET(modify_vport_state_in, in, egress_connect_valid, 1);
	MLX5_SET(modify_vport_state_in, in, ingress_connect, connect);
	MLX5_SET(modify_vport_state_in, in, egress_connect, connect);

	return mlx5_cmd_exec_in(dev, modify_vport_state, in);
}

static void mlx5_esw_destroy_esw_vport(struct mlx5_core_dev *dev, u16 vport)
{
	u32 in[MLX5_ST_SZ_DW(destroy_esw_vport_in)] = {};

	MLX5_SET(destroy_esw_vport_in, in, opcode,
		 MLX5_CMD_OPCODE_DESTROY_ESW_VPORT);
	MLX5_SET(destroy_esw_vport_in, in, vport_num, vport);

	mlx5_cmd_exec_in(dev, destroy_esw_vport, in);
}

static int mlx5_esw_create_esw_vport(struct mlx5_core_dev *dev, u16 vhca_id,
				     u16 *vport_num)
{
	u32 out[MLX5_ST_SZ_DW(create_esw_vport_out)] = {};
	u32 in[MLX5_ST_SZ_DW(create_esw_vport_in)] = {};
	int err;

	MLX5_SET(create_esw_vport_in, in, opcode,
		 MLX5_CMD_OPCODE_CREATE_ESW_VPORT);
	MLX5_SET(create_esw_vport_in, in, managed_vhca_id, vhca_id);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*vport_num = MLX5_GET(create_esw_vport_out, out, vport_num);

	return err;
}

static int mlx5_esw_adj_vport_create(struct mlx5_eswitch *esw, u16 vhca_id,
				     const void *rid_info_reg)
{
	struct mlx5_vport *vport;
	u16 vport_num;
	int err;

	err = mlx5_esw_create_esw_vport(esw->dev, vhca_id, &vport_num);
	if (err) {
		esw_warn(esw->dev,
			 "Failed to create adjacent vport for vhca_id %d, err %d\n",
			 vhca_id, err);
		return err;
	}

	esw_debug(esw->dev, "Created adjacent vport[%d] %d for vhca_id 0x%x\n",
		  esw->last_vport_idx, vport_num, vhca_id);

	err = mlx5_esw_vport_alloc(esw, esw->last_vport_idx++, vport_num);
	if (err)
		goto destroy_esw_vport;

	xa_set_mark(&esw->vports, vport_num, MLX5_ESW_VPT_VF);
	vport = mlx5_eswitch_get_vport(esw, vport_num);
	vport->adjacent = true;
	vport->vhca_id = vhca_id;

	vport->adj_info.parent_pci_devfn =
		MLX5_GET(function_vhca_rid_info_reg, rid_info_reg,
			 parent_pci_device_function);
	vport->adj_info.function_id =
		MLX5_GET(function_vhca_rid_info_reg, rid_info_reg, function_id);

	mlx5_fs_vport_egress_acl_ns_add(esw->dev->priv.steering, vport->index);
	mlx5_fs_vport_ingress_acl_ns_add(esw->dev->priv.steering, vport->index);
	err = mlx5_esw_offloads_rep_add(esw, vport);
	if (err)
		goto acl_ns_remove;

	mlx5_esw_adj_vport_modify(esw->dev, vport_num, MLX5_ADJ_VPORT_CONNECT);
	return 0;

acl_ns_remove:
	mlx5_fs_vport_ingress_acl_ns_remove(esw->dev->priv.steering,
					    vport->index);
	mlx5_fs_vport_egress_acl_ns_remove(esw->dev->priv.steering,
					   vport->index);
	mlx5_esw_vport_free(esw, vport);
destroy_esw_vport:
	mlx5_esw_destroy_esw_vport(esw->dev, vport_num);
	return err;
}

static void mlx5_esw_adj_vport_destroy(struct mlx5_eswitch *esw,
				       struct mlx5_vport *vport)
{
	u16 vport_num = vport->vport;

	esw_debug(esw->dev, "Destroying adjacent vport %d for vhca_id 0x%x\n",
		  vport_num, vport->vhca_id);
	mlx5_esw_adj_vport_modify(esw->dev, vport_num,
				  MLX5_ADJ_VPORT_DISCONNECT);
	mlx5_esw_offloads_rep_remove(esw, vport);
	mlx5_fs_vport_egress_acl_ns_remove(esw->dev->priv.steering,
					   vport->index);
	mlx5_fs_vport_ingress_acl_ns_remove(esw->dev->priv.steering,
					    vport->index);
	mlx5_esw_vport_free(esw, vport);
	/* Reset the vport index back so new adj vports can use this index.
	 * When vport count can incrementally change, this needs to be modified.
	 */
	esw->last_vport_idx--;
	mlx5_esw_destroy_esw_vport(esw->dev, vport_num);
}

void mlx5_esw_adjacent_vhcas_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_vport *vport;
	unsigned long i;

	if (!MLX5_CAP_GEN_2(esw->dev, delegated_vhca_max))
		return;

	mlx5_esw_for_each_vf_vport(esw, i, vport, U16_MAX) {
		if (!vport->adjacent)
			continue;
		mlx5_esw_adj_vport_destroy(esw, vport);
	}
}

void mlx5_esw_adjacent_vhcas_setup(struct mlx5_eswitch *esw)
{
	u32 delegated_vhca_max = MLX5_CAP_GEN_2(esw->dev, delegated_vhca_max);
	u32 in[MLX5_ST_SZ_DW(query_delegated_vhca_in)] = {};
	int outlen, err, i = 0;
	u8 *out;
	u32 count;

	if (!delegated_vhca_max)
		return;

	outlen = MLX5_ST_SZ_BYTES(query_delegated_vhca_out) +
		 delegated_vhca_max *
		 MLX5_ST_SZ_BYTES(delegated_function_vhca_rid_info);

	esw_debug(esw->dev, "delegated_vhca_max=%d\n", delegated_vhca_max);

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return;

	MLX5_SET(query_delegated_vhca_in, in, opcode,
		 MLX5_CMD_OPCODE_QUERY_DELEGATED_VHCA);

	err = mlx5_cmd_exec(esw->dev, in, sizeof(in), out, outlen);
	if (err) {
		kvfree(out);
		esw_warn(esw->dev, "Failed to query delegated vhca, err %d\n",
			 err);
		return;
	}

	count = MLX5_GET(query_delegated_vhca_out, out, functions_count);
	esw_debug(esw->dev, "Delegated vhca functions count %d\n", count);

	for (i = 0; i < count; i++) {
		const void *rid_info, *rid_info_reg;
		u16 vhca_id;

		rid_info = MLX5_ADDR_OF(query_delegated_vhca_out, out,
					delegated_function_vhca_rid_info[i]);

		rid_info_reg = MLX5_ADDR_OF(delegated_function_vhca_rid_info,
					    rid_info, function_vhca_rid_info);

		vhca_id = MLX5_GET(function_vhca_rid_info_reg, rid_info_reg,
				   vhca_id);
		esw_debug(esw->dev, "Delegating vhca_id 0x%x\n", vhca_id);

		err = mlx5_esw_adj_vport_create(esw, vhca_id, rid_info_reg);
		if (err) {
			esw_warn(esw->dev,
				 "Failed to init adjacent vhca 0x%x, err %d\n",
				 vhca_id, err);
			break;
		}
	}

	kvfree(out);
}
