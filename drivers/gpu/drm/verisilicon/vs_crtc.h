/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#ifndef _VS_CRTC_H_
#define _VS_CRTC_H_

#include <drm/drm_crtc.h>
#include <drm/drm_vblank.h>

#define VSDC_DISP_TIMING_VALUE_MAX BIT_MASK(15)

struct vs_dc;

struct vs_crtc {
	struct drm_crtc base;

	struct vs_dc *dc;
	unsigned int id;
};

static inline struct vs_crtc *drm_crtc_to_vs_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct vs_crtc, base);
}

struct vs_crtc *vs_crtc_init(struct drm_device *drm_dev, struct vs_dc *dc,
			     unsigned int output);

#endif /* _VS_CRTC_H_ */
