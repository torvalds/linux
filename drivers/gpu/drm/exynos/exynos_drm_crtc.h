/* SPDX-License-Identifier: GPL-2.0-or-later */
/* exynos_drm_crtc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#ifndef _EXYNOS_DRM_CRTC_H_
#define _EXYNOS_DRM_CRTC_H_


#include "exynos_drm_drv.h"

struct exynos_drm_crtc *exynos_drm_crtc_create(struct drm_device *drm_dev,
					struct drm_plane *plane,
					enum exynos_drm_output_type out_type,
					const struct exynos_drm_crtc_ops *ops,
					void *context);

/* This function gets crtc device matched with out_type. */
struct exynos_drm_crtc *exynos_drm_crtc_get_by_type(struct drm_device *drm_dev,
				       enum exynos_drm_output_type out_type);

int exynos_drm_set_possible_crtcs(struct drm_encoder *encoder,
		enum exynos_drm_output_type out_type);

/*
 * This function calls the crtc device(manager)'s te_handler() callback
 * to trigger to transfer video image at the tearing effect synchronization
 * signal.
 */
void exynos_drm_crtc_te_handler(struct drm_crtc *crtc);

void exynos_crtc_handle_event(struct exynos_drm_crtc *exynos_crtc);

#endif
