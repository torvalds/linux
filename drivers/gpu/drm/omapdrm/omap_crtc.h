/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_crtc.h -- OMAP DRM CRTC
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_CRTC_H__
#define __OMAPDRM_CRTC_H__

#include <linux/types.h>

enum omap_channel;

struct drm_crtc;
struct drm_device;
struct drm_plane;
struct omap_drm_pipeline;
struct omap_dss_device;
struct videomode;

struct videomode *omap_crtc_timings(struct drm_crtc *crtc);
enum omap_channel omap_crtc_channel(struct drm_crtc *crtc);
void omap_crtc_pre_init(struct omap_drm_private *priv);
void omap_crtc_pre_uninit(struct omap_drm_private *priv);
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
				struct omap_drm_pipeline *pipe,
				struct drm_plane *plane);
int omap_crtc_wait_pending(struct drm_crtc *crtc);
void omap_crtc_error_irq(struct drm_crtc *crtc, u32 irqstatus);
void omap_crtc_vblank_irq(struct drm_crtc *crtc);
void omap_crtc_framedone_irq(struct drm_crtc *crtc, uint32_t irqstatus);
void omap_crtc_flush(struct drm_crtc *crtc);

#endif /* __OMAPDRM_CRTC_H__ */
