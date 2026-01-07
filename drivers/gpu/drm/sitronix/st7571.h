/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for:
 * Driver for Sitronix ST7571, a 4 level gray scale dot matrix LCD controller
 *
 * Copyright (C) 2025 Marcus Folkesson <marcus.folkesson@gmail.com>
 */

#ifndef __ST7571_H__
#define __ST7571_H__

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_format_helper.h>

#include <linux/regmap.h>

enum st7571_color_mode {
	ST7571_COLOR_MODE_GRAY = 0,
	ST7571_COLOR_MODE_BLACKWHITE = 1,
};

struct st7571_device;

struct st7571_panel_constraints {
	u32 min_nlines;
	u32 max_nlines;
	u32 min_ncols;
	u32 max_ncols;
	bool support_grayscale;
};

struct st7571_panel_data {
	int (*init)(struct st7571_device *st7571);
	int (*parse_dt)(struct st7571_device *st7571);
	struct st7571_panel_constraints constraints;
};

struct st7571_panel_format {
	void (*prepare_buffer)(struct st7571_device *st7571,
			       const struct iosys_map *vmap,
			       struct drm_framebuffer *fb,
			       struct drm_rect *rect,
			       struct drm_format_conv_state *fmtcnv_state);
	int (*update_rect)(struct drm_framebuffer *fb, struct drm_rect *rect);
	enum st7571_color_mode mode;
	const u8 nformats;
	const u32 formats[];
};

struct st7571_device {
	struct drm_device drm;
	struct device *dev;

	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct drm_display_mode mode;

	const struct st7571_panel_format *pformat;
	const struct st7571_panel_data *pdata;
	struct gpio_desc *reset;
	struct regmap *regmap;

	bool grayscale;
	bool inverted;
	u32 height_mm;
	u32 width_mm;
	u32 startline;
	u32 nlines;
	u32 ncols;
	u32 bpp;

	/* Intermediate buffer in LCD friendly format */
	u8 *hwbuf;

	/* Row of (transformed) pixels ready to be written to the display */
	u8 *row;
};

extern const struct st7571_panel_data st7567_config;
extern const struct st7571_panel_data st7571_config;

struct st7571_device *st7571_probe(struct device *dev, struct regmap *regmap);
void st7571_remove(struct st7571_device *st7571);

#endif /* __ST7571_H__ */
