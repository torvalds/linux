// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>

#include <kunit/resource.h>

#include <linux/device.h>

#include "drm_kunit_helpers.h"

struct kunit_dev {
	struct drm_device base;
};

static const struct drm_mode_config_funcs drm_mode_config_funcs = {
};

static int dev_init(struct kunit_resource *res, void *ptr)
{
	char *name = ptr;
	struct device *dev;

	dev = root_device_register(name);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	res->data = dev;
	return 0;
}

static void dev_free(struct kunit_resource *res)
{
	struct device *dev = res->data;

	root_device_unregister(dev);
}

struct drm_device *drm_kunit_device_init(struct kunit *test, u32 features, char *name)
{
	struct kunit_dev *kdev;
	struct drm_device *drm;
	struct drm_driver *driver;
	struct device *dev;
	int ret;

	dev = kunit_alloc_resource(test, dev_init, dev_free, GFP_KERNEL, name);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	driver = kunit_kzalloc(test, sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	driver->driver_features = features;
	kdev = devm_drm_dev_alloc(dev, driver, struct kunit_dev, base);
	if (IS_ERR(kdev))
		return ERR_CAST(kdev);

	drm = &kdev->base;
	drm->mode_config.funcs = &drm_mode_config_funcs;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ERR_PTR(ret);

	return drm;
}

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_LICENSE("GPL");
