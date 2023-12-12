// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lib/sd.h"
#include "mlx5_core.h"
#include "lib/mlx5.h"
#include <linux/mlx5/vport.h>

#define sd_info(__dev, format, ...) \
	dev_info((__dev)->device, "Socket-Direct: " format, ##__VA_ARGS__)
#define sd_warn(__dev, format, ...) \
	dev_warn((__dev)->device, "Socket-Direct: " format, ##__VA_ARGS__)

struct mlx5_sd {
	u32 group_id;
	u8 host_buses;
};

static int mlx5_sd_get_host_buses(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return 1;

	return sd->host_buses;
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

static bool mlx5_sd_is_supported(struct mlx5_core_dev *dev, u8 host_buses)
{
	/* Feature is currently implemented for PFs only */
	if (!mlx5_core_is_pf(dev))
		return false;

	/* Honor the SW implementation limit */
	if (host_buses > MLX5_SD_MAX_GROUP_SZ)
		return false;

	return true;
}

static int mlx5_query_sd(struct mlx5_core_dev *dev, bool *sdm,
			 u8 *host_buses, u8 *sd_group)
{
	u32 out[MLX5_ST_SZ_DW(mpir_reg)];
	int err;

	err = mlx5_query_mpir_reg(dev, out);
	if (err)
		return err;

	err = mlx5_query_nic_vport_sd_group(dev, sd_group);
	if (err)
		return err;

	*sdm = MLX5_GET(mpir_reg, out, sdm);
	*host_buses = MLX5_GET(mpir_reg, out, host_buses);

	return 0;
}

static u32 mlx5_sd_group_id(struct mlx5_core_dev *dev, u8 sd_group)
{
	return (u32)((MLX5_CAP_GEN(dev, native_port_num) << 8) | sd_group);
}

static int sd_init(struct mlx5_core_dev *dev)
{
	u8 host_buses, sd_group;
	struct mlx5_sd *sd;
	u32 group_id;
	bool sdm;
	int err;

	err = mlx5_query_sd(dev, &sdm, &host_buses, &sd_group);
	if (err)
		return err;

	if (!sdm)
		return 0;

	if (!sd_group)
		return 0;

	group_id = mlx5_sd_group_id(dev, sd_group);

	if (!mlx5_sd_is_supported(dev, host_buses)) {
		sd_warn(dev, "can't support requested netdev combining for group id 0x%x), skipping\n",
			group_id);
		return 0;
	}

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	sd->host_buses = host_buses;
	sd->group_id = group_id;

	mlx5_set_sd(dev, sd);

	return 0;
}

static void sd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	mlx5_set_sd(dev, NULL);
	kfree(sd);
}

int mlx5_sd_init(struct mlx5_core_dev *dev)
{
	int err;

	err = sd_init(dev);
	if (err)
		return err;

	return 0;
}

void mlx5_sd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return;

	sd_cleanup(dev);
}

struct auxiliary_device *mlx5_sd_get_adev(struct mlx5_core_dev *dev,
					  struct auxiliary_device *adev,
					  int idx)
{
	return adev;
}
