/*
 * Derived from drm_pci.c
 *
 * Copyright 2003 José Fonseca.
 * Copyright 2003 Leif Delgass.
 * Copyright (c) 2009, Code Aurora Forum.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>
#include <drm/drmP.h>

/*
 * Register.
 *
 * \param platdev - Platform device struture
 * \return zero on success or a negative number on failure.
 *
 * Attempt to gets inter module "drm" information. If we are first
 * then register the character device and inter module information.
 * Try and register, if we fail to register, backout previous work.
 */

static int drm_get_platform_dev(struct platform_device *platdev,
				struct drm_driver *driver)
{
	struct drm_device *dev;
	int ret;

	DRM_DEBUG("\n");

	dev = drm_dev_alloc(driver, &platdev->dev);
	if (!dev)
		return -ENOMEM;

	dev->platformdev = platdev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free;

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 driver->name, driver->major, driver->minor, driver->patchlevel,
		 driver->date, dev->primary->index);

	return 0;

err_free:
	drm_dev_unref(dev);
	return ret;
}

static int drm_platform_set_busid(struct drm_device *dev, struct drm_master *master)
{
	int len, ret, id;

	master->unique_len = 13 + strlen(dev->platformdev->name);
	master->unique_size = master->unique_len;
	master->unique = kmalloc(master->unique_len + 1, GFP_KERNEL);

	if (master->unique == NULL)
		return -ENOMEM;

	id = dev->platformdev->id;

	/* if only a single instance of the platform device, id will be
	 * set to -1.. use 0 instead to avoid a funny looking bus-id:
	 */
	if (id == -1)
		id = 0;

	len = snprintf(master->unique, master->unique_len,
			"platform:%s:%02d", dev->platformdev->name, id);

	if (len > master->unique_len) {
		DRM_ERROR("Unique buffer overflowed\n");
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	return ret;
}

static struct drm_bus drm_platform_bus = {
	.set_busid = drm_platform_set_busid,
};

/**
 * drm_platform_init - Register a platform device with the DRM subsystem
 * @driver: DRM device driver
 * @platform_device: platform device to register
 *
 * Registers the specified DRM device driver and platform device with the DRM
 * subsystem, initializing a drm_device structure and calling the driver's
 * .load() function.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_platform_init(struct drm_driver *driver, struct platform_device *platform_device)
{
	DRM_DEBUG("\n");

	driver->bus = &drm_platform_bus;
	return drm_get_platform_dev(platform_device, driver);
}
EXPORT_SYMBOL(drm_platform_init);
