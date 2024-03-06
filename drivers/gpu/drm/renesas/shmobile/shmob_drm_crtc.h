/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * shmob_drm_crtc.h  --  SH Mobile DRM CRTCs
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __SHMOB_DRM_CRTC_H__
#define __SHMOB_DRM_CRTC_H__

#include <drm/drm_crtc.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>

#include <linux/wait.h>

#include <video/videomode.h>

struct drm_pending_vblank_event;
struct shmob_drm_device;
struct shmob_drm_format_info;

struct shmob_drm_crtc {
	struct drm_crtc base;

	struct drm_pending_vblank_event *event;
	wait_queue_head_t flip_wait;
};

/* Legacy connector */
struct shmob_drm_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
	const struct videomode *mode;
};

int shmob_drm_crtc_create(struct shmob_drm_device *sdev);
void shmob_drm_crtc_finish_page_flip(struct shmob_drm_crtc *scrtc);

int shmob_drm_encoder_create(struct shmob_drm_device *sdev);
int shmob_drm_connector_create(struct shmob_drm_device *sdev,
			       struct drm_encoder *encoder);

#endif /* __SHMOB_DRM_CRTC_H__ */
