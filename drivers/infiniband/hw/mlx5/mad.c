/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/cmd.h>
#include <linux/mlx5/vport.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_pma.h>
#include "mlx5_ib.h"

enum {
	MLX5_IB_VENDOR_CLASS1 = 0x9,
	MLX5_IB_VENDOR_CLASS2 = 0xa
};

static bool can_do_mad_ifc(struct mlx5_ib_dev *dev, u8 port_num,
			   struct ib_mad *in_mad)
{
	if (in_mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_SUBN_LID_ROUTED &&
	    in_mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		return true;
	return dev->mdev->port_caps[port_num - 1].has_smi;
}

int mlx5_MAD_IFC(struct mlx5_ib_dev *dev, int ignore_mkey, int ignore_bkey,
		 u8 port, const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		 const void *in_mad, void *response_mad)
{
	u8 op_modifier = 0;

	if (!can_do_mad_ifc(dev, port, (struct ib_mad *)in_mad))
		return -EPERM;

	/* Key check traps can't be generated unless we have in_wc to
	 * tell us where to send the trap.
	 */
	if (ignore_mkey || !in_wc)
		op_modifier |= 0x1;
	if (ignore_bkey || !in_wc)
		op_modifier |= 0x2;

	return mlx5_core_mad_ifc(dev->mdev, in_mad, response_mad, op_modifier, port);
}

static int process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
		       const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		       const struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	u16 slid;
	int err;

	slid = in_wc ? ib_lid_cpu16(in_wc->slid) : be16_to_cpu(IB_LID_PERMISSIVE);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP && slid == 0)
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;

	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	    in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
		if (in_mad->mad_hdr.method   != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_SET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_TRAP_REPRESS)
			return IB_MAD_RESULT_SUCCESS;

		/* Don't process SMInfo queries -- the SMA can't handle them.
		 */
		if (in_mad->mad_hdr.attr_id == IB_SMP_ATTR_SM_INFO)
			return IB_MAD_RESULT_SUCCESS;
	} else if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT ||
		   in_mad->mad_hdr.mgmt_class == MLX5_IB_VENDOR_CLASS1   ||
		   in_mad->mad_hdr.mgmt_class == MLX5_IB_VENDOR_CLASS2   ||
		   in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_CONG_MGMT) {
		if (in_mad->mad_hdr.method  != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method  != IB_MGMT_METHOD_SET)
			return IB_MAD_RESULT_SUCCESS;
	} else {
		return IB_MAD_RESULT_SUCCESS;
	}

	err = mlx5_MAD_IFC(to_mdev(ibdev),
			   mad_flags & IB_MAD_IGNORE_MKEY,
			   mad_flags & IB_MAD_IGNORE_BKEY,
			   port_num, in_wc, in_grh, in_mad, out_mad);
	if (err)
		return IB_MAD_RESULT_FAILURE;

	/* set return bit in status of directed route responses */
	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		out_mad->mad_hdr.status |= cpu_to_be16(1 << 15);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP_REPRESS)
		/* no response for trap repress */
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static void pma_cnt_ext_assign(struct ib_pma_portcounters_ext *pma_cnt_ext,
			       void *out)
{
#define MLX5_SUM_CNT(p, cntr1, cntr2)	\
	(MLX5_GET64(query_vport_counter_out, p, cntr1) + \
	MLX5_GET64(query_vport_counter_out, p, cntr2))

	pma_cnt_ext->port_xmit_data =
		cpu_to_be64(MLX5_SUM_CNT(out, transmitted_ib_unicast.octets,
					 transmitted_ib_multicast.octets) >> 2);
	pma_cnt_ext->port_rcv_data =
		cpu_to_be64(MLX5_SUM_CNT(out, received_ib_unicast.octets,
					 received_ib_multicast.octets) >> 2);
	pma_cnt_ext->port_xmit_packets =
		cpu_to_be64(MLX5_SUM_CNT(out, transmitted_ib_unicast.packets,
					 transmitted_ib_multicast.packets));
	pma_cnt_ext->port_rcv_packets =
		cpu_to_be64(MLX5_SUM_CNT(out, received_ib_unicast.packets,
					 received_ib_multicast.packets));
	pma_cnt_ext->port_unicast_xmit_packets =
		MLX5_GET64_BE(query_vport_counter_out,
			      out, transmitted_ib_unicast.packets);
	pma_cnt_ext->port_unicast_rcv_packets =
		MLX5_GET64_BE(query_vport_counter_out,
			      out, received_ib_unicast.packets);
	pma_cnt_ext->port_multicast_xmit_packets =
		MLX5_GET64_BE(query_vport_counter_out,
			      out, transmitted_ib_multicast.packets);
	pma_cnt_ext->port_multicast_rcv_packets =
		MLX5_GET64_BE(query_vport_counter_out,
			      out, received_ib_multicast.packets);
}

static void pma_cnt_assign(struct ib_pma_portcounters *pma_cnt,
			   void *out)
{
	/* Traffic counters will be reported in
	 * their 64bit form via ib_pma_portcounters_ext by default.
	 */
	void *out_pma = MLX5_ADDR_OF(ppcnt_reg, out,
				     counter_set);

#define MLX5_ASSIGN_PMA_CNTR(counter_var, counter_name)	{		\
	counter_var = MLX5_GET_BE(typeof(counter_var),			\
				  ib_port_cntrs_grp_data_layout,	\
				  out_pma, counter_name);		\
	}

	MLX5_ASSIGN_PMA_CNTR(pma_cnt->symbol_error_counter,
			     symbol_error_counter);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->link_error_recovery_counter,
			     link_error_recovery_counter);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->link_downed_counter,
			     link_downed_counter);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_rcv_errors,
			     port_rcv_errors);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_rcv_remphys_errors,
			     port_rcv_remote_physical_errors);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_rcv_switch_relay_errors,
			     port_rcv_switch_relay_errors);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_xmit_discards,
			     port_xmit_discards);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_xmit_constraint_errors,
			     port_xmit_constraint_errors);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_xmit_wait,
			     port_xmit_wait);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->port_rcv_constraint_errors,
			     port_rcv_constraint_errors);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->link_overrun_errors,
			     link_overrun_errors);
	MLX5_ASSIGN_PMA_CNTR(pma_cnt->vl15_dropped,
			     vl_15_dropped);
}

static int process_pma_cmd(struct mlx5_core_dev *mdev, u8 port_num,
			   const struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	int err;
	void *out_cnt;

	/* Declaring support of extended counters */
	if (in_mad->mad_hdr.attr_id == IB_PMA_CLASS_PORT_INFO) {
		struct ib_class_port_info cpi = {};

		cpi.capability_mask = IB_PMA_CLASS_CAP_EXT_WIDTH;
		memcpy((out_mad->data + 40), &cpi, sizeof(cpi));
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
	}

	if (in_mad->mad_hdr.attr_id == IB_PMA_PORT_COUNTERS_EXT) {
		struct ib_pma_portcounters_ext *pma_cnt_ext =
			(struct ib_pma_portcounters_ext *)(out_mad->data + 40);
		int sz = MLX5_ST_SZ_BYTES(query_vport_counter_out);

		out_cnt = kvzalloc(sz, GFP_KERNEL);
		if (!out_cnt)
			return IB_MAD_RESULT_FAILURE;

		err = mlx5_core_query_vport_counter(mdev, 0, 0,
						    port_num, out_cnt, sz);
		if (!err)
			pma_cnt_ext_assign(pma_cnt_ext, out_cnt);
	} else {
		struct ib_pma_portcounters *pma_cnt =
			(struct ib_pma_portcounters *)(out_mad->data + 40);
		int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

		out_cnt = kvzalloc(sz, GFP_KERNEL);
		if (!out_cnt)
			return IB_MAD_RESULT_FAILURE;

		err = mlx5_core_query_ib_ppcnt(mdev, port_num,
					       out_cnt, sz);
		if (!err)
			pma_cnt_assign(pma_cnt, out_cnt);
		}

	kvfree(out_cnt);
	if (err)
		return IB_MAD_RESULT_FAILURE;

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

int mlx5_ib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
			const struct ib_wc *in_wc, const struct ib_grh *in_grh,
			const struct ib_mad_hdr *in, size_t in_mad_size,
			struct ib_mad_hdr *out, size_t *out_mad_size,
			u16 *out_mad_pkey_index)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	const struct ib_mad *in_mad = (const struct ib_mad *)in;
	struct ib_mad *out_mad = (struct ib_mad *)out;
	struct mlx5_core_dev *mdev;
	u8 mdev_port_num;
	int ret;

	if (WARN_ON_ONCE(in_mad_size != sizeof(*in_mad) ||
			 *out_mad_size != sizeof(*out_mad)))
		return IB_MAD_RESULT_FAILURE;

	memset(out_mad->data, 0, sizeof(out_mad->data));

	mdev = mlx5_ib_get_native_port_mdev(dev, port_num, &mdev_port_num);
	if (!mdev)
		return IB_MAD_RESULT_FAILURE;

	if (MLX5_CAP_GEN(mdev, vport_counters) &&
	    in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT &&
	    in_mad->mad_hdr.method == IB_MGMT_METHOD_GET) {
		ret = process_pma_cmd(mdev, mdev_port_num, in_mad, out_mad);
	} else {
		ret =  process_mad(ibdev, mad_flags, port_num, in_wc, in_grh,
				   in_mad, out_mad);
	}
	mlx5_ib_put_native_port_mdev(dev, port_num);
	return ret;
}

int mlx5_query_ext_port_caps(struct mlx5_ib_dev *dev, u8 port)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	u16 packet_error;

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = MLX5_ATTR_EXTENDED_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	err = mlx5_MAD_IFC(dev, 1, 1, 1, NULL, NULL, in_mad, out_mad);

	packet_error = be16_to_cpu(out_mad->status);

	dev->mdev->port_caps[port - 1].ext_port_cap = (!err && !packet_error) ?
		MLX_EXT_PORT_CAP_FLAG_EXTENDED_PORT_INFO : 0;

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mlx5_query_mad_ifc_smp_attr_node_info(struct ib_device *ibdev,
					  struct ib_smp *out_mad)
{
	struct ib_smp *in_mad = NULL;
	int err = -ENOMEM;

	in_mad = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	if (!in_mad)
		return -ENOMEM;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mlx5_MAD_IFC(to_mdev(ibdev), 1, 1, 1, NULL, NULL, in_mad,
			   out_mad);

	kfree(in_mad);
	return err;
}

int mlx5_query_mad_ifc_system_image_guid(struct ib_device *ibdev,
					 __be64 *sys_image_guid)
{
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!out_mad)
		return -ENOMEM;

	err = mlx5_query_mad_ifc_smp_attr_node_info(ibdev, out_mad);
	if (err)
		goto out;

	memcpy(sys_image_guid, out_mad->data + 4, 8);

out:
	kfree(out_mad);

	return err;
}

int mlx5_query_mad_ifc_max_pkeys(struct ib_device *ibdev,
				 u16 *max_pkeys)
{
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!out_mad)
		return -ENOMEM;

	err = mlx5_query_mad_ifc_smp_attr_node_info(ibdev, out_mad);
	if (err)
		goto out;

	*max_pkeys = be16_to_cpup((__be16 *)(out_mad->data + 28));

out:
	kfree(out_mad);

	return err;
}

int mlx5_query_mad_ifc_vendor_id(struct ib_device *ibdev,
				 u32 *vendor_id)
{
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!out_mad)
		return -ENOMEM;

	err = mlx5_query_mad_ifc_smp_attr_node_info(ibdev, out_mad);
	if (err)
		goto out;

	*vendor_id = be32_to_cpup((__be32 *)(out_mad->data + 36)) & 0xffff;

out:
	kfree(out_mad);

	return err;
}

int mlx5_query_mad_ifc_node_desc(struct mlx5_ib_dev *dev, char *node_desc)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_DESC;

	err = mlx5_MAD_IFC(dev, 1, 1, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(node_desc, out_mad->data, IB_DEVICE_NODE_DESC_MAX);
out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mlx5_query_mad_ifc_node_guid(struct mlx5_ib_dev *dev, __be64 *node_guid)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mlx5_MAD_IFC(dev, 1, 1, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(node_guid, out_mad->data + 12, 8);
out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mlx5_query_mad_ifc_pkey(struct ib_device *ibdev, u8 port, u16 index,
			    u16 *pkey)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PKEY_TABLE;
	in_mad->attr_mod = cpu_to_be32(index / 32);

	err = mlx5_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad,
			   out_mad);
	if (err)
		goto out;

	*pkey = be16_to_cpu(((__be16 *)out_mad->data)[index % 32]);

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mlx5_query_mad_ifc_gids(struct ib_device *ibdev, u8 port, int index,
			    union ib_gid *gid)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	err = mlx5_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad,
			   out_mad);
	if (err)
		goto out;

	memcpy(gid->raw, out_mad->data + 8, 8);

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod = cpu_to_be32(index / 8);

	err = mlx5_MAD_IFC(to_mdev(ibdev), 1, 1, port, NULL, NULL, in_mad,
			   out_mad);
	if (err)
		goto out;

	memcpy(gid->raw + 8, out_mad->data + (index % 8) * 8, 8);

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

int mlx5_query_mad_ifc_port(struct ib_device *ibdev, u8 port,
			    struct ib_port_attr *props)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int ext_active_speed;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	/* props being zeroed by the caller, avoid zeroing it here */

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	err = mlx5_MAD_IFC(dev, 1, 1, port, NULL, NULL, in_mad, out_mad);
	if (err) {
		mlx5_ib_warn(dev, "err %d\n", err);
		goto out;
	}

	props->lid		= be16_to_cpup((__be16 *)(out_mad->data + 16));
	props->lmc		= out_mad->data[34] & 0x7;
	props->sm_lid		= be16_to_cpup((__be16 *)(out_mad->data + 18));
	props->sm_sl		= out_mad->data[36] & 0xf;
	props->state		= out_mad->data[32] & 0xf;
	props->phys_state	= out_mad->data[33] >> 4;
	props->port_cap_flags	= be32_to_cpup((__be32 *)(out_mad->data + 20));
	props->gid_tbl_len	= out_mad->data[50];
	props->max_msg_sz	= 1 << MLX5_CAP_GEN(mdev, log_max_msg);
	props->pkey_tbl_len	= mdev->port_caps[port - 1].pkey_table_len;
	props->bad_pkey_cntr	= be16_to_cpup((__be16 *)(out_mad->data + 46));
	props->qkey_viol_cntr	= be16_to_cpup((__be16 *)(out_mad->data + 48));
	props->active_width	= out_mad->data[31] & 0xf;
	props->active_speed	= out_mad->data[35] >> 4;
	props->max_mtu		= out_mad->data[41] & 0xf;
	props->active_mtu	= out_mad->data[36] >> 4;
	props->subnet_timeout	= out_mad->data[51] & 0x1f;
	props->max_vl_num	= out_mad->data[37] >> 4;
	props->init_type_reply	= out_mad->data[41] >> 4;

	if (props->port_cap_flags & IB_PORT_CAP_MASK2_SUP) {
		props->port_cap_flags2 =
			be16_to_cpup((__be16 *)(out_mad->data + 60));

		if (props->port_cap_flags2 & IB_PORT_LINK_WIDTH_2X_SUP)
			props->active_width = out_mad->data[31] & 0x1f;
	}

	/* Check if extended speeds (EDR/FDR/...) are supported */
	if (props->port_cap_flags & IB_PORT_EXTENDED_SPEEDS_SUP) {
		ext_active_speed = out_mad->data[62] >> 4;

		switch (ext_active_speed) {
		case 1:
			props->active_speed = 16; /* FDR */
			break;
		case 2:
			props->active_speed = 32; /* EDR */
			break;
		case 4:
			if (props->port_cap_flags & IB_PORT_CAP_MASK2_SUP &&
			    props->port_cap_flags2 & IB_PORT_LINK_SPEED_HDR_SUP)
				props->active_speed = IB_SPEED_HDR;
			break;
		}
	}

	/* If reported active speed is QDR, check if is FDR-10 */
	if (props->active_speed == 4) {
		if (mdev->port_caps[port - 1].ext_port_cap &
		    MLX_EXT_PORT_CAP_FLAG_EXTENDED_PORT_INFO) {
			init_query_mad(in_mad);
			in_mad->attr_id = MLX5_ATTR_EXTENDED_PORT_INFO;
			in_mad->attr_mod = cpu_to_be32(port);

			err = mlx5_MAD_IFC(dev, 1, 1, port,
					   NULL, NULL, in_mad, out_mad);
			if (err)
				goto out;

			/* Checking LinkSpeedActive for FDR-10 */
			if (out_mad->data[15] & 0x1)
				props->active_speed = 8;
		}
	}

out:
	kfree(in_mad);
	kfree(out_mad);

	return err;
}
