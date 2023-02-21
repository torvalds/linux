// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Mickael Reulier <mickael.reulier@st.com>
 */

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "ltdc.h"

#define STM_MAX_FB_WIDTH	2048
#define STM_MAX_FB_HEIGHT	2048 /* same as width to handle orientation */

static const struct drm_mode_config_funcs drv_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int stm_gem_dma_dumb_create(struct drm_file *file,
				   struct drm_device *dev,
				   struct drm_mode_create_dumb *args)
{
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/*
	 * in order to optimize data transfer, pitch is aligned on
	 * 128 bytes, height is aligned on 4 bytes
	 */
	args->pitch = roundup(min_pitch, 128);
	args->height = roundup(args->height, 4);

	return drm_gem_dma_dumb_create_internal(file, dev, args);
}

DEFINE_DRM_GEM_DMA_FOPS(drv_driver_fops);

static const struct drm_driver drv_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.name = "stm",
	.desc = "STMicroelectronics SoC DRM",
	.date = "20170330",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.fops = &drv_driver_fops,
	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(stm_gem_dma_dumb_create),
};

static int drv_load(struct drm_device *ddev)
{
	struct platform_device *pdev = to_platform_device(ddev->dev);
	struct ltdc_device *ldev;
	int ret;

	DRM_DEBUG("%s\n", __func__);

	ldev = devm_kzalloc(ddev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	ddev->dev_private = (void *)ldev;

	ret = drmm_mode_config_init(ddev);
	if (ret)
		return ret;

	/*
	 * set max width and height as default value.
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	ddev->mode_config.min_width = 0;
	ddev->mode_config.min_height = 0;
	ddev->mode_config.max_width = STM_MAX_FB_WIDTH;
	ddev->mode_config.max_height = STM_MAX_FB_HEIGHT;
	ddev->mode_config.funcs = &drv_mode_config_funcs;
	ddev->mode_config.normalize_zpos = true;

	ret = ltdc_load(ddev);
	if (ret)
		return ret;

	drm_mode_config_reset(ddev);
	drm_kms_helper_poll_init(ddev);

	platform_set_drvdata(pdev, ddev);

	return 0;
}

static void drv_unload(struct drm_device *ddev)
{
	DRM_DEBUG("%s\n", __func__);

	drm_kms_helper_poll_fini(ddev);
	ltdc_unload(ddev);
}

static __maybe_unused int drv_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct ltdc_device *ldev = ddev->dev_private;
	struct drm_atomic_state *state;

	WARN_ON(ldev->suspend_state);

	state = drm_atomic_helper_suspend(ddev);
	if (IS_ERR(state))
		return PTR_ERR(state);

	ldev->suspend_state = state;
	pm_runtime_force_suspend(dev);

	return 0;
}

static __maybe_unused int drv_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct ltdc_device *ldev = ddev->dev_private;
	int ret;

	if (WARN_ON(!ldev->suspend_state))
		return -ENOENT;

	pm_runtime_force_resume(dev);
	ret = drm_atomic_helper_resume(ddev, ldev->suspend_state);
	if (ret)
		pm_runtime_force_suspend(dev);

	ldev->suspend_state = NULL;

	return ret;
}

static __maybe_unused int drv_runtime_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("\n");
	ltdc_suspend(ddev);

	return 0;
}

static __maybe_unused int drv_runtime_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("\n");
	return ltdc_resume(ddev);
}

static const struct dev_pm_ops drv_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(drv_suspend, drv_resume)
	SET_RUNTIME_PM_OPS(drv_runtime_suspend,
			   drv_runtime_resume, NULL)
};

static int stm_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_device *ddev;
	int ret;

	DRM_DEBUG("%s\n", __func__);

	ret = drm_aperture_remove_framebuffers(false, &drv_driver);
	if (ret)
		return ret;

	dma_set_coherent_mask(dev, DMA_BIT_MASK(32));

	ddev = drm_dev_alloc(&drv_driver, dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ret = drv_load(ddev);
	if (ret)
		goto err_put;

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_put;

	drm_fbdev_generic_setup(ddev, 16);

	return 0;

err_put:
	drm_dev_put(ddev);

	return ret;
}

static int stm_drm_platform_remove(struct platform_device *pdev)
{
	struct drm_device *ddev = platform_get_drvdata(pdev);

	DRM_DEBUG("%s\n", __func__);

	drm_dev_unregister(ddev);
	drv_unload(ddev);
	drm_dev_put(ddev);

	return 0;
}

static const struct of_device_id drv_dt_ids[] = {
	{ .compatible = "st,stm32-ltdc"},
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, drv_dt_ids);

static struct platform_driver stm_drm_platform_driver = {
	.probe = stm_drm_platform_probe,
	.remove = stm_drm_platform_remove,
	.driver = {
		.name = "stm32-display",
		.of_match_table = drv_dt_ids,
		.pm = &drv_pm_ops,
	},
};

drm_module_platform_driver(stm_drm_platform_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_AUTHOR("Mickael Reulier <mickael.reulier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST DRM LTDC driver");
MODULE_LICENSE("GPL v2");
