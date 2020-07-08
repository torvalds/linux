// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for Ilitek ILI9341 panels
 *
 * Copyright 2018 David Lechner <david@lechnology.com>
 *
 * Based on mi0283qt.c:
 * Copyright 2016 Noralf Trønnes
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modeset_helper.h>
#include <video/mipi_display.h>

#define ILI9341_FRMCTR1		0xb1
#define ILI9341_DISCTRL		0xb6
#define ILI9341_ETMOD		0xb7

#define ILI9341_PWCTRL1		0xc0
#define ILI9341_PWCTRL2		0xc1
#define ILI9341_VMCTRL1		0xc5
#define ILI9341_VMCTRL2		0xc7
#define ILI9341_PWCTRLA		0xcb
#define ILI9341_PWCTRLB		0xcf

#define ILI9341_PGAMCTRL	0xe0
#define ILI9341_NGAMCTRL	0xe1
#define ILI9341_DTCTRLA		0xe8
#define ILI9341_DTCTRLB		0xea
#define ILI9341_PWRSEQ		0xed

#define ILI9341_EN3GAM		0xf2
#define ILI9341_PUMPCTRL	0xf7

#define ILI9341_MADCTL_BGR	BIT(3)
#define ILI9341_MADCTL_MV	BIT(5)
#define ILI9341_MADCTL_MX	BIT(6)
#define ILI9341_MADCTL_MY	BIT(7)

static void yx240qv29_enable(struct drm_simple_display_pipe *pipe,
			     struct drm_crtc_state *crtc_state,
			     struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 addr_mode;
	int ret, idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_conditional_reset(dbidev);
	if (ret < 0)
		goto out_exit;
	if (ret == 1)
		goto out_enable;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);

	mipi_dbi_command(dbi, ILI9341_PWCTRLB, 0x00, 0xc1, 0x30);
	mipi_dbi_command(dbi, ILI9341_PWRSEQ, 0x64, 0x03, 0x12, 0x81);
	mipi_dbi_command(dbi, ILI9341_DTCTRLA, 0x85, 0x00, 0x78);
	mipi_dbi_command(dbi, ILI9341_PWCTRLA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	mipi_dbi_command(dbi, ILI9341_PUMPCTRL, 0x20);
	mipi_dbi_command(dbi, ILI9341_DTCTRLB, 0x00, 0x00);

	/* Power Control */
	mipi_dbi_command(dbi, ILI9341_PWCTRL1, 0x23);
	mipi_dbi_command(dbi, ILI9341_PWCTRL2, 0x10);
	/* VCOM */
	mipi_dbi_command(dbi, ILI9341_VMCTRL1, 0x3e, 0x28);
	mipi_dbi_command(dbi, ILI9341_VMCTRL2, 0x86);

	/* Memory Access Control */
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);

	/* Frame Rate */
	mipi_dbi_command(dbi, ILI9341_FRMCTR1, 0x00, 0x1b);

	/* Gamma */
	mipi_dbi_command(dbi, ILI9341_EN3GAM, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_SET_GAMMA_CURVE, 0x01);
	mipi_dbi_command(dbi, ILI9341_PGAMCTRL,
			 0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1,
			 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00);
	mipi_dbi_command(dbi, ILI9341_NGAMCTRL,
			 0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1,
			 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f);

	/* DDRAM */
	mipi_dbi_command(dbi, ILI9341_ETMOD, 0x07);

	/* Display */
	mipi_dbi_command(dbi, ILI9341_DISCTRL, 0x08, 0x82, 0x27, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(100);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

out_enable:
	switch (dbidev->rotation) {
	default:
		addr_mode = ILI9341_MADCTL_MX;
		break;
	case 90:
		addr_mode = ILI9341_MADCTL_MV;
		break;
	case 180:
		addr_mode = ILI9341_MADCTL_MY;
		break;
	case 270:
		addr_mode = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			    ILI9341_MADCTL_MX;
		break;
	}
	addr_mode |= ILI9341_MADCTL_BGR;
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs ili9341_pipe_funcs = {
	.enable = yx240qv29_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode yx240qv29_mode = {
	DRM_SIMPLE_MODE(240, 320, 37, 49),
};

DEFINE_DRM_GEM_CMA_FOPS(ili9341_fops);

static struct drm_driver ili9341_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9341_fops,
	DRM_GEM_CMA_VMAP_DRIVER_OPS,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9341",
	.desc			= "Ilitek ILI9341",
	.date			= "20180514",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id ili9341_of_match[] = {
	{ .compatible = "adafruit,yx240qv29" },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9341_of_match);

static const struct spi_device_id ili9341_id[] = {
	{ "yx240qv29", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9341_id);

static int ili9341_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &ili9341_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(dbi->reset);
	}

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	ret = mipi_dbi_dev_init(dbidev, &ili9341_pipe_funcs, &yx240qv29_mode, rotation);
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

static int ili9341_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
}

static void ili9341_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver ili9341_spi_driver = {
	.driver = {
		.name = "ili9341",
		.of_match_table = ili9341_of_match,
	},
	.id_table = ili9341_id,
	.probe = ili9341_probe,
	.remove = ili9341_remove,
	.shutdown = ili9341_shutdown,
};
module_spi_driver(ili9341_spi_driver);

MODULE_DESCRIPTION("Ilitek ILI9341 DRM driver");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
