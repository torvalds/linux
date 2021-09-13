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

#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/iommu.h>

#include <drm/drm_managed.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_requests.h"
#include "gt/mock_engine.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "mock_request.h"
#include "mock_gem_device.h"
#include "mock_gtt.h"
#include "mock_uncore.h"
#include "mock_region.h"

#include "gem/selftests/mock_context.h"
#include "gem/selftests/mock_gem_object.h"

void mock_device_flush(struct drm_i915_private *i915)
{
	struct intel_gt *gt = &i915->gt;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	do {
		for_each_engine(engine, gt, id)
			mock_engine_flush(engine);
	} while (intel_gt_retire_requests_timeout(gt, MAX_SCHEDULE_TIMEOUT,
						  NULL));
}

static void mock_device_release(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);

	if (!i915->do_release)
		goto out;

	mock_device_flush(i915);
	intel_gt_driver_remove(&i915->gt);

	i915_gem_drain_workqueue(i915);
	i915_gem_drain_freed_objects(i915);

	mock_fini_ggtt(&i915->ggtt);
	destroy_workqueue(i915->wq);

	intel_region_ttm_device_fini(i915);
	intel_gt_driver_late_release(&i915->gt);
	intel_memory_regions_driver_release(i915);

	drm_mode_config_cleanup(&i915->drm);

out:
	i915_params_free(&i915->params);
}

static const struct drm_driver mock_driver = {
	.name = "mock",
	.driver_features = DRIVER_GEM,
	.release = mock_device_release,
};

static void release_dev(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	kfree(pdev);
}

static int pm_domain_resume(struct device *dev)
{
	return pm_generic_runtime_resume(dev);
}

static int pm_domain_suspend(struct device *dev)
{
	return pm_generic_runtime_suspend(dev);
}

static struct dev_pm_domain pm_domain = {
	.ops = {
		.runtime_suspend = pm_domain_suspend,
		.runtime_resume = pm_domain_resume,
	},
};

struct drm_i915_private *mock_gem_device(void)
{
#if IS_ENABLED(CONFIG_IOMMU_API) && defined(CONFIG_INTEL_IOMMU)
	static struct dev_iommu fake_iommu = { .priv = (void *)-1 };
#endif
	struct drm_i915_private *i915;
	struct pci_dev *pdev;
	int ret;

	pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
	if (!pdev)
		return NULL;
	device_initialize(&pdev->dev);
	pdev->class = PCI_BASE_CLASS_DISPLAY << 16;
	pdev->dev.release = release_dev;
	dev_set_name(&pdev->dev, "mock");
	dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

#if IS_ENABLED(CONFIG_IOMMU_API) && defined(CONFIG_INTEL_IOMMU)
	/* HACK to disable iommu for the fake device; force identity mapping */
	pdev->dev.iommu = &fake_iommu;
#endif
	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		put_device(&pdev->dev);
		return NULL;
	}

	i915 = devm_drm_dev_alloc(&pdev->dev, &mock_driver,
				  struct drm_i915_private, drm);
	if (IS_ERR(i915)) {
		pr_err("Failed to allocate mock GEM device: err=%ld\n", PTR_ERR(i915));
		devres_release_group(&pdev->dev, NULL);
		put_device(&pdev->dev);

		return NULL;
	}

	pci_set_drvdata(pdev, i915);

	dev_pm_domain_set(&pdev->dev, &pm_domain);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	if (pm_runtime_enabled(&pdev->dev))
		WARN_ON(pm_runtime_get_sync(&pdev->dev));


	i915_params_copy(&i915->params, &i915_modparams);

	intel_runtime_pm_init_early(&i915->runtime_pm);

	/* Using the global GTT may ask questions about KMS users, so prepare */
	drm_mode_config_init(&i915->drm);

	mkwrite_device_info(i915)->graphics_ver = -1;

	mkwrite_device_info(i915)->page_sizes =
		I915_GTT_PAGE_SIZE_4K |
		I915_GTT_PAGE_SIZE_64K |
		I915_GTT_PAGE_SIZE_2M;

	mkwrite_device_info(i915)->memory_regions = REGION_SMEM;
	intel_memory_regions_hw_probe(i915);

	mock_uncore_init(&i915->uncore, i915);

	i915_gem_init__mm(i915);
	intel_gt_init_early(&i915->gt, i915);
	atomic_inc(&i915->gt.wakeref.count); /* disable; no hw support */
	i915->gt.awake = -ENODEV;

	ret = intel_region_ttm_device_init(i915);
	if (ret)
		goto err_ttm;

	i915->wq = alloc_ordered_workqueue("mock", 0);
	if (!i915->wq)
		goto err_drv;

	mock_init_contexts(i915);

	mock_init_ggtt(i915, &i915->ggtt);
	i915->gt.vm = i915_vm_get(&i915->ggtt.vm);

	mkwrite_device_info(i915)->platform_engine_mask = BIT(0);
	i915->gt.info.engine_mask = BIT(0);

	i915->gt.engine[RCS0] = mock_engine(i915, "mock", RCS0);
	if (!i915->gt.engine[RCS0])
		goto err_unlock;

	if (mock_engine_init(i915->gt.engine[RCS0]))
		goto err_context;

	__clear_bit(I915_WEDGED, &i915->gt.reset.flags);
	intel_engines_driver_register(i915);

	i915->do_release = true;
	ida_init(&i915->selftest.mock_region_instances);

	return i915;

err_context:
	intel_gt_driver_remove(&i915->gt);
err_unlock:
	destroy_workqueue(i915->wq);
err_drv:
	intel_region_ttm_device_fini(i915);
err_ttm:
	intel_gt_driver_late_release(&i915->gt);
	intel_memory_regions_driver_release(i915);
	drm_mode_config_cleanup(&i915->drm);
	mock_destroy_device(i915);

	return NULL;
}

void mock_destroy_device(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;

	devres_release_group(dev, NULL);
	put_device(dev);
}
