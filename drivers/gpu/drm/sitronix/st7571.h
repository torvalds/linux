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
	struct i2c_client *client;
	struct gpio_desc *reset;
	struct regmap *regmap;

	/*
	 * Depending on the hardware design, the acknowledge signal may be hard to
	 * recognize as a valid logic "0" level.
	 * Therefor, ignore NAK if possible to stay compatible with most hardware designs
	 * and off-the-shelf panels out there.
	 *
	 * From section 6.4 MICROPOCESSOR INTERFACE section in the datasheet:
	 *
	 * "By connecting SDA_OUT to SDA_IN externally, the SDA line becomes fully
	 * I2C interface compatible.
	 * Separating acknowledge-output from serial data
	 * input is advantageous for chip-on-glass (COG) applications. In COG
	 * applications, the ITO resistance and the pull-up resistor will form a
	 * voltage  divider, which affects acknowledge-signal level. Larger ITO
	 * resistance will raise the acknowledged-signal level and system cannot
	 * recognize this level as a valid logic “0” level. By separating SDA_IN from
	 * SDA_OUT, the IC can be used in a mode that ignores the acknowledge-bit.
	 * For applications which check acknowledge-bit, it is necessary to minimize
	 * the ITO resistance of the SDA_OUT trace to guarantee a valid low level."
	 *
	 */
	bool ignore_nak;

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

#endif /* __ST7571_H__ */
