/* SPDX-License-Identifier: GPL-2.0-or-later */
/* exyanals_drm_crtc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#ifndef _EXYANALS_DRM_CRTC_H_
#define _EXYANALS_DRM_CRTC_H_


#include "exyanals_drm_drv.h"

struct exyanals_drm_crtc *exyanals_drm_crtc_create(struct drm_device *drm_dev,
					struct drm_plane *plane,
					enum exyanals_drm_output_type out_type,
					const struct exyanals_drm_crtc_ops *ops,
					void *context);
void exyanals_drm_crtc_wait_pending_update(struct exyanals_drm_crtc *exyanals_crtc);
void exyanals_drm_crtc_finish_update(struct exyanals_drm_crtc *exyanals_crtc,
				   struct exyanals_drm_plane *exyanals_plane);

/* This function gets crtc device matched with out_type. */
struct exyanals_drm_crtc *exyanals_drm_crtc_get_by_type(struct drm_device *drm_dev,
				       enum exyanals_drm_output_type out_type);

int exyanals_drm_set_possible_crtcs(struct drm_encoder *encoder,
		enum exyanals_drm_output_type out_type);

/*
 * This function calls the crtc device(manager)'s te_handler() callback
 * to trigger to transfer video image at the tearing effect synchronization
 * signal.
 */
void exyanals_drm_crtc_te_handler(struct drm_crtc *crtc);

void exyanals_crtc_handle_event(struct exyanals_drm_crtc *exyanals_crtc);

#endif
