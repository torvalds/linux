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

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "transobj.h"

int mlx5_create_rq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *rqn)
{
	u32 out[MLX5_ST_SZ_DW(create_rq_out)];
	int err;

	MLX5_SET(create_rq_in, in, opcode, MLX5_CMD_OP_CREATE_RQ);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(dev, in, inlen, out, sizeof(out));
	if (!err)
		*rqn = MLX5_GET(create_rq_out, out, rqn);

	return err;
}

int mlx5_modify_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_rq_out)];

	MLX5_SET(modify_rq_in, in, rqn, rqn);
	MLX5_SET(modify_rq_in, in, opcode, MLX5_CMD_OP_MODIFY_RQ);

	memset(out, 0, sizeof(out));
	return mlx5_cmd_exec_check_status(dev, in, inlen, out, sizeof(out));
}

void mlx5_destroy_rq(struct mlx5_core_dev *dev, u32 rqn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rq_in)];
	u32 out[MLX5_ST_SZ_DW(destroy_rq_out)];

	memset(in, 0, sizeof(in));

	MLX5_SET(destroy_rq_in, in, opcode, MLX5_CMD_OP_DESTROY_RQ);
	MLX5_SET(destroy_rq_in, in, rqn, rqn);

	mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_create_sq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *sqn)
{
	u32 out[MLX5_ST_SZ_DW(create_sq_out)];
	int err;

	MLX5_SET(create_sq_in, in, opcode, MLX5_CMD_OP_CREATE_SQ);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(dev, in, inlen, out, sizeof(out));
	if (!err)
		*sqn = MLX5_GET(create_sq_out, out, sqn);

	return err;
}

int mlx5_modify_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(modify_sq_out)];

	MLX5_SET(modify_sq_in, in, sqn, sqn);
	MLX5_SET(modify_sq_in, in, opcode, MLX5_CMD_OP_MODIFY_SQ);

	memset(out, 0, sizeof(out));
	return mlx5_cmd_exec_check_status(dev, in, inlen, out, sizeof(out));
}

void mlx5_destroy_sq(struct mlx5_core_dev *dev, u32 sqn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_sq_in)];
	u32 out[MLX5_ST_SZ_DW(destroy_sq_out)];

	memset(in, 0, sizeof(in));

	MLX5_SET(destroy_sq_in, in, opcode, MLX5_CMD_OP_DESTROY_SQ);
	MLX5_SET(destroy_sq_in, in, sqn, sqn);

	mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_create_tir(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *tirn)
{
	u32 out[MLX5_ST_SZ_DW(create_tir_out)];
	int err;

	MLX5_SET(create_tir_in, in, opcode, MLX5_CMD_OP_CREATE_TIR);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(dev, in, inlen, out, sizeof(out));
	if (!err)
		*tirn = MLX5_GET(create_tir_out, out, tirn);

	return err;
}

void mlx5_destroy_tir(struct mlx5_core_dev *dev, u32 tirn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tir_out)];
	u32 out[MLX5_ST_SZ_DW(destroy_tir_out)];

	memset(in, 0, sizeof(in));

	MLX5_SET(destroy_tir_in, in, opcode, MLX5_CMD_OP_DESTROY_TIR);
	MLX5_SET(destroy_tir_in, in, tirn, tirn);

	mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_create_tis(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *tisn)
{
	u32 out[MLX5_ST_SZ_DW(create_tis_out)];
	int err;

	MLX5_SET(create_tis_in, in, opcode, MLX5_CMD_OP_CREATE_TIS);

	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec_check_status(dev, in, inlen, out, sizeof(out));
	if (!err)
		*tisn = MLX5_GET(create_tis_out, out, tisn);

	return err;
}

void mlx5_destroy_tis(struct mlx5_core_dev *dev, u32 tisn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tis_out)];
	u32 out[MLX5_ST_SZ_DW(destroy_tis_out)];

	memset(in, 0, sizeof(in));

	MLX5_SET(destroy_tis_in, in, opcode, MLX5_CMD_OP_DESTROY_TIS);
	MLX5_SET(destroy_tis_in, in, tisn, tisn);

	mlx5_cmd_exec_check_status(dev, in, sizeof(in), out, sizeof(out));
}
