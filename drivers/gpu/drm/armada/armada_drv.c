// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_prime.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_vblank.h>

#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_gem.h"
#include "armada_fb.h"
#include "armada_hw.h"
#include <drm/armada_drm.h>
#include "armada_ioctlP.h"

static struct drm_ioctl_desc armada_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_CREATE, armada_gem_create_ioctl,0),
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_MMAP, armada_gem_mmap_ioctl, 0),
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_PWRITE, armada_gem_pwrite_ioctl, 0),
};

DEFINE_DRM_GEM_FOPS(armada_drm_fops);

static struct drm_driver armada_drm_driver = {
	.lastclose		= drm_fb_helper_lastclose,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= armada_gem_prime_import,
	.dumb_create		= armada_gem_dumb_create,
	.major			= 1,
	.minor			= 0,
	.name			= "armada-drm",
	.desc			= "Armada SoC DRM",
	.date			= "20120730",
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.ioctls			= armada_ioctls,
	.fops			= &armada_drm_fops,
};

static const struct drm_mode_config_funcs armada_drm_mode_config_funcs = {
	.fb_create		= armada_fb_create,
	.output_poll_changed	= drm_fb_helper_output_poll_changed,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static int armada_drm_bind(struct device *dev)
{
	struct armada_private *priv;
	struct resource *mem = NULL;
	int ret, n;

	for (n = 0; ; n++) {
		struct resource *r = platform_get_resource(to_platform_device(dev),
							   IORESOURCE_MEM, n);
		if (!r)
			break;

		/* Resources above 64K are graphics memory */
		if (resource_size(r) > SZ_64K)
			mem = r;
		else
			return -EINVAL;
	}

	if (!mem)
		return -ENXIO;

	if (!devm_request_mem_region(dev, mem->start, resource_size(mem),
				     "armada-drm"))
		return -EBUSY;

	priv = devm_drm_dev_alloc(dev, &armada_drm_driver,
				  struct armada_private, drm);
	if (IS_ERR(priv)) {
		dev_err(dev, "[" DRM_NAME ":%s] devm_drm_dev_alloc failed: %li\n",
			__func__, PTR_ERR(priv));
		return PTR_ERR(priv);
	}

	/* Remove early framebuffers */
	ret = drm_fb_helper_remove_conflicting_framebuffers(NULL,
							    "armada-drm-fb",
							    false);
	if (ret) {
		dev_err(dev, "[" DRM_NAME ":%s] can't kick out simple-fb: %d\n",
			__func__, ret);
		kfree(priv);
		return ret;
	}

	dev_set_drvdata(dev, &priv->drm);

	/* Mode setting support */
	drm_mode_config_init(&priv->drm);
	priv->drm.mode_config.min_width = 320;
	priv->drm.mode_config.min_height = 200;

	/*
	 * With vscale enabled, the maximum width is 1920 due to the
	 * 1920 by 3 lines RAM
	 */
	priv->drm.mode_config.max_width = 1920;
	priv->drm.mode_config.max_height = 2048;

	priv->drm.mode_config.preferred_depth = 24;
	priv->drm.mode_config.funcs = &armada_drm_mode_config_funcs;
	drm_mm_init(&priv->linear, mem->start, resource_size(mem));
	mutex_init(&priv->linear_lock);

	ret = component_bind_all(dev, &priv->drm);
	if (ret)
		goto err_kms;

	ret = drm_vblank_init(&priv->drm, priv->drm.mode_config.num_crtc);
	if (ret)
		goto err_comp;

	priv->drm.irq_enabled = true;

	drm_mode_config_reset(&priv->drm);

	ret = armada_fbdev_init(&priv->drm);
	if (ret)
		goto err_comp;

	drm_kms_helper_poll_init(&priv->drm);

	ret = drm_dev_register(&priv->drm, 0);
	if (ret)
		goto err_poll;

#ifdef CONFIG_DEBUG_FS
	armada_drm_debugfs_init(priv->drm.primary);
#endif

	return 0;

 err_poll:
	drm_kms_helper_poll_fini(&priv->drm);
	armada_fbdev_fini(&priv->drm);
 err_comp:
	component_unbind_all(dev, &priv->drm);
 err_kms:
	drm_mode_config_cleanup(&priv->drm);
	drm_mm_takedown(&priv->linear);
	return ret;
}

static void armada_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct armada_private *priv = drm_to_armada_dev(drm);

	drm_kms_helper_poll_fini(&priv->drm);
	armada_fbdev_fini(&priv->drm);

	drm_dev_unregister(&priv->drm);

	drm_atomic_helper_shutdown(&priv->drm);

	component_unbind_all(dev, &priv->drm);

	drm_mode_config_cleanup(&priv->drm);
	drm_mm_takedown(&priv->linear);
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int compare_dev_name(struct device *dev, void *data)
{
	const char *name = data;
	return !strcmp(dev_name(dev), name);
}

static void armada_add_endpoints(struct device *dev,
	struct component_match **match, struct device_node *dev_node)
{
	struct device_node *ep, *remote;

	for_each_endpoint_of_node(dev_node, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (remote && of_device_is_available(remote))
			drm_of_component_match_add(dev, match, compare_of,
						   remote);
		of_node_put(remote);
	}
}

static const struct component_master_ops armada_master_ops = {
	.bind = armada_drm_bind,
	.unbind = armada_drm_unbind,
};

static int armada_drm_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device *dev = &pdev->dev;
	int ret;

	ret = drm_of_component_probe(dev, compare_dev_name, &armada_master_ops);
	if (ret != -EINVAL)
		return ret;

	if (dev->platform_data) {
		char **devices = dev->platform_data;
		struct device *d;
		int i;

		for (i = 0; devices[i]; i++)
			component_match_add(dev, &match, compare_dev_name,
					    devices[i]);

		if (i == 0) {
			dev_err(dev, "missing 'ports' property\n");
			return -ENODEV;
		}

		for (i = 0; devices[i]; i++) {
			d = bus_find_device_by_name(&platform_bus_type, NULL,
						    devices[i]);
			if (d && d->of_node)
				armada_add_endpoints(dev, &match, d->of_node);
			put_device(d);
		}
	}

	return component_master_add_with_match(&pdev->dev, &armada_master_ops,
					       match);
}

static int armada_drm_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &armada_master_ops);
	return 0;
}

static const struct platform_device_id armada_drm_platform_ids[] = {
	{
		.name		= "armada-drm",
	}, {
		.name		= "armada-510-drm",
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, armada_drm_platform_ids);

static struct platform_driver armada_drm_platform_driver = {
	.probe	= armada_drm_probe,
	.remove	= armada_drm_remove,
	.driver	= {
		.name	= "armada-drm",
	},
	.id_table = armada_drm_platform_ids,
};

static int __init armada_drm_init(void)
{
	int ret;

	armada_drm_driver.num_ioctls = ARRAY_SIZE(armada_ioctls);

	ret = platform_driver_register(&armada_lcd_platform_driver);
	if (ret)
		return ret;
	ret = platform_driver_register(&armada_drm_platform_driver);
	if (ret)
		platform_driver_unregister(&armada_lcd_platform_driver);
	return ret;
}
module_init(armada_drm_init);

static void __exit armada_drm_exit(void)
{
	platform_driver_unregister(&armada_drm_platform_driver);
	platform_driver_unregister(&armada_lcd_platform_driver);
}
module_exit(armada_drm_exit);

MODULE_AUTHOR("Russell King <rmk+kernel@armlinux.org.uk>");
MODULE_DESCRIPTION("Armada DRM Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:armada-drm");
