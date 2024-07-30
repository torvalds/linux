// SPDX-License-Identifier: GPL-2.0+
/*
 * shmob_drm_drv.c  --  SH Mobile DRM driver
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

/* -----------------------------------------------------------------------------
 * Hardware initialization
 */

static int shmob_drm_setup_clocks(struct shmob_drm_device *sdev,
				  enum shmob_drm_clk_source clksrc)
{
	struct clk *clk;
	char *clkname;

	switch (clksrc) {
	case SHMOB_DRM_CLK_BUS:
		clkname = "fck";
		sdev->lddckr = LDDCKR_ICKSEL_BUS;
		break;
	case SHMOB_DRM_CLK_PERIPHERAL:
		clkname = "media";
		sdev->lddckr = LDDCKR_ICKSEL_MIPI;
		break;
	case SHMOB_DRM_CLK_EXTERNAL:
		clkname = "lclk";
		sdev->lddckr = LDDCKR_ICKSEL_HDMI;
		break;
	default:
		return -EINVAL;
	}

	clk = devm_clk_get(sdev->dev, clkname);
	if (IS_ERR(clk)) {
		dev_err(sdev->dev, "cannot get dot clock %s\n", clkname);
		return PTR_ERR(clk);
	}

	sdev->clock = clk;
	return 0;
}

/* -----------------------------------------------------------------------------
 * DRM operations
 */

static irqreturn_t shmob_drm_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct shmob_drm_device *sdev = to_shmob_device(dev);
	unsigned long flags;
	u32 status;

	/* Acknowledge interrupts. Putting interrupt enable and interrupt flag
	 * bits in the same register is really brain-dead design and requires
	 * taking a spinlock.
	 */
	spin_lock_irqsave(&sdev->irq_lock, flags);
	status = lcdc_read(sdev, LDINTR);
	lcdc_write(sdev, LDINTR, status ^ LDINTR_STATUS_MASK);
	spin_unlock_irqrestore(&sdev->irq_lock, flags);

	if (status & LDINTR_VES) {
		drm_crtc_handle_vblank(&sdev->crtc.base);
		shmob_drm_crtc_finish_page_flip(&sdev->crtc);
	}

	return IRQ_HANDLED;
}

DEFINE_DRM_GEM_DMA_FOPS(shmob_drm_fops);

static const struct drm_driver shmob_drm_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS,
	.fops			= &shmob_drm_fops,
	.name			= "shmob-drm",
	.desc			= "Renesas SH Mobile DRM",
	.date			= "20120424",
	.major			= 1,
	.minor			= 0,
};

/* -----------------------------------------------------------------------------
 * Power management
 */

static int shmob_drm_pm_suspend(struct device *dev)
{
	struct shmob_drm_device *sdev = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&sdev->ddev);
}

static int shmob_drm_pm_resume(struct device *dev)
{
	struct shmob_drm_device *sdev = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(&sdev->ddev);
}

static int shmob_drm_pm_runtime_suspend(struct device *dev)
{
	struct shmob_drm_device *sdev = dev_get_drvdata(dev);

	if (sdev->clock)
		clk_disable_unprepare(sdev->clock);

	return 0;
}

static int shmob_drm_pm_runtime_resume(struct device *dev)
{
	struct shmob_drm_device *sdev = dev_get_drvdata(dev);
	int ret;

	if (sdev->clock) {
		ret = clk_prepare_enable(sdev->clock);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct dev_pm_ops shmob_drm_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(shmob_drm_pm_suspend, shmob_drm_pm_resume)
	RUNTIME_PM_OPS(shmob_drm_pm_runtime_suspend,
		       shmob_drm_pm_runtime_resume, NULL)
};

/* -----------------------------------------------------------------------------
 * Platform driver
 */

static void shmob_drm_remove(struct platform_device *pdev)
{
	struct shmob_drm_device *sdev = platform_get_drvdata(pdev);
	struct drm_device *ddev = &sdev->ddev;

	drm_dev_unregister(ddev);
	drm_atomic_helper_shutdown(ddev);
	drm_kms_helper_poll_fini(ddev);
}

static void shmob_drm_shutdown(struct platform_device *pdev)
{
	struct shmob_drm_device *sdev = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&sdev->ddev);
}

static int shmob_drm_probe(struct platform_device *pdev)
{
	struct shmob_drm_platform_data *pdata = pdev->dev.platform_data;
	const struct shmob_drm_config *config;
	struct shmob_drm_device *sdev;
	struct drm_device *ddev;
	int ret;

	config = of_device_get_match_data(&pdev->dev);
	if (!config && !pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	/*
	 * Allocate and initialize the DRM device, driver private data, I/O
	 * resources and clocks.
	 */
	sdev = devm_drm_dev_alloc(&pdev->dev, &shmob_drm_driver,
				  struct shmob_drm_device, ddev);
	if (IS_ERR(sdev))
		return PTR_ERR(sdev);

	ddev = &sdev->ddev;
	sdev->dev = &pdev->dev;
	if (config) {
		sdev->config = *config;
	} else {
		sdev->pdata = pdata;
		sdev->config.clk_source = pdata->clk_source;
		sdev->config.clk_div = pdata->iface.clk_div;
	}
	spin_lock_init(&sdev->irq_lock);

	platform_set_drvdata(pdev, sdev);

	sdev->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sdev->mmio))
		return PTR_ERR(sdev->mmio);

	ret = shmob_drm_setup_clocks(sdev, sdev->config.clk_source);
	if (ret < 0)
		return ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = drm_vblank_init(ddev, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		return ret;
	}

	ret = shmob_drm_modeset_init(sdev);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to initialize mode setting\n");

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_modeset_cleanup;
	sdev->irq = ret;

	ret = devm_request_irq(&pdev->dev, sdev->irq, shmob_drm_irq, 0,
			       ddev->driver->name, ddev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to install IRQ handler\n");
		goto err_modeset_cleanup;
	}

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(ddev, 0);
	if (ret < 0)
		goto err_modeset_cleanup;

	drm_fbdev_dma_setup(ddev, 16);

	return 0;

err_modeset_cleanup:
	drm_kms_helper_poll_fini(ddev);
	return ret;
}

static const struct shmob_drm_config shmob_arm_config = {
	.clk_source = SHMOB_DRM_CLK_BUS,
	.clk_div = 5,
};

static const struct of_device_id shmob_drm_of_table[] __maybe_unused = {
	{ .compatible = "renesas,r8a7740-lcdc",	.data = &shmob_arm_config, },
	{ .compatible = "renesas,sh73a0-lcdc",	.data = &shmob_arm_config, },
	{ /* sentinel */ }
};

static struct platform_driver shmob_drm_platform_driver = {
	.probe		= shmob_drm_probe,
	.remove_new	= shmob_drm_remove,
	.shutdown	= shmob_drm_shutdown,
	.driver		= {
		.name	= "shmob-drm",
		.of_match_table = of_match_ptr(shmob_drm_of_table),
		.pm	= &shmob_drm_pm_ops,
	},
};

drm_module_platform_driver(shmob_drm_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas SH Mobile DRM Driver");
MODULE_LICENSE("GPL");
