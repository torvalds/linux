// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_drv.h>
#include <linux/array_size.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "rocket_device.h"

struct rocket_device *rocket_device_init(struct platform_device *pdev,
					 const struct drm_driver *rocket_drm_driver)
{
	struct device *dev = &pdev->dev;
	struct device_node *core_node;
	struct rocket_device *rdev;
	struct drm_device *ddev;
	unsigned int num_cores = 0;
	int err;

	rdev = devm_drm_dev_alloc(dev, rocket_drm_driver, struct rocket_device, ddev);
	if (IS_ERR(rdev))
		return rdev;

	ddev = &rdev->ddev;
	dev_set_drvdata(dev, rdev);

	for_each_compatible_node(core_node, NULL, "rockchip,rk3588-rknn-core")
		if (of_device_is_available(core_node))
			num_cores++;

	rdev->cores = devm_kcalloc(dev, num_cores, sizeof(*rdev->cores), GFP_KERNEL);
	if (!rdev->cores)
		return ERR_PTR(-ENOMEM);

	dma_set_max_seg_size(dev, UINT_MAX);

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));
	if (err)
		return ERR_PTR(err);

	err = devm_mutex_init(dev, &rdev->sched_lock);
	if (err)
		return ERR_PTR(-ENOMEM);

	err = drm_dev_register(ddev, 0);
	if (err)
		return ERR_PTR(err);

	return rdev;
}

void rocket_device_fini(struct rocket_device *rdev)
{
	WARN_ON(rdev->num_cores > 0);

	drm_dev_unregister(&rdev->ddev);
}
