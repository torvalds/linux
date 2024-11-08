// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for Ilitek ILI9225 panels
 *
 * Copyright 2017 David Lechner <david@lechnology.com>
 *
 * Some code copied from mipi-dbi.c
 * Copyright 2016 Noralf Trønnes
 */

#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_rect.h>

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

static inline int ili9225_command(struct mipi_dbi *dbi, u8 cmd, u16 data)
{
	u8 par[2] = { data >> 8, data & 0xff };

	return mipi_dbi_command_buf(dbi, cmd, par, 2);
}

static void ili9225_fb_dirty(struct iosys_map *src, struct drm_framebuffer *fb,
			     struct drm_rect *rect, struct drm_format_conv_state *fmtcnv_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(fb->dev);
	unsigned int height = rect->y2 - rect->y1;
	unsigned int width = rect->x2 - rect->x1;
	struct mipi_dbi *dbi = &dbidev->dbi;
	bool swap = dbi->swap_bytes;
	u16 x_start, y_start;
	u16 x1, x2, y1, y2;
	int ret = 0;
	bool full;
	void *tr;

	full = width == fb->width && height == fb->height;

	DRM_DEBUG_KMS("Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id, DRM_RECT_ARG(rect));

	if (!dbi->dc || !full || swap ||
	    fb->format->format == DRM_FORMAT_XRGB8888) {
		tr = dbidev->tx_buf;
		ret = mipi_dbi_buf_copy(tr, src, fb, rect, swap, fmtcnv_state);
		if (ret)
			goto err_msg;
	} else {
		tr = src->vaddr; /* TODO: Use mapping abstraction properly */
	}

	switch (dbidev->rotation) {
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

	ili9225_command(dbi, ILI9225_HORIZ_WINDOW_ADDR_1, x2);
	ili9225_command(dbi, ILI9225_HORIZ_WINDOW_ADDR_2, x1);
	ili9225_command(dbi, ILI9225_VERT_WINDOW_ADDR_1, y2);
	ili9225_command(dbi, ILI9225_VERT_WINDOW_ADDR_2, y1);

	ili9225_command(dbi, ILI9225_RAM_ADDRESS_SET_1, x_start);
	ili9225_command(dbi, ILI9225_RAM_ADDRESS_SET_2, y_start);

	ret = mipi_dbi_command_buf(dbi, ILI9225_WRITE_DATA_TO_GRAM, tr,
				   width * height * 2);
err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n", ret);
}

static void ili9225_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect rect;
	int idx;

	if (!pipe->crtc.state->active)
		return;

	if (!drm_dev_enter(fb->dev, &idx))
		return;

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		ili9225_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
				 &shadow_plane_state->fmtcnv_state);

	drm_dev_exit(idx);
}

static void ili9225_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct device *dev = pipe->crtc.dev->dev;
	struct mipi_dbi *dbi = &dbidev->dbi;
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

	mipi_dbi_hw_reset(dbi);

	/*
	 * There don't seem to be two example init sequences that match, so
	 * using the one from the popular Arduino library for this display.
	 * https://github.com/Nkawu/TFT_22_ILI9225/blob/master/src/TFT_22_ILI9225.cpp
	 */

	ret = ili9225_command(dbi, ILI9225_POWER_CONTROL_1, 0x0000);
	if (ret) {
		DRM_DEV_ERROR(dev, "Error sending command %d\n", ret);
		goto out_exit;
	}
	ili9225_command(dbi, ILI9225_POWER_CONTROL_2, 0x0000);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_3, 0x0000);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_4, 0x0000);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_5, 0x0000);

	msleep(40);

	ili9225_command(dbi, ILI9225_POWER_CONTROL_2, 0x0018);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_3, 0x6121);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_4, 0x006f);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_5, 0x495f);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_1, 0x0800);

	msleep(10);

	ili9225_command(dbi, ILI9225_POWER_CONTROL_2, 0x103b);

	msleep(50);

	switch (dbidev->rotation) {
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
	ili9225_command(dbi, ILI9225_DRIVER_OUTPUT_CONTROL, 0x011c);
	ili9225_command(dbi, ILI9225_LCD_AC_DRIVING_CONTROL, 0x0100);
	ili9225_command(dbi, ILI9225_ENTRY_MODE, 0x1000 | am_id);
	ili9225_command(dbi, ILI9225_DISPLAY_CONTROL_1, 0x0000);
	ili9225_command(dbi, ILI9225_BLANK_PERIOD_CONTROL_1, 0x0808);
	ili9225_command(dbi, ILI9225_FRAME_CYCLE_CONTROL, 0x1100);
	ili9225_command(dbi, ILI9225_INTERFACE_CONTROL, 0x0000);
	ili9225_command(dbi, ILI9225_OSCILLATION_CONTROL, 0x0d01);
	ili9225_command(dbi, ILI9225_VCI_RECYCLING, 0x0020);
	ili9225_command(dbi, ILI9225_RAM_ADDRESS_SET_1, 0x0000);
	ili9225_command(dbi, ILI9225_RAM_ADDRESS_SET_2, 0x0000);

	ili9225_command(dbi, ILI9225_GATE_SCAN_CONTROL, 0x0000);
	ili9225_command(dbi, ILI9225_VERTICAL_SCROLL_1, 0x00db);
	ili9225_command(dbi, ILI9225_VERTICAL_SCROLL_2, 0x0000);
	ili9225_command(dbi, ILI9225_VERTICAL_SCROLL_3, 0x0000);
	ili9225_command(dbi, ILI9225_PARTIAL_DRIVING_POS_1, 0x00db);
	ili9225_command(dbi, ILI9225_PARTIAL_DRIVING_POS_2, 0x0000);

	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_1, 0x0000);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_2, 0x0808);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_3, 0x080a);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_4, 0x000a);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_5, 0x0a08);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_6, 0x0808);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_7, 0x0000);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_8, 0x0a00);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_9, 0x0710);
	ili9225_command(dbi, ILI9225_GAMMA_CONTROL_10, 0x0710);

	ili9225_command(dbi, ILI9225_DISPLAY_CONTROL_1, 0x0012);

	msleep(50);

	ili9225_command(dbi, ILI9225_DISPLAY_CONTROL_1, 0x1017);

	ili9225_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
			 &shadow_plane_state->fmtcnv_state);

out_exit:
	drm_dev_exit(idx);
}

static void ili9225_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;

	DRM_DEBUG_KMS("\n");

	/*
	 * This callback is not protected by drm_dev_enter/exit since we want to
	 * turn off the display on regular driver unload. It's highly unlikely
	 * that the underlying SPI controller is gone should this be called after
	 * unplug.
	 */

	ili9225_command(dbi, ILI9225_DISPLAY_CONTROL_1, 0x0000);
	msleep(50);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_2, 0x0007);
	msleep(50);
	ili9225_command(dbi, ILI9225_POWER_CONTROL_1, 0x0a02);
}

static int ili9225_dbi_command(struct mipi_dbi *dbi, u8 *cmd, u8 *par,
			       size_t num)
{
	struct spi_device *spi = dbi->spi;
	unsigned int bpw = 8;
	u32 speed_hz;
	int ret;

	spi_bus_lock(spi->controller);
	gpiod_set_value_cansleep(dbi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 1);
	ret = mipi_dbi_spi_transfer(spi, speed_hz, 8, cmd, 1);
	spi_bus_unlock(spi->controller);
	if (ret || !num)
		return ret;

	if (*cmd == ILI9225_WRITE_DATA_TO_GRAM && !dbi->swap_bytes)
		bpw = 16;

	spi_bus_lock(spi->controller);
	gpiod_set_value_cansleep(dbi->dc, 1);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, num);
	ret = mipi_dbi_spi_transfer(spi, speed_hz, bpw, par, num);
	spi_bus_unlock(spi->controller);

	return ret;
}

static const struct drm_simple_display_pipe_funcs ili9225_pipe_funcs = {
	.mode_valid	= mipi_dbi_pipe_mode_valid,
	.enable		= ili9225_pipe_enable,
	.disable	= ili9225_pipe_disable,
	.update		= ili9225_pipe_update,
	.begin_fb_access = mipi_dbi_pipe_begin_fb_access,
	.end_fb_access	= mipi_dbi_pipe_end_fb_access,
	.reset_plane	= mipi_dbi_pipe_reset_plane,
	.duplicate_plane_state = mipi_dbi_pipe_duplicate_plane_state,
	.destroy_plane_state = mipi_dbi_pipe_destroy_plane_state,
};

static const struct drm_display_mode ili9225_mode = {
	DRM_SIMPLE_MODE(176, 220, 35, 44),
};

DEFINE_DRM_GEM_DMA_FOPS(ili9225_fops);

static const struct drm_driver ili9225_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9225_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	DRM_FBDEV_DMA_DRIVER_OPS,
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
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *rs;
	u32 rotation = 0;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &ili9225_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get GPIO 'reset'\n");

	rs = devm_gpiod_get(dev, "rs", GPIOD_OUT_LOW);
	if (IS_ERR(rs))
		return dev_err_probe(dev, PTR_ERR(rs), "Failed to get GPIO 'rs'\n");

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, rs);
	if (ret)
		return ret;

	/* override the command function set in  mipi_dbi_spi_init() */
	dbi->command = ili9225_dbi_command;

	ret = mipi_dbi_dev_init(dbidev, &ili9225_pipe_funcs, &ili9225_mode, rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_client_setup(drm, NULL);

	return 0;
}

static void ili9225_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void ili9225_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver ili9225_spi_driver = {
	.driver = {
		.name = "ili9225",
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
