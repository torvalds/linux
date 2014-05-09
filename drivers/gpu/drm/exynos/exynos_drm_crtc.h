/* exynos_drm_crtc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_CRTC_H_
#define _EXYNOS_DRM_CRTC_H_

struct drm_device;
struct drm_crtc;
struct exynos_drm_manager;
struct exynos_drm_overlay;

int exynos_drm_crtc_create(struct exynos_drm_manager *manager);
int exynos_drm_crtc_enable_vblank(struct drm_device *dev, int pipe);
void exynos_drm_crtc_disable_vblank(struct drm_device *dev, int pipe);
void exynos_drm_crtc_finish_pageflip(struct drm_device *dev, int pipe);
void exynos_drm_crtc_complete_scanout(struct drm_framebuffer *fb);

void exynos_drm_crtc_plane_mode_set(struct drm_crtc *crtc,
			struct exynos_drm_overlay *overlay);
void exynos_drm_crtc_plane_commit(struct drm_crtc *crtc, int zpos);
void exynos_drm_crtc_plane_enable(struct drm_crtc *crtc, int zpos);
void exynos_drm_crtc_plane_disable(struct drm_crtc *crtc, int zpos);

/* This function gets pipe value to crtc device matched with out_type. */
int exynos_drm_crtc_get_pipe_from_type(struct drm_device *drm_dev,
					unsigned int out_type);

#endif
