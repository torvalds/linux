// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/gpu/drm/mxsfb/mxsfb*
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "lcdif_drv.h"
#include "lcdif_regs.h"

static const struct drm_mode_config_funcs lcdif_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs lcdif_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static int lcdif_attach_bridge(struct lcdif_drm_private *lcdif)
{
	struct drm_device *drm = lcdif->drm;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	ret = drm_of_find_panel_or_bridge(drm->dev->of_node, 0, 0, &panel,
					  &bridge);
	if (ret)
		return ret;

	if (panel) {
		bridge = devm_drm_panel_bridge_add_typed(drm->dev, panel,
							 DRM_MODE_CONNECTOR_DPI);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	if (!bridge)
		return -ENODEV;

	ret = drm_bridge_attach(&lcdif->encoder, bridge, NULL, 0);
	if (ret)
		return dev_err_probe(drm->dev, ret, "Failed to attach bridge\n");

	lcdif->bridge = bridge;

	return 0;
}

static irqreturn_t lcdif_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct lcdif_drm_private *lcdif = drm->dev_private;
	u32 reg, stat;

	stat = readl(lcdif->base + LCDC_V8_INT_STATUS_D0);
	if (!stat)
		return IRQ_NONE;

	if (stat & INT_STATUS_D0_VS_BLANK) {
		reg = readl(lcdif->base + LCDC_V8_CTRLDESCL0_5);
		if (!(reg & CTRLDESCL0_5_SHADOW_LOAD_EN))
			drm_crtc_handle_vblank(&lcdif->crtc);
	}

	writel(stat, lcdif->base + LCDC_V8_INT_STATUS_D0);

	return IRQ_HANDLED;
}

static int lcdif_load(struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct lcdif_drm_private *lcdif;
	struct resource *res;
	int ret;

	lcdif = devm_kzalloc(&pdev->dev, sizeof(*lcdif), GFP_KERNEL);
	if (!lcdif)
		return -ENOMEM;

	lcdif->drm = drm;
	drm->dev_private = lcdif;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lcdif->base = devm_ioremap_resource(drm->dev, res);
	if (IS_ERR(lcdif->base))
		return PTR_ERR(lcdif->base);

	lcdif->clk = devm_clk_get(drm->dev, "pix");
	if (IS_ERR(lcdif->clk))
		return PTR_ERR(lcdif->clk);

	lcdif->clk_axi = devm_clk_get(drm->dev, "axi");
	if (IS_ERR(lcdif->clk_axi))
		return PTR_ERR(lcdif->clk_axi);

	lcdif->clk_disp_axi = devm_clk_get(drm->dev, "disp_axi");
	if (IS_ERR(lcdif->clk_disp_axi))
		return PTR_ERR(lcdif->clk_disp_axi);

	platform_set_drvdata(pdev, drm);

	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(36));
	if (ret)
		return ret;

	/* Modeset init */
	drm_mode_config_init(drm);

	ret = lcdif_kms_init(lcdif);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialize KMS pipeline\n");
		return ret;
	}

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialise vblank\n");
		return ret;
	}

	/* Start with vertical blanking interrupt reporting disabled. */
	drm_crtc_vblank_off(&lcdif->crtc);

	ret = lcdif_attach_bridge(lcdif);
	if (ret)
		return dev_err_probe(drm->dev, ret, "Cannot connect bridge\n");

	drm->mode_config.min_width	= LCDIF_MIN_XRES;
	drm->mode_config.min_height	= LCDIF_MIN_YRES;
	drm->mode_config.max_width	= LCDIF_MAX_XRES;
	drm->mode_config.max_height	= LCDIF_MAX_YRES;
	drm->mode_config.funcs		= &lcdif_mode_config_funcs;
	drm->mode_config.helper_private	= &lcdif_mode_config_helpers;

	drm_mode_config_reset(drm);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	lcdif->irq = ret;

	ret = devm_request_irq(drm->dev, lcdif->irq, lcdif_irq_handler, 0,
			       drm->driver->name, drm);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to install IRQ handler\n");
		return ret;
	}

	drm_kms_helper_poll_init(drm);

	drm_helper_hpd_irq_event(drm);

	pm_runtime_enable(drm->dev);

	return 0;
}

static void lcdif_unload(struct drm_device *drm)
{
	struct lcdif_drm_private *lcdif = drm->dev_private;

	pm_runtime_get_sync(drm->dev);

	drm_crtc_vblank_off(&lcdif->crtc);

	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);

	pm_runtime_put_sync(drm->dev);
	pm_runtime_disable(drm->dev);

	drm->dev_private = NULL;
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static const struct drm_driver lcdif_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_CMA_DRIVER_OPS,
	.fops	= &fops,
	.name	= "imx-lcdif",
	.desc	= "i.MX LCDIF Controller DRM",
	.date	= "20220417",
	.major	= 1,
	.minor	= 0,
};

static const struct of_device_id lcdif_dt_ids[] = {
	{ .compatible = "fsl,imx8mp-lcdif" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lcdif_dt_ids);

static int lcdif_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&lcdif_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = lcdif_load(drm);
	if (ret)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unload;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

err_unload:
	lcdif_unload(drm);
err_free:
	drm_dev_put(drm);

	return ret;
}

static int lcdif_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	lcdif_unload(drm);
	drm_dev_put(drm);

	return 0;
}

static void lcdif_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(drm);
}

static int __maybe_unused lcdif_rpm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct lcdif_drm_private *lcdif = drm->dev_private;

	/* These clock supply the DISPLAY CLOCK Domain */
	clk_disable_unprepare(lcdif->clk);
	/* These clock supply the System Bus, AXI, Write Path, LFIFO */
	clk_disable_unprepare(lcdif->clk_disp_axi);
	/* These clock supply the Control Bus, APB, APBH Ctrl Registers */
	clk_disable_unprepare(lcdif->clk_axi);

	return 0;
}

static int __maybe_unused lcdif_rpm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct lcdif_drm_private *lcdif = drm->dev_private;

	/* These clock supply the Control Bus, APB, APBH Ctrl Registers */
	clk_prepare_enable(lcdif->clk_axi);
	/* These clock supply the System Bus, AXI, Write Path, LFIFO */
	clk_prepare_enable(lcdif->clk_disp_axi);
	/* These clock supply the DISPLAY CLOCK Domain */
	clk_prepare_enable(lcdif->clk);

	return 0;
}

static int __maybe_unused lcdif_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	int ret;

	ret = drm_mode_config_helper_suspend(drm);
	if (ret)
		return ret;

	return lcdif_rpm_suspend(dev);
}

static int __maybe_unused lcdif_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	lcdif_rpm_resume(dev);

	return drm_mode_config_helper_resume(drm);
}

static const struct dev_pm_ops lcdif_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lcdif_suspend, lcdif_resume)
	SET_RUNTIME_PM_OPS(lcdif_rpm_suspend, lcdif_rpm_resume, NULL)
};

static struct platform_driver lcdif_platform_driver = {
	.probe		= lcdif_probe,
	.remove		= lcdif_remove,
	.shutdown	= lcdif_shutdown,
	.driver	= {
		.name		= "imx-lcdif",
		.of_match_table	= lcdif_dt_ids,
		.pm		= &lcdif_pm_ops,
	},
};

drm_module_platform_driver(lcdif_platform_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale LCDIF DRM/KMS driver");
MODULE_LICENSE("GPL");
