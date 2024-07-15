// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/ttm/ttm_tt.h>

#include "ttm_kunit_helpers.h"

static struct ttm_tt *ttm_tt_simple_create(struct ttm_buffer_object *bo,
					   uint32_t page_flags)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	ttm_tt_init(tt, bo, page_flags, ttm_cached, 0);

	return tt;
}

static void ttm_tt_simple_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	kfree(ttm);
}

static void dummy_ttm_bo_destroy(struct ttm_buffer_object *bo)
{
}

struct ttm_device_funcs ttm_dev_funcs = {
	.ttm_tt_create = ttm_tt_simple_create,
	.ttm_tt_destroy = ttm_tt_simple_destroy,
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
	struct drm_gem_object gem_obj = { };
	struct ttm_buffer_object *bo;
	int err;

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	bo->base = gem_obj;
	err = drm_gem_object_init(devs->drm, &bo->base, size);
	KUNIT_ASSERT_EQ(test, err, 0);

	bo->bdev = devs->ttm_dev;
	bo->destroy = dummy_ttm_bo_destroy;

	kref_init(&bo->kref);

	return bo;
}
EXPORT_SYMBOL_GPL(ttm_bo_kunit_init);

struct ttm_place *ttm_place_kunit_init(struct kunit *test,
				       uint32_t mem_type, uint32_t flags)
{
	struct ttm_place *place;

	place = kunit_kzalloc(test, sizeof(*place), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, place);

	place->mem_type = mem_type;
	place->flags = flags;

	return place;
}
EXPORT_SYMBOL_GPL(ttm_place_kunit_init);

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
