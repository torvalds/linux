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

#include <linux/etherdevice.h>
#include <linux/mlx5/cmd.h>
#include <linux/mlx5/driver.h>

#include "mlx5_core.h"
#include "fpga/cmd.h"

int mlx5_fpga_caps(struct mlx5_core_dev *dev, u32 *caps)
{
	u32 in[MLX5_ST_SZ_DW(fpga_cap)] = {0};

	return mlx5_core_access_reg(dev, in, sizeof(in), caps,
				    MLX5_ST_SZ_BYTES(fpga_cap),
				    MLX5_REG_FPGA_CAP, 0, 0);
}

int mlx5_fpga_query(struct mlx5_core_dev *dev, struct mlx5_fpga_query *query)
{
	u32 in[MLX5_ST_SZ_DW(fpga_ctrl)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_ctrl)];
	int err;

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_FPGA_CTRL, 0, false);
	if (err)
		return err;

	query->status = MLX5_GET(fpga_ctrl, out, status);
	query->admin_image = MLX5_GET(fpga_ctrl, out, flash_select_admin);
	query->oper_image = MLX5_GET(fpga_ctrl, out, flash_select_oper);
	return 0;
}
