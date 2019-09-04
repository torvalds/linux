// SPDX-License-Identifier: GPL-2.0
/*
 * Toppoly TD028TTEC1 Panel Driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific panel-tpo-td028ttec1 driver
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Neo 1973 code (jbt6k74.c):
 * Copyright (C) 2006-2007 OpenMoko, Inc.
 * Author: Harald Welte <laforge@openmoko.org>
 *
 * Ported and adapted from Neo 1973 U-Boot by:
 * H. Nikolaus Schaller <hns@goldelico.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define JBT_COMMAND			0x000
#define JBT_DATA			0x100

#define JBT_REG_SLEEP_IN		0x10
#define JBT_REG_SLEEP_OUT		0x11

#define JBT_REG_DISPLAY_OFF		0x28
#define JBT_REG_DISPLAY_ON		0x29

#define JBT_REG_RGB_FORMAT		0x3a
#define JBT_REG_QUAD_RATE		0x3b

#define JBT_REG_POWER_ON_OFF		0xb0
#define JBT_REG_BOOSTER_OP		0xb1
#define JBT_REG_BOOSTER_MODE		0xb2
#define JBT_REG_BOOSTER_FREQ		0xb3
#define JBT_REG_OPAMP_SYSCLK		0xb4
#define JBT_REG_VSC_VOLTAGE		0xb5
#define JBT_REG_VCOM_VOLTAGE		0xb6
#define JBT_REG_EXT_DISPL		0xb7
#define JBT_REG_OUTPUT_CONTROL		0xb8
#define JBT_REG_DCCLK_DCEV		0xb9
#define JBT_REG_DISPLAY_MODE1		0xba
#define JBT_REG_DISPLAY_MODE2		0xbb
#define JBT_REG_DISPLAY_MODE		0xbc
#define JBT_REG_ASW_SLEW		0xbd
#define JBT_REG_DUMMY_DISPLAY		0xbe
#define JBT_REG_DRIVE_SYSTEM		0xbf

#define JBT_REG_SLEEP_OUT_FR_A		0xc0
#define JBT_REG_SLEEP_OUT_FR_B		0xc1
#define JBT_REG_SLEEP_OUT_FR_C		0xc2
#define JBT_REG_SLEEP_IN_LCCNT_D	0xc3
#define JBT_REG_SLEEP_IN_LCCNT_E	0xc4
#define JBT_REG_SLEEP_IN_LCCNT_F	0xc5
#define JBT_REG_SLEEP_IN_LCCNT_G	0xc6

#define JBT_REG_GAMMA1_FINE_1		0xc7
#define JBT_REG_GAMMA1_FINE_2		0xc8
#define JBT_REG_GAMMA1_INCLINATION	0xc9
#define JBT_REG_GAMMA1_BLUE_OFFSET	0xca

#define JBT_REG_BLANK_CONTROL		0xcf
#define JBT_REG_BLANK_TH_TV		0xd0
#define JBT_REG_CKV_ON_OFF		0xd1
#define JBT_REG_CKV_1_2			0xd2
#define JBT_REG_OEV_TIMING		0xd3
#define JBT_REG_ASW_TIMING_1		0xd4
#define JBT_REG_ASW_TIMING_2		0xd5

#define JBT_REG_HCLOCK_VGA		0xec
#define JBT_REG_HCLOCK_QVGA		0xed

struct td028ttec1_panel {
	struct drm_panel panel;

	struct spi_device *spi;
	struct backlight_device *backlight;
};

#define to_td028ttec1_device(p) container_of(p, struct td028ttec1_panel, panel)

static int jbt_ret_write_0(struct td028ttec1_panel *lcd, u8 reg, int *err)
{
	struct spi_device *spi = lcd->spi;
	u16 tx_buf = JBT_COMMAND | reg;
	int ret;

	if (err && *err)
		return *err;

	ret = spi_write(spi, (u8 *)&tx_buf, sizeof(tx_buf));
	if (ret < 0) {
		dev_err(&spi->dev, "%s: SPI write failed: %d\n", __func__, ret);
		if (err)
			*err = ret;
	}

	return ret;
}

static int jbt_reg_write_1(struct td028ttec1_panel *lcd,
			   u8 reg, u8 data, int *err)
{
	struct spi_device *spi = lcd->spi;
	u16 tx_buf[2];
	int ret;

	if (err && *err)
		return *err;

	tx_buf[0] = JBT_COMMAND | reg;
	tx_buf[1] = JBT_DATA | data;

	ret = spi_write(spi, (u8 *)tx_buf, sizeof(tx_buf));
	if (ret < 0) {
		dev_err(&spi->dev, "%s: SPI write failed: %d\n", __func__, ret);
		if (err)
			*err = ret;
	}

	return ret;
}

static int jbt_reg_write_2(struct td028ttec1_panel *lcd,
			   u8 reg, u16 data, int *err)
{
	struct spi_device *spi = lcd->spi;
	u16 tx_buf[3];
	int ret;

	if (err && *err)
		return *err;

	tx_buf[0] = JBT_COMMAND | reg;
	tx_buf[1] = JBT_DATA | (data >> 8);
	tx_buf[2] = JBT_DATA | (data & 0xff);

	ret = spi_write(spi, (u8 *)tx_buf, sizeof(tx_buf));
	if (ret < 0) {
		dev_err(&spi->dev, "%s: SPI write failed: %d\n", __func__, ret);
		if (err)
			*err = ret;
	}

	return ret;
}

static int td028ttec1_prepare(struct drm_panel *panel)
{
	struct td028ttec1_panel *lcd = to_td028ttec1_device(panel);
	unsigned int i;
	int ret = 0;

	/* Three times command zero */
	for (i = 0; i < 3; ++i) {
		jbt_ret_write_0(lcd, 0x00, &ret);
		usleep_range(1000, 2000);
	}

	/* deep standby out */
	jbt_reg_write_1(lcd, JBT_REG_POWER_ON_OFF, 0x17, &ret);

	/* RGB I/F on, RAM write off, QVGA through, SIGCON enable */
	jbt_reg_write_1(lcd, JBT_REG_DISPLAY_MODE, 0x80, &ret);

	/* Quad mode off */
	jbt_reg_write_1(lcd, JBT_REG_QUAD_RATE, 0x00, &ret);

	/* AVDD on, XVDD on */
	jbt_reg_write_1(lcd, JBT_REG_POWER_ON_OFF, 0x16, &ret);

	/* Output control */
	jbt_reg_write_2(lcd, JBT_REG_OUTPUT_CONTROL, 0xfff9, &ret);

	/* Sleep mode off */
	jbt_ret_write_0(lcd, JBT_REG_SLEEP_OUT, &ret);

	/* at this point we have like 50% grey */

	/* initialize register set */
	jbt_reg_write_1(lcd, JBT_REG_DISPLAY_MODE1, 0x01, &ret);
	jbt_reg_write_1(lcd, JBT_REG_DISPLAY_MODE2, 0x00, &ret);
	jbt_reg_write_1(lcd, JBT_REG_RGB_FORMAT, 0x60, &ret);
	jbt_reg_write_1(lcd, JBT_REG_DRIVE_SYSTEM, 0x10, &ret);
	jbt_reg_write_1(lcd, JBT_REG_BOOSTER_OP, 0x56, &ret);
	jbt_reg_write_1(lcd, JBT_REG_BOOSTER_MODE, 0x33, &ret);
	jbt_reg_write_1(lcd, JBT_REG_BOOSTER_FREQ, 0x11, &ret);
	jbt_reg_write_1(lcd, JBT_REG_BOOSTER_FREQ, 0x11, &ret);
	jbt_reg_write_1(lcd, JBT_REG_OPAMP_SYSCLK, 0x02, &ret);
	jbt_reg_write_1(lcd, JBT_REG_VSC_VOLTAGE, 0x2b, &ret);
	jbt_reg_write_1(lcd, JBT_REG_VCOM_VOLTAGE, 0x40, &ret);
	jbt_reg_write_1(lcd, JBT_REG_EXT_DISPL, 0x03, &ret);
	jbt_reg_write_1(lcd, JBT_REG_DCCLK_DCEV, 0x04, &ret);
	/*
	 * default of 0x02 in JBT_REG_ASW_SLEW responsible for 72Hz requirement
	 * to avoid red / blue flicker
	 */
	jbt_reg_write_1(lcd, JBT_REG_ASW_SLEW, 0x04, &ret);
	jbt_reg_write_1(lcd, JBT_REG_DUMMY_DISPLAY, 0x00, &ret);

	jbt_reg_write_1(lcd, JBT_REG_SLEEP_OUT_FR_A, 0x11, &ret);
	jbt_reg_write_1(lcd, JBT_REG_SLEEP_OUT_FR_B, 0x11, &ret);
	jbt_reg_write_1(lcd, JBT_REG_SLEEP_OUT_FR_C, 0x11, &ret);
	jbt_reg_write_2(lcd, JBT_REG_SLEEP_IN_LCCNT_D, 0x2040, &ret);
	jbt_reg_write_2(lcd, JBT_REG_SLEEP_IN_LCCNT_E, 0x60c0, &ret);
	jbt_reg_write_2(lcd, JBT_REG_SLEEP_IN_LCCNT_F, 0x1020, &ret);
	jbt_reg_write_2(lcd, JBT_REG_SLEEP_IN_LCCNT_G, 0x60c0, &ret);

	jbt_reg_write_2(lcd, JBT_REG_GAMMA1_FINE_1, 0x5533, &ret);
	jbt_reg_write_1(lcd, JBT_REG_GAMMA1_FINE_2, 0x00, &ret);
	jbt_reg_write_1(lcd, JBT_REG_GAMMA1_INCLINATION, 0x00, &ret);
	jbt_reg_write_1(lcd, JBT_REG_GAMMA1_BLUE_OFFSET, 0x00, &ret);

	jbt_reg_write_2(lcd, JBT_REG_HCLOCK_VGA, 0x1f0, &ret);
	jbt_reg_write_1(lcd, JBT_REG_BLANK_CONTROL, 0x02, &ret);
	jbt_reg_write_2(lcd, JBT_REG_BLANK_TH_TV, 0x0804, &ret);

	jbt_reg_write_1(lcd, JBT_REG_CKV_ON_OFF, 0x01, &ret);
	jbt_reg_write_2(lcd, JBT_REG_CKV_1_2, 0x0000, &ret);

	jbt_reg_write_2(lcd, JBT_REG_OEV_TIMING, 0x0d0e, &ret);
	jbt_reg_write_2(lcd, JBT_REG_ASW_TIMING_1, 0x11a4, &ret);
	jbt_reg_write_1(lcd, JBT_REG_ASW_TIMING_2, 0x0e, &ret);

	return ret;
}

static int td028ttec1_enable(struct drm_panel *panel)
{
	struct td028ttec1_panel *lcd = to_td028ttec1_device(panel);
	int ret;

	ret = jbt_ret_write_0(lcd, JBT_REG_DISPLAY_ON, NULL);
	if (ret)
		return ret;

	backlight_enable(lcd->backlight);

	return 0;
}

static int td028ttec1_disable(struct drm_panel *panel)
{
	struct td028ttec1_panel *lcd = to_td028ttec1_device(panel);

	backlight_disable(lcd->backlight);

	jbt_ret_write_0(lcd, JBT_REG_DISPLAY_OFF, NULL);

	return 0;
}

static int td028ttec1_unprepare(struct drm_panel *panel)
{
	struct td028ttec1_panel *lcd = to_td028ttec1_device(panel);

	jbt_reg_write_2(lcd, JBT_REG_OUTPUT_CONTROL, 0x8002, NULL);
	jbt_ret_write_0(lcd, JBT_REG_SLEEP_IN, NULL);
	jbt_reg_write_1(lcd, JBT_REG_POWER_ON_OFF, 0x00, NULL);

	return 0;
}

static const struct drm_display_mode td028ttec1_mode = {
	.clock = 22153,
	.hdisplay = 480,
	.hsync_start = 480 + 24,
	.hsync_end = 480 + 24 + 8,
	.htotal = 480 + 24 + 8 + 8,
	.vdisplay = 640,
	.vsync_start = 640 + 4,
	.vsync_end = 640 + 4 + 2,
	.vtotal = 640 + 4 + 2 + 2,
	.vrefresh = 66,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 43,
	.height_mm = 58,
};

static int td028ttec1_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &td028ttec1_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = td028ttec1_mode.width_mm;
	connector->display_info.height_mm = td028ttec1_mode.height_mm;
	/*
	 * FIXME: According to the datasheet sync signals are sampled on the
	 * rising edge of the clock, but the code running on the OpenMoko Neo
	 * FreeRunner and Neo 1973 indicates sampling on the falling edge. This
	 * should be tested on a real device.
	 */
	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH
					  | DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE
					  | DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE;

	return 1;
}

static const struct drm_panel_funcs td028ttec1_funcs = {
	.prepare = td028ttec1_prepare,
	.enable = td028ttec1_enable,
	.disable = td028ttec1_disable,
	.unprepare = td028ttec1_unprepare,
	.get_modes = td028ttec1_get_modes,
};

static int td028ttec1_probe(struct spi_device *spi)
{
	struct td028ttec1_panel *lcd;
	int ret;

	lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	spi_set_drvdata(spi, lcd);
	lcd->spi = spi;

	lcd->backlight = devm_of_find_backlight(&spi->dev);
	if (IS_ERR(lcd->backlight))
		return PTR_ERR(lcd->backlight);

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 9;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to setup SPI: %d\n", ret);
		return ret;
	}

	drm_panel_init(&lcd->panel, &lcd->spi->dev, &td028ttec1_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	return drm_panel_add(&lcd->panel);
}

static int td028ttec1_remove(struct spi_device *spi)
{
	struct td028ttec1_panel *lcd = spi_get_drvdata(spi);

	drm_panel_remove(&lcd->panel);
	drm_panel_disable(&lcd->panel);
	drm_panel_unprepare(&lcd->panel);

	return 0;
}

static const struct of_device_id td028ttec1_of_match[] = {
	{ .compatible = "tpo,td028ttec1", },
	/* DT backward compatibility. */
	{ .compatible = "toppoly,td028ttec1", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, td028ttec1_of_match);

static const struct spi_device_id td028ttec1_ids[] = {
	{ "tpo,td028ttec1", 0},
	{ "toppoly,td028ttec1", 0 },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(spi, td028ttec1_ids);

static struct spi_driver td028ttec1_driver = {
	.probe		= td028ttec1_probe,
	.remove		= td028ttec1_remove,
	.id_table	= td028ttec1_ids,
	.driver		= {
		.name   = "panel-tpo-td028ttec1",
		.of_match_table = td028ttec1_of_match,
	},
};

module_spi_driver(td028ttec1_driver);

MODULE_AUTHOR("H. Nikolaus Schaller <hns@goldelico.com>");
MODULE_DESCRIPTION("Toppoly TD028TTEC1 panel driver");
MODULE_LICENSE("GPL");
