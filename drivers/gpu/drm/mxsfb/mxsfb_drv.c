// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/video/fbdev/mxsfb.c :
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
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

#include "mxsfb_drv.h"
#include "mxsfb_regs.h"

enum mxsfb_devtype {
	MXSFB_V3,
	MXSFB_V4,
	/*
	 * Starting at i.MX6 the hardware version register is gone, use the
	 * i.MX family number as the version.
	 */
	MXSFB_V6,
};

static const struct mxsfb_devdata mxsfb_devdata[] = {
	[MXSFB_V3] = {
		.transfer_count	= LCDC_V3_TRANSFER_COUNT,
		.cur_buf	= LCDC_V3_CUR_BUF,
		.next_buf	= LCDC_V3_NEXT_BUF,
		.hs_wdth_mask	= 0xff,
		.hs_wdth_shift	= 24,
		.has_overlay	= false,
		.has_ctrl2	= false,
		.has_crc32	= false,
	},
	[MXSFB_V4] = {
		.transfer_count	= LCDC_V4_TRANSFER_COUNT,
		.cur_buf	= LCDC_V4_CUR_BUF,
		.next_buf	= LCDC_V4_NEXT_BUF,
		.hs_wdth_mask	= 0x3fff,
		.hs_wdth_shift	= 18,
		.has_overlay	= false,
		.has_ctrl2	= true,
		.has_crc32	= true,
	},
	[MXSFB_V6] = {
		.transfer_count	= LCDC_V4_TRANSFER_COUNT,
		.cur_buf	= LCDC_V4_CUR_BUF,
		.next_buf	= LCDC_V4_NEXT_BUF,
		.hs_wdth_mask	= 0x3fff,
		.hs_wdth_shift	= 18,
		.has_overlay	= true,
		.has_ctrl2	= true,
		.has_crc32	= true,
	},
};

void mxsfb_enable_axi_clk(struct mxsfb_drm_private *mxsfb)
{
	if (mxsfb->clk_axi)
		clk_prepare_enable(mxsfb->clk_axi);
}

void mxsfb_disable_axi_clk(struct mxsfb_drm_private *mxsfb)
{
	if (mxsfb->clk_axi)
		clk_disable_unprepare(mxsfb->clk_axi);
}

static struct drm_framebuffer *
mxsfb_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info;

	info = drm_get_format_info(dev, mode_cmd);
	if (!info)
		return ERR_PTR(-EINVAL);

	if (mode_cmd->width * info->cpp[0] != mode_cmd->pitches[0]) {
		dev_dbg(dev->dev, "Invalid pitch: fb width must match pitch\n");
		return ERR_PTR(-EINVAL);
	}

	return drm_gem_fb_create(dev, file_priv, mode_cmd);
}

static const struct drm_mode_config_funcs mxsfb_mode_config_funcs = {
	.fb_create		= mxsfb_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs mxsfb_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static int mxsfb_attach_bridge(struct mxsfb_drm_private *mxsfb)
{
	struct drm_device *drm = mxsfb->drm;
	struct drm_connector_list_iter iter;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
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

	ret = drm_bridge_attach(&mxsfb->encoder, bridge, NULL, 0);
	if (ret)
		return dev_err_probe(drm->dev, ret, "Failed to attach bridge\n");

	mxsfb->bridge = bridge;

	/*
	 * Get hold of the connector. This is a bit of a hack, until the bridge
	 * API gives us bus flags and formats.
	 */
	drm_connector_list_iter_begin(drm, &iter);
	mxsfb->connector = drm_connector_list_iter_next(&iter);
	drm_connector_list_iter_end(&iter);

	return 0;
}

static irqreturn_t mxsfb_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct mxsfb_drm_private *mxsfb = drm->dev_private;
	u32 reg, crc;
	u64 vbc;

	reg = readl(mxsfb->base + LCDC_CTRL1);

	if (reg & CTRL1_CUR_FRAME_DONE_IRQ) {
		drm_crtc_handle_vblank(&mxsfb->crtc);
		if (mxsfb->crc_active) {
			crc = readl(mxsfb->base + LCDC_V4_CRC_STAT);
			vbc = drm_crtc_accurate_vblank_count(&mxsfb->crtc);
			drm_crtc_add_crc_entry(&mxsfb->crtc, true, vbc, &crc);
		}
	}

	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);

	return IRQ_HANDLED;
}

static void mxsfb_irq_disable(struct drm_device *drm)
{
	struct mxsfb_drm_private *mxsfb = drm->dev_private;

	mxsfb_enable_axi_clk(mxsfb);

	/* Disable and clear VBLANK IRQ */
	writel(CTRL1_CUR_FRAME_DONE_IRQ_EN, mxsfb->base + LCDC_CTRL1 + REG_CLR);
	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);

	mxsfb_disable_axi_clk(mxsfb);
}

static int mxsfb_irq_install(struct drm_device *dev, int irq)
{
	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	mxsfb_irq_disable(dev);

	return request_irq(irq, mxsfb_irq_handler, 0,  dev->driver->name, dev);
}

static void mxsfb_irq_uninstall(struct drm_device *dev)
{
	struct mxsfb_drm_private *mxsfb = dev->dev_private;

	mxsfb_irq_disable(dev);
	free_irq(mxsfb->irq, dev);
}

static int mxsfb_load(struct drm_device *drm,
		      const struct mxsfb_devdata *devdata)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct mxsfb_drm_private *mxsfb;
	struct resource *res;
	int ret;

	mxsfb = devm_kzalloc(&pdev->dev, sizeof(*mxsfb), GFP_KERNEL);
	if (!mxsfb)
		return -ENOMEM;

	mxsfb->drm = drm;
	drm->dev_private = mxsfb;
	mxsfb->devdata = devdata;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mxsfb->base = devm_ioremap_resource(drm->dev, res);
	if (IS_ERR(mxsfb->base))
		return PTR_ERR(mxsfb->base);

	mxsfb->clk = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(mxsfb->clk))
		return PTR_ERR(mxsfb->clk);

	mxsfb->clk_axi = devm_clk_get(drm->dev, "axi");
	if (IS_ERR(mxsfb->clk_axi))
		mxsfb->clk_axi = NULL;

	mxsfb->clk_disp_axi = devm_clk_get(drm->dev, "disp_axi");
	if (IS_ERR(mxsfb->clk_disp_axi))
		mxsfb->clk_disp_axi = NULL;

	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	pm_runtime_enable(drm->dev);

	/* Modeset init */
	drm_mode_config_init(drm);

	ret = mxsfb_kms_init(mxsfb);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialize KMS pipeline\n");
		goto err_vblank;
	}

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialise vblank\n");
		goto err_vblank;
	}

	/* Start with vertical blanking interrupt reporting disabled. */
	drm_crtc_vblank_off(&mxsfb->crtc);

	ret = mxsfb_attach_bridge(mxsfb);
	if (ret) {
		dev_err_probe(drm->dev, ret, "Cannot connect bridge\n");
		goto err_vblank;
	}

	drm->mode_config.min_width	= MXSFB_MIN_XRES;
	drm->mode_config.min_height	= MXSFB_MIN_YRES;
	drm->mode_config.max_width	= MXSFB_MAX_XRES;
	drm->mode_config.max_height	= MXSFB_MAX_YRES;
	drm->mode_config.funcs		= &mxsfb_mode_config_funcs;
	drm->mode_config.helper_private	= &mxsfb_mode_config_helpers;

	drm_mode_config_reset(drm);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_vblank;
	mxsfb->irq = ret;

	pm_runtime_get_sync(drm->dev);
	ret = mxsfb_irq_install(drm, mxsfb->irq);
	pm_runtime_put_sync(drm->dev);

	if (ret < 0) {
		dev_err(drm->dev, "Failed to install IRQ handler\n");
		goto err_vblank;
	}

	drm_kms_helper_poll_init(drm);

	platform_set_drvdata(pdev, drm);

	drm_helper_hpd_irq_event(drm);

	return 0;

err_vblank:
	pm_runtime_disable(drm->dev);

	return ret;
}

static void mxsfb_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);

	pm_runtime_get_sync(drm->dev);
	mxsfb_irq_uninstall(drm);
	pm_runtime_put_sync(drm->dev);

	drm->dev_private = NULL;

	pm_runtime_disable(drm->dev);
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static const struct drm_driver mxsfb_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_CMA_DRIVER_OPS,
	.fops	= &fops,
	.name	= "mxsfb-drm",
	.desc	= "MXSFB Controller DRM",
	.date	= "20160824",
	.major	= 1,
	.minor	= 0,
};

static const struct of_device_id mxsfb_dt_ids[] = {
	{ .compatible = "fsl,imx23-lcdif", .data = &mxsfb_devdata[MXSFB_V3], },
	{ .compatible = "fsl,imx28-lcdif", .data = &mxsfb_devdata[MXSFB_V4], },
	{ .compatible = "fsl,imx6sx-lcdif", .data = &mxsfb_devdata[MXSFB_V6], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxsfb_dt_ids);

static int mxsfb_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	const struct of_device_id *of_id =
			of_match_device(mxsfb_dt_ids, &pdev->dev);
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	drm = drm_dev_alloc(&mxsfb_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = mxsfb_load(drm, of_id->data);
	if (ret)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unload;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

err_unload:
	mxsfb_unload(drm);
err_free:
	drm_dev_put(drm);

	return ret;
}

static int mxsfb_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	mxsfb_unload(drm);
	drm_dev_put(drm);

	return 0;
}

static void mxsfb_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(drm);
}

#ifdef CONFIG_PM_SLEEP
static int mxsfb_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int mxsfb_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops mxsfb_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mxsfb_suspend, mxsfb_resume)
};

static struct platform_driver mxsfb_platform_driver = {
	.probe		= mxsfb_probe,
	.remove		= mxsfb_remove,
	.shutdown	= mxsfb_shutdown,
	.driver	= {
		.name		= "mxsfb",
		.of_match_table	= mxsfb_dt_ids,
		.pm		= &mxsfb_pm_ops,
	},
};

drm_module_platform_driver(mxsfb_platform_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale MXS DRM/KMS driver");
MODULE_LICENSE("GPL");
