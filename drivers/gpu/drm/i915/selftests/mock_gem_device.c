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

#include "mock_engine.h"
#include "mock_context.h"
#include "mock_request.h"
#include "mock_gem_device.h"
#include "mock_gem_object.h"
#include "mock_gtt.h"
#include "mock_uncore.h"

void mock_device_flush(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&i915->drm.struct_mutex);

	for_each_engine(engine, i915, id)
		mock_engine_flush(engine);

	i915_gem_retire_requests(i915);
}

static void mock_device_release(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	mutex_lock(&i915->drm.struct_mutex);
	mock_device_flush(i915);
	mutex_unlock(&i915->drm.struct_mutex);

	cancel_delayed_work_sync(&i915->gt.retire_work);
	cancel_delayed_work_sync(&i915->gt.idle_work);

	mutex_lock(&i915->drm.struct_mutex);
	for_each_engine(engine, i915, id)
		mock_engine_free(engine);
	i915_gem_context_fini(i915);
	mutex_unlock(&i915->drm.struct_mutex);

	drain_workqueue(i915->wq);
	i915_gem_drain_freed_objects(i915);

	mutex_lock(&i915->drm.struct_mutex);
	mock_fini_ggtt(i915);
	i915_gem_timeline_fini(&i915->gt.global_timeline);
	mutex_unlock(&i915->drm.struct_mutex);

	destroy_workqueue(i915->wq);

	kmem_cache_destroy(i915->priorities);
	kmem_cache_destroy(i915->dependencies);
	kmem_cache_destroy(i915->requests);
	kmem_cache_destroy(i915->vmas);
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

static void mock_retire_work_handler(struct work_struct *work)
{
}

static void mock_idle_work_handler(struct work_struct *work)
{
}

struct drm_i915_private *mock_gem_device(void)
{
	struct drm_i915_private *i915;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct pci_dev *pdev;
	int err;

	pdev = kzalloc(sizeof(*pdev) + sizeof(*i915), GFP_KERNEL);
	if (!pdev)
		goto err;

	device_initialize(&pdev->dev);
	pdev->class = PCI_BASE_CLASS_DISPLAY << 16;
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

	/* Using the global GTT may ask questions about KMS users, so prepare */
	drm_mode_config_init(&i915->drm);

	mkwrite_device_info(i915)->gen = -1;

	spin_lock_init(&i915->mm.object_stat_lock);
	mock_uncore_init(i915);

	init_waitqueue_head(&i915->gpu_error.wait_queue);
	init_waitqueue_head(&i915->gpu_error.reset_queue);

	i915->wq = alloc_ordered_workqueue("mock", 0);
	if (!i915->wq)
		goto put_device;

	INIT_WORK(&i915->mm.free_work, __i915_gem_free_work);
	init_llist_head(&i915->mm.free_list);
	INIT_LIST_HEAD(&i915->mm.unbound_list);
	INIT_LIST_HEAD(&i915->mm.bound_list);

	ida_init(&i915->context_hw_ida);

	INIT_DELAYED_WORK(&i915->gt.retire_work, mock_retire_work_handler);
	INIT_DELAYED_WORK(&i915->gt.idle_work, mock_idle_work_handler);

	i915->gt.awake = true;

	i915->objects = KMEM_CACHE(mock_object, SLAB_HWCACHE_ALIGN);
	if (!i915->objects)
		goto err_wq;

	i915->vmas = KMEM_CACHE(i915_vma, SLAB_HWCACHE_ALIGN);
	if (!i915->vmas)
		goto err_objects;

	i915->requests = KMEM_CACHE(mock_request,
				    SLAB_HWCACHE_ALIGN |
				    SLAB_RECLAIM_ACCOUNT |
				    SLAB_TYPESAFE_BY_RCU);
	if (!i915->requests)
		goto err_vmas;

	i915->dependencies = KMEM_CACHE(i915_dependency,
					SLAB_HWCACHE_ALIGN |
					SLAB_RECLAIM_ACCOUNT);
	if (!i915->dependencies)
		goto err_requests;

	i915->priorities = KMEM_CACHE(i915_priolist, SLAB_HWCACHE_ALIGN);
	if (!i915->priorities)
		goto err_dependencies;

	mutex_lock(&i915->drm.struct_mutex);
	INIT_LIST_HEAD(&i915->gt.timelines);
	err = i915_gem_timeline_init__global(i915);
	if (err) {
		mutex_unlock(&i915->drm.struct_mutex);
		goto err_priorities;
	}

	mock_init_ggtt(i915);
	mutex_unlock(&i915->drm.struct_mutex);

	mkwrite_device_info(i915)->ring_mask = BIT(0);
	i915->engine[RCS] = mock_engine(i915, "mock");
	if (!i915->engine[RCS])
		goto err_priorities;

	i915->kernel_context = mock_context(i915, NULL);
	if (!i915->kernel_context)
		goto err_engine;

	return i915;

err_engine:
	for_each_engine(engine, i915, id)
		mock_engine_free(engine);
err_priorities:
	kmem_cache_destroy(i915->priorities);
err_dependencies:
	kmem_cache_destroy(i915->dependencies);
err_requests:
	kmem_cache_destroy(i915->requests);
err_vmas:
	kmem_cache_destroy(i915->vmas);
err_objects:
	kmem_cache_destroy(i915->objects);
err_wq:
	destroy_workqueue(i915->wq);
put_device:
	put_device(&pdev->dev);
err:
	return NULL;
}
