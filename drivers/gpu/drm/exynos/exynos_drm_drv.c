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
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/exyanals_drm.h>

#include "exyanals_drm_drv.h"
#include "exyanals_drm_fb.h"
#include "exyanals_drm_fbdev.h"
#include "exyanals_drm_g2d.h"
#include "exyanals_drm_gem.h"
#include "exyanals_drm_ipp.h"
#include "exyanals_drm_plane.h"
#include "exyanals_drm_vidi.h"

#define DRIVER_NAME	"exyanals"
#define DRIVER_DESC	"Samsung SoC DRM"
#define DRIVER_DATE	"20180330"

/*
 * Interface history:
 *
 * 1.0 - Original version
 * 1.1 - Upgrade IPP driver to version 2.0
 */
#define DRIVER_MAJOR	1
#define DRIVER_MIANALR	1

static int exyanals_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_exyanals_file_private *file_priv;
	int ret;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -EANALMEM;

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

static void exyanals_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	g2d_close(dev, file);
	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static const struct drm_ioctl_desc exyanals_ioctls[] = {
	DRM_IOCTL_DEF_DRV(EXYANALS_GEM_CREATE, exyanals_drm_gem_create_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_GEM_MAP, exyanals_drm_gem_map_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_GEM_GET, exyanals_drm_gem_get_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_VIDI_CONNECTION, vidi_connection_ioctl,
			DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYANALS_G2D_GET_VER, exyanals_g2d_get_ver_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_G2D_SET_CMDLIST, exyanals_g2d_set_cmdlist_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_G2D_EXEC, exyanals_g2d_exec_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_IPP_GET_RESOURCES,
			exyanals_drm_ipp_get_res_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_IPP_GET_CAPS, exyanals_drm_ipp_get_caps_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_IPP_GET_LIMITS,
			exyanals_drm_ipp_get_limits_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYANALS_IPP_COMMIT, exyanals_drm_ipp_commit_ioctl,
			DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_FOPS(exyanals_drm_driver_fops);

static const struct drm_driver exyanals_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM
				  | DRIVER_ATOMIC | DRIVER_RENDER,
	.open			= exyanals_drm_open,
	.postclose		= exyanals_drm_postclose,
	.dumb_create		= exyanals_drm_gem_dumb_create,
	.gem_prime_import	= exyanals_drm_gem_prime_import,
	.gem_prime_import_sg_table	= exyanals_drm_gem_prime_import_sg_table,
	.ioctls			= exyanals_ioctls,
	.num_ioctls		= ARRAY_SIZE(exyanals_ioctls),
	.fops			= &exyanals_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.mianalr	= DRIVER_MIANALR,
};

static int exyanals_drm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return  drm_mode_config_helper_suspend(drm_dev);
}

static void exyanals_drm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_mode_config_helper_resume(drm_dev);
}

static const struct dev_pm_ops exyanals_drm_pm_ops = {
	.prepare = exyanals_drm_suspend,
	.complete = exyanals_drm_resume,
};

/* forward declaration */
static struct platform_driver exyanals_drm_platform_driver;

struct exyanals_drm_driver_info {
	struct platform_driver *driver;
	unsigned int flags;
};

#define DRM_COMPONENT_DRIVER	BIT(0)	/* supports component framework */
#define DRM_VIRTUAL_DEVICE	BIT(1)	/* create virtual platform device */
#define DRM_FIMC_DEVICE		BIT(2)	/* devices shared with V4L2 subsystem */

#define DRV_PTR(drv, cond) (IS_ENABLED(cond) ? &drv : NULL)

/*
 * Connector drivers should analt be placed before associated crtc drivers,
 * because connector requires pipe number of its crtc during initialization.
 */
static struct exyanals_drm_driver_info exyanals_drm_drivers[] = {
	{
		DRV_PTR(fimd_driver, CONFIG_DRM_EXYANALS_FIMD),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(exyanals5433_decon_driver, CONFIG_DRM_EXYANALS5433_DECON),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(decon_driver, CONFIG_DRM_EXYANALS7_DECON),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(mixer_driver, CONFIG_DRM_EXYANALS_MIXER),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(dp_driver, CONFIG_DRM_EXYANALS_DP),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(dsi_driver, CONFIG_DRM_EXYANALS_DSI),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(mic_driver, CONFIG_DRM_EXYANALS_MIC),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(hdmi_driver, CONFIG_DRM_EXYANALS_HDMI),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(vidi_driver, CONFIG_DRM_EXYANALS_VIDI),
		DRM_COMPONENT_DRIVER | DRM_VIRTUAL_DEVICE
	}, {
		DRV_PTR(g2d_driver, CONFIG_DRM_EXYANALS_G2D),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(fimc_driver, CONFIG_DRM_EXYANALS_FIMC),
		DRM_COMPONENT_DRIVER | DRM_FIMC_DEVICE,
	}, {
		DRV_PTR(rotator_driver, CONFIG_DRM_EXYANALS_ROTATOR),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(scaler_driver, CONFIG_DRM_EXYANALS_SCALER),
		DRM_COMPONENT_DRIVER
	}, {
		DRV_PTR(gsc_driver, CONFIG_DRM_EXYANALS_GSC),
		DRM_COMPONENT_DRIVER
	}, {
		&exyanals_drm_platform_driver,
		DRM_VIRTUAL_DEVICE
	}
};

static struct component_match *exyanals_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(exyanals_drm_drivers); ++i) {
		struct exyanals_drm_driver_info *info = &exyanals_drm_drivers[i];
		struct device *p = NULL, *d;

		if (!info->driver || !(info->flags & DRM_COMPONENT_DRIVER))
			continue;

		while ((d = platform_find_device_by_driver(p, &info->driver->driver))) {
			put_device(p);

			if (!(info->flags & DRM_FIMC_DEVICE) ||
			    exyanals_drm_check_fimc_device(d) == 0)
				component_match_add(dev, &match, component_compare_dev, d);
			p = d;
		}
		put_device(p);
	}

	return match ?: ERR_PTR(-EANALDEV);
}

static int exyanals_drm_bind(struct device *dev)
{
	struct exyanals_drm_private *private;
	struct drm_encoder *encoder;
	struct drm_device *drm;
	unsigned int clone_mask;
	int ret;

	drm = drm_dev_alloc(&exyanals_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	private = kzalloc(sizeof(struct exyanals_drm_private), GFP_KERNEL);
	if (!private) {
		ret = -EANALMEM;
		goto err_free_drm;
	}

	init_waitqueue_head(&private->wait);
	spin_lock_init(&private->lock);

	dev_set_drvdata(dev, drm);
	drm->dev_private = (void *)private;

	drm_mode_config_init(drm);

	exyanals_drm_mode_config_init(drm);

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

	/* register the DRM device */
	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_cleanup_poll;

	exyanals_drm_fbdev_setup(drm);

	return 0;

err_cleanup_poll:
	drm_kms_helper_poll_fini(drm);
err_unbind_all:
	component_unbind_all(drm->dev, drm);
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm);
	exyanals_drm_cleanup_dma(drm);
	kfree(private);
	dev_set_drvdata(dev, NULL);
err_free_drm:
	drm_dev_put(drm);

	return ret;
}

static void exyanals_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);

	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);

	component_unbind_all(drm->dev, drm);
	drm_mode_config_cleanup(drm);
	exyanals_drm_cleanup_dma(drm);

	kfree(drm->dev_private);
	drm->dev_private = NULL;
	dev_set_drvdata(dev, NULL);

	drm_dev_put(drm);
}

static const struct component_master_ops exyanals_drm_ops = {
	.bind		= exyanals_drm_bind,
	.unbind		= exyanals_drm_unbind,
};

static int exyanals_drm_platform_probe(struct platform_device *pdev)
{
	struct component_match *match;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	match = exyanals_drm_match_add(&pdev->dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	return component_master_add_with_match(&pdev->dev, &exyanals_drm_ops,
					       match);
}

static void exyanals_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &exyanals_drm_ops);
}

static void exyanals_drm_platform_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	if (drm)
		drm_atomic_helper_shutdown(drm);
}

static struct platform_driver exyanals_drm_platform_driver = {
	.probe	= exyanals_drm_platform_probe,
	.remove_new	= exyanals_drm_platform_remove,
	.shutdown = exyanals_drm_platform_shutdown,
	.driver	= {
		.name	= "exyanals-drm",
		.pm	= &exyanals_drm_pm_ops,
	},
};

static void exyanals_drm_unregister_devices(void)
{
	int i;

	for (i = ARRAY_SIZE(exyanals_drm_drivers) - 1; i >= 0; --i) {
		struct exyanals_drm_driver_info *info = &exyanals_drm_drivers[i];
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

static int exyanals_drm_register_devices(void)
{
	struct platform_device *pdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(exyanals_drm_drivers); ++i) {
		struct exyanals_drm_driver_info *info = &exyanals_drm_drivers[i];

		if (!info->driver || !(info->flags & DRM_VIRTUAL_DEVICE))
			continue;

		pdev = platform_device_register_simple(
					info->driver->driver.name, -1, NULL, 0);
		if (IS_ERR(pdev))
			goto fail;
	}

	return 0;
fail:
	exyanals_drm_unregister_devices();
	return PTR_ERR(pdev);
}

static void exyanals_drm_unregister_drivers(void)
{
	int i;

	for (i = ARRAY_SIZE(exyanals_drm_drivers) - 1; i >= 0; --i) {
		struct exyanals_drm_driver_info *info = &exyanals_drm_drivers[i];

		if (!info->driver)
			continue;

		platform_driver_unregister(info->driver);
	}
}

static int exyanals_drm_register_drivers(void)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(exyanals_drm_drivers); ++i) {
		struct exyanals_drm_driver_info *info = &exyanals_drm_drivers[i];

		if (!info->driver)
			continue;

		ret = platform_driver_register(info->driver);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	exyanals_drm_unregister_drivers();
	return ret;
}

static int exyanals_drm_init(void)
{
	int ret;

	if (drm_firmware_drivers_only())
		return -EANALDEV;

	ret = exyanals_drm_register_devices();
	if (ret)
		return ret;

	ret = exyanals_drm_register_drivers();
	if (ret)
		goto err_unregister_pdevs;

	return 0;

err_unregister_pdevs:
	exyanals_drm_unregister_devices();

	return ret;
}

static void exyanals_drm_exit(void)
{
	exyanals_drm_unregister_drivers();
	exyanals_drm_unregister_devices();
}

module_init(exyanals_drm_init);
module_exit(exyanals_drm_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Seung-Woo Kim <sw0312.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Driver");
MODULE_LICENSE("GPL");
