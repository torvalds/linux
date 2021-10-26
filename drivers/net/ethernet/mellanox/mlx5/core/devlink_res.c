// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include "devlink.h"
#include "mlx5_core.h"

enum {
	MLX5_EQ_MIN_SIZE = 64,
	MLX5_EQ_MAX_SIZE = 4096,
	MLX5_NUM_ASYNC_EQE = 4096,
	MLX5_COMP_EQ_SIZE = 1024,
};

static int comp_eq_res_register(struct mlx5_core_dev *dev)
{
	struct devlink_resource_size_params comp_eq_size;
	struct devlink *devlink = priv_to_devlink(dev);

	devlink_resource_size_params_init(&comp_eq_size, MLX5_EQ_MIN_SIZE,
					  MLX5_EQ_MAX_SIZE, 1, DEVLINK_RESOURCE_UNIT_ENTRY);
	return devlink_resource_register(devlink, "io_eq_size", MLX5_COMP_EQ_SIZE,
					 MLX5_DL_RES_COMP_EQ,
					 DEVLINK_RESOURCE_ID_PARENT_TOP,
					 &comp_eq_size);
}

static int async_eq_resource_register(struct mlx5_core_dev *dev)
{
	struct devlink_resource_size_params async_eq_size;
	struct devlink *devlink = priv_to_devlink(dev);

	devlink_resource_size_params_init(&async_eq_size, MLX5_EQ_MIN_SIZE,
					  MLX5_EQ_MAX_SIZE, 1, DEVLINK_RESOURCE_UNIT_ENTRY);
	return devlink_resource_register(devlink, "event_eq_size",
					 MLX5_NUM_ASYNC_EQE, MLX5_DL_RES_ASYNC_EQ,
					 DEVLINK_RESOURCE_ID_PARENT_TOP,
					 &async_eq_size);
}

void mlx5_devlink_res_register(struct mlx5_core_dev *dev)
{
	int err;

	err = comp_eq_res_register(dev);
	if (err)
		goto err_msg;

	err = async_eq_resource_register(dev);
	if (err)
		goto err;
	return;
err:
	devlink_resources_unregister(priv_to_devlink(dev), NULL);
err_msg:
	mlx5_core_err(dev, "Failed to register resources, err = %d\n", err);
}

void mlx5_devlink_res_unregister(struct mlx5_core_dev *dev)
{
	devlink_resources_unregister(priv_to_devlink(dev), NULL);
}

static const size_t default_vals[MLX5_ID_RES_MAX + 1] = {
	[MLX5_DL_RES_COMP_EQ] = MLX5_COMP_EQ_SIZE,
	[MLX5_DL_RES_ASYNC_EQ] = MLX5_NUM_ASYNC_EQE,
};

size_t mlx5_devlink_res_size(struct mlx5_core_dev *dev, enum mlx5_devlink_resource_id id)
{
	struct devlink *devlink = priv_to_devlink(dev);
	u64 size;
	int err;

	err = devlink_resource_size_get(devlink, id, &size);
	if (!err)
		return size;
	mlx5_core_err(dev, "Failed to get param. using default. err = %d, id = %u\n",
		      err, id);
	return default_vals[id];
}
