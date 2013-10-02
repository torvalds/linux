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

	mutex_lock(&drm_global_mutex);

	ret = drm_fill_in_dev(dev, NULL, driver);

	if (ret) {
		printk(KERN_ERR "DRM: Fill_in_dev failed.\n");
		goto err_g1;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_get_minor(dev, &dev->control, DRM_MINOR_CONTROL);
		if (ret)
			goto err_g1;
	}

	if (drm_core_check_feature(dev, DRIVER_RENDER) && drm_rnodes) {
		ret = drm_get_minor(dev, &dev->render, DRM_MINOR_RENDER);
		if (ret)
			goto err_g11;
	}

	ret = drm_get_minor(dev, &dev->primary, DRM_MINOR_LEGACY);
	if (ret)
		goto err_g2;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, 0);
		if (ret)
			goto err_g3;
	}

	/* setup the grouping for the legacy output */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_mode_group_init_legacy_group(dev,
				&dev->primary->mode_group);
		if (ret)
			goto err_g3;
	}

	list_add_tail(&dev->driver_item, &driver->device_list);

	mutex_unlock(&drm_global_mutex);

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 driver->name, driver->major, driver->minor, driver->patchlevel,
		 driver->date, dev->primary->index);

	return 0;

err_g3:
	drm_put_minor(&dev->primary);
err_g2:
	if (dev->render)
		drm_put_minor(&dev->render);
err_g11:
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_put_minor(&dev->control);
err_g1:
	kfree(dev);
	mutex_unlock(&drm_global_mutex);
	return ret;
}

static int drm_platform_get_irq(struct drm_device *dev)
{
	return platform_get_irq(dev->platformdev, 0);
}

static const char *drm_platform_get_name(struct drm_device *dev)
{
	return dev->platformdev->name;
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

	dev->devname =
		kmalloc(strlen(dev->platformdev->name) +
			master->unique_len + 2, GFP_KERNEL);

	if (dev->devname == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	sprintf(dev->devname, "%s@%s", dev->platformdev->name,
		master->unique);
	return 0;
err:
	return ret;
}

static struct drm_bus drm_platform_bus = {
	.bus_type = DRIVER_BUS_PLATFORM,
	.get_irq = drm_platform_get_irq,
	.get_name = drm_platform_get_name,
	.set_busid = drm_platform_set_busid,
};

/**
 * Platform device initialization. Called direct from modules.
 *
 * \return zero on success or a negative number on failure.
 *
 * Initializes a drm_device structures,registering the
 * stubs
 *
 * Expands the \c DRIVER_PREINIT and \c DRIVER_POST_INIT macros before and
 * after the initialization for driver customization.
 */

int drm_platform_init(struct drm_driver *driver, struct platform_device *platform_device)
{
	DRM_DEBUG("\n");

	driver->kdriver.platform_device = platform_device;
	driver->bus = &drm_platform_bus;
	INIT_LIST_HEAD(&driver->device_list);
	return drm_get_platform_dev(platform_device, driver);
}
EXPORT_SYMBOL(drm_platform_init);

void drm_platform_exit(struct drm_driver *driver, struct platform_device *platform_device)
{
	struct drm_device *dev, *tmp;
	DRM_DEBUG("\n");

	list_for_each_entry_safe(dev, tmp, &driver->device_list, driver_item)
		drm_put_dev(dev);
	DRM_INFO("Module unloaded\n");
}
EXPORT_SYMBOL(drm_platform_exit);
