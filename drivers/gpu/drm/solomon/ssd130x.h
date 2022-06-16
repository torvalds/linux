/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header file for:
 * DRM driver for Solomon SSD130x OLED displays
 *
 * Copyright 2022 Red Hat Inc.
 * Author: Javier Martinez Canillas <javierm@redhat.com>
 *
 * Based on drivers/video/fbdev/ssd1307fb.c
 * Copyright 2012 Free Electrons
 */

#ifndef __SSD1307X_H__
#define __SSD1307X_H__

#include <drm/drm_drv.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/regmap.h>

#define SSD130X_DATA				0x40
#define SSD130X_COMMAND				0x80

enum ssd130x_variants {
	SH1106_ID,
	SSD1305_ID,
	SSD1306_ID,
	SSD1307_ID,
	SSD1309_ID,
	NR_SSD130X_VARIANTS
};

struct ssd130x_deviceinfo {
	u32 default_vcomh;
	u32 default_dclk_div;
	u32 default_dclk_frq;
	int need_pwm;
	int need_chargepump;
	bool page_mode_only;
};

struct ssd130x_device {
	struct drm_device drm;
	struct device *dev;
	struct drm_simple_display_pipe pipe;
	struct drm_display_mode mode;
	struct drm_connector connector;
	struct i2c_client *client;

	struct regmap *regmap;

	const struct ssd130x_deviceinfo *device_info;

	unsigned page_address_mode : 1;
	unsigned area_color_enable : 1;
	unsigned com_invdir : 1;
	unsigned com_lrremap : 1;
	unsigned com_seq : 1;
	unsigned lookup_table_set : 1;
	unsigned low_power : 1;
	unsigned seg_remap : 1;
	u32 com_offset;
	u32 contrast;
	u32 dclk_div;
	u32 dclk_frq;
	u32 height;
	u8 lookup_table[4];
	u32 page_offset;
	u32 col_offset;
	u32 prechargep1;
	u32 prechargep2;

	struct backlight_device *bl_dev;
	struct pwm_device *pwm;
	struct gpio_desc *reset;
	struct regulator *vcc_reg;
	u32 vcomh;
	u32 width;
	/* Cached address ranges */
	u8 col_start;
	u8 col_end;
	u8 page_start;
	u8 page_end;
};

extern const struct ssd130x_deviceinfo ssd130x_variants[];

struct ssd130x_device *ssd130x_probe(struct device *dev, struct regmap *regmap);
void ssd130x_remove(struct ssd130x_device *ssd130x);
void ssd130x_shutdown(struct ssd130x_device *ssd130x);

#endif /* __SSD1307X_H__ */
