// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/rocket_accel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "rocket_device.h"
#include "rocket_drv.h"
#include "rocket_gem.h"
#include "rocket_job.h"

/*
 * Facade device, used to expose a single DRM device to userspace, that
 * schedules jobs to any RKNN cores in the system.
 */
static struct platform_device *drm_dev;
static struct rocket_device *rdev;
static void *rdev_group;

static void
rocket_vm_destroy(struct kref *kref)
{
	struct rocket_vm *vm = container_of(kref, struct rocket_vm, kref);

	drm_mm_takedown(&vm->mm);
	mutex_destroy(&vm->lock);
	iommu_domain_free(vm->domain);
	vm->domain = NULL;
	kfree(vm);
}

static struct rocket_vm *
rocket_vm_create(struct device *dev)
{
	struct rocket_vm *vm = kmalloc_obj(*vm);
	u64 start, end;
	void *err;

	if (!vm)
		return ERR_PTR(-ENOMEM);

	vm->domain = iommu_paging_domain_alloc(dev);
	if (IS_ERR(vm->domain)) {
		err = ERR_CAST(vm->domain);
		kfree(vm);
		return err;
	}

	start = vm->domain->geometry.aperture_start;
	end = vm->domain->geometry.aperture_end;
	drm_mm_init(&vm->mm, start, end - start + 1);
	mutex_init(&vm->lock);
	kref_init(&vm->kref);

	return vm;
}

struct rocket_vm *
rocket_vm_get(struct rocket_file_priv *rocket_priv)
{
	kref_get(&rocket_priv->vm->kref);
	return rocket_priv->vm;
}

void
rocket_vm_put(struct rocket_vm *vm)
{
	kref_put(&vm->kref, rocket_vm_destroy);
}

static int
rocket_open(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_device *rdev = to_rocket_device(dev);
	struct rocket_file_priv *rocket_priv;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	rocket_priv = kzalloc_obj(*rocket_priv);
	if (!rocket_priv) {
		ret = -ENOMEM;
		goto err_put_mod;
	}

	rocket_priv->rdev = rdev;
	rocket_priv->vm = rocket_vm_create(rdev->cores[0].dev);
	if (IS_ERR(rocket_priv->vm)) {
		ret = PTR_ERR(rocket_priv->vm);
		goto err_free;
	}

	file->driver_priv = rocket_priv;

	ret = rocket_job_open(rocket_priv);
	if (ret)
		goto err_vm_put;

	return 0;

err_vm_put:
	rocket_vm_put(rocket_priv->vm);
err_free:
	kfree(rocket_priv);
err_put_mod:
	module_put(THIS_MODULE);
	return ret;
}

static void
rocket_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_file_priv *rocket_priv = file->driver_priv;

	rocket_job_close(rocket_priv);
	rocket_vm_put(rocket_priv->vm);
	kfree(rocket_priv);
	module_put(THIS_MODULE);
}

static const struct drm_ioctl_desc rocket_drm_driver_ioctls[] = {
#define ROCKET_IOCTL(n, func) \
	DRM_IOCTL_DEF_DRV(ROCKET_##n, rocket_ioctl_##func, 0)

	ROCKET_IOCTL(CREATE_BO, create_bo),
	ROCKET_IOCTL(SUBMIT, submit),
	ROCKET_IOCTL(PREP_BO, prep_bo),
	ROCKET_IOCTL(FINI_BO, fini_bo),
};

DEFINE_DRM_ACCEL_FOPS(rocket_accel_driver_fops);

/*
 * Rocket driver version:
 * - 1.0 - initial interface
 */
static const struct drm_driver rocket_drm_driver = {
	.driver_features	= DRIVER_COMPUTE_ACCEL | DRIVER_GEM,
	.open			= rocket_open,
	.postclose		= rocket_postclose,
	.gem_create_object	= rocket_gem_create_object,
	.ioctls			= rocket_drm_driver_ioctls,
	.num_ioctls		= ARRAY_SIZE(rocket_drm_driver_ioctls),
	.fops			= &rocket_accel_driver_fops,
	.name			= "rocket",
	.desc			= "rocket DRM",
};

static int rocket_core_hw_index(struct device *dev)
{
	struct device_node *np;
	int index = 0;

	for_each_matching_node(np, dev->driver->of_match_table) {
		if (np == dev->of_node) {
			of_node_put(np);
			return index;
		}
		index++;
	}

	return -ENODEV;
}

static int rocket_probe(struct platform_device *pdev)
{
	int index = rocket_core_hw_index(&pdev->dev);
	int ret;

	if (index < 0)
		return index;

	if (rdev == NULL) {
		/* First core probing, initialize DRM device. */
		rdev_group = devres_open_group(&drm_dev->dev, NULL, GFP_KERNEL);
		if (!rdev_group)
			return -ENOMEM;
		rdev = rocket_device_init(drm_dev, &rocket_drm_driver);
		if (IS_ERR(rdev)) {
			int err = PTR_ERR(rdev);

			dev_err(&pdev->dev, "failed to initialize rocket device\n");
			rdev = NULL;
			devres_release_group(&drm_dev->dev, rdev_group);
			return err;
		}
		devres_close_group(&drm_dev->dev, rdev_group);
	}

	unsigned int core = rdev->num_cores;

	dev_set_drvdata(&pdev->dev, rdev);

	rdev->cores[core].rdev = rdev;
	rdev->cores[core].dev = &pdev->dev;
	rdev->cores[core].index = index;

	rdev->num_cores++;

	ret = rocket_core_init(&rdev->cores[core]);
	if (ret) {
		rdev->num_cores--;

		if (rdev->num_cores == 0) {
			rocket_device_fini(rdev);
			rdev = NULL;
			devres_release_group(&drm_dev->dev, rdev_group);
		}
	}

	return ret;
}

static int find_core_for_dev(struct device *dev);

static void rocket_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int core = find_core_for_dev(dev);

	if (core < 0)
		return;

	rocket_core_fini(&rdev->cores[core]);
	rdev->num_cores--;

	if (rdev->num_cores == 0) {
		/* Last core removed, deinitialize DRM device. */
		rocket_device_fini(rdev);
		rdev = NULL;
		devres_release_group(&drm_dev->dev, rdev_group);
	}
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "rockchip,rk3588-rknn-core" },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static int find_core_for_dev(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);

	for (unsigned int core = 0; core < rdev->max_cores; core++) {
		if (dev == rdev->cores[core].dev)
			return core;
	}

	return -1;
}

static int rocket_device_runtime_resume(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);
	int core = find_core_for_dev(dev);
	int err = 0;

	if (core < 0)
		return -ENODEV;

	err = clk_bulk_prepare_enable(ARRAY_SIZE(rdev->cores[core].clks), rdev->cores[core].clks);
	if (err) {
		dev_err(dev, "failed to enable (%d) clocks for core %d\n", err, core);
		return err;
	}

	return 0;
}

static int rocket_device_runtime_suspend(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);
	int core = find_core_for_dev(dev);

	if (core < 0)
		return -ENODEV;

	if (!rocket_job_is_idle(&rdev->cores[core]))
		return -EBUSY;

	clk_bulk_disable_unprepare(ARRAY_SIZE(rdev->cores[core].clks), rdev->cores[core].clks);

	return 0;
}

EXPORT_GPL_DEV_PM_OPS(rocket_pm_ops) = {
	RUNTIME_PM_OPS(rocket_device_runtime_suspend, rocket_device_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static struct platform_driver rocket_driver = {
	.probe = rocket_probe,
	.remove = rocket_remove,
	.driver	 = {
		.name = "rocket",
		.pm = pm_ptr(&rocket_pm_ops),
		.of_match_table = dt_match,
	},
};

static int __init rocket_register(void)
{
	drm_dev = platform_device_register_simple("rknn", -1, NULL, 0);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	return platform_driver_register(&rocket_driver);
}

static void __exit rocket_unregister(void)
{
	platform_driver_unregister(&rocket_driver);

	platform_device_unregister(drm_dev);
}

module_init(rocket_register);
module_exit(rocket_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DRM driver for the Rockchip NPU IP");
MODULE_AUTHOR("Tomeu Vizoso");
