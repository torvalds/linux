/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LIB_SD_H__
#define __MLX5_LIB_SD_H__

#include <linux/types.h>

#define MLX5_SD_MAX_GROUP_SZ 2

struct mlx5_sd;

struct mlx5_core_dev *mlx5_sd_get_primary(struct mlx5_core_dev *dev);
bool mlx5_sd_is_primary(struct mlx5_core_dev *dev);
int mlx5_sd_pf_num_get(struct mlx5_core_dev *dev);
struct mlx5_core_dev *mlx5_sd_primary_get_peer(struct mlx5_core_dev *primary, int idx);
int mlx5_sd_ch_ix_get_dev_ix(struct mlx5_core_dev *dev, int ch_ix);
int mlx5_sd_ch_ix_get_vec_ix(struct mlx5_core_dev *dev, int ch_ix);
struct mlx5_core_dev *mlx5_sd_ch_ix_get_dev(struct mlx5_core_dev *primary, int ch_ix);
struct auxiliary_device *mlx5_sd_get_adev(struct mlx5_core_dev *dev,
					  struct auxiliary_device *adev,
					  int idx);
void mlx5_sd_put_adev(struct auxiliary_device *actual_adev,
		      struct auxiliary_device *adev);

#ifdef CONFIG_MLX5_CORE_EN
bool mlx5_sd_is_supported(struct mlx5_core_dev *dev);
#else
static inline bool mlx5_sd_is_supported(struct mlx5_core_dev *dev)
{
	return false;
}
#endif

int mlx5_sd_init(struct mlx5_core_dev *dev);
void mlx5_sd_cleanup(struct mlx5_core_dev *dev);

#ifdef CONFIG_MLX5_CORE_EN
struct mlx5_devcom_comp_dev *mlx5_sd_get_devcom(struct mlx5_core_dev *dev);
#else
static inline struct mlx5_devcom_comp_dev *
mlx5_sd_get_devcom(struct mlx5_core_dev *dev)
{
	return NULL;
}
#endif

#ifdef CONFIG_MLX5_ESWITCH
void mlx5_sd_eswitch_mode_set(struct mlx5_core_dev *dev, u16 mlx5_mode);
#else
static inline void
mlx5_sd_eswitch_mode_set(struct mlx5_core_dev *dev, u16 mlx5_mode) { return; }
#endif

#define mlx5_sd_for_each_dev_from_to(i, primary, ix_from, to, pos)	\
	for (i = ix_from;							\
	     (pos = mlx5_sd_primary_get_peer(primary, i)) && pos != (to); i++)

#define mlx5_sd_for_each_dev(i, primary, pos)				\
	mlx5_sd_for_each_dev_from_to(i, primary, 0, NULL, pos)

#define mlx5_sd_for_each_dev_to(i, primary, to, pos)			\
	mlx5_sd_for_each_dev_from_to(i, primary, 0, to, pos)

#define mlx5_sd_for_each_secondary(i, primary, pos)			\
	mlx5_sd_for_each_dev_from_to(i, primary, 1, NULL, pos)

#define mlx5_sd_for_each_secondary_to(i, primary, to, pos)		\
	mlx5_sd_for_each_dev_from_to(i, primary, 1, to, pos)

#endif /* __MLX5_LIB_SD_H__ */
