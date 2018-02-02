/*
 * (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 *
 */

/**
 * DOC: ARM PrimeCell PL111 CLCD Driver
 *
 * The PL111 is a simple LCD controller that can support TFT and STN
 * displays.  This driver exposes a standard KMS interface for them.
 *
 * This driver uses the same Device Tree binding as the fbdev CLCD
 * driver.  While the fbdev driver supports panels that may be
 * connected to the CLCD internally to the CLCD driver, in DRM the
 * panels get split out to drivers/gpu/drm/panels/.  This means that,
 * in converting from using fbdev to using DRM, you also need to write
 * a panel driver (which may be as simple as an entry in
 * panel-simple.c).
 *
 * The driver currently doesn't expose the cursor.  The DRM API for
 * cursors requires support for 64x64 ARGB8888 cursor images, while
 * the hardware can only support 64x64 monochrome with masking
 * cursors.  While one could imagine trying to hack something together
 * to look at the ARGB8888 and program reasonable in monochrome, we
 * just don't expose the cursor at all instead, and leave cursor
 * support to the X11 software cursor layer.
 *
 * TODO:
 *
 * - Fix race between setting plane base address and getting IRQ for
 *   vsync firing the pageflip completion.
 *
 * - Use the "max-memory-bandwidth" DT property to filter the
 *   supported formats.
 *
 * - Read back hardware state at boot to skip reprogramming the
 *   hardware when doing a no-op modeset.
 *
 * - Use the CLKSEL bit to support switching between the two external
 *   clock parents.
 */

#include <linux/amba/bus.h>
#include <linux/amba/clcd-regs.h>
#include <linux/version.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>

#include "pl111_drm.h"
#include "pl111_versatile.h"

#define DRIVER_DESC      "DRM module for PL111"

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int pl111_modeset_init(struct drm_device *dev)
{
	struct drm_mode_config *mode_config;
	struct pl111_drm_dev_private *priv = dev->dev_private;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	int ret = 0;

	drm_mode_config_init(dev);
	mode_config = &dev->mode_config;
	mode_config->funcs = &mode_config_funcs;
	mode_config->min_width = 1;
	mode_config->max_width = 1024;
	mode_config->min_height = 1;
	mode_config->max_height = 768;

	ret = drm_of_find_panel_or_bridge(dev->dev->of_node,
					  0, 0, &panel, &bridge);
	if (ret && ret != -ENODEV)
		return ret;
	if (panel) {
		bridge = drm_panel_bridge_add(panel,
					      DRM_MODE_CONNECTOR_Unknown);
		if (IS_ERR(bridge)) {
			ret = PTR_ERR(bridge);
			goto out_config;
		}
		/*
		 * TODO: when we are using a different bridge than a panel
		 * (such as a dumb VGA connector) we need to devise a different
		 * method to get the connector out of the bridge.
		 */
	}

	ret = pl111_display_init(dev);
	if (ret != 0) {
		dev_err(dev->dev, "Failed to init display\n");
		goto out_bridge;
	}

	ret = drm_simple_display_pipe_attach_bridge(&priv->pipe,
						    bridge);
	if (ret)
		return ret;

	priv->bridge = bridge;
	priv->panel = panel;
	priv->connector = panel->connector;

	ret = drm_vblank_init(dev, 1);
	if (ret != 0) {
		dev_err(dev->dev, "Failed to init vblank\n");
		goto out_bridge;
	}

	drm_mode_config_reset(dev);

	drm_fb_cma_fbdev_init(dev, 32, 0);

	drm_kms_helper_poll_init(dev);

	goto finish;

out_bridge:
	if (panel)
		drm_panel_bridge_remove(bridge);
out_config:
	drm_mode_config_cleanup(dev);
finish:
	return ret;
}

DEFINE_DRM_GEM_CMA_FOPS(drm_fops);

static struct drm_driver pl111_drm_driver = {
	.driver_features =
		DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME | DRIVER_ATOMIC,
	.lastclose = drm_fb_helper_lastclose,
	.ioctls = NULL,
	.fops = &drm_fops,
	.name = "pl111",
	.desc = DRIVER_DESC,
	.date = "20170317",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.dumb_create = drm_gem_cma_dumb_create,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,

	.enable_vblank = pl111_enable_vblank,
	.disable_vblank = pl111_disable_vblank,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = pl111_debugfs_init,
#endif
};

static int pl111_amba_probe(struct amba_device *amba_dev,
			    const struct amba_id *id)
{
	struct device *dev = &amba_dev->dev;
	struct pl111_drm_dev_private *priv;
	struct pl111_variant_data *variant = id->data;
	struct drm_device *drm;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	drm = drm_dev_alloc(&pl111_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);
	amba_set_drvdata(amba_dev, drm);
	priv->drm = drm;
	drm->dev_private = priv;
	priv->variant = variant;

	/*
	 * The PL110 and PL111 variants have two registers
	 * swapped: interrupt enable and control. For this reason
	 * we use offsets that we can change per variant.
	 */
	if (variant->is_pl110) {
		/*
		 * The ARM Versatile boards are even more special:
		 * their PrimeCell ID say they are PL110 but the
		 * control and interrupt enable registers are anyway
		 * swapped to the PL111 order so they are not following
		 * the PL110 datasheet.
		 */
		if (of_machine_is_compatible("arm,versatile-ab") ||
		    of_machine_is_compatible("arm,versatile-pb")) {
			priv->ienb = CLCD_PL111_IENB;
			priv->ctrl = CLCD_PL111_CNTL;
		} else {
			priv->ienb = CLCD_PL110_IENB;
			priv->ctrl = CLCD_PL110_CNTL;
		}
	} else {
		priv->ienb = CLCD_PL111_IENB;
		priv->ctrl = CLCD_PL111_CNTL;
	}

	priv->regs = devm_ioremap_resource(dev, &amba_dev->res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "%s failed mmio\n", __func__);
		return PTR_ERR(priv->regs);
	}

	/* turn off interrupts before requesting the irq */
	writel(0, priv->regs + priv->ienb);

	ret = devm_request_irq(dev, amba_dev->irq[0], pl111_irq, 0,
			       variant->name, priv);
	if (ret != 0) {
		dev_err(dev, "%s failed irq %d\n", __func__, ret);
		return ret;
	}

	ret = pl111_versatile_init(dev, priv);
	if (ret)
		goto dev_unref;

	ret = pl111_modeset_init(drm);
	if (ret != 0)
		goto dev_unref;

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto dev_unref;

	return 0;

dev_unref:
	drm_dev_unref(drm);
	return ret;
}

static int pl111_amba_remove(struct amba_device *amba_dev)
{
	struct drm_device *drm = amba_get_drvdata(amba_dev);
	struct pl111_drm_dev_private *priv = drm->dev_private;

	drm_dev_unregister(drm);
	drm_fb_cma_fbdev_fini(drm);
	if (priv->panel)
		drm_panel_bridge_remove(priv->bridge);
	drm_mode_config_cleanup(drm);
	drm_dev_unref(drm);

	return 0;
}

/*
 * This variant exist in early versions like the ARM Integrator
 * and this version lacks the 565 and 444 pixel formats.
 */
static const u32 pl110_pixel_formats[] = {
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
};

static const struct pl111_variant_data pl110_variant = {
	.name = "PL110",
	.is_pl110 = true,
	.formats = pl110_pixel_formats,
	.nformats = ARRAY_SIZE(pl110_pixel_formats),
};

/* RealView, Versatile Express etc use this modern variant */
static const u32 pl111_pixel_formats[] = {
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB4444,
};

static const struct pl111_variant_data pl111_variant = {
	.name = "PL111",
	.formats = pl111_pixel_formats,
	.nformats = ARRAY_SIZE(pl111_pixel_formats),
};

static const struct amba_id pl111_id_table[] = {
	{
		.id = 0x00041110,
		.mask = 0x000fffff,
		.data = (void*)&pl110_variant,
	},
	{
		.id = 0x00041111,
		.mask = 0x000fffff,
		.data = (void*)&pl111_variant,
	},
	{0, 0},
};

static struct amba_driver pl111_amba_driver __maybe_unused = {
	.drv = {
		.name = "drm-clcd-pl111",
	},
	.probe = pl111_amba_probe,
	.remove = pl111_amba_remove,
	.id_table = pl111_id_table,
};

#ifdef CONFIG_ARM_AMBA
module_amba_driver(pl111_amba_driver);
#endif

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("ARM Ltd.");
MODULE_LICENSE("GPL");
