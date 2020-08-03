// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hisilicon Kirin SoCs drm master driver
 *
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 *
 * Author:
 *	Xinliang Liu <z.liuxinliang@hisilicon.com>
 *	Xinliang Liu <xinliang.liu@linaro.org>
 *	Xinwei Kong <kong.kongxinwei@hisilicon.com>
 */

#include <linux/of_platform.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "kirin_drm_drv.h"

#define KIRIN_MAX_PLANE	2

struct kirin_drm_private {
	struct kirin_crtc crtc;
	struct kirin_plane planes[KIRIN_MAX_PLANE];
	void *hw_ctx;
};

static int kirin_drm_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
			       struct drm_plane *plane,
			       const struct kirin_drm_data *driver_data)
{
	struct device_node *port;
	int ret;

	/* set crtc port so that
	 * drm_of_find_possible_crtcs call works
	 */
	port = of_get_child_by_name(dev->dev->of_node, "port");
	if (!port) {
		DRM_ERROR("no port node found in %pOF\n", dev->dev->of_node);
		return -EINVAL;
	}
	of_node_put(port);
	crtc->port = port;

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					driver_data->crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("failed to init crtc.\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, driver_data->crtc_helper_funcs);

	return 0;
}

static int kirin_drm_plane_init(struct drm_device *dev, struct drm_plane *plane,
				enum drm_plane_type type,
				const struct kirin_drm_data *data)
{
	int ret = 0;

	ret = drm_universal_plane_init(dev, plane, 1, data->plane_funcs,
				       data->channel_formats,
				       data->channel_formats_cnt,
				       NULL, type, NULL);
	if (ret) {
		DRM_ERROR("fail to init plane, ch=%d\n", 0);
		return ret;
	}

	drm_plane_helper_add(plane, data->plane_helper_funcs);

	return 0;
}

static void kirin_drm_private_cleanup(struct drm_device *dev)
{
	struct kirin_drm_private *kirin_priv = dev->dev_private;
	struct kirin_drm_data *data;

	data = (struct kirin_drm_data *)of_device_get_match_data(dev->dev);
	if (data->cleanup_hw_ctx)
		data->cleanup_hw_ctx(kirin_priv->hw_ctx);

	devm_kfree(dev->dev, kirin_priv);
	dev->dev_private = NULL;
}

static int kirin_drm_private_init(struct drm_device *dev,
				  const struct kirin_drm_data *driver_data)
{
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct kirin_drm_private *kirin_priv;
	struct drm_plane *prim_plane;
	enum drm_plane_type type;
	void *ctx;
	int ret;
	u32 ch;

	kirin_priv = devm_kzalloc(dev->dev, sizeof(*kirin_priv), GFP_KERNEL);
	if (!kirin_priv) {
		DRM_ERROR("failed to alloc kirin_drm_private\n");
		return -ENOMEM;
	}

	ctx = driver_data->alloc_hw_ctx(pdev, &kirin_priv->crtc.base);
	if (IS_ERR(ctx)) {
		DRM_ERROR("failed to initialize kirin_priv hw ctx\n");
		return -EINVAL;
	}
	kirin_priv->hw_ctx = ctx;

	/*
	 * plane init
	 * TODO: Now only support primary plane, overlay planes
	 * need to do.
	 */
	for (ch = 0; ch < driver_data->num_planes; ch++) {
		if (ch == driver_data->prim_plane)
			type = DRM_PLANE_TYPE_PRIMARY;
		else
			type = DRM_PLANE_TYPE_OVERLAY;
		ret = kirin_drm_plane_init(dev, &kirin_priv->planes[ch].base,
					   type, driver_data);
		if (ret)
			return ret;
		kirin_priv->planes[ch].ch = ch;
		kirin_priv->planes[ch].hw_ctx = ctx;
	}

	/* crtc init */
	prim_plane = &kirin_priv->planes[driver_data->prim_plane].base;
	ret = kirin_drm_crtc_init(dev, &kirin_priv->crtc.base,
				  prim_plane, driver_data);
	if (ret)
		return ret;
	kirin_priv->crtc.hw_ctx = ctx;
	dev->dev_private = kirin_priv;

	return 0;
}

static int kirin_drm_kms_init(struct drm_device *dev,
			      const struct kirin_drm_data *driver_data)
{
	int ret;

	/* dev->mode_config initialization */
	drm_mode_config_init(dev);
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = driver_data->config_max_width;
	dev->mode_config.max_height = driver_data->config_max_width;
	dev->mode_config.funcs = driver_data->mode_config_funcs;

	/* display controller init */
	ret = kirin_drm_private_init(dev, driver_data);
	if (ret)
		goto err_mode_config_cleanup;

	/* bind and init sub drivers */
	ret = component_bind_all(dev->dev, dev);
	if (ret) {
		DRM_ERROR("failed to bind all component.\n");
		goto err_private_cleanup;
	}

	/* vblank init */
	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret) {
		DRM_ERROR("failed to initialize vblank.\n");
		goto err_unbind_all;
	}
	/* with irq_enabled = true, we can use the vblank feature. */
	dev->irq_enabled = true;

	/* reset all the states of crtc/plane/encoder/connector */
	drm_mode_config_reset(dev);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(dev);

	return 0;

err_unbind_all:
	component_unbind_all(dev->dev, dev);
err_private_cleanup:
	kirin_drm_private_cleanup(dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(dev);
	return ret;
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int kirin_drm_kms_cleanup(struct drm_device *dev)
{
	drm_kms_helper_poll_fini(dev);
	kirin_drm_private_cleanup(dev);
	drm_mode_config_cleanup(dev);

	return 0;
}

static int kirin_drm_bind(struct device *dev)
{
	struct kirin_drm_data *driver_data;
	struct drm_device *drm_dev;
	int ret;

	driver_data = (struct kirin_drm_data *)of_device_get_match_data(dev);
	if (!driver_data)
		return -EINVAL;

	drm_dev = drm_dev_alloc(driver_data->driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);
	dev_set_drvdata(dev, drm_dev);

	/* display controller init */
	ret = kirin_drm_kms_init(drm_dev, driver_data);
	if (ret)
		goto err_drm_dev_put;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_kms_cleanup;

	drm_fbdev_generic_setup(drm_dev, 32);

	return 0;

err_kms_cleanup:
	kirin_drm_kms_cleanup(drm_dev);
err_drm_dev_put:
	drm_dev_put(drm_dev);

	return ret;
}

static void kirin_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);
	kirin_drm_kms_cleanup(drm_dev);
	drm_dev_put(drm_dev);
}

static const struct component_master_ops kirin_drm_ops = {
	.bind = kirin_drm_bind,
	.unbind = kirin_drm_unbind,
};

static int kirin_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct component_match *match = NULL;
	struct device_node *remote;

	remote = of_graph_get_remote_node(np, 0, 0);
	if (!remote)
		return -ENODEV;

	drm_of_component_match_add(dev, &match, compare_of, remote);
	of_node_put(remote);

	return component_master_add_with_match(dev, &kirin_drm_ops, match);
}

static int kirin_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &kirin_drm_ops);
	return 0;
}

static const struct of_device_id kirin_drm_dt_ids[] = {
	{ .compatible = "hisilicon,hi6220-ade",
	  .data = &ade_driver_data,
	},
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, kirin_drm_dt_ids);

static struct platform_driver kirin_drm_platform_driver = {
	.probe = kirin_drm_platform_probe,
	.remove = kirin_drm_platform_remove,
	.driver = {
		.name = "kirin-drm",
		.of_match_table = kirin_drm_dt_ids,
	},
};

module_platform_driver(kirin_drm_platform_driver);

MODULE_AUTHOR("Xinliang Liu <xinliang.liu@linaro.org>");
MODULE_AUTHOR("Xinliang Liu <z.liuxinliang@hisilicon.com>");
MODULE_AUTHOR("Xinwei Kong <kong.kongxinwei@hisilicon.com>");
MODULE_DESCRIPTION("hisilicon Kirin SoCs' DRM master driver");
MODULE_LICENSE("GPL v2");
