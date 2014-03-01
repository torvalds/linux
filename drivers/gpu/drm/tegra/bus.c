/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "drm.h"

static int drm_host1x_set_busid(struct drm_device *dev,
				struct drm_master *master)
{
	const char *device = dev_name(dev->dev);
	const char *driver = dev->driver->name;
	const char *bus = dev->dev->bus->name;
	int length;

	master->unique_len = strlen(bus) + 1 + strlen(device);
	master->unique_size = master->unique_len;

	master->unique = kmalloc(master->unique_len + 1, GFP_KERNEL);
	if (!master->unique)
		return -ENOMEM;

	snprintf(master->unique, master->unique_len + 1, "%s:%s", bus, device);

	length = strlen(driver) + 1 + master->unique_len;

	dev->devname = kmalloc(length + 1, GFP_KERNEL);
	if (!dev->devname)
		return -ENOMEM;

	snprintf(dev->devname, length + 1, "%s@%s", driver, master->unique);

	return 0;
}

static struct drm_bus drm_host1x_bus = {
	.bus_type = DRIVER_BUS_HOST1X,
	.set_busid = drm_host1x_set_busid,
};

int drm_host1x_init(struct drm_driver *driver, struct host1x_device *device)
{
	struct drm_device *drm;
	int ret;

	driver->bus = &drm_host1x_bus;

	drm = drm_dev_alloc(driver, &device->dev);
	if (!drm)
		return -ENOMEM;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_free;

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n", driver->name,
		 driver->major, driver->minor, driver->patchlevel,
		 driver->date, drm->primary->index);

	return 0;

err_free:
	drm_dev_free(drm);
	return ret;
}

void drm_host1x_exit(struct drm_driver *driver, struct host1x_device *device)
{
	struct tegra_drm *tegra = dev_get_drvdata(&device->dev);

	drm_put_dev(tegra->drm);
}
