// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Endless Mobile
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/sys_soc.h>
#include <linux/platform_device.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_irq.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "meson_crtc.h"
#include "meson_drv.h"
#include "meson_overlay.h"
#include "meson_plane.h"
#include "meson_osd_afbcd.h"
#include "meson_registers.h"
#include "meson_venc_cvbs.h"
#include "meson_viu.h"
#include "meson_vpp.h"
#include "meson_rdma.h"

#define DRIVER_NAME "meson"
#define DRIVER_DESC "Amlogic Meson DRM driver"

/**
 * DOC: Video Processing Unit
 *
 * VPU Handles the Global Video Processing, it includes management of the
 * clocks gates, blocks reset lines and power domains.
 *
 * What is missing :
 *
 * - Full reset of entire video processing HW blocks
 * - Scaling and setup of the VPU clock
 * - Bus clock gates
 * - Powering up video processing HW blocks
 * - Powering Up HDMI controller and PHY
 */

static const struct drm_mode_config_funcs meson_mode_config_funcs = {
	.atomic_check        = drm_atomic_helper_check,
	.atomic_commit       = drm_atomic_helper_commit,
	.fb_create           = drm_gem_fb_create,
};

static const struct drm_mode_config_helper_funcs meson_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static irqreturn_t meson_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct meson_drm *priv = dev->dev_private;

	(void)readl_relaxed(priv->io_base + _REG(VENC_INTFLAG));

	meson_crtc_irq(priv);

	return IRQ_HANDLED;
}

static int meson_dumb_create(struct drm_file *file, struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	/*
	 * We need 64bytes aligned stride, and PAGE aligned size
	 */
	args->pitch = ALIGN(DIV_ROUND_UP(args->width * args->bpp, 8), SZ_64);
	args->size = PAGE_ALIGN(args->pitch * args->height);

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static struct drm_driver meson_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,

	/* IRQ */
	.irq_handler		= meson_irq,

	/* CMA Ops */
	DRM_GEM_CMA_DRIVER_OPS_WITH_DUMB_CREATE(meson_dumb_create),

	/* Misc */
	.fops			= &fops,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= "20161109",
	.major			= 1,
	.minor			= 0,
};

static bool meson_vpu_has_available_connectors(struct device *dev)
{
	struct device_node *ep, *remote;

	/* Parses each endpoint and check if remote exists */
	for_each_endpoint_of_node(dev->of_node, ep) {
		/* If the endpoint node exists, consider it enabled */
		remote = of_graph_get_remote_port(ep);
		if (remote) {
			of_node_put(remote);
			of_node_put(ep);
			return true;
		}
	}

	return false;
}

static struct regmap_config meson_regmap_config = {
	.reg_bits       = 32,
	.val_bits       = 32,
	.reg_stride     = 4,
	.max_register   = 0x1000,
};

static void meson_vpu_init(struct meson_drm *priv)
{
	u32 value;

	/*
	 * Slave dc0 and dc5 connected to master port 1.
	 * By default other slaves are connected to master port 0.
	 */
	value = VPU_RDARB_SLAVE_TO_MASTER_PORT(0, 1) |
		VPU_RDARB_SLAVE_TO_MASTER_PORT(5, 1);
	writel_relaxed(value, priv->io_base + _REG(VPU_RDARB_MODE_L1C1));

	/* Slave dc0 connected to master port 1 */
	value = VPU_RDARB_SLAVE_TO_MASTER_PORT(0, 1);
	writel_relaxed(value, priv->io_base + _REG(VPU_RDARB_MODE_L1C2));

	/* Slave dc4 and dc7 connected to master port 1 */
	value = VPU_RDARB_SLAVE_TO_MASTER_PORT(4, 1) |
		VPU_RDARB_SLAVE_TO_MASTER_PORT(7, 1);
	writel_relaxed(value, priv->io_base + _REG(VPU_RDARB_MODE_L2C1));

	/* Slave dc1 connected to master port 1 */
	value = VPU_RDARB_SLAVE_TO_MASTER_PORT(1, 1);
	writel_relaxed(value, priv->io_base + _REG(VPU_WRARB_MODE_L2C1));
}

static void meson_remove_framebuffers(void)
{
	struct apertures_struct *ap;

	ap = alloc_apertures(1);
	if (!ap)
		return;

	/* The framebuffer can be located anywhere in RAM */
	ap->ranges[0].base = 0;
	ap->ranges[0].size = ~0;

	drm_fb_helper_remove_conflicting_framebuffers(ap, "meson-drm-fb",
						      false);
	kfree(ap);
}

struct meson_drm_soc_attr {
	struct meson_drm_soc_limits limits;
	const struct soc_device_attribute *attrs;
};

static const struct meson_drm_soc_attr meson_drm_soc_attrs[] = {
	/* S805X/S805Y HDMI PLL won't lock for HDMI PHY freq > 1,65GHz */
	{
		.limits = {
			.max_hdmi_phy_freq = 1650000,
		},
		.attrs = (const struct soc_device_attribute []) {
			{ .soc_id = "GXL (S805*)", },
			{ /* sentinel */ },
		}
	},
};

static int meson_drv_bind_master(struct device *dev, bool has_components)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct meson_drm_match_data *match;
	struct meson_drm *priv;
	struct drm_device *drm;
	struct resource *res;
	void __iomem *regs;
	int ret, i;

	/* Checks if an output connector is available */
	if (!meson_vpu_has_available_connectors(dev)) {
		dev_err(dev, "No output connector available\n");
		return -ENODEV;
	}

	match = of_device_get_match_data(dev);
	if (!match)
		return -ENODEV;

	drm = drm_dev_alloc(&meson_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto free_drm;
	}
	drm->dev_private = priv;
	priv->drm = drm;
	priv->dev = dev;
	priv->compat = match->compat;
	priv->afbcd.ops = match->afbcd_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vpu");
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto free_drm;
	}

	priv->io_base = regs;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hhi");
	if (!res) {
		ret = -EINVAL;
		goto free_drm;
	}
	/* Simply ioremap since it may be a shared register zone */
	regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!regs) {
		ret = -EADDRNOTAVAIL;
		goto free_drm;
	}

	priv->hhi = devm_regmap_init_mmio(dev, regs,
					  &meson_regmap_config);
	if (IS_ERR(priv->hhi)) {
		dev_err(&pdev->dev, "Couldn't create the HHI regmap\n");
		ret = PTR_ERR(priv->hhi);
		goto free_drm;
	}

	priv->canvas = meson_canvas_get(dev);
	if (IS_ERR(priv->canvas)) {
		ret = PTR_ERR(priv->canvas);
		goto free_drm;
	}

	ret = meson_canvas_alloc(priv->canvas, &priv->canvas_id_osd1);
	if (ret)
		goto free_drm;
	ret = meson_canvas_alloc(priv->canvas, &priv->canvas_id_vd1_0);
	if (ret) {
		meson_canvas_free(priv->canvas, priv->canvas_id_osd1);
		goto free_drm;
	}
	ret = meson_canvas_alloc(priv->canvas, &priv->canvas_id_vd1_1);
	if (ret) {
		meson_canvas_free(priv->canvas, priv->canvas_id_osd1);
		meson_canvas_free(priv->canvas, priv->canvas_id_vd1_0);
		goto free_drm;
	}
	ret = meson_canvas_alloc(priv->canvas, &priv->canvas_id_vd1_2);
	if (ret) {
		meson_canvas_free(priv->canvas, priv->canvas_id_osd1);
		meson_canvas_free(priv->canvas, priv->canvas_id_vd1_0);
		meson_canvas_free(priv->canvas, priv->canvas_id_vd1_1);
		goto free_drm;
	}

	priv->vsync_irq = platform_get_irq(pdev, 0);

	ret = drm_vblank_init(drm, 1);
	if (ret)
		goto free_drm;

	/* Assign limits per soc revision/package */
	for (i = 0 ; i < ARRAY_SIZE(meson_drm_soc_attrs) ; ++i) {
		if (soc_device_match(meson_drm_soc_attrs[i].attrs)) {
			priv->limits = &meson_drm_soc_attrs[i].limits;
			break;
		}
	}

	/* Remove early framebuffers (ie. simplefb) */
	meson_remove_framebuffers();

	ret = drmm_mode_config_init(drm);
	if (ret)
		goto free_drm;
	drm->mode_config.max_width = 3840;
	drm->mode_config.max_height = 2160;
	drm->mode_config.funcs = &meson_mode_config_funcs;
	drm->mode_config.helper_private	= &meson_mode_config_helpers;

	/* Hardware Initialization */

	meson_vpu_init(priv);
	meson_venc_init(priv);
	meson_vpp_init(priv);
	meson_viu_init(priv);
	if (priv->afbcd.ops) {
		ret = priv->afbcd.ops->init(priv);
		if (ret)
			return ret;
	}

	/* Encoder Initialization */

	ret = meson_venc_cvbs_create(priv);
	if (ret)
		goto free_drm;

	if (has_components) {
		ret = component_bind_all(drm->dev, drm);
		if (ret) {
			dev_err(drm->dev, "Couldn't bind all components\n");
			goto free_drm;
		}
	}

	ret = meson_plane_create(priv);
	if (ret)
		goto free_drm;

	ret = meson_overlay_create(priv);
	if (ret)
		goto free_drm;

	ret = meson_crtc_create(priv);
	if (ret)
		goto free_drm;

	ret = drm_irq_install(drm, priv->vsync_irq);
	if (ret)
		goto free_drm;

	drm_mode_config_reset(drm);

	drm_kms_helper_poll_init(drm);

	platform_set_drvdata(pdev, priv);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto uninstall_irq;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

uninstall_irq:
	drm_irq_uninstall(drm);
free_drm:
	drm_dev_put(drm);

	return ret;
}

static int meson_drv_bind(struct device *dev)
{
	return meson_drv_bind_master(dev, true);
}

static void meson_drv_unbind(struct device *dev)
{
	struct meson_drm *priv = dev_get_drvdata(dev);
	struct drm_device *drm = priv->drm;

	if (priv->canvas) {
		meson_canvas_free(priv->canvas, priv->canvas_id_osd1);
		meson_canvas_free(priv->canvas, priv->canvas_id_vd1_0);
		meson_canvas_free(priv->canvas, priv->canvas_id_vd1_1);
		meson_canvas_free(priv->canvas, priv->canvas_id_vd1_2);
	}

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);
	component_unbind_all(dev, drm);
	drm_irq_uninstall(drm);
	drm_dev_put(drm);

	if (priv->afbcd.ops)
		priv->afbcd.ops->exit(priv);
}

static const struct component_master_ops meson_drv_master_ops = {
	.bind	= meson_drv_bind,
	.unbind	= meson_drv_unbind,
};

static int __maybe_unused meson_drv_pm_suspend(struct device *dev)
{
	struct meson_drm *priv = dev_get_drvdata(dev);

	if (!priv)
		return 0;

	return drm_mode_config_helper_suspend(priv->drm);
}

static int __maybe_unused meson_drv_pm_resume(struct device *dev)
{
	struct meson_drm *priv = dev_get_drvdata(dev);

	if (!priv)
		return 0;

	meson_vpu_init(priv);
	meson_venc_init(priv);
	meson_vpp_init(priv);
	meson_viu_init(priv);
	if (priv->afbcd.ops)
		priv->afbcd.ops->init(priv);

	return drm_mode_config_helper_resume(priv->drm);
}

static int compare_of(struct device *dev, void *data)
{
	DRM_DEBUG_DRIVER("Comparing of node %pOF with %pOF\n",
			 dev->of_node, data);

	return dev->of_node == data;
}

/* Possible connectors nodes to ignore */
static const struct of_device_id connectors_match[] = {
	{ .compatible = "composite-video-connector" },
	{ .compatible = "svideo-connector" },
	{ .compatible = "hdmi-connector" },
	{ .compatible = "dvi-connector" },
	{}
};

static int meson_probe_remote(struct platform_device *pdev,
			      struct component_match **match,
			      struct device_node *parent,
			      struct device_node *remote)
{
	struct device_node *ep, *remote_node;
	int count = 1;

	/* If node is a connector, return and do not add to match table */
	if (of_match_node(connectors_match, remote))
		return 1;

	component_match_add(&pdev->dev, match, compare_of, remote);

	for_each_endpoint_of_node(remote, ep) {
		remote_node = of_graph_get_remote_port_parent(ep);
		if (!remote_node ||
		    remote_node == parent || /* Ignore parent endpoint */
		    !of_device_is_available(remote_node)) {
			of_node_put(remote_node);
			continue;
		}

		count += meson_probe_remote(pdev, match, remote, remote_node);

		of_node_put(remote_node);
	}

	return count;
}

static void meson_drv_shutdown(struct platform_device *pdev)
{
	struct meson_drm *priv = dev_get_drvdata(&pdev->dev);

	if (!priv)
		return;

	drm_kms_helper_poll_fini(priv->drm);
	drm_atomic_helper_shutdown(priv->drm);
}

static int meson_drv_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ep, *remote;
	int count = 0;

	for_each_endpoint_of_node(np, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote || !of_device_is_available(remote)) {
			of_node_put(remote);
			continue;
		}

		count += meson_probe_remote(pdev, &match, np, remote);
		of_node_put(remote);
	}

	if (count && !match)
		return meson_drv_bind_master(&pdev->dev, false);

	/* If some endpoints were found, initialize the nodes */
	if (count) {
		dev_info(&pdev->dev, "Queued %d outputs on vpu\n", count);

		return component_master_add_with_match(&pdev->dev,
						       &meson_drv_master_ops,
						       match);
	}

	/* If no output endpoints were available, simply bail out */
	return 0;
};

static int meson_drv_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &meson_drv_master_ops);

	return 0;
}

static struct meson_drm_match_data meson_drm_gxbb_data = {
	.compat = VPU_COMPATIBLE_GXBB,
};

static struct meson_drm_match_data meson_drm_gxl_data = {
	.compat = VPU_COMPATIBLE_GXL,
};

static struct meson_drm_match_data meson_drm_gxm_data = {
	.compat = VPU_COMPATIBLE_GXM,
	.afbcd_ops = &meson_afbcd_gxm_ops,
};

static struct meson_drm_match_data meson_drm_g12a_data = {
	.compat = VPU_COMPATIBLE_G12A,
	.afbcd_ops = &meson_afbcd_g12a_ops,
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "amlogic,meson-gxbb-vpu",
	  .data       = (void *)&meson_drm_gxbb_data },
	{ .compatible = "amlogic,meson-gxl-vpu",
	  .data       = (void *)&meson_drm_gxl_data },
	{ .compatible = "amlogic,meson-gxm-vpu",
	  .data       = (void *)&meson_drm_gxm_data },
	{ .compatible = "amlogic,meson-g12a-vpu",
	  .data       = (void *)&meson_drm_g12a_data },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static const struct dev_pm_ops meson_drv_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(meson_drv_pm_suspend, meson_drv_pm_resume)
};

static struct platform_driver meson_drm_platform_driver = {
	.probe      = meson_drv_probe,
	.remove     = meson_drv_remove,
	.shutdown   = meson_drv_shutdown,
	.driver     = {
		.name	= "meson-drm",
		.of_match_table = dt_match,
		.pm = &meson_drv_pm_ops,
	},
};

module_platform_driver(meson_drm_platform_driver);

MODULE_AUTHOR("Jasper St. Pierre <jstpierre@mecheye.net>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
