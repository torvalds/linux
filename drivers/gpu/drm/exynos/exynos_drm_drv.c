// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_g2d.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_ipp.h"
#include "exynos_drm_plane.h"
#include "exynos_drm_vidi.h"

#define DRIVER_NAME	"exynos"
#define DRIVER_DESC	"Samsung SoC DRM"
#define DRIVER_DATE	"20180330"

/*
 * Interface history:
 *
 * 1.0 - Original version
 * 1.1 - Upgrade IPP driver to version 2.0
 */
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	1

static int exynos_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv;
	int ret;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;
	ret = g2d_open(dev, file);
	if (ret)
		goto err_file_priv_free;

	return ret;

err_file_priv_free:
	kfree(file_priv);
	file->driver_priv = NULL;
	return ret;
}

static void exynos_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	g2d_close(dev, file);
	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static const struct drm_ioctl_desc exynos_ioctls[] = {
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_CREATE, exynos_drm_gem_create_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_MAP, exynos_drm_gem_map_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_GET, exynos_drm_gem_get_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_VIDI_CONNECTION, vidi_connection_ioctl,
			DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_GET_VER, exynos_g2d_get_ver_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_SET_CMDLIST, exynos_g2d_set_cmdlist_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_EXEC, exynos_g2d_exec_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_GET_RESOURCES,
			exynos_drm_ipp_get_res_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_GET_CAPS, exynos_drm_ipp_get_caps_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_GET_LIMITS,
			exynos_drm_ipp_get_limits_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_COMMIT, exynos_drm_ipp_commit_ioctl,
			DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_FOPS(exynos_drm_driver_fops);

static const struct drm_driver exynos_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM
				  | DRIVER_ATOMIC | DRIVER_RENDER,
	.open			= exynos_drm_open,
	.lastclose		= drm_fb_helper_lastclose,
	.postclose		= exynos_drm_postclose,
	.dumb_create		= exynos_drm_gem_dumb_create,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= exynos_drm_gem_prime_import,
	.gem_prime_import_sg_table	= exynos_drm_gem_prime_import_sg_table,
	.gem_prime_mmap		= drm_gem_prime_mmap,
	.ioctls			= exynos_ioctls,
	.num_ioctls		= ARRAY_SIZE(exynos_ioctls),
	.fops			= &exynos_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int exynos_drm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return  drm_mode_config_helper_suspend(drm_dev);
}

static void exynos_drm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_mode_config_helper_resume(drm_dev);
}

static const struct dev_pm_ops exynos_drm_pm_ops = {
	.prepare = exynos_drm_suspend,
	.complete = exynos_drm_resume,
};

/* forward declaration */
static struct platform_driver exynos_drm_platform_driver;

struct exynos_drm_driver_info {
	struct platform_driver *driver;
	unsigned int flags;
};

#define DRM_COMPONENT_DRIVER	BIT(0)	/* supports component framework */
#define DRM_VIRTUAL_DEVICE	BIT(1)	/* create virtual platform device */
#define DRM_FIMC_DEVICE		BIT(2)	/* devices shared with V4L2 subsystem */

#define DRV_PTR(drv, cond) (IS_ENABLED(cond) ? &drv : NULL)

/*
 * Connector drivers should not be placed before associated crtc drivers,
 * because connector requires pipe number of its crtc during initialization.
 */
static struct exynos_drm_driver_info exynos_drm_drivers[] = {
	{
		DRV_PTR(fimd_driver, CONFIG_DRM_EXYNOS_FIMD),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(exynos5433_decon_driver, CONFIG_DRM_EXYNOS5433_DECON),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(decon_driver, CONFIG_DRM_EXYNOS7_DECON),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(mixer_driver, CONFIG_DRM_EXYNOS_MIXER),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(mic_driver, CONFIG_DRM_EXYNOS_MIC),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(dp_driver, CONFIG_DRM_EXYNOS_DP),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(dsi_driver, CONFIG_DRM_EXYNOS_DSI),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(hdmi_driver, CONFIG_DRM_EXYNOS_HDMI),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(vidi_driver, CONFIG_DRM_EXYNOS_VIDI),
		DRM_COMPONENT_DRIVER | DRM_VIRTUAL_DEVICE
	}, {
		DRV_PTR(g2d_driver, CONFIG_DRM_EXYNOS_G2D),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(fimc_driver, CONFIG_DRM_EXYNOS_FIMC),
		DRM_COMPONENT_DRIVER | DRM_FIMC_DEVICE,
	}, {
		DRV_PTR(rotator_driver, CONFIG_DRM_EXYNOS_ROTATOR),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(scaler_driver, CONFIG_DRM_EXYNOS_SCALER),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(gsc_driver, CONFIG_DRM_EXYNOS_GSC),
		DRM_COMPONENT_DRIVER
	}, {
		&exynos_drm_platform_driver,
		DRM_VIRTUAL_DEVICE
	}
};

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static struct component_match *exynos_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(exynos_drm_drivers); ++i) {
		struct exynos_drm_driver_info *info = &exynos_drm_drivers[i];
		struct device *p = NULL, *d;

		if (!info->driver || !(info->flags & DRM_COMPONENT_DRIVER))
			continue;

		while ((d = platform_find_device_by_driver(p, &info->driver->driver))) {
			put_device(p);

			if (!(info->flags & DRM_FIMC_DEVICE) ||
			    exynos_drm_check_fimc_device(d) == 0)
				component_match_add(dev, &match,
						    compare_dev, d);
			p = d;
		}
		put_device(p);
	}

	return match ?: ERR_PTR(-ENODEV);
}

static int exynos_drm_bind(struct device *dev)
{
	struct exynos_drm_private *private;
	struct drm_encoder *encoder;
	struct drm_device *drm;
	unsigned int clone_mask;
	int ret;

	drm = drm_dev_alloc(&exynos_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	private = kzalloc(sizeof(struct exynos_drm_private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free_drm;
	}

	init_waitqueue_head(&private->wait);
	spin_lock_init(&private->lock);

	dev_set_drvdata(dev, drm);
	drm->dev_private = (void *)private;

	drm_mode_config_init(drm);

	exynos_drm_mode_config_init(drm);

	/* setup possible_clones. */
	clone_mask = 0;
	list_for_each_entry(encoder, &drm->mode_config.encoder_list, head)
		clone_mask |= drm_encoder_mask(encoder);

	list_for_each_entry(encoder, &drm->mode_config.encoder_list, head)
		encoder->possible_clones = clone_mask;

	/* Try to bind all sub drivers. */
	ret = component_bind_all(drm->dev, drm);
	if (ret)
		goto err_mode_config_cleanup;

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	drm_mode_config_reset(drm);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm);

	ret = exynos_drm_fbdev_init(drm);
	if (ret)
		goto err_cleanup_poll;

	/* register the DRM device */
	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_cleanup_fbdev;

	return 0;

err_cleanup_fbdev:
	exynos_drm_fbdev_fini(drm);
err_cleanup_poll:
	drm_kms_helper_poll_fini(drm);
err_unbind_all:
	component_unbind_all(drm->dev, drm);
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm);
	exynos_drm_cleanup_dma(drm);
	kfree(private);
err_free_drm:
	drm_dev_put(drm);

	return ret;
}

static void exynos_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);

	exynos_drm_fbdev_fini(drm);
	drm_kms_helper_poll_fini(drm);

	component_unbind_all(drm->dev, drm);
	drm_mode_config_cleanup(drm);
	exynos_drm_cleanup_dma(drm);

	kfree(drm->dev_private);
	drm->dev_private = NULL;
	dev_set_drvdata(dev, NULL);

	drm_dev_put(drm);
}

static const struct component_master_ops exynos_drm_ops = {
	.bind		= exynos_drm_bind,
	.unbind		= exynos_drm_unbind,
};

static int exynos_drm_platform_probe(struct platform_device *pdev)
{
	struct component_match *match;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	match = exynos_drm_match_add(&pdev->dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	return component_master_add_with_match(&pdev->dev, &exynos_drm_ops,
					       match);
}

static int exynos_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &exynos_drm_ops);
	return 0;
}

static struct platform_driver exynos_drm_platform_driver = {
	.probe	= exynos_drm_platform_probe,
	.remove	= exynos_drm_platform_remove,
	.driver	= {
		.name	= "exynos-drm",
		.pm	= &exynos_drm_pm_ops,
	},
};

static void exynos_drm_unregister_devices(void)
{
	int i;

	for (i = ARRAY_SIZE(exynos_drm_drivers) - 1; i >= 0; --i) {
		struct exynos_drm_driver_info *info = &exynos_drm_drivers[i];
		struct device *dev;

		if (!info->driver || !(info->flags & DRM_VIRTUAL_DEVICE))
			continue;

		while ((dev = platform_find_device_by_driver(NULL,
						&info->driver->driver))) {
			put_device(dev);
			platform_device_unregister(to_platform_device(dev));
		}
	}
}

static int exynos_drm_register_devices(void)
{
	struct platform_device *pdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(exynos_drm_drivers); ++i) {
		struct exynos_drm_driver_info *info = &exynos_drm_drivers[i];

		if (!info->driver || !(info->flags & DRM_VIRTUAL_DEVICE))
			continue;

		pdev = platform_device_register_simple(
					info->driver->driver.name, -1, NULL, 0);
		if (IS_ERR(pdev))
			goto fail;
	}

	return 0;
fail:
	exynos_drm_unregister_devices();
	return PTR_ERR(pdev);
}

static void exynos_drm_unregister_drivers(void)
{
	int i;

	for (i = ARRAY_SIZE(exynos_drm_drivers) - 1; i >= 0; --i) {
		struct exynos_drm_driver_info *info = &exynos_drm_drivers[i];

		if (!info->driver)
			continue;

		platform_driver_unregister(info->driver);
	}
}

static int exynos_drm_register_drivers(void)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(exynos_drm_drivers); ++i) {
		struct exynos_drm_driver_info *info = &exynos_drm_drivers[i];

		if (!info->driver)
			continue;

		ret = platform_driver_register(info->driver);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	exynos_drm_unregister_drivers();
	return ret;
}

static int exynos_drm_init(void)
{
	int ret;

	ret = exynos_drm_register_devices();
	if (ret)
		return ret;

	ret = exynos_drm_register_drivers();
	if (ret)
		goto err_unregister_pdevs;

	return 0;

err_unregister_pdevs:
	exynos_drm_unregister_devices();

	return ret;
}

static void exynos_drm_exit(void)
{
	exynos_drm_unregister_drivers();
	exynos_drm_unregister_devices();
}

module_init(exynos_drm_init);
module_exit(exynos_drm_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Seung-Woo Kim <sw0312.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Driver");
MODULE_LICENSE("GPL");
