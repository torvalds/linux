/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

/*
 * Video Encoders
 * - ENCI : Interlace Video Encoder
 * - ENCI_DVI : Interlace Video Encoder for DVI/HDMI
 * - ENCP : Progressive Video Encoder
 */

#ifndef __MESON_VENC_H
#define __MESON_VENC_H

struct drm_display_mode;

enum {
	MESON_VENC_MODE_NONE = 0,
	MESON_VENC_MODE_CVBS_PAL,
	MESON_VENC_MODE_CVBS_NTSC,
	MESON_VENC_MODE_HDMI,
};

struct meson_cvbs_enci_mode {
	unsigned int mode_tag;
	unsigned int hso_begin; /* HSO begin position */
	unsigned int hso_end; /* HSO end position */
	unsigned int vso_even; /* VSO even line */
	unsigned int vso_odd; /* VSO odd line */
	unsigned int macv_max_amp; /* Macrovision max amplitude */
	unsigned int video_prog_mode;
	unsigned int video_mode;
	unsigned int sch_adjust;
	unsigned int yc_delay;
	unsigned int pixel_start;
	unsigned int pixel_end;
	unsigned int top_field_line_start;
	unsigned int top_field_line_end;
	unsigned int bottom_field_line_start;
	unsigned int bottom_field_line_end;
	unsigned int video_saturation;
	unsigned int video_contrast;
	unsigned int video_brightness;
	unsigned int video_hue;
	unsigned int analog_sync_adj;
};

/* HDMI Clock parameters */
enum drm_mode_status
meson_venc_hdmi_supported_mode(const struct drm_display_mode *mode);
bool meson_venc_hdmi_supported_vic(int vic);
bool meson_venc_hdmi_venc_repeat(int vic);

/* CVBS Timings and Parameters */
extern struct meson_cvbs_enci_mode meson_cvbs_enci_pal;
extern struct meson_cvbs_enci_mode meson_cvbs_enci_ntsc;

void meson_venci_cvbs_mode_set(struct meson_drm *priv,
			       struct meson_cvbs_enci_mode *mode);
void meson_venc_hdmi_mode_set(struct meson_drm *priv, int vic,
			      const struct drm_display_mode *mode);
unsigned int meson_venci_get_field(struct meson_drm *priv);

void meson_venc_enable_vsync(struct meson_drm *priv);
void meson_venc_disable_vsync(struct meson_drm *priv);

void meson_venc_init(struct meson_drm *priv);

#endif /* __MESON_VENC_H */
