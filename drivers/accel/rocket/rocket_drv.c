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

#include "rocket_drv.h"
#include "rocket_gem.h"
#include "rocket_job.h"

/*
 * Facade device, used to expose a single DRM device to userspace, that
 * schedules jobs to any RKNN cores in the system.
 */
static struct platform_device *drm_dev;
static struct rocket_device *rdev;

static void
rocket_iommu_domain_destroy(struct kref *kref)
{
	struct rocket_iommu_domain *domain = container_of(kref, struct rocket_iommu_domain, kref);

	iommu_domain_free(domain->domain);
	domain->domain = NULL;
	kfree(domain);
}

static struct rocket_iommu_domain*
rocket_iommu_domain_create(struct device *dev)
{
	struct rocket_iommu_domain *domain = kmalloc(sizeof(*domain), GFP_KERNEL);
	void *err;

	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->domain = iommu_paging_domain_alloc(dev);
	if (IS_ERR(domain->domain)) {
		err = ERR_CAST(domain->domain);
		kfree(domain);
		return err;
	}
	kref_init(&domain->kref);

	return domain;
}

struct rocket_iommu_domain *
rocket_iommu_domain_get(struct rocket_file_priv *rocket_priv)
{
	kref_get(&rocket_priv->domain->kref);
	return rocket_priv->domain;
}

void
rocket_iommu_domain_put(struct rocket_iommu_domain *domain)
{
	kref_put(&domain->kref, rocket_iommu_domain_destroy);
}

static int
rocket_open(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_device *rdev = to_rocket_device(dev);
	struct rocket_file_priv *rocket_priv;
	u64 start, end;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	rocket_priv = kzalloc(sizeof(*rocket_priv), GFP_KERNEL);
	if (!rocket_priv) {
		ret = -ENOMEM;
		goto err_put_mod;
	}

	rocket_priv->rdev = rdev;
	rocket_priv->domain = rocket_iommu_domain_create(rdev->cores[0].dev);
	if (IS_ERR(rocket_priv->domain)) {
		ret = PTR_ERR(rocket_priv->domain);
		goto err_free;
	}

	file->driver_priv = rocket_priv;

	start = rocket_priv->domain->domain->geometry.aperture_start;
	end = rocket_priv->domain->domain->geometry.aperture_end;
	drm_mm_init(&rocket_priv->mm, start, end - start + 1);
	mutex_init(&rocket_priv->mm_lock);

	ret = rocket_job_open(rocket_priv);
	if (ret)
		goto err_mm_takedown;

	return 0;

err_mm_takedown:
	mutex_destroy(&rocket_priv->mm_lock);
	drm_mm_takedown(&rocket_priv->mm);
	rocket_iommu_domain_put(rocket_priv->domain);
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
	mutex_destroy(&rocket_priv->mm_lock);
	drm_mm_takedown(&rocket_priv->mm);
	rocket_iommu_domain_put(rocket_priv->domain);
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

static int rocket_probe(struct platform_device *pdev)
{
	if (rdev == NULL) {
		/* First core probing, initialize DRM device. */
		rdev = rocket_device_init(drm_dev, &rocket_drm_driver);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to initialize rocket device\n");
			return PTR_ERR(rdev);
		}
	}

	unsigned int core = rdev->num_cores;

	dev_set_drvdata(&pdev->dev, rdev);

	rdev->cores[core].rdev = rdev;
	rdev->cores[core].dev = &pdev->dev;
	rdev->cores[core].index = core;

	rdev->num_cores++;

	return rocket_core_init(&rdev->cores[core]);
}

static void rocket_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	for (unsigned int core = 0; core < rdev->num_cores; core++) {
		if (rdev->cores[core].dev == dev) {
			rocket_core_fini(&rdev->cores[core]);
			rdev->num_cores--;
			break;
		}
	}

	if (rdev->num_cores == 0) {
		/* Last core removed, deinitialize DRM device. */
		rocket_device_fini(rdev);
		rdev = NULL;
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

	for (unsigned int core = 0; core < rdev->num_cores; core++) {
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
