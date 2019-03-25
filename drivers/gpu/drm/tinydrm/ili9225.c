/*
 * DRM driver for Ilitek ILI9225 panels
 *
 * Copyright 2017 David Lechner <david@lechnology.com>
 *
 * Some code copied from mipi-dbi.c
 * Copyright 2016 Noralf Trønnes
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
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_vblank.h>
#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>

#define ILI9225_DRIVER_READ_CODE	0x00
#define ILI9225_DRIVER_OUTPUT_CONTROL	0x01
#define ILI9225_LCD_AC_DRIVING_CONTROL	0x02
#define ILI9225_ENTRY_MODE		0x03
#define ILI9225_DISPLAY_CONTROL_1	0x07
#define ILI9225_BLANK_PERIOD_CONTROL_1	0x08
#define ILI9225_FRAME_CYCLE_CONTROL	0x0b
#define ILI9225_INTERFACE_CONTROL	0x0c
#define ILI9225_OSCILLATION_CONTROL	0x0f
#define ILI9225_POWER_CONTROL_1		0x10
#define ILI9225_POWER_CONTROL_2		0x11
#define ILI9225_POWER_CONTROL_3		0x12
#define ILI9225_POWER_CONTROL_4		0x13
#define ILI9225_POWER_CONTROL_5		0x14
#define ILI9225_VCI_RECYCLING		0x15
#define ILI9225_RAM_ADDRESS_SET_1	0x20
#define ILI9225_RAM_ADDRESS_SET_2	0x21
#define ILI9225_WRITE_DATA_TO_GRAM	0x22
#define ILI9225_SOFTWARE_RESET		0x28
#define ILI9225_GATE_SCAN_CONTROL	0x30
#define ILI9225_VERTICAL_SCROLL_1	0x31
#define ILI9225_VERTICAL_SCROLL_2	0x32
#define ILI9225_VERTICAL_SCROLL_3	0x33
#define ILI9225_PARTIAL_DRIVING_POS_1	0x34
#define ILI9225_PARTIAL_DRIVING_POS_2	0x35
#define ILI9225_HORIZ_WINDOW_ADDR_1	0x36
#define ILI9225_HORIZ_WINDOW_ADDR_2	0x37
#define ILI9225_VERT_WINDOW_ADDR_1	0x38
#define ILI9225_VERT_WINDOW_ADDR_2	0x39
#define ILI9225_GAMMA_CONTROL_1		0x50
#define ILI9225_GAMMA_CONTROL_2		0x51
#define ILI9225_GAMMA_CONTROL_3		0x52
#define ILI9225_GAMMA_CONTROL_4		0x53
#define ILI9225_GAMMA_CONTROL_5		0x54
#define ILI9225_GAMMA_CONTROL_6		0x55
#define ILI9225_GAMMA_CONTROL_7		0x56
#define ILI9225_GAMMA_CONTROL_8		0x57
#define ILI9225_GAMMA_CONTROL_9		0x58
#define ILI9225_GAMMA_CONTROL_10	0x59

static inline int ili9225_command(struct mipi_dbi *mipi, u8 cmd, u16 data)
{
	u8 par[2] = { data >> 8, data & 0xff };

	return mipi_dbi_command_buf(mipi, cmd, par, 2);
}

static void ili9225_fb_dirty(struct drm_framebuffer *fb, struct drm_rect *rect)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct mipi_dbi *mipi = drm_to_mipi_dbi(fb->dev);
	unsigned int height = rect->y2 - rect->y1;
	unsigned int width = rect->x2 - rect->x1;
	bool swap = mipi->swap_bytes;
	u16 x_start, y_start;
	u16 x1, x2, y1, y2;
	int idx, ret = 0;
	bool full;
	void *tr;

	if (!mipi->enabled)
		return;

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	full = width == fb->width && height == fb->height;

	DRM_DEBUG_KMS("Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id, DRM_RECT_ARG(rect));

	if (!mipi->dc || !full || swap ||
	    fb->format->format == DRM_FORMAT_XRGB8888) {
		tr = mipi->tx_buf;
		ret = mipi_dbi_buf_copy(mipi->tx_buf, fb, rect, swap);
		if (ret)
			goto err_msg;
	} else {
		tr = cma_obj->vaddr;
	}

	switch (mipi->rotation) {
	default:
		x1 = rect->x1;
		x2 = rect->x2 - 1;
		y1 = rect->y1;
		y2 = rect->y2 - 1;
		x_start = x1;
		y_start = y1;
		break;
	case 90:
		x1 = rect->y1;
		x2 = rect->y2 - 1;
		y1 = fb->width - rect->x2;
		y2 = fb->width - rect->x1 - 1;
		x_start = x1;
		y_start = y2;
		break;
	case 180:
		x1 = fb->width - rect->x2;
		x2 = fb->width - rect->x1 - 1;
		y1 = fb->height - rect->y2;
		y2 = fb->height - rect->y1 - 1;
		x_start = x2;
		y_start = y2;
		break;
	case 270:
		x1 = fb->height - rect->y2;
		x2 = fb->height - rect->y1 - 1;
		y1 = rect->x1;
		y2 = rect->x2 - 1;
		x_start = x2;
		y_start = y1;
		break;
	}

	ili9225_command(mipi, ILI9225_HORIZ_WINDOW_ADDR_1, x2);
	ili9225_command(mipi, ILI9225_HORIZ_WINDOW_ADDR_2, x1);
	ili9225_command(mipi, ILI9225_VERT_WINDOW_ADDR_1, y2);
	ili9225_command(mipi, ILI9225_VERT_WINDOW_ADDR_2, y1);

	ili9225_command(mipi, ILI9225_RAM_ADDRESS_SET_1, x_start);
	ili9225_command(mipi, ILI9225_RAM_ADDRESS_SET_2, y_start);

	ret = mipi_dbi_command_buf(mipi, ILI9225_WRITE_DATA_TO_GRAM, tr,
				   width * height * 2);
err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n", ret);

	drm_dev_exit(idx);
}

static void ili9225_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_rect rect;

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		ili9225_fb_dirty(state->fb, &rect);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);
		crtc->state->event = NULL;
	}
}

static void ili9225_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct mipi_dbi *mipi = drm_to_mipi_dbi(pipe->crtc.dev);
	struct drm_framebuffer *fb = plane_state->fb;
	struct device *dev = pipe->crtc.dev->dev;
	struct drm_rect rect = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};
	int ret, idx;
	u8 am_id;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	mipi_dbi_hw_reset(mipi);

	/*
	 * There don't seem to be two example init sequences that match, so
	 * using the one from the popular Arduino library for this display.
	 * https://github.com/Nkawu/TFT_22_ILI9225/blob/master/src/TFT_22_ILI9225.cpp
	 */

	ret = ili9225_command(mipi, ILI9225_POWER_CONTROL_1, 0x0000);
	if (ret) {
		DRM_DEV_ERROR(dev, "Error sending command %d\n", ret);
		goto out_exit;
	}
	ili9225_command(mipi, ILI9225_POWER_CONTROL_2, 0x0000);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_3, 0x0000);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_4, 0x0000);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_5, 0x0000);

	msleep(40);

	ili9225_command(mipi, ILI9225_POWER_CONTROL_2, 0x0018);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_3, 0x6121);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_4, 0x006f);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_5, 0x495f);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_1, 0x0800);

	msleep(10);

	ili9225_command(mipi, ILI9225_POWER_CONTROL_2, 0x103b);

	msleep(50);

	switch (mipi->rotation) {
	default:
		am_id = 0x30;
		break;
	case 90:
		am_id = 0x18;
		break;
	case 180:
		am_id = 0x00;
		break;
	case 270:
		am_id = 0x28;
		break;
	}
	ili9225_command(mipi, ILI9225_DRIVER_OUTPUT_CONTROL, 0x011c);
	ili9225_command(mipi, ILI9225_LCD_AC_DRIVING_CONTROL, 0x0100);
	ili9225_command(mipi, ILI9225_ENTRY_MODE, 0x1000 | am_id);
	ili9225_command(mipi, ILI9225_DISPLAY_CONTROL_1, 0x0000);
	ili9225_command(mipi, ILI9225_BLANK_PERIOD_CONTROL_1, 0x0808);
	ili9225_command(mipi, ILI9225_FRAME_CYCLE_CONTROL, 0x1100);
	ili9225_command(mipi, ILI9225_INTERFACE_CONTROL, 0x0000);
	ili9225_command(mipi, ILI9225_OSCILLATION_CONTROL, 0x0d01);
	ili9225_command(mipi, ILI9225_VCI_RECYCLING, 0x0020);
	ili9225_command(mipi, ILI9225_RAM_ADDRESS_SET_1, 0x0000);
	ili9225_command(mipi, ILI9225_RAM_ADDRESS_SET_2, 0x0000);

	ili9225_command(mipi, ILI9225_GATE_SCAN_CONTROL, 0x0000);
	ili9225_command(mipi, ILI9225_VERTICAL_SCROLL_1, 0x00db);
	ili9225_command(mipi, ILI9225_VERTICAL_SCROLL_2, 0x0000);
	ili9225_command(mipi, ILI9225_VERTICAL_SCROLL_3, 0x0000);
	ili9225_command(mipi, ILI9225_PARTIAL_DRIVING_POS_1, 0x00db);
	ili9225_command(mipi, ILI9225_PARTIAL_DRIVING_POS_2, 0x0000);

	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_1, 0x0000);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_2, 0x0808);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_3, 0x080a);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_4, 0x000a);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_5, 0x0a08);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_6, 0x0808);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_7, 0x0000);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_8, 0x0a00);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_9, 0x0710);
	ili9225_command(mipi, ILI9225_GAMMA_CONTROL_10, 0x0710);

	ili9225_command(mipi, ILI9225_DISPLAY_CONTROL_1, 0x0012);

	msleep(50);

	ili9225_command(mipi, ILI9225_DISPLAY_CONTROL_1, 0x1017);

	mipi->enabled = true;
	ili9225_fb_dirty(fb, &rect);
out_exit:
	drm_dev_exit(idx);
}

static void ili9225_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi *mipi = drm_to_mipi_dbi(pipe->crtc.dev);

	DRM_DEBUG_KMS("\n");

	/*
	 * This callback is not protected by drm_dev_enter/exit since we want to
	 * turn off the display on regular driver unload. It's highly unlikely
	 * that the underlying SPI controller is gone should this be called after
	 * unplug.
	 */

	if (!mipi->enabled)
		return;

	ili9225_command(mipi, ILI9225_DISPLAY_CONTROL_1, 0x0000);
	msleep(50);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_2, 0x0007);
	msleep(50);
	ili9225_command(mipi, ILI9225_POWER_CONTROL_1, 0x0a02);

	mipi->enabled = false;
}

static int ili9225_dbi_command(struct mipi_dbi *mipi, u8 *cmd, u8 *par,
			       size_t num)
{
	struct spi_device *spi = mipi->spi;
	unsigned int bpw = 8;
	u32 speed_hz;
	int ret;

	gpiod_set_value_cansleep(mipi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 1);
	ret = tinydrm_spi_transfer(spi, speed_hz, NULL, 8, cmd, 1);
	if (ret || !num)
		return ret;

	if (*cmd == ILI9225_WRITE_DATA_TO_GRAM && !mipi->swap_bytes)
		bpw = 16;

	gpiod_set_value_cansleep(mipi->dc, 1);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, num);

	return tinydrm_spi_transfer(spi, speed_hz, NULL, bpw, par, num);
}

static const struct drm_simple_display_pipe_funcs ili9225_pipe_funcs = {
	.enable		= ili9225_pipe_enable,
	.disable	= ili9225_pipe_disable,
	.update		= ili9225_pipe_update,
	.prepare_fb	= drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode ili9225_mode = {
	DRM_SIMPLE_MODE(176, 220, 35, 44),
};

DEFINE_DRM_GEM_CMA_FOPS(ili9225_fops);

static struct drm_driver ili9225_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
				  DRIVER_ATOMIC,
	.fops			= &ili9225_fops,
	.release		= mipi_dbi_release,
	DRM_GEM_CMA_VMAP_DRIVER_OPS,
	.name			= "ili9225",
	.desc			= "Ilitek ILI9225",
	.date			= "20171106",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id ili9225_of_match[] = {
	{ .compatible = "vot,v220hf01a-t" },
	{},
};
MODULE_DEVICE_TABLE(of, ili9225_of_match);

static const struct spi_device_id ili9225_id[] = {
	{ "v220hf01a-t", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, ili9225_id);

static int ili9225_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct drm_device *drm;
	struct mipi_dbi *mipi;
	struct gpio_desc *rs;
	u32 rotation = 0;
	int ret;

	mipi = kzalloc(sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	drm = &mipi->drm;
	ret = devm_drm_dev_init(dev, drm, &ili9225_driver);
	if (ret) {
		kfree(mipi);
		return ret;
	}

	drm_mode_config_init(drm);

	mipi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(mipi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(mipi->reset);
	}

	rs = devm_gpiod_get(dev, "rs", GPIOD_OUT_LOW);
	if (IS_ERR(rs)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'rs'\n");
		return PTR_ERR(rs);
	}

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, mipi, rs);
	if (ret)
		return ret;

	/* override the command function set in  mipi_dbi_spi_init() */
	mipi->command = ili9225_dbi_command;

	ret = mipi_dbi_init(mipi, &ili9225_pipe_funcs, &ili9225_mode, rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 32);

	return 0;
}

static int ili9225_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
}

static void ili9225_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver ili9225_spi_driver = {
	.driver = {
		.name = "ili9225",
		.owner = THIS_MODULE,
		.of_match_table = ili9225_of_match,
	},
	.id_table = ili9225_id,
	.probe = ili9225_probe,
	.remove = ili9225_remove,
	.shutdown = ili9225_shutdown,
};
module_spi_driver(ili9225_spi_driver);

MODULE_DESCRIPTION("Ilitek ILI9225 DRM driver");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
