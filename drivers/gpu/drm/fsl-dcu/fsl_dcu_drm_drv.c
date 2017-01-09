/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

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

static int fsl_dcu_drm_irq_init(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	int ret;

	ret = drm_irq_install(dev, fsl_dev->irq);
	if (ret < 0)
		dev_err(dev->dev, "failed to install IRQ handler\n");

	regmap_write(fsl_dev->regmap, DCU_INT_STATUS, 0);
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, ~0);

	return ret;
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
		goto done;
	}

	ret = fsl_dcu_drm_irq_init(dev);
	if (ret < 0)
		goto done;
	dev->irq_enabled = true;

	if (legacyfb_depth != 16 && legacyfb_depth != 24 &&
	    legacyfb_depth != 32) {
		dev_warn(dev->dev,
			"Invalid legacyfb_depth.  Defaulting to 24bpp\n");
		legacyfb_depth = 24;
	}
	fsl_dev->fbdev = drm_fbdev_cma_init(dev, legacyfb_depth, 1, 1);
	if (IS_ERR(fsl_dev->fbdev)) {
		ret = PTR_ERR(fsl_dev->fbdev);
		fsl_dev->fbdev = NULL;
		goto done;
	}

	return 0;
done:
	drm_kms_helper_poll_fini(dev);

	if (fsl_dev->fbdev)
		drm_fbdev_cma_fini(fsl_dev->fbdev);

	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);
	drm_irq_uninstall(dev);
	dev->dev_private = NULL;

	return ret;
}

static void fsl_dcu_unload(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	drm_crtc_force_disable_all(dev);
	drm_kms_helper_poll_fini(dev);

	if (fsl_dev->fbdev)
		drm_fbdev_cma_fini(fsl_dev->fbdev);

	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);
	drm_irq_uninstall(dev);

	dev->dev_private = NULL;
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

static int fsl_dcu_drm_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int value;

	regmap_read(fsl_dev->regmap, DCU_INT_MASK, &value);
	value &= ~DCU_INT_MASK_VBLANK;
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, value);

	return 0;
}

static void fsl_dcu_drm_disable_vblank(struct drm_device *dev,
				       unsigned int pipe)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int value;

	regmap_read(fsl_dev->regmap, DCU_INT_MASK, &value);
	value |= DCU_INT_MASK_VBLANK;
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, value);
}

static void fsl_dcu_drm_lastclose(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;

	drm_fbdev_cma_restore_mode(fsl_dev->fbdev);
}

static const struct file_operations fsl_dcu_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver fsl_dcu_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_MODESET
				| DRIVER_PRIME | DRIVER_ATOMIC,
	.lastclose		= fsl_dcu_drm_lastclose,
	.load			= fsl_dcu_load,
	.unload			= fsl_dcu_unload,
	.irq_handler		= fsl_dcu_drm_irq,
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.enable_vblank		= fsl_dcu_drm_enable_vblank,
	.disable_vblank		= fsl_dcu_drm_disable_vblank,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
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

	if (!fsl_dev)
		return 0;

	disable_irq(fsl_dev->irq);
	drm_kms_helper_poll_disable(fsl_dev->drm);

	console_lock();
	drm_fbdev_cma_set_suspend(fsl_dev->fbdev, 1);
	console_unlock();

	fsl_dev->state = drm_atomic_helper_suspend(fsl_dev->drm);
	if (IS_ERR(fsl_dev->state)) {
		console_lock();
		drm_fbdev_cma_set_suspend(fsl_dev->fbdev, 0);
		console_unlock();

		drm_kms_helper_poll_enable(fsl_dev->drm);
		enable_irq(fsl_dev->irq);
		return PTR_ERR(fsl_dev->state);
	}

	clk_disable_unprepare(fsl_dev->pix_clk);
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
	drm_atomic_helper_resume(fsl_dev->drm, fsl_dev->state);

	console_lock();
	drm_fbdev_cma_set_suspend(fsl_dev->fbdev, 0);
	console_unlock();

	drm_kms_helper_poll_enable(fsl_dev->drm);
	enable_irq(fsl_dev->irq);

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
	struct drm_driver *driver = &fsl_dcu_drm_driver;
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

	drm = drm_dev_alloc(driver, dev);
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
		goto unref;

	return 0;

unref:
	drm_dev_unref(drm);
unregister_pix_clk:
	clk_unregister(fsl_dev->pix_clk);
disable_clk:
	clk_disable_unprepare(fsl_dev->clk);
	return ret;
}

static int fsl_dcu_drm_remove(struct platform_device *pdev)
{
	struct fsl_dcu_drm_device *fsl_dev = platform_get_drvdata(pdev);

	drm_dev_unregister(fsl_dev->drm);
	drm_dev_unref(fsl_dev->drm);
	clk_disable_unprepare(fsl_dev->clk);
	clk_unregister(fsl_dev->pix_clk);

	return 0;
}

static struct platform_driver fsl_dcu_drm_platform_driver = {
	.probe		= fsl_dcu_drm_probe,
	.remove		= fsl_dcu_drm_remove,
	.driver		= {
		.name	= "fsl-dcu",
		.pm	= &fsl_dcu_drm_pm_ops,
		.of_match_table = fsl_dcu_of_match,
	},
};

module_platform_driver(fsl_dcu_drm_platform_driver);

MODULE_DESCRIPTION("Freescale DCU DRM Driver");
MODULE_LICENSE("GPL");
