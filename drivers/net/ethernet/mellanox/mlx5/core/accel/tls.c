/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/mlx5/device.h>

#include "accel/tls.h"
#include "mlx5_core.h"
#include "fpga/tls.h"

int mlx5_accel_tls_add_tx_flow(struct mlx5_core_dev *mdev, void *flow,
			       struct tls_crypto_info *crypto_info,
			       u32 start_offload_tcp_sn, u32 *p_swid)
{
	return mlx5_fpga_tls_add_tx_flow(mdev, flow, crypto_info,
					 start_offload_tcp_sn, p_swid);
}

void mlx5_accel_tls_del_tx_flow(struct mlx5_core_dev *mdev, u32 swid)
{
	mlx5_fpga_tls_del_tx_flow(mdev, swid, GFP_KERNEL);
}

bool mlx5_accel_is_tls_device(struct mlx5_core_dev *mdev)
{
	return mlx5_fpga_is_tls_device(mdev);
}

u32 mlx5_accel_tls_device_caps(struct mlx5_core_dev *mdev)
{
	return mlx5_fpga_tls_device_caps(mdev);
}

int mlx5_accel_tls_init(struct mlx5_core_dev *mdev)
{
	return mlx5_fpga_tls_init(mdev);
}

void mlx5_accel_tls_cleanup(struct mlx5_core_dev *mdev)
{
	mlx5_fpga_tls_cleanup(mdev);
}
