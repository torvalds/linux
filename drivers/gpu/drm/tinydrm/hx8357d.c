// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for the HX8357D LCD controller
 *
 * Copyright 2018 Broadcom
 * Copyright 2018 David Lechner <david@lechnology.com>
 * Copyright 2016 Noralf Trønnes
 * Copyright (C) 2015 Adafruit Industries
 * Copyright (C) 2013 Christian Vogelgsang
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>
#include <video/mipi_display.h>

#define HX8357D_SETOSC 0xb0
#define HX8357D_SETPOWER 0xb1
#define HX8357D_SETRGB 0xb3
#define HX8357D_SETCYC 0xb3
#define HX8357D_SETCOM 0xb6
#define HX8357D_SETEXTC 0xb9
#define HX8357D_SETSTBA 0xc0
#define HX8357D_SETPANEL 0xcc
#define HX8357D_SETGAMMA 0xe0

#define HX8357D_MADCTL_MY  0x80
#define HX8357D_MADCTL_MX  0x40
#define HX8357D_MADCTL_MV  0x20
#define HX8357D_MADCTL_ML  0x10
#define HX8357D_MADCTL_RGB 0x00
#define HX8357D_MADCTL_BGR 0x08
#define HX8357D_MADCTL_MH  0x04

static void yx240qv29_enable(struct drm_simple_display_pipe *pipe,
			     struct drm_crtc_state *crtc_state,
			     struct drm_plane_state *plane_state)
{
	struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);
	u8 addr_mode;
	int ret;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_conditional_reset(mipi);
	if (ret < 0)
		return;
	if (ret == 1)
		goto out_enable;

	/* setextc */
	mipi_dbi_command(mipi, HX8357D_SETEXTC, 0xFF, 0x83, 0x57);
	msleep(150);

	/* setRGB which also enables SDO */
	mipi_dbi_command(mipi, HX8357D_SETRGB, 0x00, 0x00, 0x06, 0x06);

	/* -1.52V */
	mipi_dbi_command(mipi, HX8357D_SETCOM, 0x25);

	/* Normal mode 70Hz, Idle mode 55 Hz */
	mipi_dbi_command(mipi, HX8357D_SETOSC, 0x68);

	/* Set Panel - BGR, Gate direction swapped */
	mipi_dbi_command(mipi, HX8357D_SETPANEL, 0x05);

	mipi_dbi_command(mipi, HX8357D_SETPOWER,
			 0x00,  /* Not deep standby */
			 0x15,  /* BT */
			 0x1C,  /* VSPR */
			 0x1C,  /* VSNR */
			 0x83,  /* AP */
			 0xAA);  /* FS */

	mipi_dbi_command(mipi, HX8357D_SETSTBA,
			 0x50,  /* OPON normal */
			 0x50,  /* OPON idle */
			 0x01,  /* STBA */
			 0x3C,  /* STBA */
			 0x1E,  /* STBA */
			 0x08);  /* GEN */

	mipi_dbi_command(mipi, HX8357D_SETCYC,
			 0x02,  /* NW 0x02 */
			 0x40,  /* RTN */
			 0x00,  /* DIV */
			 0x2A,  /* DUM */
			 0x2A,  /* DUM */
			 0x0D,  /* GDON */
			 0x78);  /* GDOFF */

	mipi_dbi_command(mipi, HX8357D_SETGAMMA,
			 0x02,
			 0x0A,
			 0x11,
			 0x1d,
			 0x23,
			 0x35,
			 0x41,
			 0x4b,
			 0x4b,
			 0x42,
			 0x3A,
			 0x27,
			 0x1B,
			 0x08,
			 0x09,
			 0x03,
			 0x02,
			 0x0A,
			 0x11,
			 0x1d,
			 0x23,
			 0x35,
			 0x41,
			 0x4b,
			 0x4b,
			 0x42,
			 0x3A,
			 0x27,
			 0x1B,
			 0x08,
			 0x09,
			 0x03,
			 0x00,
			 0x01);

	/* 16 bit */
	mipi_dbi_command(mipi, MIPI_DCS_SET_PIXEL_FORMAT,
			 MIPI_DCS_PIXEL_FMT_16BIT);

	/* TE off */
	mipi_dbi_command(mipi, MIPI_DCS_SET_TEAR_ON, 0x00);

	/* tear line */
	mipi_dbi_command(mipi, MIPI_DCS_SET_TEAR_SCANLINE, 0x00, 0x02);

	/* Exit Sleep */
	mipi_dbi_command(mipi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(150);

	/* display on */
	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_ON);
	usleep_range(5000, 7000);

out_enable:
	switch (mipi->rotation) {
	default:
		addr_mode = HX8357D_MADCTL_MX | HX8357D_MADCTL_MY;
		break;
	case 90:
		addr_mode = HX8357D_MADCTL_MV | HX8357D_MADCTL_MY;
		break;
	case 180:
		addr_mode = 0;
		break;
	case 270:
		addr_mode = HX8357D_MADCTL_MV | HX8357D_MADCTL_MX;
		break;
	}
	mipi_dbi_command(mipi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	mipi_dbi_enable_flush(mipi, crtc_state, plane_state);
}

static const struct drm_simple_display_pipe_funcs hx8357d_pipe_funcs = {
	.enable = yx240qv29_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode yx350hv15_mode = {
	TINYDRM_MODE(320, 480, 60, 75),
};

DEFINE_DRM_GEM_CMA_FOPS(hx8357d_fops);

static struct drm_driver hx8357d_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME | DRIVER_ATOMIC,
	.fops			= &hx8357d_fops,
	DRM_GEM_CMA_VMAP_DRIVER_OPS,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "hx8357d",
	.desc			= "HX8357D",
	.date			= "20181023",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id hx8357d_of_match[] = {
	{ .compatible = "adafruit,yx350hv15" },
	{ }
};
MODULE_DEVICE_TABLE(of, hx8357d_of_match);

static const struct spi_device_id hx8357d_id[] = {
	{ "yx350hv15", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, hx8357d_id);

static int hx8357d_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi *mipi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	mipi = devm_kzalloc(dev, sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	mipi->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(mipi->backlight))
		return PTR_ERR(mipi->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, mipi, dc);
	if (ret)
		return ret;

	ret = mipi_dbi_init(&spi->dev, mipi, &hx8357d_pipe_funcs,
			    &hx8357d_driver, &yx350hv15_mode, rotation);
	if (ret)
		return ret;

	spi_set_drvdata(spi, mipi);

	return devm_tinydrm_register(&mipi->tinydrm);
}

static void hx8357d_shutdown(struct spi_device *spi)
{
	struct mipi_dbi *mipi = spi_get_drvdata(spi);

	tinydrm_shutdown(&mipi->tinydrm);
}

static struct spi_driver hx8357d_spi_driver = {
	.driver = {
		.name = "hx8357d",
		.of_match_table = hx8357d_of_match,
	},
	.id_table = hx8357d_id,
	.probe = hx8357d_probe,
	.shutdown = hx8357d_shutdown,
};
module_spi_driver(hx8357d_spi_driver);

MODULE_DESCRIPTION("HX8357D DRM driver");
MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_LICENSE("GPL");
