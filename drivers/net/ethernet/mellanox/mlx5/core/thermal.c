// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "thermal.h"

#define MLX5_THERMAL_POLL_INT_MSEC	1000
#define MLX5_THERMAL_NUM_TRIPS		0
#define MLX5_THERMAL_ASIC_SENSOR_INDEX	0

/* Bit string indicating the writeablility of trip points if any */
#define MLX5_THERMAL_TRIP_MASK	(BIT(MLX5_THERMAL_NUM_TRIPS) - 1)

struct mlx5_thermal {
	struct mlx5_core_dev *mdev;
	struct thermal_zone_device *tzdev;
};

static int mlx5_thermal_get_mtmp_temp(struct mlx5_core_dev *mdev, u32 id, int *p_temp)
{
	u32 mtmp_out[MLX5_ST_SZ_DW(mtmp_reg)] = {};
	u32 mtmp_in[MLX5_ST_SZ_DW(mtmp_reg)] = {};
	int err;

	MLX5_SET(mtmp_reg, mtmp_in, sensor_index, id);

	err = mlx5_core_access_reg(mdev, mtmp_in,  sizeof(mtmp_in),
				   mtmp_out, sizeof(mtmp_out),
				   MLX5_REG_MTMP, 0, 0);

	if (err)
		return err;

	*p_temp = MLX5_GET(mtmp_reg, mtmp_out, temperature);

	return 0;
}

static int mlx5_thermal_get_temp(struct thermal_zone_device *tzdev,
				 int *p_temp)
{
	struct mlx5_thermal *thermal = tzdev->devdata;
	struct mlx5_core_dev *mdev = thermal->mdev;
	int err;

	err = mlx5_thermal_get_mtmp_temp(mdev, MLX5_THERMAL_ASIC_SENSOR_INDEX, p_temp);

	if (err)
		return err;

	/* The unit of temp returned is in 0.125 C. The thermal
	 * framework expects the value in 0.001 C.
	 */
	*p_temp *= 125;

	return 0;
}

static struct thermal_zone_device_ops mlx5_thermal_ops = {
	.get_temp = mlx5_thermal_get_temp,
};

int mlx5_thermal_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_thermal *thermal;
	struct thermal_zone_device *tzd;
	const char *data = "mlx5";

	tzd = thermal_zone_get_zone_by_name(data);
	if (!IS_ERR(tzd))
		return 0;

	thermal = kzalloc(sizeof(*thermal), GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	thermal->mdev = mdev;
	thermal->tzdev = thermal_zone_device_register(data,
						      MLX5_THERMAL_NUM_TRIPS,
						      MLX5_THERMAL_TRIP_MASK,
						      thermal,
						      &mlx5_thermal_ops,
						      NULL, 0, MLX5_THERMAL_POLL_INT_MSEC);
	if (IS_ERR(thermal->tzdev)) {
		dev_err(mdev->device, "Failed to register thermal zone device (%s) %ld\n",
			data, PTR_ERR(thermal->tzdev));
		kfree(thermal);
		return -EINVAL;
	}

	mdev->thermal = thermal;
	return 0;
}

void mlx5_thermal_uninit(struct mlx5_core_dev *mdev)
{
	if (!mdev->thermal)
		return;

	thermal_zone_device_unregister(mdev->thermal->tzdev);
	kfree(mdev->thermal);
}
