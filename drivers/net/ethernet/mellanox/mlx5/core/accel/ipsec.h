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

#ifdef CONFIG_MLX5_ACCEL

#define MLX5_IPSEC_DEV(mdev) (mlx5_accel_ipsec_device_caps(mdev) & \
			      MLX5_ACCEL_IPSEC_CAP_DEVICE)

unsigned int mlx5_accel_ipsec_counters_count(struct mlx5_core_dev *mdev);
int mlx5_accel_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				   unsigned int count);

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle);
void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev, void *context);

void mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev);
void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev);

struct mlx5_accel_ipsec_ops {
	u32 (*device_caps)(struct mlx5_core_dev *mdev);
	unsigned int (*counters_count)(struct mlx5_core_dev *mdev);
	int (*counters_read)(struct mlx5_core_dev *mdev, u64 *counters, unsigned int count);
	void* (*create_hw_context)(struct mlx5_core_dev *mdev,
				   struct mlx5_accel_esp_xfrm *xfrm,
				   const __be32 saddr[4], const __be32 daddr[4],
				   const __be32 spi, bool is_ipv6, u32 *sa_handle);
	void (*free_hw_context)(void *context);
	int (*init)(struct mlx5_core_dev *mdev);
	void (*cleanup)(struct mlx5_core_dev *mdev);
	struct mlx5_accel_esp_xfrm* (*esp_create_xfrm)(struct mlx5_core_dev *mdev,
						       const struct mlx5_accel_esp_xfrm_attrs *attrs,
						       u32 flags);
	int (*esp_modify_xfrm)(struct mlx5_accel_esp_xfrm *xfrm,
			       const struct mlx5_accel_esp_xfrm_attrs *attrs);
	void (*esp_destroy_xfrm)(struct mlx5_accel_esp_xfrm *xfrm);
};

#else

#define MLX5_IPSEC_DEV(mdev) false

static inline void *
mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				 struct mlx5_accel_esp_xfrm *xfrm,
				 u32 *sa_handle)
{
	return NULL;
}

static inline void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev, void *context) {}

static inline void mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev) {}

static inline void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev) {}

#endif /* CONFIG_MLX5_ACCEL */

#endif	/* __MLX5_ACCEL_IPSEC_H__ */
