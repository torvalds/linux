/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#include <linux/export.h>
#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include "mlx5_core.h"

/* Mutex to hold while enabling or disabling RoCE */
static DEFINE_MUTEX(mlx5_roce_en_lock);

static int _mlx5_query_vport_state(struct mlx5_core_dev *mdev, u8 opmod,
				   u16 vport, u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_vport_state_in)] = {0};

	MLX5_SET(query_vport_state_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_STATE);
	MLX5_SET(query_vport_state_in, in, op_mod, opmod);
	MLX5_SET(query_vport_state_in, in, vport_number, vport);
	if (vport)
		MLX5_SET(query_vport_state_in, in, other_vport, 1);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
}

u8 mlx5_query_vport_state(struct mlx5_core_dev *mdev, u8 opmod, u16 vport)
{
	u32 out[MLX5_ST_SZ_DW(query_vport_state_out)] = {0};

	_mlx5_query_vport_state(mdev, opmod, vport, out, sizeof(out));

	return MLX5_GET(query_vport_state_out, out, state);
}

int mlx5_modify_vport_admin_state(struct mlx5_core_dev *mdev, u8 opmod,
				  u16 vport, u8 other_vport, u8 state)
{
	u32 in[MLX5_ST_SZ_DW(modify_vport_state_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(modify_vport_state_out)] = {0};

	MLX5_SET(modify_vport_state_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_VPORT_STATE);
	MLX5_SET(modify_vport_state_in, in, op_mod, opmod);
	MLX5_SET(modify_vport_state_in, in, vport_number, vport);
	MLX5_SET(modify_vport_state_in, in, other_vport, other_vport);
	MLX5_SET(modify_vport_state_in, in, admin_state, state);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_query_nic_vport_context(struct mlx5_core_dev *mdev, u16 vport,
					u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_nic_vport_context_in)] = {0};

	MLX5_SET(query_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT);
	MLX5_SET(query_nic_vport_context_in, in, vport_number, vport);
	if (vport)
		MLX5_SET(query_nic_vport_context_in, in, other_vport, 1);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
}

static int mlx5_modify_nic_vport_context(struct mlx5_core_dev *mdev, void *in,
					 int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_nic_vport_context_out)] = {0};

	MLX5_SET(modify_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT);
	return mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
}

int mlx5_query_nic_vport_min_inline(struct mlx5_core_dev *mdev,
				    u16 vport, u8 *min_inline)
{
	u32 out[MLX5_ST_SZ_DW(query_nic_vport_context_out)] = {0};
	int err;

	err = mlx5_query_nic_vport_context(mdev, vport, out, sizeof(out));
	if (!err)
		*min_inline = MLX5_GET(query_nic_vport_context_out, out,
				       nic_vport_context.min_wqe_inline_mode);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_min_inline);

void mlx5_query_min_inline(struct mlx5_core_dev *mdev,
			   u8 *min_inline_mode)
{
	switch (MLX5_CAP_ETH(mdev, wqe_inline_mode)) {
	case MLX5_CAP_INLINE_MODE_L2:
		*min_inline_mode = MLX5_INLINE_MODE_L2;
		break;
	case MLX5_CAP_INLINE_MODE_VPORT_CONTEXT:
		mlx5_query_nic_vport_min_inline(mdev, 0, min_inline_mode);
		break;
	case MLX5_CAP_INLINE_MODE_NOT_REQUIRED:
		*min_inline_mode = MLX5_INLINE_MODE_NONE;
		break;
	}
}
EXPORT_SYMBOL_GPL(mlx5_query_min_inline);

int mlx5_modify_nic_vport_min_inline(struct mlx5_core_dev *mdev,
				     u16 vport, u8 min_inline)
{
	u32 in[MLX5_ST_SZ_DW(modify_nic_vport_context_in)] = {0};
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *nic_vport_ctx;

	MLX5_SET(modify_nic_vport_context_in, in,
		 field_select.min_inline, 1);
	MLX5_SET(modify_nic_vport_context_in, in, vport_number, vport);
	MLX5_SET(modify_nic_vport_context_in, in, other_vport, 1);

	nic_vport_ctx = MLX5_ADDR_OF(modify_nic_vport_context_in,
				     in, nic_vport_context);
	MLX5_SET(nic_vport_context, nic_vport_ctx,
		 min_wqe_inline_mode, min_inline);

	return mlx5_modify_nic_vport_context(mdev, in, inlen);
}

int mlx5_query_nic_vport_mac_address(struct mlx5_core_dev *mdev,
				     u16 vport, bool other, u8 *addr)
{
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);
	u32 in[MLX5_ST_SZ_DW(query_nic_vport_context_in)] = {};
	u8 *out_addr;
	u32 *out;
	int err;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	out_addr = MLX5_ADDR_OF(query_nic_vport_context_out, out,
				nic_vport_context.permanent_address);

	MLX5_SET(query_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT);
	MLX5_SET(query_nic_vport_context_in, in, vport_number, vport);
	MLX5_SET(query_nic_vport_context_in, in, other_vport, other);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
	if (!err)
		ether_addr_copy(addr, &out_addr[2]);

	kvfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_mac_address);

int mlx5_query_mac_address(struct mlx5_core_dev *mdev, u8 *addr)
{
	return mlx5_query_nic_vport_mac_address(mdev, 0, false, addr);
}
EXPORT_SYMBOL_GPL(mlx5_query_mac_address);

int mlx5_modify_nic_vport_mac_address(struct mlx5_core_dev *mdev,
				      u16 vport, u8 *addr)
{
	void *in;
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	int err;
	void *nic_vport_ctx;
	u8 *perm_mac;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in,
		 field_select.permanent_address, 1);
	MLX5_SET(modify_nic_vport_context_in, in, vport_number, vport);
	MLX5_SET(modify_nic_vport_context_in, in, other_vport, 1);

	nic_vport_ctx = MLX5_ADDR_OF(modify_nic_vport_context_in,
				     in, nic_vport_context);
	perm_mac = MLX5_ADDR_OF(nic_vport_context, nic_vport_ctx,
				permanent_address);

	ether_addr_copy(&perm_mac[2], addr);

	err = mlx5_modify_nic_vport_context(mdev, in, inlen);

	kvfree(in);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_modify_nic_vport_mac_address);

int mlx5_query_nic_vport_mtu(struct mlx5_core_dev *mdev, u16 *mtu)
{
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);
	u32 *out;
	int err;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_query_nic_vport_context(mdev, 0, out, outlen);
	if (!err)
		*mtu = MLX5_GET(query_nic_vport_context_out, out,
				nic_vport_context.mtu);

	kvfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_mtu);

int mlx5_modify_nic_vport_mtu(struct mlx5_core_dev *mdev, u16 mtu)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, field_select.mtu, 1);
	MLX5_SET(modify_nic_vport_context_in, in, nic_vport_context.mtu, mtu);

	err = mlx5_modify_nic_vport_context(mdev, in, inlen);

	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_modify_nic_vport_mtu);

int mlx5_query_nic_vport_mac_list(struct mlx5_core_dev *dev,
				  u16 vport,
				  enum mlx5_list_type list_type,
				  u8 addr_list[][ETH_ALEN],
				  int *list_size)
{
	u32 in[MLX5_ST_SZ_DW(query_nic_vport_context_in)] = {0};
	void *nic_vport_ctx;
	int max_list_size;
	int req_list_size;
	int out_sz;
	void *out;
	int err;
	int i;

	req_list_size = *list_size;

	max_list_size = list_type == MLX5_NVPRT_LIST_TYPE_UC ?
		1 << MLX5_CAP_GEN(dev, log_max_current_uc_list) :
		1 << MLX5_CAP_GEN(dev, log_max_current_mc_list);

	if (req_list_size > max_list_size) {
		mlx5_core_warn(dev, "Requested list size (%d) > (%d) max_list_size\n",
			       req_list_size, max_list_size);
		req_list_size = max_list_size;
	}

	out_sz = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in) +
			req_list_size * MLX5_ST_SZ_BYTES(mac_address_layout);

	out = kzalloc(out_sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT);
	MLX5_SET(query_nic_vport_context_in, in, allowed_list_type, list_type);
	MLX5_SET(query_nic_vport_context_in, in, vport_number, vport);
	MLX5_SET(query_nic_vport_context_in, in, other_vport, 1);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, out_sz);
	if (err)
		goto out;

	nic_vport_ctx = MLX5_ADDR_OF(query_nic_vport_context_out, out,
				     nic_vport_context);
	req_list_size = MLX5_GET(nic_vport_context, nic_vport_ctx,
				 allowed_list_size);

	*list_size = req_list_size;
	for (i = 0; i < req_list_size; i++) {
		u8 *mac_addr = MLX5_ADDR_OF(nic_vport_context,
					nic_vport_ctx,
					current_uc_mac_address[i]) + 2;
		ether_addr_copy(addr_list[i], mac_addr);
	}
out:
	kfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_mac_list);

int mlx5_modify_nic_vport_mac_list(struct mlx5_core_dev *dev,
				   enum mlx5_list_type list_type,
				   u8 addr_list[][ETH_ALEN],
				   int list_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_nic_vport_context_out)];
	void *nic_vport_ctx;
	int max_list_size;
	int in_sz;
	void *in;
	int err;
	int i;

	max_list_size = list_type == MLX5_NVPRT_LIST_TYPE_UC ?
		 1 << MLX5_CAP_GEN(dev, log_max_current_uc_list) :
		 1 << MLX5_CAP_GEN(dev, log_max_current_mc_list);

	if (list_size > max_list_size)
		return -ENOSPC;

	in_sz = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in) +
		list_size * MLX5_ST_SZ_BYTES(mac_address_layout);

	memset(out, 0, sizeof(out));
	in = kzalloc(in_sz, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT);
	MLX5_SET(modify_nic_vport_context_in, in,
		 field_select.addresses_list, 1);

	nic_vport_ctx = MLX5_ADDR_OF(modify_nic_vport_context_in, in,
				     nic_vport_context);

	MLX5_SET(nic_vport_context, nic_vport_ctx,
		 allowed_list_type, list_type);
	MLX5_SET(nic_vport_context, nic_vport_ctx,
		 allowed_list_size, list_size);

	for (i = 0; i < list_size; i++) {
		u8 *curr_mac = MLX5_ADDR_OF(nic_vport_context,
					    nic_vport_ctx,
					    current_uc_mac_address[i]) + 2;
		ether_addr_copy(curr_mac, addr_list[i]);
	}

	err = mlx5_cmd_exec(dev, in, in_sz, out, sizeof(out));
	kfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_modify_nic_vport_mac_list);

int mlx5_modify_nic_vport_vlans(struct mlx5_core_dev *dev,
				u16 vlans[],
				int list_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_nic_vport_context_out)];
	void *nic_vport_ctx;
	int max_list_size;
	int in_sz;
	void *in;
	int err;
	int i;

	max_list_size = 1 << MLX5_CAP_GEN(dev, log_max_vlan_list);

	if (list_size > max_list_size)
		return -ENOSPC;

	in_sz = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in) +
		list_size * MLX5_ST_SZ_BYTES(vlan_layout);

	memset(out, 0, sizeof(out));
	in = kzalloc(in_sz, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT);
	MLX5_SET(modify_nic_vport_context_in, in,
		 field_select.addresses_list, 1);

	nic_vport_ctx = MLX5_ADDR_OF(modify_nic_vport_context_in, in,
				     nic_vport_context);

	MLX5_SET(nic_vport_context, nic_vport_ctx,
		 allowed_list_type, MLX5_NVPRT_LIST_TYPE_VLAN);
	MLX5_SET(nic_vport_context, nic_vport_ctx,
		 allowed_list_size, list_size);

	for (i = 0; i < list_size; i++) {
		void *vlan_addr = MLX5_ADDR_OF(nic_vport_context,
					       nic_vport_ctx,
					       current_uc_mac_address[i]);
		MLX5_SET(vlan_layout, vlan_addr, vlan, vlans[i]);
	}

	err = mlx5_cmd_exec(dev, in, in_sz, out, sizeof(out));
	kfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_modify_nic_vport_vlans);

int mlx5_query_nic_vport_system_image_guid(struct mlx5_core_dev *mdev,
					   u64 *system_image_guid)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	mlx5_query_nic_vport_context(mdev, 0, out, outlen);

	*system_image_guid = MLX5_GET64(query_nic_vport_context_out, out,
					nic_vport_context.system_image_guid);

	kvfree(out);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_system_image_guid);

int mlx5_query_nic_vport_node_guid(struct mlx5_core_dev *mdev, u64 *node_guid)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	mlx5_query_nic_vport_context(mdev, 0, out, outlen);

	*node_guid = MLX5_GET64(query_nic_vport_context_out, out,
				nic_vport_context.node_guid);

	kvfree(out);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_node_guid);

int mlx5_modify_nic_vport_node_guid(struct mlx5_core_dev *mdev,
				    u16 vport, u64 node_guid)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *nic_vport_context;
	void *in;
	int err;

	if (!vport)
		return -EINVAL;
	if (!MLX5_CAP_GEN(mdev, vport_group_manager))
		return -EACCES;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in,
		 field_select.node_guid, 1);
	MLX5_SET(modify_nic_vport_context_in, in, vport_number, vport);
	MLX5_SET(modify_nic_vport_context_in, in, other_vport, 1);

	nic_vport_context = MLX5_ADDR_OF(modify_nic_vport_context_in,
					 in, nic_vport_context);
	MLX5_SET64(nic_vport_context, nic_vport_context, node_guid, node_guid);

	err = mlx5_modify_nic_vport_context(mdev, in, inlen);

	kvfree(in);

	return err;
}

int mlx5_query_nic_vport_qkey_viol_cntr(struct mlx5_core_dev *mdev,
					u16 *qkey_viol_cntr)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	mlx5_query_nic_vport_context(mdev, 0, out, outlen);

	*qkey_viol_cntr = MLX5_GET(query_nic_vport_context_out, out,
				   nic_vport_context.qkey_violation_counter);

	kvfree(out);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_qkey_viol_cntr);

int mlx5_query_hca_vport_gid(struct mlx5_core_dev *dev, u8 other_vport,
			     u8 port_num, u16  vf_num, u16 gid_index,
			     union ib_gid *gid)
{
	int in_sz = MLX5_ST_SZ_BYTES(query_hca_vport_gid_in);
	int out_sz = MLX5_ST_SZ_BYTES(query_hca_vport_gid_out);
	int is_group_manager;
	void *out = NULL;
	void *in = NULL;
	union ib_gid *tmp;
	int tbsz;
	int nout;
	int err;

	is_group_manager = MLX5_CAP_GEN(dev, vport_group_manager);
	tbsz = mlx5_get_gid_table_len(MLX5_CAP_GEN(dev, gid_table_size));
	mlx5_core_dbg(dev, "vf_num %d, index %d, gid_table_size %d\n",
		      vf_num, gid_index, tbsz);

	if (gid_index > tbsz && gid_index != 0xffff)
		return -EINVAL;

	if (gid_index == 0xffff)
		nout = tbsz;
	else
		nout = 1;

	out_sz += nout * sizeof(*gid);

	in = kzalloc(in_sz, GFP_KERNEL);
	out = kzalloc(out_sz, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(query_hca_vport_gid_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_VPORT_GID);
	if (other_vport) {
		if (is_group_manager) {
			MLX5_SET(query_hca_vport_gid_in, in, vport_number, vf_num);
			MLX5_SET(query_hca_vport_gid_in, in, other_vport, 1);
		} else {
			err = -EPERM;
			goto out;
		}
	}
	MLX5_SET(query_hca_vport_gid_in, in, gid_index, gid_index);

	if (MLX5_CAP_GEN(dev, num_ports) == 2)
		MLX5_SET(query_hca_vport_gid_in, in, port_num, port_num);

	err = mlx5_cmd_exec(dev, in, in_sz, out, out_sz);
	if (err)
		goto out;

	tmp = out + MLX5_ST_SZ_BYTES(query_hca_vport_gid_out);
	gid->global.subnet_prefix = tmp->global.subnet_prefix;
	gid->global.interface_id = tmp->global.interface_id;

out:
	kfree(in);
	kfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_hca_vport_gid);

int mlx5_query_hca_vport_pkey(struct mlx5_core_dev *dev, u8 other_vport,
			      u8 port_num, u16 vf_num, u16 pkey_index,
			      u16 *pkey)
{
	int in_sz = MLX5_ST_SZ_BYTES(query_hca_vport_pkey_in);
	int out_sz = MLX5_ST_SZ_BYTES(query_hca_vport_pkey_out);
	int is_group_manager;
	void *out = NULL;
	void *in = NULL;
	void *pkarr;
	int nout;
	int tbsz;
	int err;
	int i;

	is_group_manager = MLX5_CAP_GEN(dev, vport_group_manager);

	tbsz = mlx5_to_sw_pkey_sz(MLX5_CAP_GEN(dev, pkey_table_size));
	if (pkey_index > tbsz && pkey_index != 0xffff)
		return -EINVAL;

	if (pkey_index == 0xffff)
		nout = tbsz;
	else
		nout = 1;

	out_sz += nout * MLX5_ST_SZ_BYTES(pkey);

	in = kzalloc(in_sz, GFP_KERNEL);
	out = kzalloc(out_sz, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(query_hca_vport_pkey_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_VPORT_PKEY);
	if (other_vport) {
		if (is_group_manager) {
			MLX5_SET(query_hca_vport_pkey_in, in, vport_number, vf_num);
			MLX5_SET(query_hca_vport_pkey_in, in, other_vport, 1);
		} else {
			err = -EPERM;
			goto out;
		}
	}
	MLX5_SET(query_hca_vport_pkey_in, in, pkey_index, pkey_index);

	if (MLX5_CAP_GEN(dev, num_ports) == 2)
		MLX5_SET(query_hca_vport_pkey_in, in, port_num, port_num);

	err = mlx5_cmd_exec(dev, in, in_sz, out, out_sz);
	if (err)
		goto out;

	pkarr = MLX5_ADDR_OF(query_hca_vport_pkey_out, out, pkey);
	for (i = 0; i < nout; i++, pkey++, pkarr += MLX5_ST_SZ_BYTES(pkey))
		*pkey = MLX5_GET_PR(pkey, pkarr, pkey);

out:
	kfree(in);
	kfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_hca_vport_pkey);

int mlx5_query_hca_vport_context(struct mlx5_core_dev *dev,
				 u8 other_vport, u8 port_num,
				 u16 vf_num,
				 struct mlx5_hca_vport_context *rep)
{
	int out_sz = MLX5_ST_SZ_BYTES(query_hca_vport_context_out);
	int in[MLX5_ST_SZ_DW(query_hca_vport_context_in)] = {0};
	int is_group_manager;
	void *out;
	void *ctx;
	int err;

	is_group_manager = MLX5_CAP_GEN(dev, vport_group_manager);

	out = kzalloc(out_sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_hca_vport_context_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_VPORT_CONTEXT);

	if (other_vport) {
		if (is_group_manager) {
			MLX5_SET(query_hca_vport_context_in, in, other_vport, 1);
			MLX5_SET(query_hca_vport_context_in, in, vport_number, vf_num);
		} else {
			err = -EPERM;
			goto ex;
		}
	}

	if (MLX5_CAP_GEN(dev, num_ports) == 2)
		MLX5_SET(query_hca_vport_context_in, in, port_num, port_num);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out,  out_sz);
	if (err)
		goto ex;

	ctx = MLX5_ADDR_OF(query_hca_vport_context_out, out, hca_vport_context);
	rep->field_select = MLX5_GET_PR(hca_vport_context, ctx, field_select);
	rep->sm_virt_aware = MLX5_GET_PR(hca_vport_context, ctx, sm_virt_aware);
	rep->has_smi = MLX5_GET_PR(hca_vport_context, ctx, has_smi);
	rep->has_raw = MLX5_GET_PR(hca_vport_context, ctx, has_raw);
	rep->policy = MLX5_GET_PR(hca_vport_context, ctx, vport_state_policy);
	rep->phys_state = MLX5_GET_PR(hca_vport_context, ctx,
				      port_physical_state);
	rep->vport_state = MLX5_GET_PR(hca_vport_context, ctx, vport_state);
	rep->port_physical_state = MLX5_GET_PR(hca_vport_context, ctx,
					       port_physical_state);
	rep->port_guid = MLX5_GET64_PR(hca_vport_context, ctx, port_guid);
	rep->node_guid = MLX5_GET64_PR(hca_vport_context, ctx, node_guid);
	rep->cap_mask1 = MLX5_GET_PR(hca_vport_context, ctx, cap_mask1);
	rep->cap_mask1_perm = MLX5_GET_PR(hca_vport_context, ctx,
					  cap_mask1_field_select);
	rep->cap_mask2 = MLX5_GET_PR(hca_vport_context, ctx, cap_mask2);
	rep->cap_mask2_perm = MLX5_GET_PR(hca_vport_context, ctx,
					  cap_mask2_field_select);
	rep->lid = MLX5_GET_PR(hca_vport_context, ctx, lid);
	rep->init_type_reply = MLX5_GET_PR(hca_vport_context, ctx,
					   init_type_reply);
	rep->lmc = MLX5_GET_PR(hca_vport_context, ctx, lmc);
	rep->subnet_timeout = MLX5_GET_PR(hca_vport_context, ctx,
					  subnet_timeout);
	rep->sm_lid = MLX5_GET_PR(hca_vport_context, ctx, sm_lid);
	rep->sm_sl = MLX5_GET_PR(hca_vport_context, ctx, sm_sl);
	rep->qkey_violation_counter = MLX5_GET_PR(hca_vport_context, ctx,
						  qkey_violation_counter);
	rep->pkey_violation_counter = MLX5_GET_PR(hca_vport_context, ctx,
						  pkey_violation_counter);
	rep->grh_required = MLX5_GET_PR(hca_vport_context, ctx, grh_required);
	rep->sys_image_guid = MLX5_GET64_PR(hca_vport_context, ctx,
					    system_image_guid);

ex:
	kfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_hca_vport_context);

int mlx5_query_hca_vport_system_image_guid(struct mlx5_core_dev *dev,
					   u64 *sys_image_guid)
{
	struct mlx5_hca_vport_context *rep;
	int err;

	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (!rep)
		return -ENOMEM;

	err = mlx5_query_hca_vport_context(dev, 0, 1, 0, rep);
	if (!err)
		*sys_image_guid = rep->sys_image_guid;

	kfree(rep);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_hca_vport_system_image_guid);

int mlx5_query_hca_vport_node_guid(struct mlx5_core_dev *dev,
				   u64 *node_guid)
{
	struct mlx5_hca_vport_context *rep;
	int err;

	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (!rep)
		return -ENOMEM;

	err = mlx5_query_hca_vport_context(dev, 0, 1, 0, rep);
	if (!err)
		*node_guid = rep->node_guid;

	kfree(rep);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_hca_vport_node_guid);

int mlx5_query_nic_vport_promisc(struct mlx5_core_dev *mdev,
				 u16 vport,
				 int *promisc_uc,
				 int *promisc_mc,
				 int *promisc_all)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_query_nic_vport_context(mdev, vport, out, outlen);
	if (err)
		goto out;

	*promisc_uc = MLX5_GET(query_nic_vport_context_out, out,
			       nic_vport_context.promisc_uc);
	*promisc_mc = MLX5_GET(query_nic_vport_context_out, out,
			       nic_vport_context.promisc_mc);
	*promisc_all = MLX5_GET(query_nic_vport_context_out, out,
				nic_vport_context.promisc_all);

out:
	kfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_vport_promisc);

int mlx5_modify_nic_vport_promisc(struct mlx5_core_dev *mdev,
				  int promisc_uc,
				  int promisc_mc,
				  int promisc_all)
{
	void *in;
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, field_select.promisc, 1);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.promisc_uc, promisc_uc);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.promisc_mc, promisc_mc);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.promisc_all, promisc_all);

	err = mlx5_modify_nic_vport_context(mdev, in, inlen);

	kvfree(in);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_modify_nic_vport_promisc);

enum {
	UC_LOCAL_LB,
	MC_LOCAL_LB
};

int mlx5_nic_vport_update_local_lb(struct mlx5_core_dev *mdev, bool enable)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *in;
	int err;

	if (!MLX5_CAP_GEN(mdev, disable_local_lb_mc) &&
	    !MLX5_CAP_GEN(mdev, disable_local_lb_uc))
		return 0;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.disable_mc_local_lb, !enable);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.disable_uc_local_lb, !enable);

	if (MLX5_CAP_GEN(mdev, disable_local_lb_mc))
		MLX5_SET(modify_nic_vport_context_in, in,
			 field_select.disable_mc_local_lb, 1);

	if (MLX5_CAP_GEN(mdev, disable_local_lb_uc))
		MLX5_SET(modify_nic_vport_context_in, in,
			 field_select.disable_uc_local_lb, 1);

	err = mlx5_modify_nic_vport_context(mdev, in, inlen);

	if (!err)
		mlx5_core_dbg(mdev, "%s local_lb\n",
			      enable ? "enable" : "disable");

	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_nic_vport_update_local_lb);

int mlx5_nic_vport_query_local_lb(struct mlx5_core_dev *mdev, bool *status)
{
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);
	u32 *out;
	int value;
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_query_nic_vport_context(mdev, 0, out, outlen);
	if (err)
		goto out;

	value = MLX5_GET(query_nic_vport_context_out, out,
			 nic_vport_context.disable_mc_local_lb) << MC_LOCAL_LB;

	value |= MLX5_GET(query_nic_vport_context_out, out,
			  nic_vport_context.disable_uc_local_lb) << UC_LOCAL_LB;

	*status = !value;

out:
	kfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_nic_vport_query_local_lb);

enum mlx5_vport_roce_state {
	MLX5_VPORT_ROCE_DISABLED = 0,
	MLX5_VPORT_ROCE_ENABLED  = 1,
};

static int mlx5_nic_vport_update_roce_state(struct mlx5_core_dev *mdev,
					    enum mlx5_vport_roce_state state)
{
	void *in;
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, field_select.roce_en, 1);
	MLX5_SET(modify_nic_vport_context_in, in, nic_vport_context.roce_en,
		 state);

	err = mlx5_modify_nic_vport_context(mdev, in, inlen);

	kvfree(in);

	return err;
}

int mlx5_nic_vport_enable_roce(struct mlx5_core_dev *mdev)
{
	int err = 0;

	mutex_lock(&mlx5_roce_en_lock);
	if (!mdev->roce.roce_en)
		err = mlx5_nic_vport_update_roce_state(mdev, MLX5_VPORT_ROCE_ENABLED);

	if (!err)
		mdev->roce.roce_en++;
	mutex_unlock(&mlx5_roce_en_lock);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_nic_vport_enable_roce);

int mlx5_nic_vport_disable_roce(struct mlx5_core_dev *mdev)
{
	int err = 0;

	mutex_lock(&mlx5_roce_en_lock);
	if (mdev->roce.roce_en) {
		mdev->roce.roce_en--;
		if (mdev->roce.roce_en == 0)
			err = mlx5_nic_vport_update_roce_state(mdev, MLX5_VPORT_ROCE_DISABLED);

		if (err)
			mdev->roce.roce_en++;
	}
	mutex_unlock(&mlx5_roce_en_lock);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_nic_vport_disable_roce);

int mlx5_core_query_vport_counter(struct mlx5_core_dev *dev, u8 other_vport,
				  int vf, u8 port_num, void *out,
				  size_t out_sz)
{
	int	in_sz = MLX5_ST_SZ_BYTES(query_vport_counter_in);
	int	is_group_manager;
	void   *in;
	int	err;

	is_group_manager = MLX5_CAP_GEN(dev, vport_group_manager);
	in = kvzalloc(in_sz, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		return err;
	}

	MLX5_SET(query_vport_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	if (other_vport) {
		if (is_group_manager) {
			MLX5_SET(query_vport_counter_in, in, other_vport, 1);
			MLX5_SET(query_vport_counter_in, in, vport_number, vf + 1);
		} else {
			err = -EPERM;
			goto free;
		}
	}
	if (MLX5_CAP_GEN(dev, num_ports) == 2)
		MLX5_SET(query_vport_counter_in, in, port_num, port_num);

	err = mlx5_cmd_exec(dev, in, in_sz, out,  out_sz);
free:
	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_query_vport_counter);

int mlx5_query_vport_down_stats(struct mlx5_core_dev *mdev, u16 vport,
				u8 other_vport, u64 *rx_discard_vport_down,
				u64 *tx_discard_vport_down)
{
	u32 out[MLX5_ST_SZ_DW(query_vnic_env_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(query_vnic_env_in)] = {0};
	int err;

	MLX5_SET(query_vnic_env_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VNIC_ENV);
	MLX5_SET(query_vnic_env_in, in, op_mod, 0);
	MLX5_SET(query_vnic_env_in, in, vport_number, vport);
	MLX5_SET(query_vnic_env_in, in, other_vport, other_vport);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	*rx_discard_vport_down = MLX5_GET64(query_vnic_env_out, out,
					    vport_env.receive_discard_vport_down);
	*tx_discard_vport_down = MLX5_GET64(query_vnic_env_out, out,
					    vport_env.transmit_discard_vport_down);
	return 0;
}

int mlx5_core_modify_hca_vport_context(struct mlx5_core_dev *dev,
				       u8 other_vport, u8 port_num,
				       int vf,
				       struct mlx5_hca_vport_context *req)
{
	int in_sz = MLX5_ST_SZ_BYTES(modify_hca_vport_context_in);
	u8 out[MLX5_ST_SZ_BYTES(modify_hca_vport_context_out)];
	int is_group_manager;
	void *in;
	int err;
	void *ctx;

	mlx5_core_dbg(dev, "vf %d\n", vf);
	is_group_manager = MLX5_CAP_GEN(dev, vport_group_manager);
	in = kzalloc(in_sz, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	memset(out, 0, sizeof(out));
	MLX5_SET(modify_hca_vport_context_in, in, opcode, MLX5_CMD_OP_MODIFY_HCA_VPORT_CONTEXT);
	if (other_vport) {
		if (is_group_manager) {
			MLX5_SET(modify_hca_vport_context_in, in, other_vport, 1);
			MLX5_SET(modify_hca_vport_context_in, in, vport_number, vf);
		} else {
			err = -EPERM;
			goto ex;
		}
	}

	if (MLX5_CAP_GEN(dev, num_ports) > 1)
		MLX5_SET(modify_hca_vport_context_in, in, port_num, port_num);

	ctx = MLX5_ADDR_OF(modify_hca_vport_context_in, in, hca_vport_context);
	MLX5_SET(hca_vport_context, ctx, field_select, req->field_select);
	MLX5_SET(hca_vport_context, ctx, sm_virt_aware, req->sm_virt_aware);
	MLX5_SET(hca_vport_context, ctx, has_smi, req->has_smi);
	MLX5_SET(hca_vport_context, ctx, has_raw, req->has_raw);
	MLX5_SET(hca_vport_context, ctx, vport_state_policy, req->policy);
	MLX5_SET(hca_vport_context, ctx, port_physical_state, req->phys_state);
	MLX5_SET(hca_vport_context, ctx, vport_state, req->vport_state);
	MLX5_SET64(hca_vport_context, ctx, port_guid, req->port_guid);
	MLX5_SET64(hca_vport_context, ctx, node_guid, req->node_guid);
	MLX5_SET(hca_vport_context, ctx, cap_mask1, req->cap_mask1);
	MLX5_SET(hca_vport_context, ctx, cap_mask1_field_select, req->cap_mask1_perm);
	MLX5_SET(hca_vport_context, ctx, cap_mask2, req->cap_mask2);
	MLX5_SET(hca_vport_context, ctx, cap_mask2_field_select, req->cap_mask2_perm);
	MLX5_SET(hca_vport_context, ctx, lid, req->lid);
	MLX5_SET(hca_vport_context, ctx, init_type_reply, req->init_type_reply);
	MLX5_SET(hca_vport_context, ctx, lmc, req->lmc);
	MLX5_SET(hca_vport_context, ctx, subnet_timeout, req->subnet_timeout);
	MLX5_SET(hca_vport_context, ctx, sm_lid, req->sm_lid);
	MLX5_SET(hca_vport_context, ctx, sm_sl, req->sm_sl);
	MLX5_SET(hca_vport_context, ctx, qkey_violation_counter, req->qkey_violation_counter);
	MLX5_SET(hca_vport_context, ctx, pkey_violation_counter, req->pkey_violation_counter);
	err = mlx5_cmd_exec(dev, in, in_sz, out, sizeof(out));
ex:
	kfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_modify_hca_vport_context);

int mlx5_nic_vport_affiliate_multiport(struct mlx5_core_dev *master_mdev,
				       struct mlx5_core_dev *port_mdev)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	err = mlx5_nic_vport_enable_roce(port_mdev);
	if (err)
		goto free;

	MLX5_SET(modify_nic_vport_context_in, in, field_select.affiliation, 1);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.affiliated_vhca_id,
		 MLX5_CAP_GEN(master_mdev, vhca_id));
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.affiliation_criteria,
		 MLX5_CAP_GEN(port_mdev, affiliate_nic_vport_criteria));

	err = mlx5_modify_nic_vport_context(port_mdev, in, inlen);
	if (err)
		mlx5_nic_vport_disable_roce(port_mdev);

free:
	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_nic_vport_affiliate_multiport);

int mlx5_nic_vport_unaffiliate_multiport(struct mlx5_core_dev *port_mdev)
{
	int inlen = MLX5_ST_SZ_BYTES(modify_nic_vport_context_in);
	void *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_nic_vport_context_in, in, field_select.affiliation, 1);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.affiliated_vhca_id, 0);
	MLX5_SET(modify_nic_vport_context_in, in,
		 nic_vport_context.affiliation_criteria, 0);

	err = mlx5_modify_nic_vport_context(port_mdev, in, inlen);
	if (!err)
		mlx5_nic_vport_disable_roce(port_mdev);

	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_nic_vport_unaffiliate_multiport);

u64 mlx5_query_nic_system_image_guid(struct mlx5_core_dev *mdev)
{
	int port_type_cap = MLX5_CAP_GEN(mdev, port_type);
	u64 tmp = 0;

	if (mdev->sys_image_guid)
		return mdev->sys_image_guid;

	if (port_type_cap == MLX5_CAP_PORT_TYPE_ETH)
		mlx5_query_nic_vport_system_image_guid(mdev, &tmp);
	else
		mlx5_query_hca_vport_system_image_guid(mdev, &tmp);

	mdev->sys_image_guid = tmp;

	return tmp;
}
EXPORT_SYMBOL_GPL(mlx5_query_nic_system_image_guid);
