/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ZX_VOU_H__
#define __ZX_VOU_H__

#define VOU_CRTC_MASK		0x3

/* VOU output interfaces */
enum vou_inf_id {
	VOU_HDMI	= 0,
	VOU_RGB_LCD	= 1,
	VOU_TV_ENC	= 2,
	VOU_MIPI_DSI	= 3,
	VOU_LVDS	= 4,
	VOU_VGA		= 5,
};

enum vou_inf_hdmi_audio {
	VOU_HDMI_AUD_SPDIF	= BIT(0),
	VOU_HDMI_AUD_I2S	= BIT(1),
	VOU_HDMI_AUD_DSD	= BIT(2),
	VOU_HDMI_AUD_HBR	= BIT(3),
	VOU_HDMI_AUD_PARALLEL	= BIT(4),
};

void vou_inf_hdmi_audio_sel(struct drm_crtc *crtc,
			    enum vou_inf_hdmi_audio aud);
void vou_inf_enable(enum vou_inf_id id, struct drm_crtc *crtc);
void vou_inf_disable(enum vou_inf_id id, struct drm_crtc *crtc);

int zx_vou_enable_vblank(struct drm_device *drm, unsigned int pipe);
void zx_vou_disable_vblank(struct drm_device *drm, unsigned int pipe);

void zx_vou_layer_enable(struct drm_plane *plane);
void zx_vou_layer_disable(struct drm_plane *plane);

#endif /* __ZX_VOU_H__ */
