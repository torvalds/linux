// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_managed.h>

#include <kunit/resource.h>

#include <linux/device.h>
#include <linux/platform_device.h>

#define KUNIT_DEVICE_NAME	"drm-kunit-mock-device"

struct kunit_dev {
	struct drm_device base;
};

static const struct drm_mode_config_funcs drm_mode_config_funcs = {
};

/**
 * drm_kunit_helper_alloc_device - Allocate a mock device for a KUnit test
 * @test: The test context object
 *
 * This allocates a fake struct &device to create a mock for a KUnit
 * test.
 *
 * Callers need to make sure drm_kunit_helper_free_device() on the
 * device when done.
 *
 * Returns:
 * A pointer to the new device, or an ERR_PTR() otherwise.
 */
struct device *drm_kunit_helper_alloc_device(struct kunit *test)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc(KUNIT_DEVICE_NAME, PLATFORM_DEVID_NONE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	ret = platform_device_add(pdev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return &pdev->dev;
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_alloc_device);

/**
 * drm_kunit_helper_free_device - Frees a mock device
 * @test: The test context object
 * @dev: The device to free
 *
 * Frees a device allocated with drm_kunit_helper_alloc_device().
 */
void drm_kunit_helper_free_device(struct kunit *test, struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_free_device);

/**
 * drm_kunit_helper_alloc_drm_device - Allocates a mock DRM device for KUnit tests
 * @test: The test context object
 * @dev: The parent device object
 * @features: Mocked DRM device driver features
 *
 * This function creates a struct &drm_driver and will create a struct
 * &drm_device from @dev and that driver.
 *
 * @dev should be allocated using drm_kunit_helper_alloc_device().
 *
 * The driver is tied to the @test context and will get cleaned at the
 * end of the test. The drm_device is allocated through
 * devm_drm_dev_alloc() and will thus be freed through a device-managed
 * resource.
 *
 * Returns:
 * A pointer to the new drm_device, or an ERR_PTR() otherwise.
 */
struct drm_device *
drm_kunit_helper_alloc_drm_device(struct kunit *test, struct device *dev,
				  u32 features)
{
	struct kunit_dev *kdev;
	struct drm_device *drm;
	struct drm_driver *driver;
	int ret;

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
EXPORT_SYMBOL_GPL(drm_kunit_helper_alloc_drm_device);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_LICENSE("GPL");
