/*
 * shmob_drm_crtc.h  --  SH Mobile DRM CRTCs
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __SHMOB_DRM_CRTC_H__
#define __SHMOB_DRM_CRTC_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>

struct backlight_device;
struct shmob_drm_device;

struct shmob_drm_crtc {
	struct drm_crtc crtc;

	struct drm_pending_vblank_event *event;
	int dpms;

	const struct shmob_drm_format_info *format;
	void *cache;
	unsigned long dma[2];
	unsigned int line_size;
	bool started;
};

struct shmob_drm_encoder {
	struct drm_encoder encoder;
	int dpms;
};

struct shmob_drm_connector {
	struct drm_connector connector;
	struct drm_encoder *encoder;

	struct backlight_device *backlight;
};

int shmob_drm_crtc_create(struct shmob_drm_device *sdev);
void shmob_drm_crtc_finish_page_flip(struct shmob_drm_crtc *scrtc);
void shmob_drm_crtc_suspend(struct shmob_drm_crtc *scrtc);
void shmob_drm_crtc_resume(struct shmob_drm_crtc *scrtc);

int shmob_drm_encoder_create(struct shmob_drm_device *sdev);
int shmob_drm_connector_create(struct shmob_drm_device *sdev,
			       struct drm_encoder *encoder);

#endif /* __SHMOB_DRM_CRTC_H__ */
