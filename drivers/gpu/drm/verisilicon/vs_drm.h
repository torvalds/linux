/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#ifndef _VS_DRM_H_
#define _VS_DRM_H_

#include <linux/platform_device.h>
#include <linux/types.h>

#include <drm/drm_device.h>

struct vs_dc;

struct vs_drm_dev {
	struct drm_device base;

	struct vs_dc *dc;
	struct vs_crtc *crtcs[VSDC_MAX_OUTPUTS];
};

int vs_drm_initialize(struct vs_dc *dc, struct platform_device *pdev);
void vs_drm_finalize(struct vs_dc *dc);
void vs_drm_shutdown_handler(struct vs_dc *dc);
void vs_drm_handle_irq(struct vs_dc *dc, u32 irqs);

#endif /* _VS_DRM_H_ */
