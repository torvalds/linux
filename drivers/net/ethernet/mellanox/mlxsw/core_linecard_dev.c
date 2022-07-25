// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2022 NVIDIA Corporation and Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/auxiliary_bus.h>
#include <linux/idr.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <net/devlink.h>
#include "core.h"

#define MLXSW_LINECARD_DEV_ID_NAME "lc"

struct mlxsw_linecard_dev {
	struct mlxsw_linecard *linecard;
};

struct mlxsw_linecard_bdev {
	struct auxiliary_device adev;
	struct mlxsw_linecard *linecard;
	struct mlxsw_linecard_dev *linecard_dev;
};

static DEFINE_IDA(mlxsw_linecard_bdev_ida);

static int mlxsw_linecard_bdev_id_alloc(void)
{
	return ida_alloc(&mlxsw_linecard_bdev_ida, GFP_KERNEL);
}

static void mlxsw_linecard_bdev_id_free(int id)
{
	ida_free(&mlxsw_linecard_bdev_ida, id);
}

static void mlxsw_linecard_bdev_release(struct device *device)
{
	struct auxiliary_device *adev =
			container_of(device, struct auxiliary_device, dev);
	struct mlxsw_linecard_bdev *linecard_bdev =
			container_of(adev, struct mlxsw_linecard_bdev, adev);

	mlxsw_linecard_bdev_id_free(adev->id);
	kfree(linecard_bdev);
}

int mlxsw_linecard_bdev_add(struct mlxsw_linecard *linecard)
{
	struct mlxsw_linecard_bdev *linecard_bdev;
	int err;
	int id;

	id = mlxsw_linecard_bdev_id_alloc();
	if (id < 0)
		return id;

	linecard_bdev = kzalloc(sizeof(*linecard_bdev), GFP_KERNEL);
	if (!linecard_bdev) {
		mlxsw_linecard_bdev_id_free(id);
		return -ENOMEM;
	}
	linecard_bdev->adev.id = id;
	linecard_bdev->adev.name = MLXSW_LINECARD_DEV_ID_NAME;
	linecard_bdev->adev.dev.release = mlxsw_linecard_bdev_release;
	linecard_bdev->adev.dev.parent = linecard->linecards->bus_info->dev;
	linecard_bdev->linecard = linecard;

	err = auxiliary_device_init(&linecard_bdev->adev);
	if (err) {
		mlxsw_linecard_bdev_id_free(id);
		kfree(linecard_bdev);
		return err;
	}

	err = auxiliary_device_add(&linecard_bdev->adev);
	if (err) {
		auxiliary_device_uninit(&linecard_bdev->adev);
		return err;
	}

	linecard->bdev = linecard_bdev;
	return 0;
}

void mlxsw_linecard_bdev_del(struct mlxsw_linecard *linecard)
{
	struct mlxsw_linecard_bdev *linecard_bdev = linecard->bdev;

	if (!linecard_bdev)
		/* Unprovisioned line cards do not have an auxiliary device. */
		return;
	auxiliary_device_delete(&linecard_bdev->adev);
	auxiliary_device_uninit(&linecard_bdev->adev);
	linecard->bdev = NULL;
}

static int mlxsw_linecard_dev_devlink_info_get(struct devlink *devlink,
					       struct devlink_info_req *req,
					       struct netlink_ext_ack *extack)
{
	struct mlxsw_linecard_dev *linecard_dev = devlink_priv(devlink);
	struct mlxsw_linecard *linecard = linecard_dev->linecard;

	return mlxsw_linecard_devlink_info_get(linecard, req, extack);
}

static const struct devlink_ops mlxsw_linecard_dev_devlink_ops = {
	.info_get			= mlxsw_linecard_dev_devlink_info_get,
};

static int mlxsw_linecard_bdev_probe(struct auxiliary_device *adev,
				     const struct auxiliary_device_id *id)
{
	struct mlxsw_linecard_bdev *linecard_bdev =
			container_of(adev, struct mlxsw_linecard_bdev, adev);
	struct mlxsw_linecard *linecard = linecard_bdev->linecard;
	struct mlxsw_linecard_dev *linecard_dev;
	struct devlink *devlink;

	devlink = devlink_alloc(&mlxsw_linecard_dev_devlink_ops,
				sizeof(*linecard_dev), &adev->dev);
	if (!devlink)
		return -ENOMEM;
	linecard_dev = devlink_priv(devlink);
	linecard_dev->linecard = linecard_bdev->linecard;
	linecard_bdev->linecard_dev = linecard_dev;

	devlink_register(devlink);
	devlink_linecard_nested_dl_set(linecard->devlink_linecard, devlink);
	return 0;
}

static void mlxsw_linecard_bdev_remove(struct auxiliary_device *adev)
{
	struct mlxsw_linecard_bdev *linecard_bdev =
			container_of(adev, struct mlxsw_linecard_bdev, adev);
	struct devlink *devlink = priv_to_devlink(linecard_bdev->linecard_dev);
	struct mlxsw_linecard *linecard = linecard_bdev->linecard;

	devlink_linecard_nested_dl_set(linecard->devlink_linecard, NULL);
	devlink_unregister(devlink);
	devlink_free(devlink);
}

static const struct auxiliary_device_id mlxsw_linecard_bdev_id_table[] = {
	{ .name = KBUILD_MODNAME "." MLXSW_LINECARD_DEV_ID_NAME },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlxsw_linecard_bdev_id_table);

static struct auxiliary_driver mlxsw_linecard_driver = {
	.name = MLXSW_LINECARD_DEV_ID_NAME,
	.probe = mlxsw_linecard_bdev_probe,
	.remove = mlxsw_linecard_bdev_remove,
	.id_table = mlxsw_linecard_bdev_id_table,
};

int mlxsw_linecard_driver_register(void)
{
	return auxiliary_driver_register(&mlxsw_linecard_driver);
}

void mlxsw_linecard_driver_unregister(void)
{
	auxiliary_driver_unregister(&mlxsw_linecard_driver);
}
