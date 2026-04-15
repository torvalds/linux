// SPDX-License-Identifier: GPL-2.0
/*
 * SPI interface to the Ilitek ILI9806E panel.
 *
 * Copyright (c) 2026 Amarula Solutions, Dario Binacchi <dario.binacchi@amarulasolutions.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <drm/drm_mipi_dbi.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <video/mipi_display.h>

#include "panel-ilitek-ili9806e-core.h"

struct ili9806e_spi_panel {
	struct spi_device *spi;
	struct mipi_dbi dbi;
	const struct ili9806e_spi_panel_desc *desc;
};

struct ili9806e_spi_panel_desc {
	const struct drm_display_mode *display_mode;
	u32 bus_format;
	u32 bus_flags;
	void (*init_sequence)(struct ili9806e_spi_panel *ctx);
};

static int ili9806e_spi_off(struct ili9806e_spi_panel *ctx)
{
	struct mipi_dbi *dbi = &ctx->dbi;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE, 0x00);

	return 0;
}

static int ili9806e_spi_unprepare(struct drm_panel *panel)
{
	struct ili9806e_spi_panel *ctx = ili9806e_get_transport(panel);
	struct device *dev = &ctx->spi->dev;
	int ret;

	ili9806e_spi_off(ctx);

	ret = ili9806e_power_off(dev);
	if (ret)
		dev_err(dev, "power off failed: %d\n", ret);

	return 0;
}

static int ili9806e_spi_prepare(struct drm_panel *panel)
{
	struct ili9806e_spi_panel *ctx = ili9806e_get_transport(panel);
	struct device *dev = &ctx->spi->dev;
	int ret;

	ret = ili9806e_power_on(dev);
	if (ret)
		return ret;

	if (ctx->desc->init_sequence)
		ctx->desc->init_sequence(ctx);

	return 0;
}

static int ili9806e_spi_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct ili9806e_spi_panel *ctx = ili9806e_get_transport(panel);
	const struct ili9806e_spi_panel_desc *desc = ctx->desc;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc->display_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = desc->bus_flags;
	drm_display_info_set_bus_formats(&connector->display_info,
					 &desc->bus_format, 1);

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs ili9806e_spi_funcs = {
	.unprepare = ili9806e_spi_unprepare,
	.prepare   = ili9806e_spi_prepare,
	.get_modes = ili9806e_spi_get_modes,
};

static int ili9806e_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ili9806e_spi_panel *ctx;
	int err;

	ctx = devm_kzalloc(dev, sizeof(struct ili9806e_spi_panel), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->spi = spi;
	ctx->desc = device_get_match_data(dev);

	err = mipi_dbi_spi_init(spi, &ctx->dbi, NULL);
	if (err)
		return dev_err_probe(dev, err, "MIPI DBI init failed\n");

	return ili9806e_probe(dev, ctx, &ili9806e_spi_funcs,
			      DRM_MODE_CONNECTOR_DPI);
}

static void ili9806e_spi_remove(struct spi_device *spi)
{
	ili9806e_remove(&spi->dev);
}

static void rk050hr345_ct106a_init(struct ili9806e_spi_panel *ctx)
{
	struct mipi_dbi *dbi = &ctx->dbi;

	/* Switch to page 1 */
	mipi_dbi_command(dbi, 0xff, 0xff, 0x98, 0x06, 0x04, 0x01);
	/* Interface Settings */
	mipi_dbi_command(dbi, 0x08, 0x10);
	mipi_dbi_command(dbi, 0x21, 0x01);
	/* Panel Settings */
	mipi_dbi_command(dbi, 0x30, 0x01);
	mipi_dbi_command(dbi, 0x31, 0x00);
	/* Power Control */
	mipi_dbi_command(dbi, 0x40, 0x15);
	mipi_dbi_command(dbi, 0x41, 0x44);
	mipi_dbi_command(dbi, 0x42, 0x03);
	mipi_dbi_command(dbi, 0x43, 0x09);
	mipi_dbi_command(dbi, 0x44, 0x09);
	mipi_dbi_command(dbi, 0x50, 0x78);
	mipi_dbi_command(dbi, 0x51, 0x78);
	mipi_dbi_command(dbi, 0x52, 0x00);
	mipi_dbi_command(dbi, 0x53, 0x3a);
	mipi_dbi_command(dbi, 0x57, 0x50);
	/* Timing Control */
	mipi_dbi_command(dbi, 0x60, 0x07);
	mipi_dbi_command(dbi, 0x61, 0x00);
	mipi_dbi_command(dbi, 0x62, 0x08);
	mipi_dbi_command(dbi, 0x63, 0x00);
	/* Gamma Settings */
	mipi_dbi_command(dbi, 0xa0, 0x00);
	mipi_dbi_command(dbi, 0xa1, 0x03);
	mipi_dbi_command(dbi, 0xa2, 0x0b);
	mipi_dbi_command(dbi, 0xa3, 0x0f);
	mipi_dbi_command(dbi, 0xa4, 0x0b);
	mipi_dbi_command(dbi, 0xa5, 0x1b);
	mipi_dbi_command(dbi, 0xa6, 0x0a);
	mipi_dbi_command(dbi, 0xa7, 0x0a);
	mipi_dbi_command(dbi, 0xa8, 0x02);
	mipi_dbi_command(dbi, 0xa9, 0x07);
	mipi_dbi_command(dbi, 0xaa, 0x05);
	mipi_dbi_command(dbi, 0xab, 0x03);
	mipi_dbi_command(dbi, 0xac, 0x0e);
	mipi_dbi_command(dbi, 0xad, 0x32);
	mipi_dbi_command(dbi, 0xae, 0x2d);
	mipi_dbi_command(dbi, 0xaf, 0x00);
	mipi_dbi_command(dbi, 0xc0, 0x00);
	mipi_dbi_command(dbi, 0xc1, 0x03);
	mipi_dbi_command(dbi, 0xc2, 0x0e);
	mipi_dbi_command(dbi, 0xc3, 0x10);
	mipi_dbi_command(dbi, 0xc4, 0x09);
	mipi_dbi_command(dbi, 0xc5, 0x17);
	mipi_dbi_command(dbi, 0xc6, 0x09);
	mipi_dbi_command(dbi, 0xc7, 0x07);
	mipi_dbi_command(dbi, 0xc8, 0x04);
	mipi_dbi_command(dbi, 0xc9, 0x09);
	mipi_dbi_command(dbi, 0xca, 0x06);
	mipi_dbi_command(dbi, 0xcb, 0x06);
	mipi_dbi_command(dbi, 0xcc, 0x0c);
	mipi_dbi_command(dbi, 0xcd, 0x25);
	mipi_dbi_command(dbi, 0xce, 0x20);
	mipi_dbi_command(dbi, 0xcf, 0x00);

	/* Switch to page 6 */
	mipi_dbi_command(dbi, 0xff, 0xff, 0x98, 0x06, 0x04, 0x06);
	/* GIP settings */
	mipi_dbi_command(dbi, 0x00, 0x21);
	mipi_dbi_command(dbi, 0x01, 0x09);
	mipi_dbi_command(dbi, 0x02, 0x00);
	mipi_dbi_command(dbi, 0x03, 0x00);
	mipi_dbi_command(dbi, 0x04, 0x01);
	mipi_dbi_command(dbi, 0x05, 0x01);
	mipi_dbi_command(dbi, 0x06, 0x80);
	mipi_dbi_command(dbi, 0x07, 0x05);
	mipi_dbi_command(dbi, 0x08, 0x02);
	mipi_dbi_command(dbi, 0x09, 0x80);
	mipi_dbi_command(dbi, 0x0a, 0x00);
	mipi_dbi_command(dbi, 0x0b, 0x00);
	mipi_dbi_command(dbi, 0x0c, 0x0a);
	mipi_dbi_command(dbi, 0x0d, 0x0a);
	mipi_dbi_command(dbi, 0x0e, 0x00);
	mipi_dbi_command(dbi, 0x0f, 0x00);
	mipi_dbi_command(dbi, 0x10, 0xe0);
	mipi_dbi_command(dbi, 0x11, 0xe4);
	mipi_dbi_command(dbi, 0x12, 0x04);
	mipi_dbi_command(dbi, 0x13, 0x00);
	mipi_dbi_command(dbi, 0x14, 0x00);
	mipi_dbi_command(dbi, 0x15, 0xc0);
	mipi_dbi_command(dbi, 0x16, 0x08);
	mipi_dbi_command(dbi, 0x17, 0x00);
	mipi_dbi_command(dbi, 0x18, 0x00);
	mipi_dbi_command(dbi, 0x19, 0x00);
	mipi_dbi_command(dbi, 0x1a, 0x00);
	mipi_dbi_command(dbi, 0x1b, 0x00);
	mipi_dbi_command(dbi, 0x1c, 0x00);
	mipi_dbi_command(dbi, 0x1d, 0x00);
	mipi_dbi_command(dbi, 0x20, 0x01);
	mipi_dbi_command(dbi, 0x21, 0x23);
	mipi_dbi_command(dbi, 0x22, 0x45);
	mipi_dbi_command(dbi, 0x23, 0x67);
	mipi_dbi_command(dbi, 0x24, 0x01);
	mipi_dbi_command(dbi, 0x25, 0x23);
	mipi_dbi_command(dbi, 0x26, 0x45);
	mipi_dbi_command(dbi, 0x27, 0x67);
	mipi_dbi_command(dbi, 0x30, 0x01);
	mipi_dbi_command(dbi, 0x31, 0x11);
	mipi_dbi_command(dbi, 0x32, 0x00);
	mipi_dbi_command(dbi, 0x33, 0xee);
	mipi_dbi_command(dbi, 0x34, 0xff);
	mipi_dbi_command(dbi, 0x35, 0xbb);
	mipi_dbi_command(dbi, 0x36, 0xca);
	mipi_dbi_command(dbi, 0x37, 0xdd);
	mipi_dbi_command(dbi, 0x38, 0xac);
	mipi_dbi_command(dbi, 0x39, 0x76);
	mipi_dbi_command(dbi, 0x3a, 0x67);
	mipi_dbi_command(dbi, 0x3b, 0x22);
	mipi_dbi_command(dbi, 0x3c, 0x22);
	mipi_dbi_command(dbi, 0x3d, 0x22);
	mipi_dbi_command(dbi, 0x3e, 0x22);
	mipi_dbi_command(dbi, 0x3f, 0x22);
	mipi_dbi_command(dbi, 0x40, 0x22);
	mipi_dbi_command(dbi, 0x52, 0x10);
	mipi_dbi_command(dbi, 0x53, 0x10);

	/* Switch to page 7 */
	mipi_dbi_command(dbi, 0xff, 0xff, 0x98, 0x06, 0x04, 0x07);
	mipi_dbi_command(dbi, 0x17, 0x22);
	mipi_dbi_command(dbi, 0x02, 0x77);
	mipi_dbi_command(dbi, 0xe1, 0x79);
	mipi_dbi_command(dbi, 0xb3, 0x10);

	/* Switch to page 0 */
	mipi_dbi_command(dbi, 0xff, 0xff, 0x98, 0x06, 0x04, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, 0x00); // 0x36
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE); // 0x11

	msleep(120);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	msleep(120);
}

static const struct drm_display_mode rk050hr345_ct106a_mode = {
	.width_mm    = 62,
	.height_mm   = 110,
	.clock       = 27000,
	.hdisplay    = 480,
	.hsync_start = 480 + 10,
	.hsync_end   = 480 + 10 + 10,
	.htotal      = 480 + 10 + 10 + 10,
	.vdisplay    = 854,
	.vsync_start = 854 + 10,
	.vsync_end   = 854 + 10 + 10,
	.vtotal      = 854 + 10 + 10 + 10,
	.flags       = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.type        = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER,
};

static const struct ili9806e_spi_panel_desc rk050hr345_ct106a_desc = {
	.init_sequence = rk050hr345_ct106a_init,
	.display_mode = &rk050hr345_ct106a_mode,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH |
		     DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct of_device_id ili9806e_spi_of_match[] = {
	{ .compatible = "rocktech,rk050hr345-ct106a", .data = &rk050hr345_ct106a_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9806e_spi_of_match);

static const struct spi_device_id ili9806e_spi_ids[] = {
	{ "rk050hr345-ct106a", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, ili9806e_spi_ids);

static struct spi_driver ili9806e_spi_driver = {
	.driver = {
		.name = "ili9806e-spi",
		.of_match_table = ili9806e_spi_of_match,
	},
	.probe = ili9806e_spi_probe,
	.remove = ili9806e_spi_remove,
	.id_table = ili9806e_spi_ids,
};
module_spi_driver(ili9806e_spi_driver);

MODULE_AUTHOR("Dario Binacchi <dario.binacchi@amarulasolutions.com>");
MODULE_DESCRIPTION("Ilitek ILI9806E LCD SPI Driver");
MODULE_LICENSE("GPL");
