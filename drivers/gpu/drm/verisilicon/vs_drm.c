// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/aperture.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/console.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_dumb_buffers.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "vs_bridge.h"
#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_dc_top_regs.h"
#include "vs_drm.h"

#define DRIVER_NAME	"verisilicon"
#define DRIVER_DESC	"Verisilicon DC-series display controller driver"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static int vs_gem_dumb_create(struct drm_file *file_priv,
			      struct drm_device *drm,
			      struct drm_mode_create_dumb *args)
{
	int ret;

	/* The hardware wants 128B-aligned pitches for linear buffers. */
	ret = drm_mode_size_dumb(drm, args, 128, 0);
	if (ret)
		return ret;

	return drm_gem_dma_dumb_create_internal(file_priv, drm, args);
}

DEFINE_DRM_GEM_FOPS(vs_drm_driver_fops);

static const struct drm_driver vs_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops			= &vs_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,

	/* GEM Operations */
	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(vs_gem_dumb_create),
	DRM_FBDEV_DMA_DRIVER_OPS,
};

static const struct drm_mode_config_funcs vs_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs vs_mode_config_helper_funcs = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail,
};

static void vs_mode_config_init(struct drm_device *drm)
{
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;
	drm->mode_config.funcs = &vs_mode_config_funcs;
	drm->mode_config.helper_private = &vs_mode_config_helper_funcs;
}

int vs_drm_initialize(struct vs_dc *dc, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_drm_dev *vdrm;
	struct drm_device *drm;
	struct vs_crtc *crtc;
	struct vs_bridge *bridge;
	unsigned int i;
	int ret;

	vdrm = devm_drm_dev_alloc(dev, &vs_drm_driver, struct vs_drm_dev, base);
	if (IS_ERR(vdrm))
		return PTR_ERR(vdrm);

	drm = &vdrm->base;
	vdrm->dc = dc;
	dc->drm_dev = vdrm;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	/* Remove early framebuffers (ie. simple-framebuffer) */
	ret = aperture_remove_all_conflicting_devices(DRIVER_NAME);
	if (ret)
		return ret;

	for (i = 0; i < dc->identity.display_count; i++) {
		crtc = vs_crtc_init(drm, dc, i);
		if (IS_ERR(crtc))
			return PTR_ERR(crtc);

		bridge = vs_bridge_init(drm, crtc);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);

		vdrm->crtcs[i] = crtc;
	}

	ret = drm_vblank_init(drm, dc->identity.display_count);
	if (ret)
		return ret;

	vs_mode_config_init(drm);

	/* Enable connectors polling */
	drm_kms_helper_poll_init(drm);

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_fini_poll;

	drm_client_setup(drm, NULL);

	return 0;

err_fini_poll:
	drm_kms_helper_poll_fini(drm);
	return ret;
}

void vs_drm_finalize(struct vs_dc *dc)
{
	struct vs_drm_dev *vdrm = dc->drm_dev;
	struct drm_device *drm = &vdrm->base;

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);
	dc->drm_dev = NULL;
}

void vs_drm_shutdown_handler(struct vs_dc *dc)
{
	struct vs_drm_dev *vdrm = dc->drm_dev;

	drm_atomic_helper_shutdown(&vdrm->base);
}

void vs_drm_handle_irq(struct vs_dc *dc, u32 irqs)
{
	unsigned int i;

	for (i = 0; i < dc->identity.display_count; i++) {
		if (irqs & VSDC_TOP_IRQ_VSYNC(i)) {
			irqs &= ~VSDC_TOP_IRQ_VSYNC(i);
			if (dc->drm_dev->crtcs[i])
				drm_crtc_handle_vblank(&dc->drm_dev->crtcs[i]->base);
		}
	}

	if (irqs)
		drm_warn_once(&dc->drm_dev->base,
			      "Unknown Verisilicon DC interrupt 0x%x fired!\n",
			      irqs);
}
