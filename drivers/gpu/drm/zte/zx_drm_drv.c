/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drmP.h>

#include "zx_drm_drv.h"
#include "zx_vou.h"

static const struct drm_mode_config_funcs zx_drm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_CMA_FOPS(zx_drm_fops);

static struct drm_driver zx_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
			   DRIVER_ATOMIC,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
	.fops = &zx_drm_fops,
	.name = "zx-vou",
	.desc = "ZTE VOU Controller DRM",
	.date = "20160811",
	.major = 1,
	.minor = 0,
};

static int zx_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&zx_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	dev_set_drvdata(dev, drm);

	drm_mode_config_init(drm);
	drm->mode_config.min_width = 16;
	drm->mode_config.min_height = 16;
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &zx_drm_mode_config_funcs;

	ret = component_bind_all(dev, drm);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to bind all components: %d\n", ret);
		goto out_unregister;
	}

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to init vblank: %d\n", ret);
		goto out_unbind;
	}

	/*
	 * We will manage irq handler on our own.  In this case, irq_enabled
	 * need to be true for using vblank core support.
	 */
	drm->irq_enabled = true;

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto out_poll_fini;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

out_poll_fini:
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);
out_unbind:
	component_unbind_all(dev, drm);
out_unregister:
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm);
	return ret;
}

static void zx_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);
	drm_mode_config_cleanup(drm);
	component_unbind_all(dev, drm);
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm);
}

static const struct component_master_ops zx_drm_master_ops = {
	.bind = zx_drm_bind,
	.unbind = zx_drm_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int zx_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *parent = dev->of_node;
	struct device_node *child;
	struct component_match *match = NULL;
	int ret;

	ret = devm_of_platform_populate(dev);
	if (ret)
		return ret;

	for_each_available_child_of_node(parent, child)
		component_match_add(dev, &match, compare_of, child);

	return component_master_add_with_match(dev, &zx_drm_master_ops, match);
}

static int zx_drm_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &zx_drm_master_ops);
	return 0;
}

static const struct of_device_id zx_drm_of_match[] = {
	{ .compatible = "zte,zx296718-vou", },
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, zx_drm_of_match);

static struct platform_driver zx_drm_platform_driver = {
	.probe = zx_drm_probe,
	.remove = zx_drm_remove,
	.driver	= {
		.name = "zx-drm",
		.of_match_table	= zx_drm_of_match,
	},
};

static struct platform_driver *drivers[] = {
	&zx_crtc_driver,
	&zx_hdmi_driver,
	&zx_tvenc_driver,
	&zx_vga_driver,
	&zx_drm_platform_driver,
};

static int zx_drm_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}
module_init(zx_drm_init);

static void zx_drm_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(zx_drm_exit);

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("ZTE ZX VOU DRM driver");
MODULE_LICENSE("GPL v2");
