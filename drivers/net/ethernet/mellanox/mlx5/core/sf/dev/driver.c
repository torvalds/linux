// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>
#include "mlx5_core.h"
#include "dev.h"
#include "devlink.h"

static int mlx5_sf_dev_probe(struct auxiliary_device *adev, const struct auxiliary_device_id *id)
{
	struct mlx5_sf_dev *sf_dev = container_of(adev, struct mlx5_sf_dev, adev);
	struct mlx5_core_dev *mdev;
	struct devlink *devlink;
	int err;

	devlink = mlx5_devlink_alloc(&adev->dev);
	if (!devlink)
		return -ENOMEM;

	mdev = devlink_priv(devlink);
	mdev->device = &adev->dev;
	mdev->pdev = sf_dev->parent_mdev->pdev;
	mdev->bar_addr = sf_dev->bar_base_addr;
	mdev->iseg_base = sf_dev->bar_base_addr;
	mdev->coredev_type = MLX5_COREDEV_SF;
	mdev->priv.parent_mdev = sf_dev->parent_mdev;
	mdev->priv.adev_idx = adev->id;
	sf_dev->mdev = mdev;

	err = mlx5_mdev_init(mdev, MLX5_DEFAULT_PROF);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_mdev_init on err=%d\n", err);
		goto mdev_err;
	}

	mdev->iseg = ioremap(mdev->iseg_base, sizeof(*mdev->iseg));
	if (!mdev->iseg) {
		mlx5_core_warn(mdev, "remap error\n");
		err = -ENOMEM;
		goto remap_err;
	}

	err = mlx5_init_one(mdev);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_init_one err=%d\n", err);
		goto init_one_err;
	}
	devlink_register(devlink);
	return 0;

init_one_err:
	iounmap(mdev->iseg);
remap_err:
	mlx5_mdev_uninit(mdev);
mdev_err:
	mlx5_devlink_free(devlink);
	return err;
}

static void mlx5_sf_dev_remove(struct auxiliary_device *adev)
{
	struct mlx5_sf_dev *sf_dev = container_of(adev, struct mlx5_sf_dev, adev);
	struct devlink *devlink = priv_to_devlink(sf_dev->mdev);

	devlink_unregister(devlink);
	mlx5_uninit_one(sf_dev->mdev);
	iounmap(sf_dev->mdev->iseg);
	mlx5_mdev_uninit(sf_dev->mdev);
	mlx5_devlink_free(devlink);
}

static void mlx5_sf_dev_shutdown(struct auxiliary_device *adev)
{
	struct mlx5_sf_dev *sf_dev = container_of(adev, struct mlx5_sf_dev, adev);

	mlx5_unload_one(sf_dev->mdev);
}

static const struct auxiliary_device_id mlx5_sf_dev_id_table[] = {
	{ .name = MLX5_ADEV_NAME "." MLX5_SF_DEV_ID_NAME, },
	{ },
};

MODULE_DEVICE_TABLE(auxiliary, mlx5_sf_dev_id_table);

static struct auxiliary_driver mlx5_sf_driver = {
	.name = MLX5_SF_DEV_ID_NAME,
	.probe = mlx5_sf_dev_probe,
	.remove = mlx5_sf_dev_remove,
	.shutdown = mlx5_sf_dev_shutdown,
	.id_table = mlx5_sf_dev_id_table,
};

int mlx5_sf_driver_register(void)
{
	return auxiliary_driver_register(&mlx5_sf_driver);
}

void mlx5_sf_driver_unregister(void)
{
	auxiliary_driver_unregister(&mlx5_sf_driver);
}
