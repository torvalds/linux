// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/clk.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include "arcpgu.h"
#include "arcpgu_regs.h"

static const struct drm_mode_config_funcs arcpgu_drm_modecfg_funcs = {
	.fb_create  = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void arcpgu_setup_mode_config(struct drm_device *drm)
{
	drm_mode_config_init(drm);
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 1920;
	drm->mode_config.max_height = 1080;
	drm->mode_config.funcs = &arcpgu_drm_modecfg_funcs;
}

DEFINE_DRM_GEM_CMA_FOPS(arcpgu_drm_ops);

static int arcpgu_load(struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct arcpgu_drm_private *arcpgu;
	struct device_node *encoder_node = NULL, *endpoint_node = NULL;
	struct resource *res;
	int ret;

	arcpgu = devm_kzalloc(&pdev->dev, sizeof(*arcpgu), GFP_KERNEL);
	if (arcpgu == NULL)
		return -ENOMEM;

	drm->dev_private = arcpgu;

	arcpgu->clk = devm_clk_get(drm->dev, "pxlclk");
	if (IS_ERR(arcpgu->clk))
		return PTR_ERR(arcpgu->clk);

	arcpgu_setup_mode_config(drm);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	arcpgu->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(arcpgu->regs))
		return PTR_ERR(arcpgu->regs);

	dev_info(drm->dev, "arc_pgu ID: 0x%x\n",
		 arc_pgu_read(arcpgu, ARCPGU_REG_ID));

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(drm->dev);
	if (ret && ret != -ENODEV)
		return ret;

	if (dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32)))
		return -ENODEV;

	if (arc_pgu_setup_crtc(drm) < 0)
		return -ENODEV;

	/*
	 * There is only one output port inside each device. It is linked with
	 * encoder endpoint.
	 */
	endpoint_node = of_graph_get_next_endpoint(pdev->dev.of_node, NULL);
	if (endpoint_node) {
		encoder_node = of_graph_get_remote_port_parent(endpoint_node);
		of_node_put(endpoint_node);
	}

	if (encoder_node) {
		ret = arcpgu_drm_hdmi_init(drm, encoder_node);
		of_node_put(encoder_node);
		if (ret < 0)
			return ret;
	} else {
		dev_info(drm->dev, "no encoder found. Assumed virtual LCD on simulation platform\n");
		ret = arcpgu_drm_sim_init(drm, NULL);
		if (ret < 0)
			return ret;
	}

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	platform_set_drvdata(pdev, drm);
	return 0;
}

static int arcpgu_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);
	drm_mode_config_cleanup(drm);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int arcpgu_show_pxlclock(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *drm = node->minor->dev;
	struct arcpgu_drm_private *arcpgu = drm->dev_private;
	unsigned long clkrate = clk_get_rate(arcpgu->clk);
	unsigned long mode_clock = arcpgu->crtc.mode.crtc_clock * 1000;

	seq_printf(m, "hw  : %lu\n", clkrate);
	seq_printf(m, "mode: %lu\n", mode_clock);
	return 0;
}

static struct drm_info_list arcpgu_debugfs_list[] = {
	{ "clocks", arcpgu_show_pxlclock, 0 },
};

static void arcpgu_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(arcpgu_debugfs_list,
				 ARRAY_SIZE(arcpgu_debugfs_list),
				 minor->debugfs_root, minor);
}
#endif

static const struct drm_driver arcpgu_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.name = "arcpgu",
	.desc = "ARC PGU Controller",
	.date = "20160219",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.fops = &arcpgu_drm_ops,
	DRM_GEM_CMA_DRIVER_OPS,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = arcpgu_debugfs_init,
#endif
};

static int arcpgu_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&arcpgu_drm_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = arcpgu_load(drm);
	if (ret)
		goto err_unref;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unload;

	drm_fbdev_generic_setup(drm, 16);

	return 0;

err_unload:
	arcpgu_unload(drm);

err_unref:
	drm_dev_put(drm);

	return ret;
}

static int arcpgu_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	arcpgu_unload(drm);
	drm_dev_put(drm);

	return 0;
}

static const struct of_device_id arcpgu_of_table[] = {
	{.compatible = "snps,arcpgu"},
	{}
};

MODULE_DEVICE_TABLE(of, arcpgu_of_table);

static struct platform_driver arcpgu_platform_driver = {
	.probe = arcpgu_probe,
	.remove = arcpgu_remove,
	.driver = {
		   .name = "arcpgu",
		   .of_match_table = arcpgu_of_table,
		   },
};

module_platform_driver(arcpgu_platform_driver);

MODULE_AUTHOR("Carlos Palminha <palminha@synopsys.com>");
MODULE_DESCRIPTION("ARC PGU DRM driver");
MODULE_LICENSE("GPL");
