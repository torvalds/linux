/*
 * DRM driver for Pervasive Displays RePaper branded e-ink panels
 *
 * Copyright 2013-2017 Pervasive Displays, Inc.
 * Copyright 2017 Noralf Trønnes
 *
 * The driver supports:
 * Material Film: Aurora Mb (V231)
 * Driver IC: G2 (eTC)
 *
 * The controller code was taken from the userspace driver:
 * https://github.com/repaper/gratis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/sched/clock.h>
#include <linux/spi/spi.h>
#include <linux/thermal.h>

#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_vblank.h>
#include <drm/tinydrm/tinydrm.h>
#include <drm/tinydrm/tinydrm-helpers.h>

#define REPAPER_RID_G2_COG_ID	0x12

enum repaper_model {
	E1144CS021 = 1,
	E1190CS021,
	E2200CS021,
	E2271CS021,
};

enum repaper_stage {         /* Image pixel -> Display pixel */
	REPAPER_COMPENSATE,  /* B -> W, W -> B (Current Image) */
	REPAPER_WHITE,       /* B -> N, W -> W (Current Image) */
	REPAPER_INVERSE,     /* B -> N, W -> B (New Image) */
	REPAPER_NORMAL       /* B -> B, W -> W (New Image) */
};

enum repaper_epd_border_byte {
	REPAPER_BORDER_BYTE_NONE,
	REPAPER_BORDER_BYTE_ZERO,
	REPAPER_BORDER_BYTE_SET,
};

struct repaper_epd {
	struct tinydrm_device tinydrm;
	struct spi_device *spi;

	struct gpio_desc *panel_on;
	struct gpio_desc *border;
	struct gpio_desc *discharge;
	struct gpio_desc *reset;
	struct gpio_desc *busy;

	struct thermal_zone_device *thermal;

	unsigned int height;
	unsigned int width;
	unsigned int bytes_per_scan;
	const u8 *channel_select;
	unsigned int stage_time;
	unsigned int factored_stage_time;
	bool middle_scan;
	bool pre_border_byte;
	enum repaper_epd_border_byte border_byte;

	u8 *line_buffer;
	void *current_frame;

	bool enabled;
	bool cleared;
	bool partial;
};

static inline struct repaper_epd *
epd_from_tinydrm(struct tinydrm_device *tdev)
{
	return container_of(tdev, struct repaper_epd, tinydrm);
}

static int repaper_spi_transfer(struct spi_device *spi, u8 header,
				const void *tx, void *rx, size_t len)
{
	void *txbuf = NULL, *rxbuf = NULL;
	struct spi_transfer tr[2] = {};
	u8 *headerbuf;
	int ret;

	headerbuf = kmalloc(1, GFP_KERNEL);
	if (!headerbuf)
		return -ENOMEM;

	headerbuf[0] = header;
	tr[0].tx_buf = headerbuf;
	tr[0].len = 1;

	/* Stack allocated tx? */
	if (tx && len <= 32) {
		txbuf = kmemdup(tx, len, GFP_KERNEL);
		if (!txbuf) {
			ret = -ENOMEM;
			goto out_free;
		}
	}

	if (rx) {
		rxbuf = kmalloc(len, GFP_KERNEL);
		if (!rxbuf) {
			ret = -ENOMEM;
			goto out_free;
		}
	}

	tr[1].tx_buf = txbuf ? txbuf : tx;
	tr[1].rx_buf = rxbuf;
	tr[1].len = len;

	ndelay(80);
	ret = spi_sync_transfer(spi, tr, 2);
	if (rx && !ret)
		memcpy(rx, rxbuf, len);

out_free:
	kfree(headerbuf);
	kfree(txbuf);
	kfree(rxbuf);

	return ret;
}

static int repaper_write_buf(struct spi_device *spi, u8 reg,
			     const u8 *buf, size_t len)
{
	int ret;

	ret = repaper_spi_transfer(spi, 0x70, &reg, NULL, 1);
	if (ret)
		return ret;

	return repaper_spi_transfer(spi, 0x72, buf, NULL, len);
}

static int repaper_write_val(struct spi_device *spi, u8 reg, u8 val)
{
	return repaper_write_buf(spi, reg, &val, 1);
}

static int repaper_read_val(struct spi_device *spi, u8 reg)
{
	int ret;
	u8 val;

	ret = repaper_spi_transfer(spi, 0x70, &reg, NULL, 1);
	if (ret)
		return ret;

	ret = repaper_spi_transfer(spi, 0x73, NULL, &val, 1);

	return ret ? ret : val;
}

static int repaper_read_id(struct spi_device *spi)
{
	int ret;
	u8 id;

	ret = repaper_spi_transfer(spi, 0x71, NULL, &id, 1);

	return ret ? ret : id;
}

static void repaper_spi_mosi_low(struct spi_device *spi)
{
	const u8 buf[1] = { 0 };

	spi_write(spi, buf, 1);
}

/* pixels on display are numbered from 1 so even is actually bits 1,3,5,... */
static void repaper_even_pixels(struct repaper_epd *epd, u8 **pp,
				const u8 *data, u8 fixed_value, const u8 *mask,
				enum repaper_stage stage)
{
	unsigned int b;

	for (b = 0; b < (epd->width / 8); b++) {
		if (data) {
			u8 pixels = data[b] & 0xaa;
			u8 pixel_mask = 0xff;
			u8 p1, p2, p3, p4;

			if (mask) {
				pixel_mask = (mask[b] ^ pixels) & 0xaa;
				pixel_mask |= pixel_mask >> 1;
			}

			switch (stage) {
			case REPAPER_COMPENSATE: /* B -> W, W -> B (Current) */
				pixels = 0xaa | ((pixels ^ 0xaa) >> 1);
				break;
			case REPAPER_WHITE:      /* B -> N, W -> W (Current) */
				pixels = 0x55 + ((pixels ^ 0xaa) >> 1);
				break;
			case REPAPER_INVERSE:    /* B -> N, W -> B (New) */
				pixels = 0x55 | (pixels ^ 0xaa);
				break;
			case REPAPER_NORMAL:     /* B -> B, W -> W (New) */
				pixels = 0xaa | (pixels >> 1);
				break;
			}

			pixels = (pixels & pixel_mask) | (~pixel_mask & 0x55);
			p1 = (pixels >> 6) & 0x03;
			p2 = (pixels >> 4) & 0x03;
			p3 = (pixels >> 2) & 0x03;
			p4 = (pixels >> 0) & 0x03;
			pixels = (p1 << 0) | (p2 << 2) | (p3 << 4) | (p4 << 6);
			*(*pp)++ = pixels;
		} else {
			*(*pp)++ = fixed_value;
		}
	}
}

/* pixels on display are numbered from 1 so odd is actually bits 0,2,4,... */
static void repaper_odd_pixels(struct repaper_epd *epd, u8 **pp,
			       const u8 *data, u8 fixed_value, const u8 *mask,
			       enum repaper_stage stage)
{
	unsigned int b;

	for (b = epd->width / 8; b > 0; b--) {
		if (data) {
			u8 pixels = data[b - 1] & 0x55;
			u8 pixel_mask = 0xff;

			if (mask) {
				pixel_mask = (mask[b - 1] ^ pixels) & 0x55;
				pixel_mask |= pixel_mask << 1;
			}

			switch (stage) {
			case REPAPER_COMPENSATE: /* B -> W, W -> B (Current) */
				pixels = 0xaa | (pixels ^ 0x55);
				break;
			case REPAPER_WHITE:      /* B -> N, W -> W (Current) */
				pixels = 0x55 + (pixels ^ 0x55);
				break;
			case REPAPER_INVERSE:    /* B -> N, W -> B (New) */
				pixels = 0x55 | ((pixels ^ 0x55) << 1);
				break;
			case REPAPER_NORMAL:     /* B -> B, W -> W (New) */
				pixels = 0xaa | pixels;
				break;
			}

			pixels = (pixels & pixel_mask) | (~pixel_mask & 0x55);
			*(*pp)++ = pixels;
		} else {
			*(*pp)++ = fixed_value;
		}
	}
}

/* interleave bits: (byte)76543210 -> (16 bit).7.6.5.4.3.2.1 */
static inline u16 repaper_interleave_bits(u16 value)
{
	value = (value | (value << 4)) & 0x0f0f;
	value = (value | (value << 2)) & 0x3333;
	value = (value | (value << 1)) & 0x5555;

	return value;
}

/* pixels on display are numbered from 1 */
static void repaper_all_pixels(struct repaper_epd *epd, u8 **pp,
			       const u8 *data, u8 fixed_value, const u8 *mask,
			       enum repaper_stage stage)
{
	unsigned int b;

	for (b = epd->width / 8; b > 0; b--) {
		if (data) {
			u16 pixels = repaper_interleave_bits(data[b - 1]);
			u16 pixel_mask = 0xffff;

			if (mask) {
				pixel_mask = repaper_interleave_bits(mask[b - 1]);

				pixel_mask = (pixel_mask ^ pixels) & 0x5555;
				pixel_mask |= pixel_mask << 1;
			}

			switch (stage) {
			case REPAPER_COMPENSATE: /* B -> W, W -> B (Current) */
				pixels = 0xaaaa | (pixels ^ 0x5555);
				break;
			case REPAPER_WHITE:      /* B -> N, W -> W (Current) */
				pixels = 0x5555 + (pixels ^ 0x5555);
				break;
			case REPAPER_INVERSE:    /* B -> N, W -> B (New) */
				pixels = 0x5555 | ((pixels ^ 0x5555) << 1);
				break;
			case REPAPER_NORMAL:     /* B -> B, W -> W (New) */
				pixels = 0xaaaa | pixels;
				break;
			}

			pixels = (pixels & pixel_mask) | (~pixel_mask & 0x5555);
			*(*pp)++ = pixels >> 8;
			*(*pp)++ = pixels;
		} else {
			*(*pp)++ = fixed_value;
			*(*pp)++ = fixed_value;
		}
	}
}

/* output one line of scan and data bytes to the display */
static void repaper_one_line(struct repaper_epd *epd, unsigned int line,
			     const u8 *data, u8 fixed_value, const u8 *mask,
			     enum repaper_stage stage)
{
	u8 *p = epd->line_buffer;
	unsigned int b;

	repaper_spi_mosi_low(epd->spi);

	if (epd->pre_border_byte)
		*p++ = 0x00;

	if (epd->middle_scan) {
		/* data bytes */
		repaper_odd_pixels(epd, &p, data, fixed_value, mask, stage);

		/* scan line */
		for (b = epd->bytes_per_scan; b > 0; b--) {
			if (line / 4 == b - 1)
				*p++ = 0x03 << (2 * (line & 0x03));
			else
				*p++ = 0x00;
		}

		/* data bytes */
		repaper_even_pixels(epd, &p, data, fixed_value, mask, stage);
	} else {
		/*
		 * even scan line, but as lines on display are numbered from 1,
		 * line: 1,3,5,...
		 */
		for (b = 0; b < epd->bytes_per_scan; b++) {
			if (0 != (line & 0x01) && line / 8 == b)
				*p++ = 0xc0 >> (line & 0x06);
			else
				*p++ = 0x00;
		}

		/* data bytes */
		repaper_all_pixels(epd, &p, data, fixed_value, mask, stage);

		/*
		 * odd scan line, but as lines on display are numbered from 1,
		 * line: 0,2,4,6,...
		 */
		for (b = epd->bytes_per_scan; b > 0; b--) {
			if (0 == (line & 0x01) && line / 8 == b - 1)
				*p++ = 0x03 << (line & 0x06);
			else
				*p++ = 0x00;
		}
	}

	switch (epd->border_byte) {
	case REPAPER_BORDER_BYTE_NONE:
		break;

	case REPAPER_BORDER_BYTE_ZERO:
		*p++ = 0x00;
		break;

	case REPAPER_BORDER_BYTE_SET:
		switch (stage) {
		case REPAPER_COMPENSATE:
		case REPAPER_WHITE:
		case REPAPER_INVERSE:
			*p++ = 0x00;
			break;
		case REPAPER_NORMAL:
			*p++ = 0xaa;
			break;
		}
		break;
	}

	repaper_write_buf(epd->spi, 0x0a, epd->line_buffer,
			  p - epd->line_buffer);

	/* Output data to panel */
	repaper_write_val(epd->spi, 0x02, 0x07);

	repaper_spi_mosi_low(epd->spi);
}

static void repaper_frame_fixed(struct repaper_epd *epd, u8 fixed_value,
				enum repaper_stage stage)
{
	unsigned int line;

	for (line = 0; line < epd->height; line++)
		repaper_one_line(epd, line, NULL, fixed_value, NULL, stage);
}

static void repaper_frame_data(struct repaper_epd *epd, const u8 *image,
			       const u8 *mask, enum repaper_stage stage)
{
	unsigned int line;

	if (!mask) {
		for (line = 0; line < epd->height; line++) {
			repaper_one_line(epd, line,
					 &image[line * (epd->width / 8)],
					 0, NULL, stage);
		}
	} else {
		for (line = 0; line < epd->height; line++) {
			size_t n = line * epd->width / 8;

			repaper_one_line(epd, line, &image[n], 0, &mask[n],
					 stage);
		}
	}
}

static void repaper_frame_fixed_repeat(struct repaper_epd *epd, u8 fixed_value,
				       enum repaper_stage stage)
{
	u64 start = local_clock();
	u64 end = start + (epd->factored_stage_time * 1000 * 1000);

	do {
		repaper_frame_fixed(epd, fixed_value, stage);
	} while (local_clock() < end);
}

static void repaper_frame_data_repeat(struct repaper_epd *epd, const u8 *image,
				      const u8 *mask, enum repaper_stage stage)
{
	u64 start = local_clock();
	u64 end = start + (epd->factored_stage_time * 1000 * 1000);

	do {
		repaper_frame_data(epd, image, mask, stage);
	} while (local_clock() < end);
}

static void repaper_get_temperature(struct repaper_epd *epd)
{
	int ret, temperature = 0;
	unsigned int factor10x;

	if (!epd->thermal)
		return;

	ret = thermal_zone_get_temp(epd->thermal, &temperature);
	if (ret) {
		DRM_DEV_ERROR(&epd->spi->dev, "Failed to get temperature (%d)\n", ret);
		return;
	}

	temperature /= 1000;

	if (temperature <= -10)
		factor10x = 170;
	else if (temperature <= -5)
		factor10x = 120;
	else if (temperature <= 5)
		factor10x = 80;
	else if (temperature <= 10)
		factor10x = 40;
	else if (temperature <= 15)
		factor10x = 30;
	else if (temperature <= 20)
		factor10x = 20;
	else if (temperature <= 40)
		factor10x = 10;
	else
		factor10x = 7;

	epd->factored_stage_time = epd->stage_time * factor10x / 10;
}

static void repaper_gray8_to_mono_reversed(u8 *buf, u32 width, u32 height)
{
	u8 *gray8 = buf, *mono = buf;
	int y, xb, i;

	for (y = 0; y < height; y++)
		for (xb = 0; xb < width / 8; xb++) {
			u8 byte = 0x00;

			for (i = 0; i < 8; i++) {
				int x = xb * 8 + i;

				byte >>= 1;
				if (gray8[y * width + x] >> 7)
					byte |= BIT(7);
			}
			*mono++ = byte;
		}
}

static int repaper_fb_dirty(struct drm_framebuffer *fb)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct dma_buf_attachment *import_attach = cma_obj->base.import_attach;
	struct tinydrm_device *tdev = fb->dev->dev_private;
	struct repaper_epd *epd = epd_from_tinydrm(tdev);
	struct drm_rect clip;
	u8 *buf = NULL;
	int ret = 0;

	/* repaper can't do partial updates */
	clip.x1 = 0;
	clip.x2 = fb->width;
	clip.y1 = 0;
	clip.y2 = fb->height;

	if (!epd->enabled)
		return 0;

	repaper_get_temperature(epd);

	DRM_DEBUG("Flushing [FB:%d] st=%ums\n", fb->base.id,
		  epd->factored_stage_time);

	buf = kmalloc_array(fb->width, fb->height, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			goto out_free;
	}

	tinydrm_xrgb8888_to_gray8(buf, cma_obj->vaddr, fb, &clip);

	if (import_attach) {
		ret = dma_buf_end_cpu_access(import_attach->dmabuf,
					     DMA_FROM_DEVICE);
		if (ret)
			goto out_free;
	}

	repaper_gray8_to_mono_reversed(buf, fb->width, fb->height);

	if (epd->partial) {
		repaper_frame_data_repeat(epd, buf, epd->current_frame,
					  REPAPER_NORMAL);
	} else if (epd->cleared) {
		repaper_frame_data_repeat(epd, epd->current_frame, NULL,
					  REPAPER_COMPENSATE);
		repaper_frame_data_repeat(epd, epd->current_frame, NULL,
					  REPAPER_WHITE);
		repaper_frame_data_repeat(epd, buf, NULL, REPAPER_INVERSE);
		repaper_frame_data_repeat(epd, buf, NULL, REPAPER_NORMAL);

		epd->partial = true;
	} else {
		/* Clear display (anything -> white) */
		repaper_frame_fixed_repeat(epd, 0xff, REPAPER_COMPENSATE);
		repaper_frame_fixed_repeat(epd, 0xff, REPAPER_WHITE);
		repaper_frame_fixed_repeat(epd, 0xaa, REPAPER_INVERSE);
		repaper_frame_fixed_repeat(epd, 0xaa, REPAPER_NORMAL);

		/* Assuming a clear (white) screen output an image */
		repaper_frame_fixed_repeat(epd, 0xaa, REPAPER_COMPENSATE);
		repaper_frame_fixed_repeat(epd, 0xaa, REPAPER_WHITE);
		repaper_frame_data_repeat(epd, buf, NULL, REPAPER_INVERSE);
		repaper_frame_data_repeat(epd, buf, NULL, REPAPER_NORMAL);

		epd->cleared = true;
		epd->partial = true;
	}

	memcpy(epd->current_frame, buf, fb->width * fb->height / 8);

	/*
	 * An extra frame write is needed if pixels are set in the bottom line,
	 * or else grey lines rises up from the pixels
	 */
	if (epd->pre_border_byte) {
		unsigned int x;

		for (x = 0; x < (fb->width / 8); x++)
			if (buf[x + (fb->width * (fb->height - 1) / 8)]) {
				repaper_frame_data_repeat(epd, buf,
							  epd->current_frame,
							  REPAPER_NORMAL);
				break;
			}
	}

out_free:
	kfree(buf);

	return ret;
}

static void power_off(struct repaper_epd *epd)
{
	/* Turn off power and all signals */
	gpiod_set_value_cansleep(epd->reset, 0);
	gpiod_set_value_cansleep(epd->panel_on, 0);
	if (epd->border)
		gpiod_set_value_cansleep(epd->border, 0);

	/* Ensure SPI MOSI and CLOCK are Low before CS Low */
	repaper_spi_mosi_low(epd->spi);

	/* Discharge pulse */
	gpiod_set_value_cansleep(epd->discharge, 1);
	msleep(150);
	gpiod_set_value_cansleep(epd->discharge, 0);
}

static void repaper_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
	struct repaper_epd *epd = epd_from_tinydrm(tdev);
	struct spi_device *spi = epd->spi;
	struct device *dev = &spi->dev;
	bool dc_ok = false;
	int i, ret;

	DRM_DEBUG_DRIVER("\n");

	/* Power up sequence */
	gpiod_set_value_cansleep(epd->reset, 0);
	gpiod_set_value_cansleep(epd->panel_on, 0);
	gpiod_set_value_cansleep(epd->discharge, 0);
	if (epd->border)
		gpiod_set_value_cansleep(epd->border, 0);
	repaper_spi_mosi_low(spi);
	usleep_range(5000, 10000);

	gpiod_set_value_cansleep(epd->panel_on, 1);
	/*
	 * This delay comes from the repaper.org userspace driver, it's not
	 * mentioned in the datasheet.
	 */
	usleep_range(10000, 15000);
	gpiod_set_value_cansleep(epd->reset, 1);
	if (epd->border)
		gpiod_set_value_cansleep(epd->border, 1);
	usleep_range(5000, 10000);
	gpiod_set_value_cansleep(epd->reset, 0);
	usleep_range(5000, 10000);
	gpiod_set_value_cansleep(epd->reset, 1);
	usleep_range(5000, 10000);

	/* Wait for COG to become ready */
	for (i = 100; i > 0; i--) {
		if (!gpiod_get_value_cansleep(epd->busy))
			break;

		usleep_range(10, 100);
	}

	if (!i) {
		DRM_DEV_ERROR(dev, "timeout waiting for panel to become ready.\n");
		power_off(epd);
		return;
	}

	repaper_read_id(spi);
	ret = repaper_read_id(spi);
	if (ret != REPAPER_RID_G2_COG_ID) {
		if (ret < 0)
			dev_err(dev, "failed to read chip (%d)\n", ret);
		else
			dev_err(dev, "wrong COG ID 0x%02x\n", ret);
		power_off(epd);
		return;
	}

	/* Disable OE */
	repaper_write_val(spi, 0x02, 0x40);

	ret = repaper_read_val(spi, 0x0f);
	if (ret < 0 || !(ret & 0x80)) {
		if (ret < 0)
			DRM_DEV_ERROR(dev, "failed to read chip (%d)\n", ret);
		else
			DRM_DEV_ERROR(dev, "panel is reported broken\n");
		power_off(epd);
		return;
	}

	/* Power saving mode */
	repaper_write_val(spi, 0x0b, 0x02);
	/* Channel select */
	repaper_write_buf(spi, 0x01, epd->channel_select, 8);
	/* High power mode osc */
	repaper_write_val(spi, 0x07, 0xd1);
	/* Power setting */
	repaper_write_val(spi, 0x08, 0x02);
	/* Vcom level */
	repaper_write_val(spi, 0x09, 0xc2);
	/* Power setting */
	repaper_write_val(spi, 0x04, 0x03);
	/* Driver latch on */
	repaper_write_val(spi, 0x03, 0x01);
	/* Driver latch off */
	repaper_write_val(spi, 0x03, 0x00);
	usleep_range(5000, 10000);

	/* Start chargepump */
	for (i = 0; i < 4; ++i) {
		/* Charge pump positive voltage on - VGH/VDL on */
		repaper_write_val(spi, 0x05, 0x01);
		msleep(240);

		/* Charge pump negative voltage on - VGL/VDL on */
		repaper_write_val(spi, 0x05, 0x03);
		msleep(40);

		/* Charge pump Vcom on - Vcom driver on */
		repaper_write_val(spi, 0x05, 0x0f);
		msleep(40);

		/* check DC/DC */
		ret = repaper_read_val(spi, 0x0f);
		if (ret < 0) {
			DRM_DEV_ERROR(dev, "failed to read chip (%d)\n", ret);
			power_off(epd);
			return;
		}

		if (ret & 0x40) {
			dc_ok = true;
			break;
		}
	}

	if (!dc_ok) {
		DRM_DEV_ERROR(dev, "dc/dc failed\n");
		power_off(epd);
		return;
	}

	/*
	 * Output enable to disable
	 * The userspace driver sets this to 0x04, but the datasheet says 0x06
	 */
	repaper_write_val(spi, 0x02, 0x04);

	epd->enabled = true;
	epd->partial = false;
}

static void repaper_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
	struct repaper_epd *epd = epd_from_tinydrm(tdev);
	struct spi_device *spi = epd->spi;
	unsigned int line;

	DRM_DEBUG_DRIVER("\n");

	epd->enabled = false;

	/* Nothing frame */
	for (line = 0; line < epd->height; line++)
		repaper_one_line(epd, 0x7fffu, NULL, 0x00, NULL,
				 REPAPER_COMPENSATE);

	/* 2.7" */
	if (epd->border) {
		/* Dummy line */
		repaper_one_line(epd, 0x7fffu, NULL, 0x00, NULL,
				 REPAPER_COMPENSATE);
		msleep(25);
		gpiod_set_value_cansleep(epd->border, 0);
		msleep(200);
		gpiod_set_value_cansleep(epd->border, 1);
	} else {
		/* Border dummy line */
		repaper_one_line(epd, 0x7fffu, NULL, 0x00, NULL,
				 REPAPER_NORMAL);
		msleep(200);
	}

	/* not described in datasheet */
	repaper_write_val(spi, 0x0b, 0x00);
	/* Latch reset turn on */
	repaper_write_val(spi, 0x03, 0x01);
	/* Power off charge pump Vcom */
	repaper_write_val(spi, 0x05, 0x03);
	/* Power off charge pump neg voltage */
	repaper_write_val(spi, 0x05, 0x01);
	msleep(120);
	/* Discharge internal */
	repaper_write_val(spi, 0x04, 0x80);
	/* turn off all charge pumps */
	repaper_write_val(spi, 0x05, 0x00);
	/* Turn off osc */
	repaper_write_val(spi, 0x07, 0x01);
	msleep(50);

	power_off(epd);
}

static void repaper_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_rect rect;

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		repaper_fb_dirty(state->fb);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);
		crtc->state->event = NULL;
	}
}

static const struct drm_simple_display_pipe_funcs repaper_pipe_funcs = {
	.enable = repaper_pipe_enable,
	.disable = repaper_pipe_disable,
	.update = repaper_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const uint32_t repaper_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_display_mode repaper_e1144cs021_mode = {
	TINYDRM_MODE(128, 96, 29, 22),
};

static const u8 repaper_e1144cs021_cs[] = { 0x00, 0x00, 0x00, 0x00,
					    0x00, 0x0f, 0xff, 0x00 };

static const struct drm_display_mode repaper_e1190cs021_mode = {
	TINYDRM_MODE(144, 128, 36, 32),
};

static const u8 repaper_e1190cs021_cs[] = { 0x00, 0x00, 0x00, 0x03,
					    0xfc, 0x00, 0x00, 0xff };

static const struct drm_display_mode repaper_e2200cs021_mode = {
	TINYDRM_MODE(200, 96, 46, 22),
};

static const u8 repaper_e2200cs021_cs[] = { 0x00, 0x00, 0x00, 0x00,
					    0x01, 0xff, 0xe0, 0x00 };

static const struct drm_display_mode repaper_e2271cs021_mode = {
	TINYDRM_MODE(264, 176, 57, 38),
};

static const u8 repaper_e2271cs021_cs[] = { 0x00, 0x00, 0x00, 0x7f,
					    0xff, 0xfe, 0x00, 0x00 };

DEFINE_DRM_GEM_CMA_FOPS(repaper_fops);

static struct drm_driver repaper_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
				  DRIVER_ATOMIC,
	.fops			= &repaper_fops,
	DRM_GEM_CMA_VMAP_DRIVER_OPS,
	.name			= "repaper",
	.desc			= "Pervasive Displays RePaper e-ink panels",
	.date			= "20170405",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id repaper_of_match[] = {
	{ .compatible = "pervasive,e1144cs021", .data = (void *)E1144CS021 },
	{ .compatible = "pervasive,e1190cs021", .data = (void *)E1190CS021 },
	{ .compatible = "pervasive,e2200cs021", .data = (void *)E2200CS021 },
	{ .compatible = "pervasive,e2271cs021", .data = (void *)E2271CS021 },
	{},
};
MODULE_DEVICE_TABLE(of, repaper_of_match);

static const struct spi_device_id repaper_id[] = {
	{ "e1144cs021", E1144CS021 },
	{ "e1190cs021", E1190CS021 },
	{ "e2200cs021", E2200CS021 },
	{ "e2271cs021", E2271CS021 },
	{ },
};
MODULE_DEVICE_TABLE(spi, repaper_id);

static int repaper_probe(struct spi_device *spi)
{
	const struct drm_display_mode *mode;
	const struct spi_device_id *spi_id;
	const struct of_device_id *match;
	struct device *dev = &spi->dev;
	struct tinydrm_device *tdev;
	enum repaper_model model;
	const char *thermal_zone;
	struct repaper_epd *epd;
	size_t line_buffer_size;
	int ret;

	match = of_match_device(repaper_of_match, dev);
	if (match) {
		model = (enum repaper_model)match->data;
	} else {
		spi_id = spi_get_device_id(spi);
		model = spi_id->driver_data;
	}

	/* The SPI device is used to allocate dma memory */
	if (!dev->coherent_dma_mask) {
		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_warn(dev, "Failed to set dma mask %d\n", ret);
			return ret;
		}
	}

	epd = devm_kzalloc(dev, sizeof(*epd), GFP_KERNEL);
	if (!epd)
		return -ENOMEM;

	epd->spi = spi;

	epd->panel_on = devm_gpiod_get(dev, "panel-on", GPIOD_OUT_LOW);
	if (IS_ERR(epd->panel_on)) {
		ret = PTR_ERR(epd->panel_on);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "Failed to get gpio 'panel-on'\n");
		return ret;
	}

	epd->discharge = devm_gpiod_get(dev, "discharge", GPIOD_OUT_LOW);
	if (IS_ERR(epd->discharge)) {
		ret = PTR_ERR(epd->discharge);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "Failed to get gpio 'discharge'\n");
		return ret;
	}

	epd->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(epd->reset)) {
		ret = PTR_ERR(epd->reset);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return ret;
	}

	epd->busy = devm_gpiod_get(dev, "busy", GPIOD_IN);
	if (IS_ERR(epd->busy)) {
		ret = PTR_ERR(epd->busy);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "Failed to get gpio 'busy'\n");
		return ret;
	}

	if (!device_property_read_string(dev, "pervasive,thermal-zone",
					 &thermal_zone)) {
		epd->thermal = thermal_zone_get_zone_by_name(thermal_zone);
		if (IS_ERR(epd->thermal)) {
			DRM_DEV_ERROR(dev, "Failed to get thermal zone: %s\n", thermal_zone);
			return PTR_ERR(epd->thermal);
		}
	}

	switch (model) {
	case E1144CS021:
		mode = &repaper_e1144cs021_mode;
		epd->channel_select = repaper_e1144cs021_cs;
		epd->stage_time = 480;
		epd->bytes_per_scan = 96 / 4;
		epd->middle_scan = true; /* data-scan-data */
		epd->pre_border_byte = false;
		epd->border_byte = REPAPER_BORDER_BYTE_ZERO;
		break;

	case E1190CS021:
		mode = &repaper_e1190cs021_mode;
		epd->channel_select = repaper_e1190cs021_cs;
		epd->stage_time = 480;
		epd->bytes_per_scan = 128 / 4 / 2;
		epd->middle_scan = false; /* scan-data-scan */
		epd->pre_border_byte = false;
		epd->border_byte = REPAPER_BORDER_BYTE_SET;
		break;

	case E2200CS021:
		mode = &repaper_e2200cs021_mode;
		epd->channel_select = repaper_e2200cs021_cs;
		epd->stage_time = 480;
		epd->bytes_per_scan = 96 / 4;
		epd->middle_scan = true; /* data-scan-data */
		epd->pre_border_byte = true;
		epd->border_byte = REPAPER_BORDER_BYTE_NONE;
		break;

	case E2271CS021:
		epd->border = devm_gpiod_get(dev, "border", GPIOD_OUT_LOW);
		if (IS_ERR(epd->border)) {
			ret = PTR_ERR(epd->border);
			if (ret != -EPROBE_DEFER)
				DRM_DEV_ERROR(dev, "Failed to get gpio 'border'\n");
			return ret;
		}

		mode = &repaper_e2271cs021_mode;
		epd->channel_select = repaper_e2271cs021_cs;
		epd->stage_time = 630;
		epd->bytes_per_scan = 176 / 4;
		epd->middle_scan = true; /* data-scan-data */
		epd->pre_border_byte = true;
		epd->border_byte = REPAPER_BORDER_BYTE_NONE;
		break;

	default:
		return -ENODEV;
	}

	epd->width = mode->hdisplay;
	epd->height = mode->vdisplay;
	epd->factored_stage_time = epd->stage_time;

	line_buffer_size = 2 * epd->width / 8 + epd->bytes_per_scan + 2;
	epd->line_buffer = devm_kzalloc(dev, line_buffer_size, GFP_KERNEL);
	if (!epd->line_buffer)
		return -ENOMEM;

	epd->current_frame = devm_kzalloc(dev, epd->width * epd->height / 8,
					  GFP_KERNEL);
	if (!epd->current_frame)
		return -ENOMEM;

	tdev = &epd->tinydrm;

	ret = devm_tinydrm_init(dev, tdev, &repaper_driver);
	if (ret)
		return ret;

	ret = tinydrm_display_pipe_init(tdev, &repaper_pipe_funcs,
					DRM_MODE_CONNECTOR_VIRTUAL,
					repaper_formats,
					ARRAY_SIZE(repaper_formats), mode, 0);
	if (ret)
		return ret;

	drm_mode_config_reset(tdev->drm);
	spi_set_drvdata(spi, tdev);

	DRM_DEBUG_DRIVER("SPI speed: %uMHz\n", spi->max_speed_hz / 1000000);

	return devm_tinydrm_register(tdev);
}

static void repaper_shutdown(struct spi_device *spi)
{
	struct tinydrm_device *tdev = spi_get_drvdata(spi);

	tinydrm_shutdown(tdev);
}

static struct spi_driver repaper_spi_driver = {
	.driver = {
		.name = "repaper",
		.owner = THIS_MODULE,
		.of_match_table = repaper_of_match,
	},
	.id_table = repaper_id,
	.probe = repaper_probe,
	.shutdown = repaper_shutdown,
};
module_spi_driver(repaper_spi_driver);

MODULE_DESCRIPTION("Pervasive Displays RePaper DRM driver");
MODULE_AUTHOR("Noralf Trønnes");
MODULE_LICENSE("GPL");
