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
