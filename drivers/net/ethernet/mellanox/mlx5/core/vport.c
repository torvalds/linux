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
#include "vport.h"
#include "mlx5_core.h"

u8 mlx5_query_vport_state(struct mlx5_core_dev *mdev, u8 opmod)
{
	u32 in[MLX5_ST_SZ_DW(query_vport_state_in)];
	u32 out[MLX5_ST_SZ_DW(query_vport_state_out)];
	int err;

	memset(in, 0, sizeof(in));

	MLX5_SET(query_vport_state_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_STATE);
	MLX5_SET(query_vport_state_in, in, op_mod, opmod);

	err = mlx5_cmd_exec_check_status(mdev, in, sizeof(in), out,
					 sizeof(out));
	if (err)
		mlx5_core_warn(mdev, "MLX5_CMD_OP_QUERY_VPORT_STATE failed\n");

	return MLX5_GET(query_vport_state_out, out, state);
}

void mlx5_query_vport_mac_address(struct mlx5_core_dev *mdev, u8 *addr)
{
	u32  in[MLX5_ST_SZ_DW(query_nic_vport_context_in)];
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_nic_vport_context_out);
	u8 *out_addr;

	out = mlx5_vzalloc(outlen);
	if (!out)
		return;

	out_addr = MLX5_ADDR_OF(query_nic_vport_context_out, out,
				nic_vport_context.permanent_address);

	memset(in, 0, sizeof(in));

	MLX5_SET(query_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT);

	memset(out, 0, outlen);
	mlx5_cmd_exec_check_status(mdev, in, sizeof(in), out, outlen);

	ether_addr_copy(addr, &out_addr[2]);

	kvfree(out);
}
