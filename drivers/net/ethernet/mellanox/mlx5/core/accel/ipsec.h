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

#ifndef __MLX5_ACCEL_IPSEC_H__
#define __MLX5_ACCEL_IPSEC_H__

#include <linux/mlx5/driver.h>
#include <linux/mlx5/accel.h>

#ifdef CONFIG_MLX5_FPGA_IPSEC

#define MLX5_IPSEC_DEV(mdev) (mlx5_accel_ipsec_device_caps(mdev) & \
			      MLX5_ACCEL_IPSEC_CAP_DEVICE)

unsigned int mlx5_accel_ipsec_counters_count(struct mlx5_core_dev *mdev);
int mlx5_accel_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				   unsigned int count);

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle);
void mlx5_accel_esp_free_hw_context(void *context);

int mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev);
void mlx5_accel_ipsec_build_fs_cmds(void);
void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev);

#else

#define MLX5_IPSEC_DEV(mdev) false

static inline void *
mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				 struct mlx5_accel_esp_xfrm *xfrm,
				 u32 *sa_handle)
{
	return NULL;
}

static inline void mlx5_accel_esp_free_hw_context(void *context)
{
}

static inline int mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev)
{
	return 0;
}

static inline void mlx5_accel_ipsec_build_fs_cmds(void)
{
}

static inline void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev)
{
}

#endif

#endif	/* __MLX5_ACCEL_IPSEC_H__ */
