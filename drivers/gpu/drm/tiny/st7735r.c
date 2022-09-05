// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for display panels connected to a Sitronix ST7715R or ST7735R
 * display controller in SPI mode.
 *
 * Copyright 2017 David Lechner <david@lechnology.com>
 * Copyright (C) 2019 Glider bvba
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>

#define ST7735R_FRMCTR1		0xb1
#define ST7735R_FRMCTR2		0xb2
#define ST7735R_FRMCTR3		0xb3
#define ST7735R_INVCTR		0xb4
#define ST7735R_PWCTR1		0xc0
#define ST7735R_PWCTR2		0xc1
#define ST7735R_PWCTR3		0xc2
#define ST7735R_PWCTR4		0xc3
#define ST7735R_PWCTR5		0xc4
#define ST7735R_VMCTR1		0xc5
#define ST7735R_GAMCTRP1	0xe0
#define ST7735R_GAMCTRN1	0xe1

#define ST7735R_MY	BIT(7)
#define ST7735R_MX	BIT(6)
#define ST7735R_MV	BIT(5)
#define ST7735R_RGB	BIT(3)

struct st7735r_cfg {
	const struct drm_display_mode mode;
	unsigned int left_offset;
	unsigned int top_offset;
	unsigned int write_only:1;
	unsigned int rgb:1;		/* RGB (vs. BGR) */
};

struct st7735r_priv {
	struct mipi_dbi_dev dbidev;	/* Must be first for .release() */
	const struct st7735r_cfg *cfg;
};

static void st7735r_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct st7735r_priv *priv = container_of(dbidev, struct st7735r_priv,
						 dbidev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	int ret, idx;
	u8 addr_mode;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_reset(dbidev);
	if (ret)
		goto out_exit;

	msleep(150);

	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(500);

	mipi_dbi_command(dbi, ST7735R_FRMCTR1, 0x01, 0x2c, 0x2d);
	mipi_dbi_command(dbi, ST7735R_FRMCTR2, 0x01, 0x2c, 0x2d);
	mipi_dbi_command(dbi, ST7735R_FRMCTR3, 0x01, 0x2c, 0x2d, 0x01, 0x2c,
			 0x2d);
	mipi_dbi_command(dbi, ST7735R_INVCTR, 0x07);
	mipi_dbi_command(dbi, ST7735R_PWCTR1, 0xa2, 0x02, 0x84);
	mipi_dbi_command(dbi, ST7735R_PWCTR2, 0xc5);
	mipi_dbi_command(dbi, ST7735R_PWCTR3, 0x0a, 0x00);
	mipi_dbi_command(dbi, ST7735R_PWCTR4, 0x8a, 0x2a);
	mipi_dbi_command(dbi, ST7735R_PWCTR5, 0x8a, 0xee);
	mipi_dbi_command(dbi, ST7735R_VMCTR1, 0x0e);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_INVERT_MODE);
	switch (dbidev->rotation) {
	default:
		addr_mode = ST7735R_MX | ST7735R_MY;
		break;
	case 90:
		addr_mode = ST7735R_MX | ST7735R_MV;
		break;
	case 180:
		addr_mode = 0;
		break;
	case 270:
		addr_mode = ST7735R_MY | ST7735R_MV;
		break;
	}

	if (priv->cfg->rgb)
		addr_mode |= ST7735R_RGB;

	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT,
			 MIPI_DCS_PIXEL_FMT_16BIT);
	mipi_dbi_command(dbi, ST7735R_GAMCTRP1, 0x02, 0x1c, 0x07, 0x12, 0x37,
			 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2b, 0x39, 0x00, 0x01,
			 0x03, 0x10);
	mipi_dbi_command(dbi, ST7735R_GAMCTRN1, 0x03, 0x1d, 0x07, 0x06, 0x2e,
			 0x2c, 0x29, 0x2d, 0x2e, 0x2e, 0x37, 0x3f, 0x00, 0x00,
			 0x02, 0x10);
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	msleep(100);

	mipi_dbi_command(dbi, MIPI_DCS_ENTER_NORMAL_MODE);

	msleep(20);

	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs st7735r_pipe_funcs = {
	.mode_valid	= mipi_dbi_pipe_mode_valid,
	.enable		= st7735r_pipe_enable,
	.disable	= mipi_dbi_pipe_disable,
	.update		= mipi_dbi_pipe_update,
};

static const struct st7735r_cfg jd_t18003_t01_cfg = {
	.mode		= { DRM_SIMPLE_MODE(128, 160, 28, 35) },
	/* Cannot read from Adafruit 1.8" display via SPI */
	.write_only	= true,
};

static const struct st7735r_cfg rh128128t_cfg = {
	.mode		= { DRM_SIMPLE_MODE(128, 128, 25, 26) },
	.left_offset	= 2,
	.top_offset	= 3,
	.rgb		= true,
};

DEFINE_DRM_GEM_DMA_FOPS(st7735r_fops);

static const struct drm_driver st7735r_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &st7735r_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "st7735r",
	.desc			= "Sitronix ST7735R",
	.date			= "20171128",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id st7735r_of_match[] = {
	{ .compatible = "jianda,jd-t18003-t01", .data = &jd_t18003_t01_cfg },
	{ .compatible = "okaya,rh128128t", .data = &rh128128t_cfg },
	{ },
};
MODULE_DEVICE_TABLE(of, st7735r_of_match);

static const struct spi_device_id st7735r_id[] = {
	{ "jd-t18003-t01", (uintptr_t)&jd_t18003_t01_cfg },
	{ "rh128128t", (uintptr_t)&rh128128t_cfg },
	{ },
};
MODULE_DEVICE_TABLE(spi, st7735r_id);

static int st7735r_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct st7735r_cfg *cfg;
	struct mipi_dbi_dev *dbidev;
	struct st7735r_priv *priv;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	cfg = device_get_match_data(&spi->dev);
	if (!cfg)
		cfg = (void *)spi_get_device_id(spi)->driver_data;

	priv = devm_drm_dev_alloc(dev, &st7735r_driver,
				  struct st7735r_priv, dbidev.drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	dbidev = &priv->dbidev;
	priv->cfg = cfg;

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get GPIO 'reset'\n");

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		return dev_err_probe(dev, PTR_ERR(dc), "Failed to get GPIO 'dc'\n");

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	if (cfg->write_only)
		dbi->read_commands = NULL;

	dbidev->left_offset = cfg->left_offset;
	dbidev->top_offset = cfg->top_offset;

	ret = mipi_dbi_dev_init(dbidev, &st7735r_pipe_funcs, &cfg->mode,
				rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static void st7735r_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void st7735r_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver st7735r_spi_driver = {
	.driver = {
		.name = "st7735r",
		.of_match_table = st7735r_of_match,
	},
	.id_table = st7735r_id,
	.probe = st7735r_probe,
	.remove = st7735r_remove,
	.shutdown = st7735r_shutdown,
};
module_spi_driver(st7735r_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7735R DRM driver");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
