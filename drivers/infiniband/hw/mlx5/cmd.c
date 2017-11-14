/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#include "cmd.h"

int mlx5_cmd_null_mkey(struct mlx5_core_dev *dev, u32 *null_mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)]   = {};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*null_mkey = MLX5_GET(query_special_contexts_out, out,
				      null_mkey);
	return err;
}

int mlx5_cmd_query_cong_counter(struct mlx5_core_dev *dev,
				bool reset, void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_statistics_in)] = { };

	MLX5_SET(query_cong_statistics_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_STATISTICS);
	MLX5_SET(query_cong_statistics_in, in, clear, reset);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}

int mlx5_cmd_query_cong_params(struct mlx5_core_dev *dev, int cong_point,
			       void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_params_in)] = { };

	MLX5_SET(query_cong_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_PARAMS);
	MLX5_SET(query_cong_params_in, in, cong_protocol, cong_point);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}

int mlx5_cmd_modify_cong_params(struct mlx5_core_dev *dev,
				void *in, int in_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_cong_params_out)] = { };

	return mlx5_cmd_exec(dev, in, in_size, out, sizeof(out));
}
