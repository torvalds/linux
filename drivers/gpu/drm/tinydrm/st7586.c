/*
 * DRM driver for Sitronix ST7586 panels
 *
 * Copyright 2017 David Lechner <david@lechnology.com>
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

#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>

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
				       struct drm_clip_rect *clip)
{
	size_t len = (clip->x2 - clip->x1) * (clip->y2 - clip->y1);
	unsigned int x, y;
	u8 *src, *buf, val;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	tinydrm_xrgb8888_to_gray8(buf, vaddr, fb, clip);
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

static int st7586_buf_copy(void *dst, struct drm_framebuffer *fb,
			   struct drm_clip_rect *clip)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct dma_buf_attachment *import_attach = cma_obj->base.import_attach;
	void *src = cma_obj->vaddr;
	int ret = 0;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	st7586_xrgb8888_to_gray332(dst, src, fb, clip);

	if (import_attach)
		ret = dma_buf_end_cpu_access(import_attach->dmabuf,
					     DMA_FROM_DEVICE);

	return ret;
}

static int st7586_fb_dirty(struct drm_framebuffer *fb,
			   struct drm_file *file_priv, unsigned int flags,
			   unsigned int color, struct drm_clip_rect *clips,
			   unsigned int num_clips)
{
	struct tinydrm_device *tdev = fb->dev->dev_private;
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);
	struct drm_clip_rect clip;
	int start, end;
	int ret = 0;

	mutex_lock(&tdev->dirty_lock);

	if (!mipi->enabled)
		goto out_unlock;

	/* fbdev can flush even when we're not interested */
	if (tdev->pipe.plane.fb != fb)
		goto out_unlock;

	tinydrm_merge_clips(&clip, clips, num_clips, flags, fb->width,
			    fb->height);

	/* 3 pixels per byte, so grow clip to nearest multiple of 3 */
	clip.x1 = rounddown(clip.x1, 3);
	clip.x2 = roundup(clip.x2, 3);

	DRM_DEBUG("Flushing [FB:%d] x1=%u, x2=%u, y1=%u, y2=%u\n", fb->base.id,
		  clip.x1, clip.x2, clip.y1, clip.y2);

	ret = st7586_buf_copy(mipi->tx_buf, fb, &clip);
	if (ret)
		goto out_unlock;

	/* Pixels are packed 3 per byte */
	start = clip.x1 / 3;
	end = clip.x2 / 3;

	mipi_dbi_command(mipi, MIPI_DCS_SET_COLUMN_ADDRESS,
			 (start >> 8) & 0xFF, start & 0xFF,
			 (end >> 8) & 0xFF, (end - 1) & 0xFF);
	mipi_dbi_command(mipi, MIPI_DCS_SET_PAGE_ADDRESS,
			 (clip.y1 >> 8) & 0xFF, clip.y1 & 0xFF,
			 (clip.y2 >> 8) & 0xFF, (clip.y2 - 1) & 0xFF);

	ret = mipi_dbi_command_buf(mipi, MIPI_DCS_WRITE_MEMORY_START,
				   (u8 *)mipi->tx_buf,
				   (end - start) * (clip.y2 - clip.y1));

out_unlock:
	mutex_unlock(&tdev->dirty_lock);

	if (ret)
		dev_err_once(fb->dev->dev, "Failed to update display %d\n",
			     ret);

	return ret;
}

static const struct drm_framebuffer_funcs st7586_fb_funcs = {
	.destroy	= drm_fb_cma_destroy,
	.create_handle	= drm_fb_cma_create_handle,
	.dirty		= st7586_fb_dirty,
};

static void st7586_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state)
{
	struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);
	struct drm_framebuffer *fb = pipe->plane.fb;
	struct device *dev = tdev->drm->dev;
	int ret;
	u8 addr_mode;

	DRM_DEBUG_KMS("\n");

	mipi_dbi_hw_reset(mipi);
	ret = mipi_dbi_command(mipi, ST7586_AUTO_READ_CTRL, 0x9f);
	if (ret) {
		dev_err(dev, "Error sending command %d\n", ret);
		return;
	}

	mipi_dbi_command(mipi, ST7586_OTP_RW_CTRL, 0x00);

	msleep(10);

	mipi_dbi_command(mipi, ST7586_OTP_READ);

	msleep(20);

	mipi_dbi_command(mipi, ST7586_OTP_CTRL_OUT);
	mipi_dbi_command(mipi, MIPI_DCS_EXIT_SLEEP_MODE);
	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_OFF);

	msleep(50);

	mipi_dbi_command(mipi, ST7586_SET_VOP_OFFSET, 0x00);
	mipi_dbi_command(mipi, ST7586_SET_VOP, 0xe3, 0x00);
	mipi_dbi_command(mipi, ST7586_SET_BIAS_SYSTEM, 0x02);
	mipi_dbi_command(mipi, ST7586_SET_BOOST_LEVEL, 0x04);
	mipi_dbi_command(mipi, ST7586_ENABLE_ANALOG, 0x1d);
	mipi_dbi_command(mipi, ST7586_SET_NLINE_INV, 0x00);
	mipi_dbi_command(mipi, ST7586_DISP_MODE_GRAY);
	mipi_dbi_command(mipi, ST7586_ENABLE_DDRAM, 0x02);

	switch (mipi->rotation) {
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
	mipi_dbi_command(mipi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	mipi_dbi_command(mipi, ST7586_SET_DISP_DUTY, 0x7f);
	mipi_dbi_command(mipi, ST7586_SET_PART_DISP, 0xa0);
	mipi_dbi_command(mipi, MIPI_DCS_SET_PARTIAL_AREA, 0x00, 0x00, 0x00, 0x77);
	mipi_dbi_command(mipi, MIPI_DCS_EXIT_INVERT_MODE);

	msleep(100);

	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_ON);

	mipi->enabled = true;

	if (fb)
		fb->funcs->dirty(fb, NULL, 0, 0, NULL, 0);
}

static void st7586_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);

	DRM_DEBUG_KMS("\n");

	if (!mipi->enabled)
		return;

	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_OFF);
	mipi->enabled = false;
}

static const u32 st7586_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static int st7586_init(struct device *dev, struct mipi_dbi *mipi,
		const struct drm_simple_display_pipe_funcs *pipe_funcs,
		struct drm_driver *driver, const struct drm_display_mode *mode,
		unsigned int rotation)
{
	size_t bufsize = (mode->vdisplay + 2) / 3 * mode->hdisplay;
	struct tinydrm_device *tdev = &mipi->tinydrm;
	int ret;

	mutex_init(&mipi->cmdlock);

	mipi->tx_buf = devm_kmalloc(dev, bufsize, GFP_KERNEL);
	if (!mipi->tx_buf)
		return -ENOMEM;

	ret = devm_tinydrm_init(dev, tdev, &st7586_fb_funcs, driver);
	if (ret)
		return ret;

	ret = tinydrm_display_pipe_init(tdev, pipe_funcs,
					DRM_MODE_CONNECTOR_VIRTUAL,
					st7586_formats,
					ARRAY_SIZE(st7586_formats),
					mode, rotation);
	if (ret)
		return ret;

	tdev->drm->mode_config.preferred_depth = 32;
	mipi->rotation = rotation;

	drm_mode_config_reset(tdev->drm);

	DRM_DEBUG_KMS("preferred_depth=%u, rotation = %u\n",
		      tdev->drm->mode_config.preferred_depth, rotation);

	return 0;
}

static const struct drm_simple_display_pipe_funcs st7586_pipe_funcs = {
	.enable		= st7586_pipe_enable,
	.disable	= st7586_pipe_disable,
	.update		= tinydrm_display_pipe_update,
	.prepare_fb	= tinydrm_display_pipe_prepare_fb,
};

static const struct drm_display_mode st7586_mode = {
	TINYDRM_MODE(178, 128, 37, 27),
};

DEFINE_DRM_GEM_CMA_FOPS(st7586_fops);

static struct drm_driver st7586_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
				  DRIVER_ATOMIC,
	.fops			= &st7586_fops,
	TINYDRM_GEM_DRIVER_OPS,
	.lastclose		= tinydrm_lastclose,
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
	struct mipi_dbi *mipi;
	struct gpio_desc *a0;
	u32 rotation = 0;
	int ret;

	mipi = devm_kzalloc(dev, sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	mipi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(mipi->reset)) {
		dev_err(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(mipi->reset);
	}

	a0 = devm_gpiod_get(dev, "a0", GPIOD_OUT_LOW);
	if (IS_ERR(a0)) {
		dev_err(dev, "Failed to get gpio 'a0'\n");
		return PTR_ERR(a0);
	}

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, mipi, a0);
	if (ret)
		return ret;

	/* Cannot read from this controller via SPI */
	mipi->read_commands = NULL;

	/*
	 * we are using 8-bit data, so we are not actually swapping anything,
	 * but setting mipi->swap_bytes makes mipi_dbi_typec3_command() do the
	 * right thing and not use 16-bit transfers (which results in swapped
	 * bytes on little-endian systems and causes out of order data to be
	 * sent to the display).
	 */
	mipi->swap_bytes = true;

	ret = st7586_init(&spi->dev, mipi, &st7586_pipe_funcs, &st7586_driver,
			  &st7586_mode, rotation);
	if (ret)
		return ret;

	spi_set_drvdata(spi, mipi);

	return devm_tinydrm_register(&mipi->tinydrm);
}

static void st7586_shutdown(struct spi_device *spi)
{
	struct mipi_dbi *mipi = spi_get_drvdata(spi);

	tinydrm_shutdown(&mipi->tinydrm);
}

static struct spi_driver st7586_spi_driver = {
	.driver = {
		.name = "st7586",
		.owner = THIS_MODULE,
		.of_match_table = st7586_of_match,
	},
	.id_table = st7586_id,
	.probe = st7586_probe,
	.shutdown = st7586_shutdown,
};
module_spi_driver(st7586_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7586 DRM driver");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
