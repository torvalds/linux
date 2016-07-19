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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <rdma/ib_verbs.h>
#include "mlx5_core.h"

int mlx5_core_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn)
{
	u32 out[MLX5_ST_SZ_DW(attach_to_mcg_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(attach_to_mcg_in)]   = {0};
	void *gid;

	MLX5_SET(attach_to_mcg_in, in, opcode, MLX5_CMD_OP_ATTACH_TO_MCG);
	MLX5_SET(attach_to_mcg_in, in, qpn, qpn);
	gid = MLX5_ADDR_OF(attach_to_mcg_in, in, multicast_gid);
	memcpy(gid, mgid, sizeof(*mgid));
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_attach_mcg);

int mlx5_core_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn)
{
	u32 out[MLX5_ST_SZ_DW(detach_from_mcg_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(detach_from_mcg_in)]   = {0};
	void *gid;

	MLX5_SET(detach_from_mcg_in, in, opcode, MLX5_CMD_OP_DETACH_FROM_MCG);
	MLX5_SET(detach_from_mcg_in, in, qpn, qpn);
	gid = MLX5_ADDR_OF(detach_from_mcg_in, in, multicast_gid);
	memcpy(gid, mgid, sizeof(*mgid));
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_detach_mcg);
