// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRM driver for Solomon SSD130x OLED displays
 *
 * Copyright 2022 Red Hat Inc.
 * Author: Javier Martinez Canillas <javierm@redhat.com>
 *
 * Based on drivers/video/fbdev/ssd1307fb.c
 * Copyright 2012 Free Electrons
 */

#include <linux/backlight.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modes.h>
#include <drm/drm_rect.h>
#include <drm/drm_probe_helper.h>

#include "ssd130x.h"

#define DRIVER_NAME	"ssd130x"
#define DRIVER_DESC	"DRM driver for Solomon SSD130x OLED displays"
#define DRIVER_DATE	"20220131"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define SSD130X_PAGE_COL_START_LOW		0x00
#define SSD130X_PAGE_COL_START_HIGH		0x10
#define SSD130X_SET_ADDRESS_MODE		0x20
#define SSD130X_SET_COL_RANGE			0x21
#define SSD130X_SET_PAGE_RANGE			0x22
#define SSD130X_CONTRAST			0x81
#define SSD130X_SET_LOOKUP_TABLE		0x91
#define SSD130X_CHARGE_PUMP			0x8d
#define SSD130X_SET_SEG_REMAP			0xa0
#define SSD130X_DISPLAY_OFF			0xae
#define SSD130X_SET_MULTIPLEX_RATIO		0xa8
#define SSD130X_DISPLAY_ON			0xaf
#define SSD130X_START_PAGE_ADDRESS		0xb0
#define SSD130X_SET_COM_SCAN_DIR		0xc0
#define SSD130X_SET_DISPLAY_OFFSET		0xd3
#define SSD130X_SET_CLOCK_FREQ			0xd5
#define SSD130X_SET_AREA_COLOR_MODE		0xd8
#define SSD130X_SET_PRECHARGE_PERIOD		0xd9
#define SSD130X_SET_COM_PINS_CONFIG		0xda
#define SSD130X_SET_VCOMH			0xdb

#define SSD130X_PAGE_COL_START_MASK		GENMASK(3, 0)
#define SSD130X_PAGE_COL_START_HIGH_SET(val)	FIELD_PREP(SSD130X_PAGE_COL_START_MASK, (val) >> 4)
#define SSD130X_PAGE_COL_START_LOW_SET(val)	FIELD_PREP(SSD130X_PAGE_COL_START_MASK, (val))
#define SSD130X_START_PAGE_ADDRESS_MASK		GENMASK(2, 0)
#define SSD130X_START_PAGE_ADDRESS_SET(val)	FIELD_PREP(SSD130X_START_PAGE_ADDRESS_MASK, (val))
#define SSD130X_SET_SEG_REMAP_MASK		GENMASK(0, 0)
#define SSD130X_SET_SEG_REMAP_SET(val)		FIELD_PREP(SSD130X_SET_SEG_REMAP_MASK, (val))
#define SSD130X_SET_COM_SCAN_DIR_MASK		GENMASK(3, 3)
#define SSD130X_SET_COM_SCAN_DIR_SET(val)	FIELD_PREP(SSD130X_SET_COM_SCAN_DIR_MASK, (val))
#define SSD130X_SET_CLOCK_DIV_MASK		GENMASK(3, 0)
#define SSD130X_SET_CLOCK_DIV_SET(val)		FIELD_PREP(SSD130X_SET_CLOCK_DIV_MASK, (val))
#define SSD130X_SET_CLOCK_FREQ_MASK		GENMASK(7, 4)
#define SSD130X_SET_CLOCK_FREQ_SET(val)		FIELD_PREP(SSD130X_SET_CLOCK_FREQ_MASK, (val))
#define SSD130X_SET_PRECHARGE_PERIOD1_MASK	GENMASK(3, 0)
#define SSD130X_SET_PRECHARGE_PERIOD1_SET(val)	FIELD_PREP(SSD130X_SET_PRECHARGE_PERIOD1_MASK, (val))
#define SSD130X_SET_PRECHARGE_PERIOD2_MASK	GENMASK(7, 4)
#define SSD130X_SET_PRECHARGE_PERIOD2_SET(val)	FIELD_PREP(SSD130X_SET_PRECHARGE_PERIOD2_MASK, (val))
#define SSD130X_SET_COM_PINS_CONFIG1_MASK	GENMASK(4, 4)
#define SSD130X_SET_COM_PINS_CONFIG1_SET(val)	FIELD_PREP(SSD130X_SET_COM_PINS_CONFIG1_MASK, !(val))
#define SSD130X_SET_COM_PINS_CONFIG2_MASK	GENMASK(5, 5)
#define SSD130X_SET_COM_PINS_CONFIG2_SET(val)	FIELD_PREP(SSD130X_SET_COM_PINS_CONFIG2_MASK, (val))

#define SSD130X_SET_ADDRESS_MODE_HORIZONTAL	0x00
#define SSD130X_SET_ADDRESS_MODE_VERTICAL	0x01
#define SSD130X_SET_ADDRESS_MODE_PAGE		0x02

#define SSD130X_SET_AREA_COLOR_MODE_ENABLE	0x1e
#define SSD130X_SET_AREA_COLOR_MODE_LOW_POWER	0x05

#define MAX_CONTRAST 255

const struct ssd130x_deviceinfo ssd130x_variants[] = {
	[SH1106_ID] = {
		.default_vcomh = 0x40,
		.default_dclk_div = 1,
		.default_dclk_frq = 5,
		.page_mode_only = 1,
	},
	[SSD1305_ID] = {
		.default_vcomh = 0x34,
		.default_dclk_div = 1,
		.default_dclk_frq = 7,
	},
	[SSD1306_ID] = {
		.default_vcomh = 0x20,
		.default_dclk_div = 1,
		.default_dclk_frq = 8,
		.need_chargepump = 1,
	},
	[SSD1307_ID] = {
		.default_vcomh = 0x20,
		.default_dclk_div = 2,
		.default_dclk_frq = 12,
		.need_pwm = 1,
	},
	[SSD1309_ID] = {
		.default_vcomh = 0x34,
		.default_dclk_div = 1,
		.default_dclk_frq = 10,
	}
};
EXPORT_SYMBOL_NS_GPL(ssd130x_variants, DRM_SSD130X);

static inline struct ssd130x_device *drm_to_ssd130x(struct drm_device *drm)
{
	return container_of(drm, struct ssd130x_device, drm);
}

/*
 * Helper to write data (SSD130X_DATA) to the device.
 */
static int ssd130x_write_data(struct ssd130x_device *ssd130x, u8 *values, int count)
{
	return regmap_bulk_write(ssd130x->regmap, SSD130X_DATA, values, count);
}

/*
 * Helper to write command (SSD130X_COMMAND). The fist variadic argument
 * is the command to write and the following are the command options.
 *
 * Note that the ssd130x protocol requires each command and option to be
 * written as a SSD130X_COMMAND device register value. That is why a call
 * to regmap_write(..., SSD130X_COMMAND, ...) is done for each argument.
 */
static int ssd130x_write_cmd(struct ssd130x_device *ssd130x, int count,
			     /* u8 cmd, u8 option, ... */...)
{
	va_list ap;
	u8 value;
	int ret;

	va_start(ap, count);

	do {
		value = va_arg(ap, int);
		ret = regmap_write(ssd130x->regmap, SSD130X_COMMAND, value);
		if (ret)
			goto out_end;
	} while (--count);

out_end:
	va_end(ap);

	return ret;
}

/* Set address range for horizontal/vertical addressing modes */
static int ssd130x_set_col_range(struct ssd130x_device *ssd130x,
				 u8 col_start, u8 cols)
{
	u8 col_end = col_start + cols - 1;
	int ret;

	if (col_start == ssd130x->col_start && col_end == ssd130x->col_end)
		return 0;

	ret = ssd130x_write_cmd(ssd130x, 3, SSD130X_SET_COL_RANGE, col_start, col_end);
	if (ret < 0)
		return ret;

	ssd130x->col_start = col_start;
	ssd130x->col_end = col_end;
	return 0;
}

static int ssd130x_set_page_range(struct ssd130x_device *ssd130x,
				  u8 page_start, u8 pages)
{
	u8 page_end = page_start + pages - 1;
	int ret;

	if (page_start == ssd130x->page_start && page_end == ssd130x->page_end)
		return 0;

	ret = ssd130x_write_cmd(ssd130x, 3, SSD130X_SET_PAGE_RANGE, page_start, page_end);
	if (ret < 0)
		return ret;

	ssd130x->page_start = page_start;
	ssd130x->page_end = page_end;
	return 0;
}

/* Set page and column start address for page addressing mode */
static int ssd130x_set_page_pos(struct ssd130x_device *ssd130x,
				u8 page_start, u8 col_start)
{
	int ret;
	u32 page, col_low, col_high;

	page = SSD130X_START_PAGE_ADDRESS |
	       SSD130X_START_PAGE_ADDRESS_SET(page_start);
	col_low = SSD130X_PAGE_COL_START_LOW |
		  SSD130X_PAGE_COL_START_LOW_SET(col_start);
	col_high = SSD130X_PAGE_COL_START_HIGH |
		   SSD130X_PAGE_COL_START_HIGH_SET(col_start);
	ret = ssd130x_write_cmd(ssd130x, 3, page, col_low, col_high);
	if (ret < 0)
		return ret;

	return 0;
}

static int ssd130x_pwm_enable(struct ssd130x_device *ssd130x)
{
	struct device *dev = ssd130x->dev;
	struct pwm_state pwmstate;

	ssd130x->pwm = pwm_get(dev, NULL);
	if (IS_ERR(ssd130x->pwm)) {
		dev_err(dev, "Could not get PWM from firmware description!\n");
		return PTR_ERR(ssd130x->pwm);
	}

	pwm_init_state(ssd130x->pwm, &pwmstate);
	pwm_set_relative_duty_cycle(&pwmstate, 50, 100);
	pwm_apply_state(ssd130x->pwm, &pwmstate);

	/* Enable the PWM */
	pwm_enable(ssd130x->pwm);

	dev_dbg(dev, "Using PWM%d with a %lluns period.\n",
		ssd130x->pwm->pwm, pwm_get_period(ssd130x->pwm));

	return 0;
}

static void ssd130x_reset(struct ssd130x_device *ssd130x)
{
	if (!ssd130x->reset)
		return;

	/* Reset the screen */
	gpiod_set_value_cansleep(ssd130x->reset, 1);
	udelay(4);
	gpiod_set_value_cansleep(ssd130x->reset, 0);
	udelay(4);
}

static int ssd130x_power_on(struct ssd130x_device *ssd130x)
{
	struct device *dev = ssd130x->dev;
	int ret;

	ssd130x_reset(ssd130x);

	ret = regulator_enable(ssd130x->vcc_reg);
	if (ret) {
		dev_err(dev, "Failed to enable VCC: %d\n", ret);
		return ret;
	}

	if (ssd130x->device_info->need_pwm) {
		ret = ssd130x_pwm_enable(ssd130x);
		if (ret) {
			dev_err(dev, "Failed to enable PWM: %d\n", ret);
			regulator_disable(ssd130x->vcc_reg);
			return ret;
		}
	}

	return 0;
}

static void ssd130x_power_off(struct ssd130x_device *ssd130x)
{
	pwm_disable(ssd130x->pwm);
	pwm_put(ssd130x->pwm);

	regulator_disable(ssd130x->vcc_reg);
}

static int ssd130x_init(struct ssd130x_device *ssd130x)
{
	u32 precharge, dclk, com_invdir, compins, chargepump, seg_remap;
	int ret;

	/* Set initial contrast */
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_CONTRAST, ssd130x->contrast);
	if (ret < 0)
		return ret;

	/* Set segment re-map */
	seg_remap = (SSD130X_SET_SEG_REMAP |
		     SSD130X_SET_SEG_REMAP_SET(ssd130x->seg_remap));
	ret = ssd130x_write_cmd(ssd130x, 1, seg_remap);
	if (ret < 0)
		return ret;

	/* Set COM direction */
	com_invdir = (SSD130X_SET_COM_SCAN_DIR |
		      SSD130X_SET_COM_SCAN_DIR_SET(ssd130x->com_invdir));
	ret = ssd130x_write_cmd(ssd130x,  1, com_invdir);
	if (ret < 0)
		return ret;

	/* Set multiplex ratio value */
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_MULTIPLEX_RATIO, ssd130x->height - 1);
	if (ret < 0)
		return ret;

	/* set display offset value */
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_DISPLAY_OFFSET, ssd130x->com_offset);
	if (ret < 0)
		return ret;

	/* Set clock frequency */
	dclk = (SSD130X_SET_CLOCK_DIV_SET(ssd130x->dclk_div - 1) |
		SSD130X_SET_CLOCK_FREQ_SET(ssd130x->dclk_frq));
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_CLOCK_FREQ, dclk);
	if (ret < 0)
		return ret;

	/* Set Area Color Mode ON/OFF & Low Power Display Mode */
	if (ssd130x->area_color_enable || ssd130x->low_power) {
		u32 mode = 0;

		if (ssd130x->area_color_enable)
			mode |= SSD130X_SET_AREA_COLOR_MODE_ENABLE;

		if (ssd130x->low_power)
			mode |= SSD130X_SET_AREA_COLOR_MODE_LOW_POWER;

		ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_AREA_COLOR_MODE, mode);
		if (ret < 0)
			return ret;
	}

	/* Set precharge period in number of ticks from the internal clock */
	precharge = (SSD130X_SET_PRECHARGE_PERIOD1_SET(ssd130x->prechargep1) |
		     SSD130X_SET_PRECHARGE_PERIOD1_SET(ssd130x->prechargep2));
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_PRECHARGE_PERIOD, precharge);
	if (ret < 0)
		return ret;

	/* Set COM pins configuration */
	compins = BIT(1);
	compins |= (SSD130X_SET_COM_PINS_CONFIG1_SET(ssd130x->com_seq) |
		    SSD130X_SET_COM_PINS_CONFIG2_SET(ssd130x->com_lrremap));
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_COM_PINS_CONFIG, compins);
	if (ret < 0)
		return ret;

	/* Set VCOMH */
	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_VCOMH, ssd130x->vcomh);
	if (ret < 0)
		return ret;

	/* Turn on the DC-DC Charge Pump */
	chargepump = BIT(4);

	if (ssd130x->device_info->need_chargepump)
		chargepump |= BIT(2);

	ret = ssd130x_write_cmd(ssd130x, 2, SSD130X_CHARGE_PUMP, chargepump);
	if (ret < 0)
		return ret;

	/* Set lookup table */
	if (ssd130x->lookup_table_set) {
		int i;

		ret = ssd130x_write_cmd(ssd130x, 1, SSD130X_SET_LOOKUP_TABLE);
		if (ret < 0)
			return ret;

		for (i = 0; i < ARRAY_SIZE(ssd130x->lookup_table); i++) {
			u8 val = ssd130x->lookup_table[i];

			if (val < 31 || val > 63)
				dev_warn(ssd130x->dev,
					 "lookup table index %d value out of range 31 <= %d <= 63\n",
					 i, val);
			ret = ssd130x_write_cmd(ssd130x, 1, val);
			if (ret < 0)
				return ret;
		}
	}

	/* Switch to page addressing mode */
	if (ssd130x->page_address_mode)
		return ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_ADDRESS_MODE,
					 SSD130X_SET_ADDRESS_MODE_PAGE);

	/* Switch to horizontal addressing mode */
	return ssd130x_write_cmd(ssd130x, 2, SSD130X_SET_ADDRESS_MODE,
				 SSD130X_SET_ADDRESS_MODE_HORIZONTAL);
}

static int ssd130x_update_rect(struct ssd130x_device *ssd130x, u8 *buf,
			       struct drm_rect *rect)
{
	unsigned int x = rect->x1;
	unsigned int y = rect->y1;
	unsigned int width = drm_rect_width(rect);
	unsigned int height = drm_rect_height(rect);
	unsigned int line_length = DIV_ROUND_UP(width, 8);
	unsigned int pages = DIV_ROUND_UP(height, 8);
	struct drm_device *drm = &ssd130x->drm;
	u32 array_idx = 0;
	int ret, i, j, k;
	u8 *data_array = NULL;

	drm_WARN_ONCE(drm, y % 8 != 0, "y must be aligned to screen page\n");

	data_array = kcalloc(width, pages, GFP_KERNEL);
	if (!data_array)
		return -ENOMEM;

	/*
	 * The screen is divided in pages, each having a height of 8
	 * pixels, and the width of the screen. When sending a byte of
	 * data to the controller, it gives the 8 bits for the current
	 * column. I.e, the first byte are the 8 bits of the first
	 * column, then the 8 bits for the second column, etc.
	 *
	 *
	 * Representation of the screen, assuming it is 5 bits
	 * wide. Each letter-number combination is a bit that controls
	 * one pixel.
	 *
	 * A0 A1 A2 A3 A4
	 * B0 B1 B2 B3 B4
	 * C0 C1 C2 C3 C4
	 * D0 D1 D2 D3 D4
	 * E0 E1 E2 E3 E4
	 * F0 F1 F2 F3 F4
	 * G0 G1 G2 G3 G4
	 * H0 H1 H2 H3 H4
	 *
	 * If you want to update this screen, you need to send 5 bytes:
	 *  (1) A0 B0 C0 D0 E0 F0 G0 H0
	 *  (2) A1 B1 C1 D1 E1 F1 G1 H1
	 *  (3) A2 B2 C2 D2 E2 F2 G2 H2
	 *  (4) A3 B3 C3 D3 E3 F3 G3 H3
	 *  (5) A4 B4 C4 D4 E4 F4 G4 H4
	 */

	if (!ssd130x->page_address_mode) {
		/* Set address range for horizontal addressing mode */
		ret = ssd130x_set_col_range(ssd130x, ssd130x->col_offset + x, width);
		if (ret < 0)
			goto out_free;

		ret = ssd130x_set_page_range(ssd130x, ssd130x->page_offset + y / 8, pages);
		if (ret < 0)
			goto out_free;
	}

	for (i = 0; i < pages; i++) {
		int m = 8;

		/* Last page may be partial */
		if (8 * (y / 8 + i + 1) > ssd130x->height)
			m = ssd130x->height % 8;
		for (j = 0; j < width; j++) {
			u8 data = 0;

			for (k = 0; k < m; k++) {
				u8 byte = buf[(8 * i + k) * line_length + j / 8];
				u8 bit = (byte >> (j % 8)) & 1;

				data |= bit << k;
			}
			data_array[array_idx++] = data;
		}

		/*
		 * In page addressing mode, the start address needs to be reset,
		 * and each page then needs to be written out separately.
		 */
		if (ssd130x->page_address_mode) {
			ret = ssd130x_set_page_pos(ssd130x,
						   ssd130x->page_offset + i,
						   ssd130x->col_offset + x);
			if (ret < 0)
				goto out_free;

			ret = ssd130x_write_data(ssd130x, data_array, width);
			if (ret < 0)
				goto out_free;

			array_idx = 0;
		}
	}

	/* Write out update in one go if we aren't using page addressing mode */
	if (!ssd130x->page_address_mode)
		ret = ssd130x_write_data(ssd130x, data_array, width * pages);

out_free:
	kfree(data_array);
	return ret;
}

static void ssd130x_clear_screen(struct ssd130x_device *ssd130x)
{
	u8 *buf = NULL;
	struct drm_rect fullscreen = {
		.x1 = 0,
		.x2 = ssd130x->width,
		.y1 = 0,
		.y2 = ssd130x->height,
	};

	buf = kcalloc(DIV_ROUND_UP(ssd130x->width, 8), ssd130x->height,
		      GFP_KERNEL);
	if (!buf)
		return;

	ssd130x_update_rect(ssd130x, buf, &fullscreen);

	kfree(buf);
}

static int ssd130x_fb_blit_rect(struct drm_framebuffer *fb, const struct iosys_map *map,
				struct drm_rect *rect)
{
	struct ssd130x_device *ssd130x = drm_to_ssd130x(fb->dev);
	void *vmap = map->vaddr; /* TODO: Use mapping abstraction properly */
	unsigned int dst_pitch;
	int ret = 0;
	u8 *buf = NULL;

	/* Align y to display page boundaries */
	rect->y1 = round_down(rect->y1, 8);
	rect->y2 = min_t(unsigned int, round_up(rect->y2, 8), ssd130x->height);

	dst_pitch = DIV_ROUND_UP(drm_rect_width(rect), 8);
	buf = kcalloc(dst_pitch, drm_rect_height(rect), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	drm_fb_xrgb8888_to_mono(buf, dst_pitch, vmap, fb, rect);

	ssd130x_update_rect(ssd130x, buf, rect);

	kfree(buf);

	return ret;
}

static int ssd130x_display_pipe_mode_valid(struct drm_simple_display_pipe *pipe,
					   const struct drm_display_mode *mode)
{
	struct ssd130x_device *ssd130x = drm_to_ssd130x(pipe->crtc.dev);

	if (mode->hdisplay != ssd130x->mode.hdisplay &&
	    mode->vdisplay != ssd130x->mode.vdisplay)
		return MODE_ONE_SIZE;

	if (mode->hdisplay != ssd130x->mode.hdisplay)
		return MODE_ONE_WIDTH;

	if (mode->vdisplay != ssd130x->mode.vdisplay)
		return MODE_ONE_HEIGHT;

	return MODE_OK;
}

static void ssd130x_display_pipe_enable(struct drm_simple_display_pipe *pipe,
					struct drm_crtc_state *crtc_state,
					struct drm_plane_state *plane_state)
{
	struct ssd130x_device *ssd130x = drm_to_ssd130x(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_device *drm = &ssd130x->drm;
	int idx, ret;

	ret = ssd130x_power_on(ssd130x);
	if (ret)
		return;

	ret = ssd130x_init(ssd130x);
	if (ret)
		goto out_power_off;

	if (!drm_dev_enter(drm, &idx))
		goto out_power_off;

	ssd130x_fb_blit_rect(plane_state->fb, &shadow_plane_state->data[0], &plane_state->dst);

	ssd130x_write_cmd(ssd130x, 1, SSD130X_DISPLAY_ON);

	backlight_enable(ssd130x->bl_dev);

	drm_dev_exit(idx);

	return;
out_power_off:
	ssd130x_power_off(ssd130x);
}

static void ssd130x_display_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct ssd130x_device *ssd130x = drm_to_ssd130x(pipe->crtc.dev);
	struct drm_device *drm = &ssd130x->drm;
	int idx;

	if (!drm_dev_enter(drm, &idx))
		return;

	ssd130x_clear_screen(ssd130x);

	backlight_disable(ssd130x->bl_dev);

	ssd130x_write_cmd(ssd130x, 1, SSD130X_DISPLAY_OFF);

	ssd130x_power_off(ssd130x);

	drm_dev_exit(idx);
}

static void ssd130x_display_pipe_update(struct drm_simple_display_pipe *pipe,
					struct drm_plane_state *old_plane_state)
{
	struct ssd130x_device *ssd130x = drm_to_ssd130x(pipe->crtc.dev);
	struct drm_plane_state *plane_state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_device *drm = &ssd130x->drm;
	struct drm_rect src_clip, dst_clip;
	int idx;

	if (!fb)
		return;

	if (!pipe->crtc.state->active)
		return;

	if (!drm_atomic_helper_damage_merged(old_plane_state, plane_state, &src_clip))
		return;

	dst_clip = plane_state->dst;
	if (!drm_rect_intersect(&dst_clip, &src_clip))
		return;

	if (!drm_dev_enter(drm, &idx))
		return;

	ssd130x_fb_blit_rect(plane_state->fb, &shadow_plane_state->data[0], &dst_clip);

	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs ssd130x_pipe_funcs = {
	.mode_valid = ssd130x_display_pipe_mode_valid,
	.enable = ssd130x_display_pipe_enable,
	.disable = ssd130x_display_pipe_disable,
	.update = ssd130x_display_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static int ssd130x_connector_get_modes(struct drm_connector *connector)
{
	struct ssd130x_device *ssd130x = drm_to_ssd130x(connector->dev);
	struct drm_display_mode *mode;
	struct device *dev = ssd130x->dev;

	mode = drm_mode_duplicate(connector->dev, &ssd130x->mode);
	if (!mode) {
		dev_err(dev, "Failed to duplicated mode\n");
		return 0;
	}

	drm_mode_probed_add(connector, mode);
	drm_set_preferred_mode(connector, mode->hdisplay, mode->vdisplay);

	/* There is only a single mode */
	return 1;
}

static const struct drm_connector_helper_funcs ssd130x_connector_helper_funcs = {
	.get_modes = ssd130x_connector_get_modes,
};

static const struct drm_connector_funcs ssd130x_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs ssd130x_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const uint32_t ssd130x_formats[] = {
	DRM_FORMAT_XRGB8888,
};

DEFINE_DRM_GEM_FOPS(ssd130x_fops);

static const struct drm_driver ssd130x_drm_driver = {
	DRM_GEM_SHMEM_DRIVER_OPS,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.driver_features	= DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops			= &ssd130x_fops,
};

static int ssd130x_update_bl(struct backlight_device *bdev)
{
	struct ssd130x_device *ssd130x = bl_get_data(bdev);
	int brightness = backlight_get_brightness(bdev);
	int ret;

	ssd130x->contrast = brightness;

	ret = ssd130x_write_cmd(ssd130x, 1, SSD130X_CONTRAST);
	if (ret < 0)
		return ret;

	ret = ssd130x_write_cmd(ssd130x, 1, ssd130x->contrast);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops ssd130xfb_bl_ops = {
	.update_status	= ssd130x_update_bl,
};

static void ssd130x_parse_properties(struct ssd130x_device *ssd130x)
{
	struct device *dev = ssd130x->dev;

	if (device_property_read_u32(dev, "solomon,width", &ssd130x->width))
		ssd130x->width = 96;

	if (device_property_read_u32(dev, "solomon,height", &ssd130x->height))
		ssd130x->height = 16;

	if (device_property_read_u32(dev, "solomon,page-offset", &ssd130x->page_offset))
		ssd130x->page_offset = 1;

	if (device_property_read_u32(dev, "solomon,col-offset", &ssd130x->col_offset))
		ssd130x->col_offset = 0;

	if (device_property_read_u32(dev, "solomon,com-offset", &ssd130x->com_offset))
		ssd130x->com_offset = 0;

	if (device_property_read_u32(dev, "solomon,prechargep1", &ssd130x->prechargep1))
		ssd130x->prechargep1 = 2;

	if (device_property_read_u32(dev, "solomon,prechargep2", &ssd130x->prechargep2))
		ssd130x->prechargep2 = 2;

	if (!device_property_read_u8_array(dev, "solomon,lookup-table",
					   ssd130x->lookup_table,
					   ARRAY_SIZE(ssd130x->lookup_table)))
		ssd130x->lookup_table_set = 1;

	ssd130x->seg_remap = !device_property_read_bool(dev, "solomon,segment-no-remap");
	ssd130x->com_seq = device_property_read_bool(dev, "solomon,com-seq");
	ssd130x->com_lrremap = device_property_read_bool(dev, "solomon,com-lrremap");
	ssd130x->com_invdir = device_property_read_bool(dev, "solomon,com-invdir");
	ssd130x->area_color_enable =
		device_property_read_bool(dev, "solomon,area-color-enable");
	ssd130x->low_power = device_property_read_bool(dev, "solomon,low-power");

	ssd130x->contrast = 127;
	ssd130x->vcomh = ssd130x->device_info->default_vcomh;

	/* Setup display timing */
	if (device_property_read_u32(dev, "solomon,dclk-div", &ssd130x->dclk_div))
		ssd130x->dclk_div = ssd130x->device_info->default_dclk_div;
	if (device_property_read_u32(dev, "solomon,dclk-frq", &ssd130x->dclk_frq))
		ssd130x->dclk_frq = ssd130x->device_info->default_dclk_frq;
}

static int ssd130x_init_modeset(struct ssd130x_device *ssd130x)
{
	struct drm_display_mode *mode = &ssd130x->mode;
	struct device *dev = ssd130x->dev;
	struct drm_device *drm = &ssd130x->drm;
	unsigned long max_width, max_height;
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret) {
		dev_err(dev, "DRM mode config init failed: %d\n", ret);
		return ret;
	}

	mode->type = DRM_MODE_TYPE_DRIVER;
	mode->clock = 1;
	mode->hdisplay = mode->htotal = ssd130x->width;
	mode->hsync_start = mode->hsync_end = ssd130x->width;
	mode->vdisplay = mode->vtotal = ssd130x->height;
	mode->vsync_start = mode->vsync_end = ssd130x->height;
	mode->width_mm = 27;
	mode->height_mm = 27;

	max_width = max_t(unsigned long, mode->hdisplay, DRM_SHADOW_PLANE_MAX_WIDTH);
	max_height = max_t(unsigned long, mode->vdisplay, DRM_SHADOW_PLANE_MAX_HEIGHT);

	drm->mode_config.min_width = mode->hdisplay;
	drm->mode_config.max_width = max_width;
	drm->mode_config.min_height = mode->vdisplay;
	drm->mode_config.max_height = max_height;
	drm->mode_config.preferred_depth = 32;
	drm->mode_config.funcs = &ssd130x_mode_config_funcs;

	ret = drm_connector_init(drm, &ssd130x->connector, &ssd130x_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret) {
		dev_err(dev, "DRM connector init failed: %d\n", ret);
		return ret;
	}

	drm_connector_helper_add(&ssd130x->connector, &ssd130x_connector_helper_funcs);

	ret = drm_simple_display_pipe_init(drm, &ssd130x->pipe, &ssd130x_pipe_funcs,
					   ssd130x_formats, ARRAY_SIZE(ssd130x_formats),
					   NULL, &ssd130x->connector);
	if (ret) {
		dev_err(dev, "DRM simple display pipeline init failed: %d\n", ret);
		return ret;
	}

	drm_plane_enable_fb_damage_clips(&ssd130x->pipe.plane);

	drm_mode_config_reset(drm);

	return 0;
}

static int ssd130x_get_resources(struct ssd130x_device *ssd130x)
{
	struct device *dev = ssd130x->dev;

	ssd130x->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ssd130x->reset))
		return dev_err_probe(dev, PTR_ERR(ssd130x->reset),
				     "Failed to get reset gpio\n");

	ssd130x->vcc_reg = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ssd130x->vcc_reg))
		return dev_err_probe(dev, PTR_ERR(ssd130x->vcc_reg),
				     "Failed to get VCC regulator\n");

	return 0;
}

struct ssd130x_device *ssd130x_probe(struct device *dev, struct regmap *regmap)
{
	struct ssd130x_device *ssd130x;
	struct backlight_device *bl;
	struct drm_device *drm;
	int ret;

	ssd130x = devm_drm_dev_alloc(dev, &ssd130x_drm_driver,
				     struct ssd130x_device, drm);
	if (IS_ERR(ssd130x))
		return ERR_PTR(dev_err_probe(dev, PTR_ERR(ssd130x),
					     "Failed to allocate DRM device\n"));

	drm = &ssd130x->drm;

	ssd130x->dev = dev;
	ssd130x->regmap = regmap;
	ssd130x->device_info = device_get_match_data(dev);

	if (ssd130x->device_info->page_mode_only)
		ssd130x->page_address_mode = 1;

	ssd130x_parse_properties(ssd130x);

	ret = ssd130x_get_resources(ssd130x);
	if (ret)
		return ERR_PTR(ret);

	bl = devm_backlight_device_register(dev, dev_name(dev), dev, ssd130x,
					    &ssd130xfb_bl_ops, NULL);
	if (IS_ERR(bl))
		return ERR_PTR(dev_err_probe(dev, PTR_ERR(bl),
					     "Unable to register backlight device\n"));

	bl->props.brightness = ssd130x->contrast;
	bl->props.max_brightness = MAX_CONTRAST;
	ssd130x->bl_dev = bl;

	ret = ssd130x_init_modeset(ssd130x);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ERR_PTR(dev_err_probe(dev, ret, "DRM device register failed\n"));

	drm_fbdev_generic_setup(drm, 0);

	return ssd130x;
}
EXPORT_SYMBOL_GPL(ssd130x_probe);

void ssd130x_remove(struct ssd130x_device *ssd130x)
{
	drm_dev_unplug(&ssd130x->drm);
}
EXPORT_SYMBOL_GPL(ssd130x_remove);

void ssd130x_shutdown(struct ssd130x_device *ssd130x)
{
	drm_atomic_helper_shutdown(&ssd130x->drm);
}
EXPORT_SYMBOL_GPL(ssd130x_shutdown);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Javier Martinez Canillas <javierm@redhat.com>");
MODULE_LICENSE("GPL v2");
