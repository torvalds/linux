/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
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

enum vou_div_id {
	VOU_DIV_VGA,
	VOU_DIV_PIC,
	VOU_DIV_TVENC,
	VOU_DIV_HDMI_PNX,
	VOU_DIV_HDMI,
	VOU_DIV_INF,
	VOU_DIV_LAYER,
};

enum vou_div_val {
	VOU_DIV_1 = 0,
	VOU_DIV_2 = 1,
	VOU_DIV_4 = 3,
	VOU_DIV_8 = 7,
};

struct vou_div_config {
	enum vou_div_id id;
	enum vou_div_val val;
};

void zx_vou_config_dividers(struct drm_crtc *crtc,
			    struct vou_div_config *configs, int num);

void zx_vou_layer_enable(struct drm_plane *plane);
void zx_vou_layer_disable(struct drm_plane *plane,
			  struct drm_plane_state *old_state);

#endif /* __ZX_VOU_H__ */
