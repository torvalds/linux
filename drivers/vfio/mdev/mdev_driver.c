// SPDX-License-Identifier: GPL-2.0-only
/*
 * MDEV driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 */

#include <linux/iommu.h>
#include <linux/mdev.h>

#include "mdev_private.h"

static int mdev_probe(struct device *dev)
{
	struct mdev_driver *drv =
		container_of(dev->driver, struct mdev_driver, driver);

	if (!drv->probe)
		return 0;
	return drv->probe(to_mdev_device(dev));
}

static void mdev_remove(struct device *dev)
{
	struct mdev_driver *drv =
		container_of(dev->driver, struct mdev_driver, driver);

	if (drv->remove)
		drv->remove(to_mdev_device(dev));
}

static int mdev_match(struct device *dev, const struct device_driver *drv)
{
	/*
	 * No drivers automatically match. Drivers are only bound by explicit
	 * device_driver_attach()
	 */
	return 0;
}

const struct bus_type mdev_bus_type = {
	.name		= "mdev",
	.probe		= mdev_probe,
	.remove		= mdev_remove,
	.match		= mdev_match,
};

/**
 * mdev_register_driver - register a new MDEV driver
 * @drv: the driver to register
 *
 * Returns a negative value on error, otherwise 0.
 **/
int mdev_register_driver(struct mdev_driver *drv)
{
	if (!drv->device_api)
		return -EINVAL;

	/* initialize common driver fields */
	drv->driver.bus = &mdev_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(mdev_register_driver);

/*
 * mdev_unregister_driver - unregister MDEV driver
 * @drv: the driver to unregister
 */
void mdev_unregister_driver(struct mdev_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(mdev_unregister_driver);
