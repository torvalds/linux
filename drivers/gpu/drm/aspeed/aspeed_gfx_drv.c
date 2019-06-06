// SPDX-License-Identifier: GPL-2.0+
// Copyright 2018 IBM Corporation

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_drv.h>

#include "aspeed_gfx.h"

/**
 * DOC: ASPEED GFX Driver
 *
 * This driver is for the ASPEED BMC SoC's 'GFX' display hardware, also called
 * the 'SOC Display Controller' in the datasheet. This driver runs on the ARM
 * based BMC systems, unlike the ast driver which runs on a host CPU and is for
 * a PCIe graphics device.
 *
 * The AST2500 supports a total of 3 output paths:
 *
 *   1. VGA output, the output target can choose either or both to the DAC
 *   or DVO interface.
 *
 *   2. Graphics CRT output, the output target can choose either or both to
 *   the DAC or DVO interface.
 *
 *   3. Video input from DVO, the video input can be used for video engine
 *   capture or DAC display output.
 *
 * Output options are selected in SCU2C.
 *
 * The "VGA mode" device is the PCI attached controller. The "Graphics CRT"
 * is the ARM's internal display controller.
 *
 * The driver only supports a simple configuration consisting of a 40MHz
 * pixel clock, fixed by hardware limitations, and the VGA output path.
 *
 * The driver was written with the 'AST2500 Software Programming Guide' v17,
 * which is available under NDA from ASPEED.
 */

static const struct drm_mode_config_funcs aspeed_gfx_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static void aspeed_gfx_setup_mode_config(struct drm_device *drm)
{
	drm_mode_config_init(drm);

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 800;
	drm->mode_config.max_height = 600;
	drm->mode_config.funcs = &aspeed_gfx_mode_config_funcs;
}

static irqreturn_t aspeed_gfx_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct aspeed_gfx *priv = drm->dev_private;
	u32 reg;

	reg = readl(priv->base + CRT_CTRL1);

	if (reg & CRT_CTRL_VERTICAL_INTR_STS) {
		drm_crtc_handle_vblank(&priv->pipe.crtc);
		writel(reg, priv->base + CRT_CTRL1);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}



static int aspeed_gfx_load(struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct aspeed_gfx *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	drm->dev_private = priv;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(drm->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2500-scu");
	if (IS_ERR(priv->scu)) {
		dev_err(&pdev->dev, "failed to find SCU regmap\n");
		return PTR_ERR(priv->scu);
	}

	ret = of_reserved_mem_device_init(drm->dev);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to initialize reserved mem: %d\n", ret);
		return ret;
	}

	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", ret);
		return ret;
	}

	priv->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rst)) {
		dev_err(&pdev->dev,
			"missing or invalid reset controller device tree entry");
		return PTR_ERR(priv->rst);
	}
	reset_control_deassert(priv->rst);

	priv->clk = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev,
			"missing or invalid clk device tree entry");
		return PTR_ERR(priv->clk);
	}
	clk_prepare_enable(priv->clk);

	/* Sanitize control registers */
	writel(0, priv->base + CRT_CTRL1);
	writel(0, priv->base + CRT_CTRL2);

	aspeed_gfx_setup_mode_config(drm);

	ret = drm_vblank_init(drm, 1);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialise vblank\n");
		return ret;
	}

	ret = aspeed_gfx_create_output(drm);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to create outputs\n");
		return ret;
	}

	ret = aspeed_gfx_create_pipe(drm);
	if (ret < 0) {
		dev_err(drm->dev, "Cannot setup simple display pipe\n");
		return ret;
	}

	ret = devm_request_irq(drm->dev, platform_get_irq(pdev, 0),
			       aspeed_gfx_irq_handler, 0, "aspeed gfx", drm);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to install IRQ handler\n");
		return ret;
	}

	drm_mode_config_reset(drm);

	drm_fbdev_generic_setup(drm, 32);

	return 0;
}

static void aspeed_gfx_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);

	drm->dev_private = NULL;
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static struct drm_driver aspeed_gfx_driver = {
	.driver_features        = DRIVER_GEM | DRIVER_MODESET |
				DRIVER_PRIME | DRIVER_ATOMIC,
	.gem_create_object	= drm_cma_gem_create_object_default_funcs,
	.dumb_create		= drm_gem_cma_dumb_create,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_mmap		= drm_gem_prime_mmap,
	.fops = &fops,
	.name = "aspeed-gfx-drm",
	.desc = "ASPEED GFX DRM",
	.date = "20180319",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id aspeed_gfx_match[] = {
	{ .compatible = "aspeed,ast2500-gfx" },
	{ }
};

static int aspeed_gfx_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&aspeed_gfx_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = aspeed_gfx_load(drm);
	if (ret)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unload;

	return 0;

err_unload:
	aspeed_gfx_unload(drm);
err_free:
	drm_dev_put(drm);

	return ret;
}

static int aspeed_gfx_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	aspeed_gfx_unload(drm);
	drm_dev_put(drm);

	return 0;
}

static struct platform_driver aspeed_gfx_platform_driver = {
	.probe		= aspeed_gfx_probe,
	.remove		= aspeed_gfx_remove,
	.driver = {
		.name = "aspeed_gfx",
		.of_match_table = aspeed_gfx_match,
	},
};

module_platform_driver(aspeed_gfx_platform_driver);

MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_DESCRIPTION("ASPEED BMC DRM/KMS driver");
MODULE_LICENSE("GPL");
