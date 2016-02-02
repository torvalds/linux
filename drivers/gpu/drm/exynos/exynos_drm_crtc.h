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

#include "exynos_drm_drv.h"

struct exynos_drm_crtc *exynos_drm_crtc_create(struct drm_device *drm_dev,
					struct drm_plane *plane,
					int pipe,
					enum exynos_drm_output_type type,
					const struct exynos_drm_crtc_ops *ops,
					void *context);
int exynos_drm_crtc_enable_vblank(struct drm_device *dev, unsigned int pipe);
void exynos_drm_crtc_disable_vblank(struct drm_device *dev, unsigned int pipe);
void exynos_drm_crtc_wait_pending_update(struct exynos_drm_crtc *exynos_crtc);
void exynos_drm_crtc_finish_update(struct exynos_drm_crtc *exynos_crtc,
				   struct exynos_drm_plane *exynos_plane);

/* This function gets pipe value to crtc device matched with out_type. */
int exynos_drm_crtc_get_pipe_from_type(struct drm_device *drm_dev,
				       enum exynos_drm_output_type out_type);

/*
 * This function calls the crtc device(manager)'s te_handler() callback
 * to trigger to transfer video image at the tearing effect synchronization
 * signal.
 */
void exynos_drm_crtc_te_handler(struct drm_crtc *crtc);

/* This function cancels a page flip request. */
void exynos_drm_crtc_cancel_page_flip(struct drm_crtc *crtc,
					struct drm_file *file);

#endif
