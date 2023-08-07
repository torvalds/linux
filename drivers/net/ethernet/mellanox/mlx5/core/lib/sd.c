// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lib/sd.h"
#include "mlx5_core.h"

#define sd_info(__dev, format, ...) \
	dev_info((__dev)->device, "Socket-Direct: " format, ##__VA_ARGS__)
#define sd_warn(__dev, format, ...) \
	dev_warn((__dev)->device, "Socket-Direct: " format, ##__VA_ARGS__)

struct mlx5_sd {
};

static int mlx5_sd_get_host_buses(struct mlx5_core_dev *dev)
{
	return 1;
}

struct mlx5_core_dev *
mlx5_sd_primary_get_peer(struct mlx5_core_dev *primary, int idx)
{
	if (idx == 0)
		return primary;

	return NULL;
}

int mlx5_sd_ch_ix_get_dev_ix(struct mlx5_core_dev *dev, int ch_ix)
{
	return ch_ix % mlx5_sd_get_host_buses(dev);
}

int mlx5_sd_ch_ix_get_vec_ix(struct mlx5_core_dev *dev, int ch_ix)
{
	return ch_ix / mlx5_sd_get_host_buses(dev);
}

struct mlx5_core_dev *mlx5_sd_ch_ix_get_dev(struct mlx5_core_dev *primary, int ch_ix)
{
	int mdev_idx = mlx5_sd_ch_ix_get_dev_ix(primary, ch_ix);

	return mlx5_sd_primary_get_peer(primary, mdev_idx);
}

int mlx5_sd_init(struct mlx5_core_dev *dev)
{
	return 0;
}

void mlx5_sd_cleanup(struct mlx5_core_dev *dev)
{
}

struct auxiliary_device *mlx5_sd_get_adev(struct mlx5_core_dev *dev,
					  struct auxiliary_device *adev,
					  int idx)
{
	return adev;
}
