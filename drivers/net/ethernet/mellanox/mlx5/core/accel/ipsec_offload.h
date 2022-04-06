/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_IPSEC_OFFLOAD_H__
#define __MLX5_IPSEC_OFFLOAD_H__

#include <linux/mlx5/driver.h>
#include <linux/mlx5/accel.h>

#ifdef CONFIG_MLX5_IPSEC

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
	int (*counters_read)(struct mlx5_core_dev *mdev, u64 *counters,
			     unsigned int count);
	void *(*create_hw_context)(struct mlx5_core_dev *mdev,
				   struct mlx5_accel_esp_xfrm *xfrm,
				   const __be32 saddr[4], const __be32 daddr[4],
				   const __be32 spi, bool is_ipv6,
				   u32 *sa_handle);
	void (*free_hw_context)(void *context);
	int (*init)(struct mlx5_core_dev *mdev);
	void (*cleanup)(struct mlx5_core_dev *mdev);
	struct mlx5_accel_esp_xfrm *(*esp_create_xfrm)(
		struct mlx5_core_dev *mdev,
		const struct mlx5_accel_esp_xfrm_attrs *attrs, u32 flags);
	int (*esp_modify_xfrm)(struct mlx5_accel_esp_xfrm *xfrm,
			       const struct mlx5_accel_esp_xfrm_attrs *attrs);
	void (*esp_destroy_xfrm)(struct mlx5_accel_esp_xfrm *xfrm);
};

static inline bool mlx5_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, ipsec_offload))
		return false;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return false;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	    MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return false;

	return MLX5_CAP_IPSEC(mdev, ipsec_crypto_offload) &&
		MLX5_CAP_ETH(mdev, insert_trailer);
}
#else

#define MLX5_IPSEC_DEV(mdev) false

static inline void *
mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				 struct mlx5_accel_esp_xfrm *xfrm,
				 u32 *sa_handle)
{
	return NULL;
}

static inline void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev,
						  void *context)
{
}

static inline void mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev) {}

static inline void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev) {}
static inline bool mlx5_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	return false;
}

#endif /* CONFIG_MLX5_IPSEC */
#endif /* __MLX5_IPSEC_OFFLOAD_H__ */
