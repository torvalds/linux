// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "fsl_dcu_drm_crtc.h"
#include "fsl_dcu_drm_drv.h"
#include "fsl_tcon.h"

static int legacyfb_depth = 24;
module_param(legacyfb_depth, int, 0444);

static bool fsl_dcu_drm_is_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == DCU_INT_STATUS || reg == DCU_UPDATE_MODE)
		return true;

	return false;
}

static const struct regmap_config fsl_dcu_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.volatile_reg = fsl_dcu_drm_is_volatile_reg,
};

static void fsl_dcu_irq_reset(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	regmap_write(fsl_dev->regmap, DCU_INT_STATUS, ~0);
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, ~0);
}

static irqreturn_t fsl_dcu_drm_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int int_status;
	int ret;

	ret = regmap_read(fsl_dev->regmap, DCU_INT_STATUS, &int_status);
	if (ret) {
		dev_err(dev->dev, "read DCU_INT_STATUS failed\n");
		return IRQ_NONE;
	}

	if (int_status & DCU_INT_STATUS_VBLANK)
		drm_handle_vblank(dev, 0);

	regmap_write(fsl_dev->regmap, DCU_INT_STATUS, int_status);

	return IRQ_HANDLED;
}

static int fsl_dcu_irq_install(struct drm_device *dev, unsigned int irq)
{
	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	fsl_dcu_irq_reset(dev);

	return request_irq(irq, fsl_dcu_drm_irq, 0, dev->driver->name, dev);
}

static void fsl_dcu_irq_uninstall(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	fsl_dcu_irq_reset(dev);
	free_irq(fsl_dev->irq, dev);
}

static int fsl_dcu_load(struct drm_device *dev, unsigned long flags)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	int ret;

	ret = fsl_dcu_drm_modeset_init(fsl_dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize mode setting\n");
		return ret;
	}

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		goto done_vblank;
	}

	ret = fsl_dcu_irq_install(dev, fsl_dev->irq);
	if (ret < 0) {
		dev_err(dev->dev, "failed to install IRQ handler\n");
		goto done_irq;
	}

	if (legacyfb_depth != 16 && legacyfb_depth != 24 &&
	    legacyfb_depth != 32) {
		dev_warn(dev->dev,
			"Invalid legacyfb_depth.  Defaulting to 24bpp\n");
		legacyfb_depth = 24;
	}

	return 0;
done_irq:
	drm_kms_helper_poll_fini(dev);

	drm_mode_config_cleanup(dev);
done_vblank:
	dev->dev_private = NULL;

	return ret;
}

static void fsl_dcu_unload(struct drm_device *dev)
{
	drm_atomic_helper_shutdown(dev);
	drm_kms_helper_poll_fini(dev);

	drm_mode_config_cleanup(dev);
	fsl_dcu_irq_uninstall(dev);

	dev->dev_private = NULL;
}

DEFINE_DRM_GEM_DMA_FOPS(fsl_dcu_drm_fops);

static const struct drm_driver fsl_dcu_drm_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.load			= fsl_dcu_load,
	.unload			= fsl_dcu_unload,
	DRM_GEM_DMA_DRIVER_OPS,
	.fops			= &fsl_dcu_drm_fops,
	.name			= "fsl-dcu-drm",
	.desc			= "Freescale DCU DRM",
	.date			= "20160425",
	.major			= 1,
	.minor			= 1,
};

#ifdef CONFIG_PM_SLEEP
static int fsl_dcu_drm_pm_suspend(struct device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev_get_drvdata(dev);
	int ret;

	if (!fsl_dev)
		return 0;

	disable_irq(fsl_dev->irq);

	ret = drm_mode_config_helper_suspend(fsl_dev->drm);
	if (ret) {
		enable_irq(fsl_dev->irq);
		return ret;
	}

	clk_disable_unprepare(fsl_dev->clk);

	return 0;
}

static int fsl_dcu_drm_pm_resume(struct device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev_get_drvdata(dev);
	int ret;

	if (!fsl_dev)
		return 0;

	ret = clk_prepare_enable(fsl_dev->clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable dcu clk\n");
		return ret;
	}

	if (fsl_dev->tcon)
		fsl_tcon_bypass_enable(fsl_dev->tcon);
	fsl_dcu_drm_init_planes(fsl_dev->drm);
	enable_irq(fsl_dev->irq);

	drm_mode_config_helper_resume(fsl_dev->drm);

	return 0;
}
#endif

static const struct dev_pm_ops fsl_dcu_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fsl_dcu_drm_pm_suspend, fsl_dcu_drm_pm_resume)
};

static const struct fsl_dcu_soc_data fsl_dcu_ls1021a_data = {
	.name = "ls1021a",
	.total_layer = 16,
	.max_layer = 4,
	.layer_regs = LS1021A_LAYER_REG_NUM,
};

static const struct fsl_dcu_soc_data fsl_dcu_vf610_data = {
	.name = "vf610",
	.total_layer = 64,
	.max_layer = 6,
	.layer_regs = VF610_LAYER_REG_NUM,
};

static const struct of_device_id fsl_dcu_of_match[] = {
	{
		.compatible = "fsl,ls1021a-dcu",
		.data = &fsl_dcu_ls1021a_data,
	}, {
		.compatible = "fsl,vf610-dcu",
		.data = &fsl_dcu_vf610_data,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, fsl_dcu_of_match);

static int fsl_dcu_drm_probe(struct platform_device *pdev)
{
	struct fsl_dcu_drm_device *fsl_dev;
	struct drm_device *drm;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *base;
	struct clk *pix_clk_in;
	char pix_clk_name[32];
	const char *pix_clk_in_name;
	const struct of_device_id *id;
	int ret;
	u8 div_ratio_shift = 0;

	fsl_dev = devm_kzalloc(dev, sizeof(*fsl_dev), GFP_KERNEL);
	if (!fsl_dev)
		return -ENOMEM;

	id = of_match_node(fsl_dcu_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;
	fsl_dev->soc = id->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		return ret;
	}

	fsl_dev->irq = platform_get_irq(pdev, 0);
	if (fsl_dev->irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return fsl_dev->irq;
	}

	fsl_dev->regmap = devm_regmap_init_mmio(dev, base,
			&fsl_dcu_regmap_config);
	if (IS_ERR(fsl_dev->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(fsl_dev->regmap);
	}

	fsl_dev->clk = devm_clk_get(dev, "dcu");
	if (IS_ERR(fsl_dev->clk)) {
		dev_err(dev, "failed to get dcu clock\n");
		return PTR_ERR(fsl_dev->clk);
	}
	ret = clk_prepare_enable(fsl_dev->clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable dcu clk\n");
		return ret;
	}

	pix_clk_in = devm_clk_get(dev, "pix");
	if (IS_ERR(pix_clk_in)) {
		/* legancy binding, use dcu clock as pixel clock input */
		pix_clk_in = fsl_dev->clk;
	}

	if (of_property_read_bool(dev->of_node, "big-endian"))
		div_ratio_shift = 24;

	pix_clk_in_name = __clk_get_name(pix_clk_in);
	snprintf(pix_clk_name, sizeof(pix_clk_name), "%s_pix", pix_clk_in_name);
	fsl_dev->pix_clk = clk_register_divider(dev, pix_clk_name,
			pix_clk_in_name, 0, base + DCU_DIV_RATIO,
			div_ratio_shift, 8, CLK_DIVIDER_ROUND_CLOSEST, NULL);
	if (IS_ERR(fsl_dev->pix_clk)) {
		dev_err(dev, "failed to register pix clk\n");
		ret = PTR_ERR(fsl_dev->pix_clk);
		goto disable_clk;
	}

	fsl_dev->tcon = fsl_tcon_init(dev);

	drm = drm_dev_alloc(&fsl_dcu_drm_driver, dev);
	if (IS_ERR(drm)) {
		ret = PTR_ERR(drm);
		goto unregister_pix_clk;
	}

	fsl_dev->dev = dev;
	fsl_dev->drm = drm;
	fsl_dev->np = dev->of_node;
	drm->dev_private = fsl_dev;
	dev_set_drvdata(dev, fsl_dev);

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto put;

	drm_fbdev_dma_setup(drm, legacyfb_depth);

	return 0;

put:
	drm_dev_put(drm);
unregister_pix_clk:
	clk_unregister(fsl_dev->pix_clk);
disable_clk:
	clk_disable_unprepare(fsl_dev->clk);
	return ret;
}

static void fsl_dcu_drm_remove(struct platform_device *pdev)
{
	struct fsl_dcu_drm_device *fsl_dev = platform_get_drvdata(pdev);

	drm_dev_unregister(fsl_dev->drm);
	drm_dev_put(fsl_dev->drm);
	clk_disable_unprepare(fsl_dev->clk);
	clk_unregister(fsl_dev->pix_clk);
}

static void fsl_dcu_drm_shutdown(struct platform_device *pdev)
{
	struct fsl_dcu_drm_device *fsl_dev = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(fsl_dev->drm);
}

static struct platform_driver fsl_dcu_drm_platform_driver = {
	.probe		= fsl_dcu_drm_probe,
	.remove_new	= fsl_dcu_drm_remove,
	.shutdown	= fsl_dcu_drm_shutdown,
	.driver		= {
		.name	= "fsl-dcu",
		.pm	= &fsl_dcu_drm_pm_ops,
		.of_match_table = fsl_dcu_of_match,
	},
};

drm_module_platform_driver(fsl_dcu_drm_platform_driver);

MODULE_DESCRIPTION("Freescale DCU DRM Driver");
MODULE_LICENSE("GPL");
