// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include "ttm_kunit_helpers.h"

struct ttm_device_funcs ttm_dev_funcs = {
};
EXPORT_SYMBOL_GPL(ttm_dev_funcs);

int ttm_device_kunit_init(struct ttm_test_devices *priv,
			  struct ttm_device *ttm,
			  bool use_dma_alloc,
			  bool use_dma32)
{
	struct drm_device *drm = priv->drm;
	int err;

	err = ttm_device_init(ttm, &ttm_dev_funcs, drm->dev,
			      drm->anon_inode->i_mapping,
			      drm->vma_offset_manager,
			      use_dma_alloc, use_dma32);

	return err;
}
EXPORT_SYMBOL_GPL(ttm_device_kunit_init);

struct ttm_buffer_object *ttm_bo_kunit_init(struct kunit *test,
					    struct ttm_test_devices *devs,
					    size_t size)
{
	struct drm_gem_object gem_obj = { .size = size };
	struct ttm_buffer_object *bo;

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	bo->base = gem_obj;
	bo->bdev = devs->ttm_dev;

	return bo;
}
EXPORT_SYMBOL_GPL(ttm_bo_kunit_init);

struct ttm_test_devices *ttm_test_devices_basic(struct kunit *test)
{
	struct ttm_test_devices *devs;

	devs = kunit_kzalloc(test, sizeof(*devs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, devs);

	devs->dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, devs->dev);

	devs->drm = __drm_kunit_helper_alloc_drm_device(test, devs->dev,
							sizeof(*devs->drm), 0,
							DRIVER_GEM);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, devs->drm);

	return devs;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_basic);

struct ttm_test_devices *ttm_test_devices_all(struct kunit *test)
{
	struct ttm_test_devices *devs;
	struct ttm_device *ttm_dev;
	int err;

	devs = ttm_test_devices_basic(test);

	ttm_dev = kunit_kzalloc(test, sizeof(*ttm_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev);

	err = ttm_device_kunit_init(devs, ttm_dev, false, false);
	KUNIT_ASSERT_EQ(test, err, 0);

	devs->ttm_dev = ttm_dev;

	return devs;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_all);

void ttm_test_devices_put(struct kunit *test, struct ttm_test_devices *devs)
{
	if (devs->ttm_dev)
		ttm_device_fini(devs->ttm_dev);

	drm_kunit_helper_free_device(test, devs->dev);
}
EXPORT_SYMBOL_GPL(ttm_test_devices_put);

int ttm_test_devices_init(struct kunit *test)
{
	struct ttm_test_devices *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	priv = ttm_test_devices_basic(test);
	test->priv = priv;

	return 0;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_init);

void ttm_test_devices_fini(struct kunit *test)
{
	ttm_test_devices_put(test, test->priv);
}
EXPORT_SYMBOL_GPL(ttm_test_devices_fini);

MODULE_LICENSE("GPL");
