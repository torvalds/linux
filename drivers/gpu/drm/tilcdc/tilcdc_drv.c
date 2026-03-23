// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 */

/* LCDC DRM driver, based on da8xx-fb */

#include <linux/mod_devicetable.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mm.h>
#include <drm/drm_module.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>


#include "tilcdc_drv.h"
#include "tilcdc_encoder.h"
#include "tilcdc_regs.h"

enum tilcdc_variant {
	AM33XX_TILCDC,
	DA850_TILCDC,
};

static const u32 tilcdc_rev1_formats[] = { DRM_FORMAT_RGB565 };

static const u32 tilcdc_straight_formats[] = { DRM_FORMAT_RGB565,
					       DRM_FORMAT_BGR888,
					       DRM_FORMAT_XBGR8888 };

static const u32 tilcdc_crossed_formats[] = { DRM_FORMAT_BGR565,
					      DRM_FORMAT_RGB888,
					      DRM_FORMAT_XRGB8888 };

static const u32 tilcdc_legacy_formats[] = { DRM_FORMAT_RGB565,
					     DRM_FORMAT_RGB888,
					     DRM_FORMAT_XRGB8888 };

static int tilcdc_atomic_check(struct drm_device *dev,
			       struct drm_atomic_state *state)
{
	int ret;

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		return ret;

	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		return ret;

	/*
	 * tilcdc ->atomic_check can update ->mode_changed if pixel format
	 * changes, hence will we check modeset changes again.
	 */
	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		return ret;

	return ret;
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = tilcdc_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void modeset_init(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = priv->max_width;
	dev->mode_config.max_height = 2048;
	dev->mode_config.funcs = &mode_config_funcs;
}

#ifdef CONFIG_CPU_FREQ
static int cpufreq_transition(struct notifier_block *nb,
				     unsigned long val, void *data)
{
	struct tilcdc_drm_private *priv = container_of(nb,
			struct tilcdc_drm_private, freq_transition);

	if (val == CPUFREQ_POSTCHANGE)
		tilcdc_crtc_update_clk(priv->crtc);

	return 0;
}
#endif

static irqreturn_t tilcdc_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(dev);

	return tilcdc_crtc_irq(priv->crtc);
}

static int tilcdc_irq_install(struct drm_device *dev, unsigned int irq)
{
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(dev);
	int ret;

	ret = request_irq(irq, tilcdc_irq, 0, dev->driver->name, dev);
	if (ret)
		return ret;

	priv->irq_enabled = true;

	return 0;
}

static void tilcdc_irq_uninstall(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(dev);

	if (!priv->irq_enabled)
		return;

	free_irq(priv->irq, dev);
	priv->irq_enabled = false;
}

/*
 * DRM operations:
 */

#if defined(CONFIG_DEBUG_FS)
static const struct {
	const char *name;
	uint8_t  rev;
	uint8_t  save;
	uint32_t reg;
} registers[] =		{
#define REG(rev, save, reg) { #reg, rev, save, reg }
		/* exists in revision 1: */
		REG(1, false, LCDC_PID_REG),
		REG(1, true,  LCDC_CTRL_REG),
		REG(1, false, LCDC_STAT_REG),
		REG(1, true,  LCDC_RASTER_CTRL_REG),
		REG(1, true,  LCDC_RASTER_TIMING_0_REG),
		REG(1, true,  LCDC_RASTER_TIMING_1_REG),
		REG(1, true,  LCDC_RASTER_TIMING_2_REG),
		REG(1, true,  LCDC_DMA_CTRL_REG),
		REG(1, true,  LCDC_DMA_FB_BASE_ADDR_0_REG),
		REG(1, true,  LCDC_DMA_FB_CEILING_ADDR_0_REG),
		REG(1, true,  LCDC_DMA_FB_BASE_ADDR_1_REG),
		REG(1, true,  LCDC_DMA_FB_CEILING_ADDR_1_REG),
		/* new in revision 2: */
		REG(2, false, LCDC_RAW_STAT_REG),
		REG(2, false, LCDC_MASKED_STAT_REG),
		REG(2, true, LCDC_INT_ENABLE_SET_REG),
		REG(2, false, LCDC_INT_ENABLE_CLR_REG),
		REG(2, false, LCDC_END_OF_INT_IND_REG),
		REG(2, true,  LCDC_CLK_ENABLE_REG),
#undef REG
};

static int tilcdc_regs_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(dev);
	unsigned i;

	pm_runtime_get_sync(dev->dev);

	seq_printf(m, "revision: %d\n", priv->rev);

	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (priv->rev >= registers[i].rev)
			seq_printf(m, "%s:\t %08x\n", registers[i].name,
					tilcdc_read(dev, registers[i].reg));

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static int tilcdc_mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);
	drm_mm_print(&dev->vma_offset_manager->vm_addr_space_mm, &p);
	return 0;
}

static struct drm_info_list tilcdc_debugfs_list[] = {
		{ "regs", tilcdc_regs_show, 0, NULL },
		{ "mm",   tilcdc_mm_show,   0, NULL },
};

static void tilcdc_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(tilcdc_debugfs_list,
				 ARRAY_SIZE(tilcdc_debugfs_list),
				 minor->debugfs_root, minor);
}
#endif

DEFINE_DRM_GEM_DMA_FOPS(fops);

static const struct drm_driver tilcdc_driver = {
	.driver_features    = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = tilcdc_debugfs_init,
#endif
	.fops               = &fops,
	.name               = "tilcdc",
	.desc               = "TI LCD Controller DRM",
	.major              = 1,
	.minor              = 0,
};

/*
 * Power management:
 */

static int tilcdc_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	int ret = 0;

	ret = drm_mode_config_helper_suspend(ddev);

	/* Select sleep pin state */
	pinctrl_pm_select_sleep_state(dev);

	return ret;
}

static int tilcdc_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	/* Select default pin state */
	pinctrl_pm_select_default_state(dev);
	return  drm_mode_config_helper_resume(ddev);
}

static DEFINE_SIMPLE_DEV_PM_OPS(tilcdc_pm_ops,
				tilcdc_pm_suspend, tilcdc_pm_resume);

static int tilcdc_pdev_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct tilcdc_drm_private *priv;
	struct device *dev = &pdev->dev;
	enum tilcdc_variant variant;
	struct drm_device *ddev;
	u32 bpp = 0;
	int ret;

	priv = devm_drm_dev_alloc(dev, &tilcdc_driver,
				  struct tilcdc_drm_private, ddev);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	variant = (uintptr_t)of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);
	ddev = &priv->ddev;
	ret = drmm_mode_config_init(ddev);
	if (ret)
		return ret;

	priv->wq = alloc_ordered_workqueue("tilcdc", 0);
	if (!priv->wq)
		return -ENOMEM;

	priv->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->mmio)) {
		drm_err(ddev, "failed to request / ioremap\n");
		ret = PTR_ERR(priv->mmio);
		goto free_wq;
	}

	priv->clk = clk_get(dev, "fck");
	if (IS_ERR(priv->clk)) {
		drm_err(ddev, "failed to get functional clock\n");
		ret = -ENODEV;
		goto free_wq;
	}

	pm_runtime_enable(dev);

	/* Determine LCD IP Version */
	pm_runtime_get_sync(dev);
	switch (tilcdc_read(ddev, LCDC_PID_REG)) {
	case 0x4c100102:
		priv->rev = 1;
		break;
	case 0x4f200800:
	case 0x4f201000:
		priv->rev = 2;
		break;
	default:
		drm_warn(ddev, "Unknown PID Reg value 0x%08x, "
			"defaulting to LCD revision 1\n",
			tilcdc_read(ddev, LCDC_PID_REG));
		priv->rev = 1;
		break;
	}

	pm_runtime_put_sync(dev);

	if (priv->rev == 1) {
		DBG("Revision 1 LCDC supports only RGB565 format");
		priv->pixelformats = tilcdc_rev1_formats;
		priv->num_pixelformats = ARRAY_SIZE(tilcdc_rev1_formats);
		bpp = 16;
	} else {
		const char *str = "\0";

		of_property_read_string(node, "blue-and-red-wiring", &str);
		if (0 == strcmp(str, "crossed")) {
			DBG("Configured for crossed blue and red wires");
			priv->pixelformats = tilcdc_crossed_formats;
			priv->num_pixelformats =
				ARRAY_SIZE(tilcdc_crossed_formats);
			bpp = 32; /* Choose bpp with RGB support for fbdef */
		} else if (0 == strcmp(str, "straight")) {
			DBG("Configured for straight blue and red wires");
			priv->pixelformats = tilcdc_straight_formats;
			priv->num_pixelformats =
				ARRAY_SIZE(tilcdc_straight_formats);
			bpp = 16; /* Choose bpp with RGB support for fbdef */
		} else {
			DBG("Blue and red wiring '%s' unknown, use legacy mode",
			    str);
			priv->pixelformats = tilcdc_legacy_formats;
			priv->num_pixelformats =
				ARRAY_SIZE(tilcdc_legacy_formats);
			bpp = 16; /* This is just a guess */
		}
	}

	if (of_property_read_u32(node, "max-bandwidth", &priv->max_bandwidth))
		priv->max_bandwidth = TILCDC_DEFAULT_MAX_BANDWIDTH;

	DBG("Maximum Bandwidth Value %d", priv->max_bandwidth);

	if (of_property_read_u32(node, "max-width", &priv->max_width)) {
		if (priv->rev == 1)
			priv->max_width = TILCDC_DEFAULT_MAX_WIDTH_V1;
		else
			priv->max_width = TILCDC_DEFAULT_MAX_WIDTH_V2;
	}

	DBG("Maximum Horizontal Pixel Width Value %dpixels", priv->max_width);

	if (of_property_read_u32(node, "max-pixelclock",
				 &priv->max_pixelclock))
		priv->max_pixelclock = TILCDC_DEFAULT_MAX_PIXELCLOCK;

	DBG("Maximum Pixel Clock Value %dKHz", priv->max_pixelclock);

	if (variant == DA850_TILCDC)
		priv->fifo_th = 16;
	else
		priv->fifo_th = 8;

	ret = tilcdc_crtc_create(ddev);
	if (ret < 0) {
		drm_err(ddev, "failed to create crtc\n");
		goto disable_pm;
	}
	modeset_init(ddev);

#ifdef CONFIG_CPU_FREQ
	priv->freq_transition.notifier_call = cpufreq_transition;
	ret = cpufreq_register_notifier(&priv->freq_transition,
			CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		drm_err(ddev, "failed to register cpufreq notifier\n");
		priv->freq_transition.notifier_call = NULL;
		goto disable_pm;
	}
#endif

	ret = tilcdc_encoder_create(ddev);
	if (ret)
		goto unregister_cpufreq_notif;

	if (!priv->connector) {
		drm_err(ddev, "no encoders/connectors found\n");
		ret = -EPROBE_DEFER;
		goto unregister_cpufreq_notif;
	}

	ret = drm_vblank_init(ddev, 1);
	if (ret < 0) {
		drm_err(ddev, "failed to initialize vblank\n");
		goto unregister_cpufreq_notif;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto unregister_cpufreq_notif;
	priv->irq = ret;

	ret = tilcdc_irq_install(ddev, priv->irq);
	if (ret < 0) {
		drm_err(ddev, "failed to install IRQ handler\n");
		goto unregister_cpufreq_notif;
	}

	drm_mode_config_reset(ddev);

	drm_kms_helper_poll_init(ddev);

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto stop_poll;

	drm_client_setup_with_color_mode(ddev, bpp);

	return 0;

stop_poll:
	drm_kms_helper_poll_fini(ddev);
	tilcdc_irq_uninstall(ddev);
unregister_cpufreq_notif:
#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&priv->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
#endif
disable_pm:
	pm_runtime_disable(dev);
	clk_put(priv->clk);
free_wq:
	destroy_workqueue(priv->wq);

	return ret;
}

static void tilcdc_pdev_remove(struct platform_device *pdev)
{
	struct tilcdc_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *ddev = &priv->ddev;

	drm_dev_unregister(ddev);
	drm_kms_helper_poll_fini(ddev);
	tilcdc_irq_uninstall(ddev);
#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&priv->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
#endif
	pm_runtime_disable(&pdev->dev);
	clk_put(priv->clk);
	destroy_workqueue(priv->wq);
}

static void tilcdc_pdev_shutdown(struct platform_device *pdev)
{
	struct tilcdc_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *ddev = &priv->ddev;

	drm_atomic_helper_shutdown(ddev);
}

static const struct of_device_id tilcdc_of_match[] = {
		{ .compatible = "ti,am33xx-tilcdc", .data = (void *)AM33XX_TILCDC},
		{ .compatible = "ti,da850-tilcdc", .data = (void *)DA850_TILCDC},
		{ },
};
MODULE_DEVICE_TABLE(of, tilcdc_of_match);

static struct platform_driver tilcdc_platform_driver = {
	.probe      = tilcdc_pdev_probe,
	.remove     = tilcdc_pdev_remove,
	.shutdown   = tilcdc_pdev_shutdown,
	.driver     = {
		.name   = "tilcdc",
		.pm     = pm_sleep_ptr(&tilcdc_pm_ops),
		.of_match_table = tilcdc_of_match,
	},
};

drm_module_platform_driver(tilcdc_platform_driver);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("TI LCD Controller DRM Driver");
MODULE_LICENSE("GPL");
