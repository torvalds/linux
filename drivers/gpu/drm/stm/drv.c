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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
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

static int stm_gem_cma_dumb_create(struct drm_file *file,
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

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

DEFINE_DRM_GEM_CMA_FOPS(drv_driver_fops);

static struct drm_driver drv_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME |
			   DRIVER_ATOMIC,
	.name = "stm",
	.desc = "STMicroelectronics SoC DRM",
	.date = "20170330",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.fops = &drv_driver_fops,
	.dumb_create = stm_gem_cma_dumb_create,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
	.get_scanout_position = ltdc_crtc_scanoutpos,
	.get_vblank_timestamp = drm_calc_vbltimestamp_from_scanoutpos,
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

	drm_mode_config_init(ddev);

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

	ret = ltdc_load(ddev);
	if (ret)
		goto err;

	drm_mode_config_reset(ddev);
	drm_kms_helper_poll_init(ddev);

	platform_set_drvdata(pdev, ddev);

	return 0;
err:
	drm_mode_config_cleanup(ddev);
	return ret;
}

static void drv_unload(struct drm_device *ddev)
{
	DRM_DEBUG("%s\n", __func__);

	drm_kms_helper_poll_fini(ddev);
	ltdc_unload(ddev);
	drm_mode_config_cleanup(ddev);
}

static int stm_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_device *ddev;
	int ret;

	DRM_DEBUG("%s\n", __func__);

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
	},
};

module_platform_driver(stm_drm_platform_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_AUTHOR("Mickael Reulier <mickael.reulier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST DRM LTDC driver");
MODULE_LICENSE("GPL v2");
