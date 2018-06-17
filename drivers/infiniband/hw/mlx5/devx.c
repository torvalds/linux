// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/ib_umem.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include "mlx5_ib.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static struct mlx5_ib_ucontext *devx_ufile2uctx(struct ib_uverbs_file *file)
{
	return to_mucontext(ib_uverbs_get_ucontext(file));
}

int mlx5_ib_devx_create(struct mlx5_ib_dev *dev, struct mlx5_ib_ucontext *context)
{
	u32 in[MLX5_ST_SZ_DW(create_uctx_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u64 general_obj_types;
	void *uctx;
	void *hdr;
	int err;

	uctx = MLX5_ADDR_OF(create_uctx_in, in, uctx);
	hdr = MLX5_ADDR_OF(create_uctx_in, in, hdr);

	general_obj_types = MLX5_CAP_GEN_64(dev->mdev, general_obj_types);
	if (!(general_obj_types & MLX5_GENERAL_OBJ_TYPES_CAP_UCTX) ||
	    !(general_obj_types & MLX5_GENERAL_OBJ_TYPES_CAP_UMEM))
		return -EINVAL;

	if (!capable(CAP_NET_RAW))
		return -EPERM;

	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type, MLX5_OBJ_TYPE_UCTX);

	err = mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	context->devx_uid = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
	return 0;
}

void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev,
			  struct mlx5_ib_ucontext *context)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {0};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_UCTX);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, context->devx_uid);

	mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
}

static bool devx_is_general_cmd(void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_QUERY_HCA_CAP:
	case MLX5_CMD_OP_QUERY_VPORT_STATE:
	case MLX5_CMD_OP_QUERY_ADAPTER:
	case MLX5_CMD_OP_QUERY_ISSI:
	case MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_ROCE_ADDRESS:
	case MLX5_CMD_OP_QUERY_VNIC_ENV:
	case MLX5_CMD_OP_QUERY_VPORT_COUNTER:
	case MLX5_CMD_OP_GET_DROPPED_PACKET_LOG:
	case MLX5_CMD_OP_NOP:
	case MLX5_CMD_OP_QUERY_CONG_STATUS:
	case MLX5_CMD_OP_QUERY_CONG_PARAMS:
	case MLX5_CMD_OP_QUERY_CONG_STATISTICS:
		return true;
	default:
		return false;
	}
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OTHER)(struct ib_device *ib_dev,
				  struct ib_uverbs_file *file,
				  struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *c = devx_ufile2uctx(file);
	struct mlx5_ib_dev *dev = to_mdev(ib_dev);
	void *cmd_in = uverbs_attr_get_alloced_ptr(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_IN);
	int cmd_out_len = uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT);
	void *cmd_out;
	int err;

	if (!c->devx_uid)
		return -EPERM;

	/* Only white list of some general HCA commands are allowed for this method. */
	if (!devx_is_general_cmd(cmd_in))
		return -EINVAL;

	cmd_out = kvzalloc(cmd_out_len, GFP_KERNEL);
	if (!cmd_out)
		return -ENOMEM;

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, c->devx_uid);
	err = mlx5_cmd_exec(dev->mdev, cmd_in,
			    uverbs_attr_get_len(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_IN),
			    cmd_out, cmd_out_len);
	if (err)
		goto other_cmd_free;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT, cmd_out, cmd_out_len);

other_cmd_free:
	kvfree(cmd_out);
	return err;
}

static DECLARE_UVERBS_NAMED_METHOD(MLX5_IB_METHOD_DEVX_OTHER,
	&UVERBS_ATTR_PTR_IN_SZ(MLX5_IB_ATTR_DEVX_OTHER_CMD_IN,
			       UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
			       UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
					UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO |
					UVERBS_ATTR_SPEC_F_ALLOC_AND_COPY)),
	&UVERBS_ATTR_PTR_OUT_SZ(MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT,
				UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
				UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
					 UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO))
);

static DECLARE_UVERBS_GLOBAL_METHODS(MLX5_IB_OBJECT_DEVX,
	&UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OTHER));

static DECLARE_UVERBS_OBJECT_TREE(devx_objects,
	&UVERBS_OBJECT(MLX5_IB_OBJECT_DEVX));
