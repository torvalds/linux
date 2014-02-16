/*
 * shmob_drm_drv.c  --  SH Mobile DRM driver
 *
 * Copyright (C) 2012 Renesas Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

/* -----------------------------------------------------------------------------
 * Hardware initialization
 */

static int shmob_drm_init_interface(struct shmob_drm_device *sdev)
{
	static const u32 ldmt1r[] = {
		[SHMOB_DRM_IFACE_RGB8] = LDMT1R_MIFTYP_RGB8,
		[SHMOB_DRM_IFACE_RGB9] = LDMT1R_MIFTYP_RGB9,
		[SHMOB_DRM_IFACE_RGB12A] = LDMT1R_MIFTYP_RGB12A,
		[SHMOB_DRM_IFACE_RGB12B] = LDMT1R_MIFTYP_RGB12B,
		[SHMOB_DRM_IFACE_RGB16] = LDMT1R_MIFTYP_RGB16,
		[SHMOB_DRM_IFACE_RGB18] = LDMT1R_MIFTYP_RGB18,
		[SHMOB_DRM_IFACE_RGB24] = LDMT1R_MIFTYP_RGB24,
		[SHMOB_DRM_IFACE_YUV422] = LDMT1R_MIFTYP_YCBCR,
		[SHMOB_DRM_IFACE_SYS8A] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS8A,
		[SHMOB_DRM_IFACE_SYS8B] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS8B,
		[SHMOB_DRM_IFACE_SYS8C] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS8C,
		[SHMOB_DRM_IFACE_SYS8D] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS8D,
		[SHMOB_DRM_IFACE_SYS9] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS9,
		[SHMOB_DRM_IFACE_SYS12] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS12,
		[SHMOB_DRM_IFACE_SYS16A] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS16A,
		[SHMOB_DRM_IFACE_SYS16B] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS16B,
		[SHMOB_DRM_IFACE_SYS16C] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS16C,
		[SHMOB_DRM_IFACE_SYS18] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS18,
		[SHMOB_DRM_IFACE_SYS24] = LDMT1R_IFM | LDMT1R_MIFTYP_SYS24,
	};

	if (sdev->pdata->iface.interface >= ARRAY_SIZE(ldmt1r)) {
		dev_err(sdev->dev, "invalid interface type %u\n",
			sdev->pdata->iface.interface);
		return -EINVAL;
	}

	sdev->ldmt1r = ldmt1r[sdev->pdata->iface.interface];
	return 0;
}

static int shmob_drm_setup_clocks(struct shmob_drm_device *sdev,
					    enum shmob_drm_clk_source clksrc)
{
	struct clk *clk;
	char *clkname;

	switch (clksrc) {
	case SHMOB_DRM_CLK_BUS:
		clkname = "bus_clk";
		sdev->lddckr = LDDCKR_ICKSEL_BUS;
		break;
	case SHMOB_DRM_CLK_PERIPHERAL:
		clkname = "peripheral_clk";
		sdev->lddckr = LDDCKR_ICKSEL_MIPI;
		break;
	case SHMOB_DRM_CLK_EXTERNAL:
		clkname = NULL;
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

static int shmob_drm_unload(struct drm_device *dev)
{
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);
	drm_irq_uninstall(dev);

	dev->dev_private = NULL;

	return 0;
}

static int shmob_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct shmob_drm_platform_data *pdata = dev->dev->platform_data;
	struct platform_device *pdev = dev->platformdev;
	struct shmob_drm_device *sdev;
	struct resource *res;
	unsigned int i;
	int ret;

	if (pdata == NULL) {
		dev_err(dev->dev, "no platform data\n");
		return -EINVAL;
	}

	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (sdev == NULL) {
		dev_err(dev->dev, "failed to allocate private data\n");
		return -ENOMEM;
	}

	sdev->dev = &pdev->dev;
	sdev->pdata = pdata;
	spin_lock_init(&sdev->irq_lock);

	sdev->ddev = dev;
	dev->dev_private = sdev;

	/* I/O resources and clocks */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -EINVAL;
	}

	sdev->mmio = devm_ioremap_nocache(&pdev->dev, res->start,
					  resource_size(res));
	if (sdev->mmio == NULL) {
		dev_err(&pdev->dev, "failed to remap memory resource\n");
		return -ENOMEM;
	}

	ret = shmob_drm_setup_clocks(sdev, pdata->clk_source);
	if (ret < 0)
		return ret;

	ret = shmob_drm_init_interface(sdev);
	if (ret < 0)
		return ret;

	ret = shmob_drm_modeset_init(sdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize mode setting\n");
		return ret;
	}

	for (i = 0; i < 4; ++i) {
		ret = shmob_drm_plane_create(sdev, i);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to create plane %u\n", i);
			goto done;
		}
	}

	ret = drm_vblank_init(dev, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto done;
	}

	ret = drm_irq_install(dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to install IRQ handler\n");
		goto done;
	}

	platform_set_drvdata(pdev, sdev);

done:
	if (ret)
		shmob_drm_unload(dev);

	return ret;
}

static void shmob_drm_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct shmob_drm_device *sdev = dev->dev_private;

	shmob_drm_crtc_cancel_page_flip(&sdev->crtc, file);
}

static irqreturn_t shmob_drm_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct shmob_drm_device *sdev = dev->dev_private;
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
		drm_handle_vblank(dev, 0);
		shmob_drm_crtc_finish_page_flip(&sdev->crtc);
	}

	return IRQ_HANDLED;
}

static int shmob_drm_enable_vblank(struct drm_device *dev, int crtc)
{
	struct shmob_drm_device *sdev = dev->dev_private;

	shmob_drm_crtc_enable_vblank(sdev, true);

	return 0;
}

static void shmob_drm_disable_vblank(struct drm_device *dev, int crtc)
{
	struct shmob_drm_device *sdev = dev->dev_private;

	shmob_drm_crtc_enable_vblank(sdev, false);
}

static const struct file_operations shmob_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver shmob_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_MODESET
				| DRIVER_PRIME,
	.load			= shmob_drm_load,
	.unload			= shmob_drm_unload,
	.preclose		= shmob_drm_preclose,
	.irq_handler		= shmob_drm_irq,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= shmob_drm_enable_vblank,
	.disable_vblank		= shmob_drm_disable_vblank,
	.gem_free_object	= drm_gem_cma_free_object,
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

#if CONFIG_PM_SLEEP
static int shmob_drm_pm_suspend(struct device *dev)
{
	struct shmob_drm_device *sdev = dev_get_drvdata(dev);

	drm_kms_helper_poll_disable(sdev->ddev);
	shmob_drm_crtc_suspend(&sdev->crtc);

	return 0;
}

static int shmob_drm_pm_resume(struct device *dev)
{
	struct shmob_drm_device *sdev = dev_get_drvdata(dev);

	drm_modeset_lock_all(sdev->ddev);
	shmob_drm_crtc_resume(&sdev->crtc);
	drm_modeset_unlock_all(sdev->ddev);

	drm_kms_helper_poll_enable(sdev->ddev);
	return 0;
}
#endif

static const struct dev_pm_ops shmob_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(shmob_drm_pm_suspend, shmob_drm_pm_resume)
};

/* -----------------------------------------------------------------------------
 * Platform driver
 */

static int shmob_drm_probe(struct platform_device *pdev)
{
	return drm_platform_init(&shmob_drm_driver, pdev);
}

static int shmob_drm_remove(struct platform_device *pdev)
{
	struct shmob_drm_device *sdev = platform_get_drvdata(pdev);

	drm_put_dev(sdev->ddev);

	return 0;
}

static struct platform_driver shmob_drm_platform_driver = {
	.probe		= shmob_drm_probe,
	.remove		= shmob_drm_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "shmob-drm",
		.pm	= &shmob_drm_pm_ops,
	},
};

module_platform_driver(shmob_drm_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas SH Mobile DRM Driver");
MODULE_LICENSE("GPL");
