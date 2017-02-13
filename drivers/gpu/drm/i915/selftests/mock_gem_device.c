/*
 * Copyright © 2016 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/pm_runtime.h>

#include "mock_gem_device.h"
#include "mock_gem_object.h"

static void mock_device_release(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);

	i915_gem_drain_freed_objects(i915);

	kmem_cache_destroy(i915->objects);

	drm_dev_fini(&i915->drm);
	put_device(&i915->drm.pdev->dev);
}

static struct drm_driver mock_driver = {
	.name = "mock",
	.driver_features = DRIVER_GEM,
	.release = mock_device_release,

	.gem_close_object = i915_gem_close_object,
	.gem_free_object_unlocked = i915_gem_free_object,
};

static void release_dev(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	kfree(pdev);
}

struct drm_i915_private *mock_gem_device(void)
{
	struct drm_i915_private *i915;
	struct pci_dev *pdev;
	int err;

	pdev = kzalloc(sizeof(*pdev) + sizeof(*i915), GFP_KERNEL);
	if (!pdev)
		goto err;

	device_initialize(&pdev->dev);
	pdev->dev.release = release_dev;
	dev_set_name(&pdev->dev, "mock");
	dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	i915 = (struct drm_i915_private *)(pdev + 1);
	pci_set_drvdata(pdev, i915);

	err = drm_dev_init(&i915->drm, &mock_driver, &pdev->dev);
	if (err) {
		pr_err("Failed to initialise mock GEM device: err=%d\n", err);
		goto put_device;
	}
	i915->drm.pdev = pdev;
	i915->drm.dev_private = i915;

	mkwrite_device_info(i915)->gen = -1;

	spin_lock_init(&i915->mm.object_stat_lock);

	INIT_WORK(&i915->mm.free_work, __i915_gem_free_work);
	init_llist_head(&i915->mm.free_list);

	i915->objects = KMEM_CACHE(mock_object, SLAB_HWCACHE_ALIGN);
	if (!i915->objects)
		goto put_device;

	return i915;

put_device:
	put_device(&pdev->dev);
err:
	return NULL;
}
