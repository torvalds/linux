/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 NXP.
 */

#ifndef _DCSS_KMS_H_
#define _DCSS_KMS_H_

#include <drm/drm_encoder.h>

struct dcss_plane {
	struct drm_plane base;

	int ch_num;
};

struct dcss_crtc {
	struct drm_crtc		base;
	struct drm_crtc_state	*state;

	struct dcss_plane	*plane[3];

	int			irq;

	bool disable_ctxld_kick_irq;
};

struct dcss_kms_dev {
	struct drm_device base;
	struct dcss_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector *connector;
};

struct dcss_kms_dev *dcss_kms_attach(struct dcss_dev *dcss);
void dcss_kms_detach(struct dcss_kms_dev *kms);
void dcss_kms_shutdown(struct dcss_kms_dev *kms);
int dcss_crtc_init(struct dcss_crtc *crtc, struct drm_device *drm);
void dcss_crtc_deinit(struct dcss_crtc *crtc, struct drm_device *drm);
struct dcss_plane *dcss_plane_init(struct drm_device *drm,
				   unsigned int possible_crtcs,
				   enum drm_plane_type type,
				   unsigned int zpos);

#endif
