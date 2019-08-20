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

static struct kirin_drm_data *driver_data;

static int kirin_drm_kms_cleanup(struct drm_device *dev)
{
	drm_kms_helper_poll_fini(dev);
	driver_data->cleanup(to_platform_device(dev->dev));
	drm_mode_config_cleanup(dev);

	return 0;
}

static int kirin_drm_kms_init(struct drm_device *dev)
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
	ret = driver_data->init(to_platform_device(dev->dev));
	if (ret)
		goto err_mode_config_cleanup;

	/* bind and init sub drivers */
	ret = component_bind_all(dev->dev, dev);
	if (ret) {
		DRM_ERROR("failed to bind all component.\n");
		goto err_dc_cleanup;
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
err_dc_cleanup:
	driver_data->cleanup(to_platform_device(dev->dev));
err_mode_config_cleanup:
	drm_mode_config_cleanup(dev);

	return ret;
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int kirin_drm_connectors_register(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct drm_connector *failed_connector;
	struct drm_connector_list_iter conn_iter;
	int ret;

	mutex_lock(&dev->mode_config.mutex);
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		ret = drm_connector_register(connector);
		if (ret) {
			failed_connector = connector;
			goto err;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev->mode_config.mutex);

	return 0;

err:
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (failed_connector == connector)
			break;
		drm_connector_unregister(connector);
	}
	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev->mode_config.mutex);

	return ret;
}

static int kirin_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	int ret;

	drm_dev = drm_dev_alloc(driver_data->driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);
	dev_set_drvdata(dev, drm_dev);

	ret = kirin_drm_kms_init(drm_dev);
	if (ret)
		goto err_drm_dev_put;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_kms_cleanup;

	drm_fbdev_generic_setup(drm_dev, 32);

	/* connectors should be registered after drm device register */
	if (driver_data->register_connects) {
		ret = kirin_drm_connectors_register(drm_dev);
		if (ret)
			goto err_drm_dev_unregister;
	}

	return 0;

err_drm_dev_unregister:
	drm_dev_unregister(drm_dev);
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

	driver_data = (struct kirin_drm_data *)of_device_get_match_data(dev);
	if (!driver_data) {
		DRM_ERROR("failed to get dt id data\n");
		return -EINVAL;
	}

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
	driver_data = NULL;
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
