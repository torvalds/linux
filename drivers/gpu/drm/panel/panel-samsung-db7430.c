// SPDX-License-Identifier: GPL-2.0
/*
 * Panel driver for the Samsung LMS397KF04 480x800 DPI RGB panel.
 * According to the data sheet the display controller is called DB7430.
 * Found in the Samsung Galaxy Beam GT-I8350 mobile phone.
 * Linus Walleij <linus.walleij@linaro.org>
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

#define DB7430_ACCESS_PROT_OFF		0xb0
#define DB7430_UNKNOWN_B4		0xb4
#define DB7430_USER_SELECT		0xb5
#define DB7430_UNKNOWN_B7		0xb7
#define DB7430_UNKNOWN_B8		0xb8
#define DB7430_PANEL_DRIVING		0xc0
#define DB7430_SOURCE_CONTROL		0xc1
#define DB7430_GATE_INTERFACE		0xc4
#define DB7430_DISPLAY_H_TIMING		0xc5
#define DB7430_RGB_SYNC_OPTION		0xc6
#define DB7430_GAMMA_SET_RED		0xc8
#define DB7430_GAMMA_SET_GREEN		0xc9
#define DB7430_GAMMA_SET_BLUE		0xca
#define DB7430_BIAS_CURRENT_CTRL	0xd1
#define DB7430_DDV_CTRL			0xd2
#define DB7430_GAMMA_CTRL_REF		0xd3
#define DB7430_UNKNOWN_D4		0xd4
#define DB7430_DCDC_CTRL		0xd5
#define DB7430_VCL_CTRL			0xd6
#define DB7430_UNKNOWN_F8		0xf8
#define DB7430_UNKNOWN_FC		0xfc

#define DATA_MASK	0x100

/**
 * struct db7430 - state container for a panel controlled by the DB7430
 * controller
 */
struct db7430 {
	/** @dev: the container device */
	struct device *dev;
	/** @dbi: the DBI bus abstraction handle */
	struct mipi_dbi dbi;
	/** @panel: the DRM panel instance for this device */
	struct drm_panel panel;
	/** @width: the width of this panel in mm */
	u32 width;
	/** @height: the height of this panel in mm */
	u32 height;
	/** @reset: reset GPIO line */
	struct gpio_desc *reset;
	/** @regulators: VCCIO and VIO supply regulators */
	struct regulator_bulk_data regulators[2];
};

static const struct drm_display_mode db7430_480_800_mode = {
	/*
	 * 31 ns period min (htotal*vtotal*vrefresh)/1000
	 * gives a Vrefresh of ~71 Hz.
	 */
	.clock = 32258,
	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 4,
	.htotal = 480 + 10 + 4 + 40,
	.vdisplay = 800,
	.vsync_start = 800 + 6,
	.vsync_end = 800 + 6 + 1,
	.vtotal = 800 + 6 + 1 + 7,
	.width_mm = 53,
	.height_mm = 87,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static inline struct db7430 *to_db7430(struct drm_panel *panel)
{
	return container_of(panel, struct db7430, panel);
}

static int db7430_power_on(struct db7430 *db)
{
	struct mipi_dbi *dbi = &db->dbi;
	int ret;

	/* Power up */
	ret = regulator_bulk_enable(ARRAY_SIZE(db->regulators),
				    db->regulators);
	if (ret) {
		dev_err(db->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}
	msleep(50);

	/* Assert reset >=1 ms */
	gpiod_set_value_cansleep(db->reset, 1);
	usleep_range(1000, 5000);
	/* De-assert reset */
	gpiod_set_value_cansleep(db->reset, 0);
	/* Wait >= 10 ms */
	msleep(10);
	dev_dbg(db->dev, "de-asserted RESET\n");

	/*
	 * This is set to 0x0a (RGB/BGR order + horizontal flip) in order
	 * to make the display behave normally. If this is not set the displays
	 * normal output behaviour is horizontally flipped and BGR ordered. Do
	 * it twice because the first message doesn't always "take".
	 */
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, 0x0a);
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, 0x0a);
	mipi_dbi_command(dbi, DB7430_ACCESS_PROT_OFF, 0x00);
	mipi_dbi_command(dbi, DB7430_PANEL_DRIVING, 0x28, 0x08);
	mipi_dbi_command(dbi, DB7430_SOURCE_CONTROL,
			 0x01, 0x30, 0x15, 0x05, 0x22);
	mipi_dbi_command(dbi, DB7430_GATE_INTERFACE,
			 0x10, 0x01, 0x00);
	mipi_dbi_command(dbi, DB7430_DISPLAY_H_TIMING,
			 0x06, 0x55, 0x03, 0x07, 0x0b,
			 0x33, 0x00, 0x01, 0x03);
	/*
	 * 0x00 in datasheet 0x01 in vendor code 0x00, it seems 0x01 means
	 * DE active high and 0x00 means DE active low.
	 */
	mipi_dbi_command(dbi, DB7430_RGB_SYNC_OPTION, 0x01);
	mipi_dbi_command(dbi, DB7430_GAMMA_SET_RED,
		/* R positive gamma */ 0x00,
		0x0A, 0x31, 0x3B, 0x4E, 0x58, 0x59, 0x5B, 0x58, 0x5E, 0x62,
		0x60, 0x61, 0x5E, 0x62, 0x55, 0x55, 0x7F, 0x08,
		/* R negative gamma */ 0x00,
		0x0A, 0x31, 0x3B, 0x4E, 0x58, 0x59, 0x5B, 0x58, 0x5E, 0x62,
		0x60, 0x61, 0x5E, 0x62, 0x55, 0x55, 0x7F, 0x08);
	mipi_dbi_command(dbi, DB7430_GAMMA_SET_GREEN,
		/* G positive gamma */ 0x00,
		0x25, 0x15, 0x28, 0x3D, 0x4A, 0x48, 0x4C, 0x4A, 0x52, 0x59,
		0x59, 0x5B, 0x56, 0x60, 0x5D, 0x55, 0x7F, 0x0A,
		/* G negative gamma */ 0x00,
		0x25, 0x15, 0x28, 0x3D, 0x4A, 0x48, 0x4C, 0x4A, 0x52, 0x59,
		0x59, 0x5B, 0x56, 0x60, 0x5D, 0x55, 0x7F, 0x0A);
	mipi_dbi_command(dbi, DB7430_GAMMA_SET_BLUE,
		/* B positive gamma */ 0x00,
		0x48, 0x10, 0x1F, 0x2F, 0x35, 0x38, 0x3D, 0x3C, 0x45, 0x4D,
		0x4E, 0x52, 0x51, 0x60, 0x7F, 0x7E, 0x7F, 0x0C,
		/* B negative gamma */ 0x00,
		0x48, 0x10, 0x1F, 0x2F, 0x35, 0x38, 0x3D, 0x3C, 0x45, 0x4D,
		0x4E, 0x52, 0x51, 0x60, 0x7F, 0x7E, 0x7F, 0x0C);
	mipi_dbi_command(dbi, DB7430_BIAS_CURRENT_CTRL, 0x33, 0x13);
	mipi_dbi_command(dbi, DB7430_DDV_CTRL, 0x11, 0x00, 0x00);
	mipi_dbi_command(dbi, DB7430_GAMMA_CTRL_REF, 0x50, 0x50);
	mipi_dbi_command(dbi, DB7430_DCDC_CTRL, 0x2f, 0x11, 0x1e, 0x46);
	mipi_dbi_command(dbi, DB7430_VCL_CTRL, 0x11, 0x0a);

	return 0;
}

static int db7430_power_off(struct db7430 *db)
{
	/* Go into RESET and disable regulators */
	gpiod_set_value_cansleep(db->reset, 1);
	return regulator_bulk_disable(ARRAY_SIZE(db->regulators),
				      db->regulators);
}

static int db7430_unprepare(struct drm_panel *panel)
{
	return db7430_power_off(to_db7430(panel));
}

static int db7430_disable(struct drm_panel *panel)
{
	struct db7430 *db = to_db7430(panel);
	struct mipi_dbi *dbi = &db->dbi;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(25);
	mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);

	return 0;
}

static int db7430_prepare(struct drm_panel *panel)
{
	return db7430_power_on(to_db7430(panel));
}

static int db7430_enable(struct drm_panel *panel)
{
	struct db7430 *db = to_db7430(panel);
	struct mipi_dbi *dbi = &db->dbi;

	/* Exit sleep mode */
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(20);

	/* NVM (non-volatile memory) load sequence */
	mipi_dbi_command(dbi, DB7430_UNKNOWN_D4, 0x52, 0x5e);
	mipi_dbi_command(dbi, DB7430_UNKNOWN_F8, 0x01, 0xf5, 0xf2, 0x71, 0x44);
	mipi_dbi_command(dbi, DB7430_UNKNOWN_FC, 0x00, 0x08);
	msleep(150);

	/* CABC turn on sequence (BC = backlight control) */
	mipi_dbi_command(dbi, DB7430_UNKNOWN_B4, 0x0f, 0x00, 0x50);
	mipi_dbi_command(dbi, DB7430_USER_SELECT, 0x80);
	mipi_dbi_command(dbi, DB7430_UNKNOWN_B7, 0x24);
	mipi_dbi_command(dbi, DB7430_UNKNOWN_B8, 0x01);

	/* Turn on display */
	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

/**
 * db7430_get_modes() - return the mode
 * @panel: the panel to get the mode for
 * @connector: reference to the central DRM connector control structure
 */
static int db7430_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct db7430 *db = to_db7430(panel);
	struct drm_display_mode *mode;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	mode = drm_mode_duplicate(connector->dev, &db7430_480_800_mode);
	if (!mode) {
		dev_err(db->dev, "failed to add mode\n");
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

static const struct drm_panel_funcs db7430_drm_funcs = {
	.disable = db7430_disable,
	.unprepare = db7430_unprepare,
	.prepare = db7430_prepare,
	.enable = db7430_enable,
	.get_modes = db7430_get_modes,
};

static int db7430_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct db7430 *db;
	int ret;

	db = devm_kzalloc(dev, sizeof(*db), GFP_KERNEL);
	if (!db)
		return -ENOMEM;
	db->dev = dev;

	/*
	 * VCI   is the analog voltage supply
	 * VCCIO is the digital I/O voltage supply
	 */
	db->regulators[0].supply = "vci";
	db->regulators[1].supply = "vccio";
	ret = devm_regulator_bulk_get(dev,
				      ARRAY_SIZE(db->regulators),
				      db->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	db->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(db->reset)) {
		ret = PTR_ERR(db->reset);
		return dev_err_probe(dev, ret, "no RESET GPIO\n");
	}

	ret = mipi_dbi_spi_init(spi, &db->dbi, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "MIPI DBI init failed\n");

	drm_panel_init(&db->panel, dev, &db7430_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	/* FIXME: if no external backlight, use internal backlight */
	ret = drm_panel_of_backlight(&db->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add backlight\n");

	spi_set_drvdata(spi, db);

	drm_panel_add(&db->panel);
	dev_dbg(dev, "added panel\n");

	return 0;
}

static void db7430_remove(struct spi_device *spi)
{
	struct db7430 *db = spi_get_drvdata(spi);

	drm_panel_remove(&db->panel);
}

/*
 * The DB7430 display controller may be used in several display products,
 * so list the different variants here and add per-variant data if needed.
 */
static const struct of_device_id db7430_match[] = {
	{ .compatible = "samsung,lms397kf04", },
	{},
};
MODULE_DEVICE_TABLE(of, db7430_match);

static const struct spi_device_id db7430_ids[] = {
	{ "lms397kf04" },
	{ },
};
MODULE_DEVICE_TABLE(spi, db7430_ids);

static struct spi_driver db7430_driver = {
	.probe		= db7430_probe,
	.remove		= db7430_remove,
	.id_table	= db7430_ids,
	.driver		= {
		.name	= "db7430-panel",
		.of_match_table = db7430_match,
	},
};
module_spi_driver(db7430_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Samsung DB7430 panel driver");
MODULE_LICENSE("GPL v2");
