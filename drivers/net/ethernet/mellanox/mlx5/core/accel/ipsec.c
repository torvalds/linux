/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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

#ifdef CONFIG_MLX5_FPGA_IPSEC

#include <linux/mlx5/device.h>

#include "accel/ipsec.h"
#include "mlx5_core.h"
#include "fpga/ipsec.h"

u32 mlx5_accel_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	return mlx5_fpga_ipsec_device_caps(mdev);
}
EXPORT_SYMBOL_GPL(mlx5_accel_ipsec_device_caps);

unsigned int mlx5_accel_ipsec_counters_count(struct mlx5_core_dev *mdev)
{
	return mlx5_fpga_ipsec_counters_count(mdev);
}

int mlx5_accel_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				   unsigned int count)
{
	return mlx5_fpga_ipsec_counters_read(mdev, counters, count);
}

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle)
{
	__be32 saddr[4] = {}, daddr[4] = {};

	if (!xfrm->attrs.is_ipv6) {
		saddr[3] = xfrm->attrs.saddr.a4;
		daddr[3] = xfrm->attrs.daddr.a4;
	} else {
		memcpy(saddr, xfrm->attrs.saddr.a6, sizeof(saddr));
		memcpy(daddr, xfrm->attrs.daddr.a6, sizeof(daddr));
	}

	return mlx5_fpga_ipsec_create_sa_ctx(mdev, xfrm, saddr,
					     daddr, xfrm->attrs.spi,
					     xfrm->attrs.is_ipv6, sa_handle);
}

void mlx5_accel_esp_free_hw_context(void *context)
{
	mlx5_fpga_ipsec_delete_sa_ctx(context);
}

int mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev)
{
	return mlx5_fpga_ipsec_init(mdev);
}

void mlx5_accel_ipsec_build_fs_cmds(void)
{
	mlx5_fpga_ipsec_build_fs_cmds();
}

void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev)
{
	mlx5_fpga_ipsec_cleanup(mdev);
}

struct mlx5_accel_esp_xfrm *
mlx5_accel_esp_create_xfrm(struct mlx5_core_dev *mdev,
			   const struct mlx5_accel_esp_xfrm_attrs *attrs,
			   u32 flags)
{
	struct mlx5_accel_esp_xfrm *xfrm;

	xfrm = mlx5_fpga_esp_create_xfrm(mdev, attrs, flags);
	if (IS_ERR(xfrm))
		return xfrm;

	xfrm->mdev = mdev;
	return xfrm;
}
EXPORT_SYMBOL_GPL(mlx5_accel_esp_create_xfrm);

void mlx5_accel_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm)
{
	mlx5_fpga_esp_destroy_xfrm(xfrm);
}
EXPORT_SYMBOL_GPL(mlx5_accel_esp_destroy_xfrm);

int mlx5_accel_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
			       const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	return mlx5_fpga_esp_modify_xfrm(xfrm, attrs);
}
EXPORT_SYMBOL_GPL(mlx5_accel_esp_modify_xfrm);

#endif
