// SPDX-License-Identifier: GPL-2.0
/*
 * Panel driver for the Samsung S6D27A1 480x800 DPI RGB panel.
 * Found in the Samsung Galaxy Ace 2 GT-I8160 mobile phone.
 */

#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>

#define S6D27A1_PASSWD_L2	0xF0	/* Password Command for Level 2 Control */
#define S6D27A1_RESCTL		0xB3	/* Resolution Select Control */
#define S6D27A1_PANELCTL2	0xB4	/* ASG Signal Control */
#define S6D27A1_READID1		0xDA	/* Read panel ID 1 */
#define S6D27A1_READID2		0xDB	/* Read panel ID 2 */
#define S6D27A1_READID3		0xDC	/* Read panel ID 3 */
#define S6D27A1_DISPCTL		0xF2	/* Display Control */
#define S6D27A1_MANPWR		0xF3	/* Manual Control */
#define S6D27A1_PWRCTL1		0xF4	/* Power Control */
#define S6D27A1_SRCCTL		0xF6	/* Source Control */
#define S6D27A1_PANELCTL	0xF7	/* Panel Control*/

static const u8 s6d27a1_dbi_read_commands[] = {
	S6D27A1_READID1,
	S6D27A1_READID2,
	S6D27A1_READID3,
	0, /* sentinel */
};

struct s6d27a1 {
	struct device *dev;
	struct mipi_dbi dbi;
	struct drm_panel panel;
	struct gpio_desc *reset;
	struct regulator_bulk_data regulators[2];
};

static const struct drm_display_mode s6d27a1_480_800_mode = {
	/*
	 * The vendor driver states that the S6D27A1 panel
	 * has a pixel clock frequency of 49920000 Hz / 2 = 24960000 Hz.
	 */
	.clock = 24960,
	.hdisplay = 480,
	.hsync_start = 480 + 63,
	.hsync_end = 480 + 63 + 2,
	.htotal = 480 + 63 + 2 + 63,
	.vdisplay = 800,
	.vsync_start = 800 + 11,
	.vsync_end = 800 + 11 + 2,
	.vtotal = 800 + 11 + 2 + 10,
	.width_mm = 50,
	.height_mm = 84,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static inline struct s6d27a1 *to_s6d27a1(struct drm_panel *panel)
{
	return container_of(panel, struct s6d27a1, panel);
}

static void s6d27a1_read_mtp_id(struct s6d27a1 *ctx)
{
	struct mipi_dbi *dbi = &ctx->dbi;
	u8 id1, id2, id3;
	int ret;

	ret = mipi_dbi_command_read(dbi, S6D27A1_READID1, &id1);
	if (ret) {
		dev_err(ctx->dev, "unable to read MTP ID 1\n");
		return;
	}
	ret = mipi_dbi_command_read(dbi, S6D27A1_READID2, &id2);
	if (ret) {
		dev_err(ctx->dev, "unable to read MTP ID 2\n");
		return;
	}
	ret = mipi_dbi_command_read(dbi, S6D27A1_READID3, &id3);
	if (ret) {
		dev_err(ctx->dev, "unable to read MTP ID 3\n");
		return;
	}
	dev_info(ctx->dev, "MTP ID: %02x %02x %02x\n", id1, id2, id3);
}

static int s6d27a1_power_on(struct s6d27a1 *ctx)
{
	struct mipi_dbi *dbi = &ctx->dbi;
	int ret;

	/* Power up */
	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->regulators),
				    ctx->regulators);
	if (ret) {
		dev_err(ctx->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	msleep(20);

	/* Assert reset >=1 ms */
	gpiod_set_value_cansleep(ctx->reset, 1);
	usleep_range(1000, 5000);
	/* De-assert reset */
	gpiod_set_value_cansleep(ctx->reset, 0);
	/* Wait >= 10 ms */
	msleep(20);

	/*
	 * Exit sleep mode and initialize display - some hammering is
	 * necessary.
	 */
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(120);

	/* Magic to unlock level 2 control of the display */
	mipi_dbi_command(dbi, S6D27A1_PASSWD_L2, 0x5A, 0x5A);

	/* Configure resolution to 480RGBx800 */
	mipi_dbi_command(dbi, S6D27A1_RESCTL, 0x22);

	mipi_dbi_command(dbi, S6D27A1_PANELCTL2, 0x00, 0x02, 0x03, 0x04, 0x05, 0x08, 0x00, 0x0c);

	mipi_dbi_command(dbi, S6D27A1_MANPWR, 0x01, 0x00, 0x00, 0x08, 0x08, 0x02, 0x00);

	mipi_dbi_command(dbi, S6D27A1_DISPCTL, 0x19, 0x00, 0x08, 0x0D, 0x03, 0x41, 0x3F);

	mipi_dbi_command(dbi, S6D27A1_PWRCTL1, 0x00, 0x00, 0x00, 0x00, 0x55,
					0x44, 0x05, 0x88, 0x4B, 0x50);

	mipi_dbi_command(dbi, S6D27A1_SRCCTL, 0x03, 0x09, 0x8A, 0x00, 0x01, 0x16);

	mipi_dbi_command(dbi, S6D27A1_PANELCTL, 0x00, 0x05, 0x06, 0x07, 0x08,
					0x01, 0x09, 0x0D, 0x0A, 0x0E,
					0x0B, 0x0F, 0x0C, 0x10, 0x01,
					0x11, 0x12, 0x13, 0x14, 0x05,
					0x06, 0x07, 0x08, 0x01, 0x09,
					0x0D, 0x0A, 0x0E, 0x0B, 0x0F,
					0x0C, 0x10, 0x01, 0x11, 0x12,
					0x13, 0x14);

	/* lock the level 2 control */
	mipi_dbi_command(dbi, S6D27A1_PASSWD_L2, 0xA5, 0xA5);

	s6d27a1_read_mtp_id(ctx);

	return 0;
}

static int s6d27a1_power_off(struct s6d27a1 *ctx)
{
	/* Go into RESET and disable regulators */
	gpiod_set_value_cansleep(ctx->reset, 1);
	return regulator_bulk_disable(ARRAY_SIZE(ctx->regulators),
				      ctx->regulators);
}

static int s6d27a1_unprepare(struct drm_panel *panel)
{
	struct s6d27a1 *ctx = to_s6d27a1(panel);
	struct mipi_dbi *dbi = &ctx->dbi;

	mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);
	return s6d27a1_power_off(to_s6d27a1(panel));
}

static int s6d27a1_disable(struct drm_panel *panel)
{
	struct s6d27a1 *ctx = to_s6d27a1(panel);
	struct mipi_dbi *dbi = &ctx->dbi;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(25);

	return 0;
}

static int s6d27a1_prepare(struct drm_panel *panel)
{
	return s6d27a1_power_on(to_s6d27a1(panel));
}

static int s6d27a1_enable(struct drm_panel *panel)
{
	struct s6d27a1 *ctx = to_s6d27a1(panel);
	struct mipi_dbi *dbi = &ctx->dbi;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int s6d27a1_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct s6d27a1 *ctx = to_s6d27a1(panel);
	struct drm_display_mode *mode;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	mode = drm_mode_duplicate(connector->dev, &s6d27a1_480_800_mode);
	if (!mode) {
		dev_err(ctx->dev, "failed to add mode\n");
		return -ENOMEM;
	}

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags =
		DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;
	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6d27a1_drm_funcs = {
	.disable = s6d27a1_disable,
	.unprepare = s6d27a1_unprepare,
	.prepare = s6d27a1_prepare,
	.enable = s6d27a1_enable,
	.get_modes = s6d27a1_get_modes,
};

static int s6d27a1_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct s6d27a1 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	/*
	 * VCI   is the analog voltage supply
	 * VCCIO is the digital I/O voltage supply
	 */
	ctx->regulators[0].supply = "vci";
	ctx->regulators[1].supply = "vccio";
	ret = devm_regulator_bulk_get(dev,
				      ARRAY_SIZE(ctx->regulators),
				      ctx->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset)) {
		ret = PTR_ERR(ctx->reset);
		return dev_err_probe(dev, ret, "no RESET GPIO\n");
	}

	ret = mipi_dbi_spi_init(spi, &ctx->dbi, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "MIPI DBI init failed\n");

	ctx->dbi.read_commands = s6d27a1_dbi_read_commands;

	drm_panel_init(&ctx->panel, dev, &s6d27a1_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add backlight\n");

	spi_set_drvdata(spi, ctx);

	drm_panel_add(&ctx->panel);

	return 0;
}

static void s6d27a1_remove(struct spi_device *spi)
{
	struct s6d27a1 *ctx = spi_get_drvdata(spi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6d27a1_match[] = {
	{ .compatible = "samsung,s6d27a1", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, s6d27a1_match);

static struct spi_driver s6d27a1_driver = {
	.probe		= s6d27a1_probe,
	.remove		= s6d27a1_remove,
	.driver		= {
		.name	= "s6d27a1-panel",
		.of_match_table = s6d27a1_match,
	},
};
module_spi_driver(s6d27a1_driver);

MODULE_AUTHOR("Markuss Broks <markuss.broks@gmail.com>");
MODULE_DESCRIPTION("Samsung S6D27A1 panel driver");
MODULE_LICENSE("GPL v2");
