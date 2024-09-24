// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for Sitronix ST7586 panels
 *
 * Copyright 2017 David Lechner <david@lechnology.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_rect.h>

/* controller-specific commands */
#define ST7586_DISP_MODE_GRAY	0x38
#define ST7586_DISP_MODE_MONO	0x39
#define ST7586_ENABLE_DDRAM	0x3a
#define ST7586_SET_DISP_DUTY	0xb0
#define ST7586_SET_PART_DISP	0xb4
#define ST7586_SET_NLINE_INV	0xb5
#define ST7586_SET_VOP		0xc0
#define ST7586_SET_BIAS_SYSTEM	0xc3
#define ST7586_SET_BOOST_LEVEL	0xc4
#define ST7586_SET_VOP_OFFSET	0xc7
#define ST7586_ENABLE_ANALOG	0xd0
#define ST7586_AUTO_READ_CTRL	0xd7
#define ST7586_OTP_RW_CTRL	0xe0
#define ST7586_OTP_CTRL_OUT	0xe1
#define ST7586_OTP_READ		0xe3

#define ST7586_DISP_CTRL_MX	BIT(6)
#define ST7586_DISP_CTRL_MY	BIT(7)

/*
 * The ST7586 controller has an unusual pixel format where 2bpp grayscale is
 * packed 3 pixels per byte with the first two pixels using 3 bits and the 3rd
 * pixel using only 2 bits.
 *
 * |  D7  |  D6  |  D5  ||      |      || 2bpp |
 * | (D4) | (D3) | (D2) ||  D1  |  D0  || GRAY |
 * +------+------+------++------+------++------+
 * |  1   |  1   |  1   ||  1   |  1   || 0  0 | black
 * |  1   |  0   |  0   ||  1   |  0   || 0  1 | dark gray
 * |  0   |  1   |  0   ||  0   |  1   || 1  0 | light gray
 * |  0   |  0   |  0   ||  0   |  0   || 1  1 | white
 */

static const u8 st7586_lookup[] = { 0x7, 0x4, 0x2, 0x0 };

static void st7586_xrgb8888_to_gray332(u8 *dst, void *vaddr,
				       struct drm_framebuffer *fb,
				       struct drm_rect *clip,
				       struct drm_format_conv_state *fmtcnv_state)
{
	size_t len = (clip->x2 - clip->x1) * (clip->y2 - clip->y1);
	unsigned int x, y;
	u8 *src, *buf, val;
	struct iosys_map dst_map, vmap;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	iosys_map_set_vaddr(&dst_map, buf);
	iosys_map_set_vaddr(&vmap, vaddr);
	drm_fb_xrgb8888_to_gray8(&dst_map, NULL, &vmap, fb, clip, fmtcnv_state);
	src = buf;

	for (y = clip->y1; y < clip->y2; y++) {
		for (x = clip->x1; x < clip->x2; x += 3) {
			val = st7586_lookup[*src++ >> 6] << 5;
			val |= st7586_lookup[*src++ >> 6] << 2;
			val |= st7586_lookup[*src++ >> 6] >> 1;
			*dst++ = val;
		}
	}

	kfree(buf);
}

static int st7586_buf_copy(void *dst, struct iosys_map *src, struct drm_framebuffer *fb,
			   struct drm_rect *clip, struct drm_format_conv_state *fmtcnv_state)
{
	int ret;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return ret;

	st7586_xrgb8888_to_gray332(dst, src->vaddr, fb, clip, fmtcnv_state);

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	return 0;
}

static void st7586_fb_dirty(struct iosys_map *src, struct drm_framebuffer *fb,
			    struct drm_rect *rect, struct drm_format_conv_state *fmtcnv_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(fb->dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	int start, end, ret = 0;

	/* 3 pixels per byte, so grow clip to nearest multiple of 3 */
	rect->x1 = rounddown(rect->x1, 3);
	rect->x2 = roundup(rect->x2, 3);

	DRM_DEBUG_KMS("Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id, DRM_RECT_ARG(rect));

	ret = st7586_buf_copy(dbidev->tx_buf, src, fb, rect, fmtcnv_state);
	if (ret)
		goto err_msg;

	/* Pixels are packed 3 per byte */
	start = rect->x1 / 3;
	end = rect->x2 / 3;

	mipi_dbi_command(dbi, MIPI_DCS_SET_COLUMN_ADDRESS,
			 (start >> 8) & 0xFF, start & 0xFF,
			 (end >> 8) & 0xFF, (end - 1) & 0xFF);
	mipi_dbi_command(dbi, MIPI_DCS_SET_PAGE_ADDRESS,
			 (rect->y1 >> 8) & 0xFF, rect->y1 & 0xFF,
			 (rect->y2 >> 8) & 0xFF, (rect->y2 - 1) & 0xFF);

	ret = mipi_dbi_command_buf(dbi, MIPI_DCS_WRITE_MEMORY_START,
				   (u8 *)dbidev->tx_buf,
				   (end - start) * (rect->y2 - rect->y1));
err_msg:
	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n", ret);
}

static void st7586_pipe_update(struct drm_simple_display_pipe *pipe,
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
		st7586_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
				&shadow_plane_state->fmtcnv_state);

	drm_dev_exit(idx);
}

static void st7586_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct mipi_dbi *dbi = &dbidev->dbi;
	struct drm_rect rect = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};
	int idx, ret;
	u8 addr_mode;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_reset(dbidev);
	if (ret)
		goto out_exit;

	mipi_dbi_command(dbi, ST7586_AUTO_READ_CTRL, 0x9f);
	mipi_dbi_command(dbi, ST7586_OTP_RW_CTRL, 0x00);

	msleep(10);

	mipi_dbi_command(dbi, ST7586_OTP_READ);

	msleep(20);

	mipi_dbi_command(dbi, ST7586_OTP_CTRL_OUT);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);

	msleep(50);

	mipi_dbi_command(dbi, ST7586_SET_VOP_OFFSET, 0x00);
	mipi_dbi_command(dbi, ST7586_SET_VOP, 0xe3, 0x00);
	mipi_dbi_command(dbi, ST7586_SET_BIAS_SYSTEM, 0x02);
	mipi_dbi_command(dbi, ST7586_SET_BOOST_LEVEL, 0x04);
	mipi_dbi_command(dbi, ST7586_ENABLE_ANALOG, 0x1d);
	mipi_dbi_command(dbi, ST7586_SET_NLINE_INV, 0x00);
	mipi_dbi_command(dbi, ST7586_DISP_MODE_GRAY);
	mipi_dbi_command(dbi, ST7586_ENABLE_DDRAM, 0x02);

	switch (dbidev->rotation) {
	default:
		addr_mode = 0x00;
		break;
	case 90:
		addr_mode = ST7586_DISP_CTRL_MY;
		break;
	case 180:
		addr_mode = ST7586_DISP_CTRL_MX | ST7586_DISP_CTRL_MY;
		break;
	case 270:
		addr_mode = ST7586_DISP_CTRL_MX;
		break;
	}
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	mipi_dbi_command(dbi, ST7586_SET_DISP_DUTY, 0x7f);
	mipi_dbi_command(dbi, ST7586_SET_PART_DISP, 0xa0);
	mipi_dbi_command(dbi, MIPI_DCS_SET_PARTIAL_ROWS, 0x00, 0x00, 0x00, 0x77);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_INVERT_MODE);

	msleep(100);

	st7586_fb_dirty(&shadow_plane_state->data[0], fb, &rect,
			&shadow_plane_state->fmtcnv_state);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
out_exit:
	drm_dev_exit(idx);
}

static void st7586_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);

	/*
	 * This callback is not protected by drm_dev_enter/exit since we want to
	 * turn off the display on regular driver unload. It's highly unlikely
	 * that the underlying SPI controller is gone should this be called after
	 * unplug.
	 */

	DRM_DEBUG_KMS("\n");

	mipi_dbi_command(&dbidev->dbi, MIPI_DCS_SET_DISPLAY_OFF);
}

static const u32 st7586_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_simple_display_pipe_funcs st7586_pipe_funcs = {
	.mode_valid	= mipi_dbi_pipe_mode_valid,
	.enable		= st7586_pipe_enable,
	.disable	= st7586_pipe_disable,
	.update		= st7586_pipe_update,
	.begin_fb_access = mipi_dbi_pipe_begin_fb_access,
	.end_fb_access	= mipi_dbi_pipe_end_fb_access,
	.reset_plane	= mipi_dbi_pipe_reset_plane,
	.duplicate_plane_state = mipi_dbi_pipe_duplicate_plane_state,
	.destroy_plane_state = mipi_dbi_pipe_destroy_plane_state,
};

static const struct drm_display_mode st7586_mode = {
	DRM_SIMPLE_MODE(178, 128, 37, 27),
};

DEFINE_DRM_GEM_DMA_FOPS(st7586_fops);

static const struct drm_driver st7586_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &st7586_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "st7586",
	.desc			= "Sitronix ST7586",
	.date			= "20170801",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id st7586_of_match[] = {
	{ .compatible = "lego,ev3-lcd" },
	{},
};
MODULE_DEVICE_TABLE(of, st7586_of_match);

static const struct spi_device_id st7586_id[] = {
	{ "ev3-lcd", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, st7586_id);

static int st7586_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *a0;
	u32 rotation = 0;
	size_t bufsize;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &st7586_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	bufsize = (st7586_mode.vdisplay + 2) / 3 * st7586_mode.hdisplay;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get GPIO 'reset'\n");

	a0 = devm_gpiod_get(dev, "a0", GPIOD_OUT_LOW);
	if (IS_ERR(a0))
		return dev_err_probe(dev, PTR_ERR(a0), "Failed to get GPIO 'a0'\n");

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, a0);
	if (ret)
		return ret;

	/* Cannot read from this controller via SPI */
	dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init_with_formats(dbidev, &st7586_pipe_funcs,
					     st7586_formats, ARRAY_SIZE(st7586_formats),
					     &st7586_mode, rotation, bufsize);
	if (ret)
		return ret;

	/*
	 * we are using 8-bit data, so we are not actually swapping anything,
	 * but setting mipi->swap_bytes makes mipi_dbi_typec3_command() do the
	 * right thing and not use 16-bit transfers (which results in swapped
	 * bytes on little-endian systems and causes out of order data to be
	 * sent to the display).
	 */
	dbi->swap_bytes = true;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_dma_setup(drm, 0);

	return 0;
}

static void st7586_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void st7586_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver st7586_spi_driver = {
	.driver = {
		.name = "st7586",
		.of_match_table = st7586_of_match,
	},
	.id_table = st7586_id,
	.probe = st7586_probe,
	.remove = st7586_remove,
	.shutdown = st7586_shutdown,
};
module_spi_driver(st7586_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7586 DRM driver");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
