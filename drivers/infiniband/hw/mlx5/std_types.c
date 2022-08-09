// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/mlx5_user_ioctl_verbs.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/vport.h>
#include "mlx5_ib.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static int UVERBS_HANDLER(MLX5_IB_METHOD_PD_QUERY)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, MLX5_IB_ATTR_QUERY_PD_HANDLE);
	struct mlx5_ib_pd *mpd = to_mpd(pd);

	return uverbs_copy_to(attrs, MLX5_IB_ATTR_QUERY_PD_RESP_PDN,
			      &mpd->pdn, sizeof(mpd->pdn));
}

static int fill_vport_icm_addr(struct mlx5_core_dev *mdev, u16 vport,
			       struct mlx5_ib_uapi_query_port *info)
{
	u32 out[MLX5_ST_SZ_DW(query_esw_vport_context_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_esw_vport_context_in)] = {};
	bool sw_owner_supp;
	u64 icm_rx;
	u64 icm_tx;
	int err;

	sw_owner_supp = MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, sw_owner) ||
			MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, sw_owner_v2);

	if (vport == MLX5_VPORT_UPLINK) {
		icm_rx = MLX5_CAP64_ESW_FLOWTABLE(mdev,
			sw_steering_uplink_icm_address_rx);
		icm_tx = MLX5_CAP64_ESW_FLOWTABLE(mdev,
			sw_steering_uplink_icm_address_tx);
	} else {
		MLX5_SET(query_esw_vport_context_in, in, opcode,
			 MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT);
		MLX5_SET(query_esw_vport_context_in, in, vport_number, vport);
		MLX5_SET(query_esw_vport_context_in, in, other_vport, true);

		err = mlx5_cmd_exec_inout(mdev, query_esw_vport_context, in,
					  out);

		if (err)
			return err;

		icm_rx = MLX5_GET64(
			query_esw_vport_context_out, out,
			esw_vport_context.sw_steering_vport_icm_address_rx);

		icm_tx = MLX5_GET64(
			query_esw_vport_context_out, out,
			esw_vport_context.sw_steering_vport_icm_address_tx);
	}

	if (sw_owner_supp && icm_rx) {
		info->vport_steering_icm_rx = icm_rx;
		info->flags |=
			MLX5_IB_UAPI_QUERY_PORT_VPORT_STEERING_ICM_RX;
	}

	if (sw_owner_supp && icm_tx) {
		info->vport_steering_icm_tx = icm_tx;
		info->flags |=
			MLX5_IB_UAPI_QUERY_PORT_VPORT_STEERING_ICM_TX;
	}

	return 0;
}

static int fill_vport_vhca_id(struct mlx5_core_dev *mdev, u16 vport,
			      struct mlx5_ib_uapi_query_port *info)
{
	size_t out_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	void *out;
	int err;

	out = kzalloc(out_sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, other_function, true);
	MLX5_SET(query_hca_cap_in, in, function_id, vport);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE |
		 HCA_CAP_OPMOD_GET_CUR);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, out_sz);
	if (err)
		goto out;

	info->vport_vhca_id = MLX5_GET(query_hca_cap_out, out,
				       capability.cmd_hca_cap.vhca_id);

	info->flags |= MLX5_IB_UAPI_QUERY_PORT_VPORT_VHCA_ID;
out:
	kfree(out);
	return err;
}

static int fill_switchdev_info(struct mlx5_ib_dev *dev, u32 port_num,
			       struct mlx5_ib_uapi_query_port *info)
{
	struct mlx5_eswitch_rep *rep;
	struct mlx5_core_dev *mdev;
	int err;

	rep = dev->port[port_num - 1].rep;
	if (!rep)
		return -EOPNOTSUPP;

	mdev = mlx5_eswitch_get_core_dev(rep->esw);
	if (!mdev)
		return -EINVAL;

	info->vport = rep->vport;
	info->flags |= MLX5_IB_UAPI_QUERY_PORT_VPORT;

	if (rep->vport != MLX5_VPORT_UPLINK) {
		err = fill_vport_vhca_id(mdev, rep->vport, info);
		if (err)
			return err;
	}

	info->esw_owner_vhca_id = MLX5_CAP_GEN(mdev, vhca_id);
	info->flags |= MLX5_IB_UAPI_QUERY_PORT_ESW_OWNER_VHCA_ID;

	err = fill_vport_icm_addr(mdev, rep->vport, info);
	if (err)
		return err;

	if (mlx5_eswitch_vport_match_metadata_enabled(rep->esw)) {
		info->reg_c0.value = mlx5_eswitch_get_vport_metadata_for_match(
			rep->esw, rep->vport);
		info->reg_c0.mask = mlx5_eswitch_get_vport_metadata_mask();
		info->flags |= MLX5_IB_UAPI_QUERY_PORT_VPORT_REG_C0;
	}

	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_QUERY_PORT)(
	struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_uapi_query_port info = {};
	struct mlx5_ib_ucontext *c;
	struct mlx5_ib_dev *dev;
	u32 port_num;
	int ret;

	if (uverbs_copy_from(&port_num, attrs,
			     MLX5_IB_ATTR_QUERY_PORT_PORT_NUM))
		return -EFAULT;

	c = to_mucontext(ib_uverbs_get_ucontext(attrs));
	if (IS_ERR(c))
		return PTR_ERR(c);
	dev = to_mdev(c->ibucontext.device);

	if (!rdma_is_port_valid(&dev->ib_dev, port_num))
		return -EINVAL;

	if (mlx5_eswitch_mode(dev->mdev) == MLX5_ESWITCH_OFFLOADS) {
		ret = fill_switchdev_info(dev, port_num, &info);
		if (ret)
			return ret;
	}

	return uverbs_copy_to_struct_or_zero(attrs, MLX5_IB_ATTR_QUERY_PORT, &info,
					     sizeof(info));
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_QUERY_PORT,
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_QUERY_PORT_PORT_NUM,
			   UVERBS_ATTR_TYPE(u32), UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(
		MLX5_IB_ATTR_QUERY_PORT,
		UVERBS_ATTR_STRUCT(struct mlx5_ib_uapi_query_port,
				   reg_c0),
		UA_MANDATORY));

ADD_UVERBS_METHODS(mlx5_ib_device,
		   UVERBS_OBJECT_DEVICE,
		   &UVERBS_METHOD(MLX5_IB_METHOD_QUERY_PORT));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_PD_QUERY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_QUERY_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_QUERY_PD_RESP_PDN,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY));

ADD_UVERBS_METHODS(mlx5_ib_pd,
		   UVERBS_OBJECT_PD,
		   &UVERBS_METHOD(MLX5_IB_METHOD_PD_QUERY));

const struct uapi_definition mlx5_ib_std_types_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE(
		UVERBS_OBJECT_PD,
		&mlx5_ib_pd),
	UAPI_DEF_CHAIN_OBJ_TREE(
		UVERBS_OBJECT_DEVICE,
		&mlx5_ib_device),
	{},
};
