// SPDX-License-Identifier: GPL-2.0-only
/*
 * BOE BF060Y8M-AJ0 5.99" MIPI-DSI OLED Panel on SW43404 DriverIC
 *
 * Copyright (c) 2020 AngeloGioacchino Del Regno
 *                    <angelogioacchino.delregno@somainline.org>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DCS_ALLOW_HBM_RANGE		0x0c
#define DCS_DISALLOW_HBM_RANGE		0x08

enum boe_bf060y8m_aj0_supplies {
	BF060Y8M_VREG_VCC,
	BF060Y8M_VREG_VDDIO,
	BF060Y8M_VREG_VCI,
	BF060Y8M_VREG_EL_VDD,
	BF060Y8M_VREG_EL_VSS,
	BF060Y8M_VREG_MAX
};

struct boe_bf060y8m_aj0 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data vregs[BF060Y8M_VREG_MAX];
	struct gpio_desc *reset_gpio;
};

static inline
struct boe_bf060y8m_aj0 *to_boe_bf060y8m_aj0(struct drm_panel *panel)
{
	return container_of(panel, struct boe_bf060y8m_aj0, panel);
}

static void boe_bf060y8m_aj0_reset(struct boe_bf060y8m_aj0 *boe)
{
	gpiod_set_value_cansleep(boe->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(boe->reset_gpio, 1);
	usleep_range(15000, 16000);
	gpiod_set_value_cansleep(boe->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static int boe_bf060y8m_aj0_on(struct boe_bf060y8m_aj0 *boe)
{
	struct mipi_dsi_device *dsi = boe->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x00, 0x4c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_3D_CONTROL, 0x10);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, DCS_ALLOW_HBM_RANGE);
	mipi_dsi_dcs_write_seq(dsi, 0xf8,
			       0x00, 0x08, 0x10, 0x00, 0x22, 0x00, 0x00, 0x2d);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(30);

	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc0,
			       0x08, 0x48, 0x65, 0x33, 0x33, 0x33,
			       0x2a, 0x31, 0x39, 0x20, 0x09);
	mipi_dsi_dcs_write_seq(dsi, 0xc1, 0x00, 0x00, 0x00, 0x1f, 0x1f,
			       0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
			       0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0xe2, 0x20, 0x04, 0x10, 0x12, 0x92,
			       0x4f, 0x8f, 0x44, 0x84, 0x83, 0x83, 0x83,
			       0x5c, 0x5c, 0x5c);
	mipi_dsi_dcs_write_seq(dsi, 0xde, 0x01, 0x2c, 0x00, 0x77, 0x3e);

	msleep(30);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(50);

	return 0;
}

static int boe_bf060y8m_aj0_off(struct boe_bf060y8m_aj0 *boe)
{
	struct mipi_dsi_device *dsi = boe->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	/* OFF commands sent in HS mode */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 2000);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int boe_bf060y8m_aj0_prepare(struct drm_panel *panel)
{
	struct boe_bf060y8m_aj0 *boe = to_boe_bf060y8m_aj0(panel);
	struct device *dev = &boe->dsi->dev;
	int ret;

	/*
	 * Enable EL Driving Voltage first - doing that at the beginning
	 * or at the end of the power sequence doesn't matter, so enable
	 * it here to avoid yet another usleep at the end.
	 */
	ret = regulator_enable(boe->vregs[BF060Y8M_VREG_EL_VDD].consumer);
	if (ret)
		return ret;
	ret = regulator_enable(boe->vregs[BF060Y8M_VREG_EL_VSS].consumer);
	if (ret)
		goto err_elvss;

	ret = regulator_enable(boe->vregs[BF060Y8M_VREG_VCC].consumer);
	if (ret)
		goto err_vcc;
	usleep_range(1000, 2000);
	ret = regulator_enable(boe->vregs[BF060Y8M_VREG_VDDIO].consumer);
	if (ret)
		goto err_vddio;
	usleep_range(500, 1000);
	ret = regulator_enable(boe->vregs[BF060Y8M_VREG_VCI].consumer);
	if (ret)
		goto err_vci;
	usleep_range(2000, 3000);

	boe_bf060y8m_aj0_reset(boe);

	ret = boe_bf060y8m_aj0_on(boe);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(boe->reset_gpio, 1);
		return ret;
	}

	return 0;

err_vci:
	regulator_disable(boe->vregs[BF060Y8M_VREG_VDDIO].consumer);
err_vddio:
	regulator_disable(boe->vregs[BF060Y8M_VREG_VCC].consumer);
err_vcc:
	regulator_disable(boe->vregs[BF060Y8M_VREG_EL_VSS].consumer);
err_elvss:
	regulator_disable(boe->vregs[BF060Y8M_VREG_EL_VDD].consumer);
	return ret;
}

static int boe_bf060y8m_aj0_unprepare(struct drm_panel *panel)
{
	struct boe_bf060y8m_aj0 *boe = to_boe_bf060y8m_aj0(panel);
	struct device *dev = &boe->dsi->dev;
	int ret;

	ret = boe_bf060y8m_aj0_off(boe);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(boe->reset_gpio, 1);
	ret = regulator_bulk_disable(ARRAY_SIZE(boe->vregs), boe->vregs);

	return 0;
}

static const struct drm_display_mode boe_bf060y8m_aj0_mode = {
	.clock = 165268,
	.hdisplay = 1080,
	.hsync_start = 1080 + 36,
	.hsync_end = 1080 + 36 + 24,
	.htotal = 1080 + 36 + 24 + 96,
	.vdisplay = 2160,
	.vsync_start = 2160 + 16,
	.vsync_end = 2160 + 16 + 1,
	.vtotal = 2160 + 16 + 1 + 15,
	.width_mm = 68,   /* 68.04 mm */
	.height_mm = 136, /* 136.08 mm */
};

static int boe_bf060y8m_aj0_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &boe_bf060y8m_aj0_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs boe_bf060y8m_aj0_panel_funcs = {
	.prepare = boe_bf060y8m_aj0_prepare,
	.unprepare = boe_bf060y8m_aj0_unprepare,
	.get_modes = boe_bf060y8m_aj0_get_modes,
};

static int boe_bf060y8m_aj0_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static int boe_bf060y8m_aj0_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	return brightness & 0xff;
}

static const struct backlight_ops boe_bf060y8m_aj0_bl_ops = {
	.update_status = boe_bf060y8m_aj0_bl_update_status,
	.get_brightness = boe_bf060y8m_aj0_bl_get_brightness,
};

static struct backlight_device *
boe_bf060y8m_aj0_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 127,
		.max_brightness = 255,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &boe_bf060y8m_aj0_bl_ops, &props);
}

static int boe_bf060y8m_aj0_init_vregs(struct boe_bf060y8m_aj0 *boe,
				       struct device *dev)
{
	struct regulator *vreg;
	int ret;

	boe->vregs[BF060Y8M_VREG_VCC].supply = "vcc";
	boe->vregs[BF060Y8M_VREG_VDDIO].supply = "vddio";
	boe->vregs[BF060Y8M_VREG_VCI].supply = "vci";
	boe->vregs[BF060Y8M_VREG_EL_VDD].supply = "elvdd";
	boe->vregs[BF060Y8M_VREG_EL_VSS].supply = "elvss";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(boe->vregs),
				      boe->vregs);
	if (ret < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	vreg = boe->vregs[BF060Y8M_VREG_VCC].consumer;
	ret = regulator_is_supported_voltage(vreg, 2700000, 3600000);
	if (!ret)
		return ret;

	vreg = boe->vregs[BF060Y8M_VREG_VDDIO].consumer;
	ret = regulator_is_supported_voltage(vreg, 1620000, 1980000);
	if (!ret)
		return ret;

	vreg = boe->vregs[BF060Y8M_VREG_VCI].consumer;
	ret = regulator_is_supported_voltage(vreg, 2600000, 3600000);
	if (!ret)
		return ret;

	vreg = boe->vregs[BF060Y8M_VREG_EL_VDD].consumer;
	ret = regulator_is_supported_voltage(vreg, 4400000, 4800000);
	if (!ret)
		return ret;

	/* ELVSS is negative: -5.00V to -1.40V */
	vreg = boe->vregs[BF060Y8M_VREG_EL_VSS].consumer;
	ret = regulator_is_supported_voltage(vreg, 1400000, 5000000);
	if (!ret)
		return ret;

	/*
	 * Set min/max rated current, known only for VCI and VDDIO and,
	 * in case of failure, just go on gracefully, as this step is not
	 * guaranteed to succeed on all regulator HW but do a debug print
	 * to inform the developer during debugging.
	 * In any case, these two supplies are also optional, so they may
	 * be fixed-regulator which, at the time of writing, does not
	 * support fake current limiting.
	 */
	vreg = boe->vregs[BF060Y8M_VREG_VDDIO].consumer;
	ret = regulator_set_current_limit(vreg, 1500, 2500);
	if (ret)
		dev_dbg(dev, "Current limit cannot be set on %s: %d\n",
			boe->vregs[1].supply, ret);

	vreg = boe->vregs[BF060Y8M_VREG_VCI].consumer;
	ret = regulator_set_current_limit(vreg, 20000, 40000);
	if (ret)
		dev_dbg(dev, "Current limit cannot be set on %s: %d\n",
			boe->vregs[2].supply, ret);

	return 0;
}

static int boe_bf060y8m_aj0_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_bf060y8m_aj0 *boe;
	int ret;

	boe = devm_kzalloc(dev, sizeof(*boe), GFP_KERNEL);
	if (!boe)
		return -ENOMEM;

	ret = boe_bf060y8m_aj0_init_vregs(boe, dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to initialize supplies.\n");

	boe->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(boe->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(boe->reset_gpio),
				     "Failed to get reset-gpios\n");

	boe->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, boe);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_LPM;

	drm_panel_init(&boe->panel, dev, &boe_bf060y8m_aj0_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	boe->panel.prepare_prev_first = true;

	boe->panel.backlight = boe_bf060y8m_aj0_create_backlight(dsi);
	if (IS_ERR(boe->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(boe->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&boe->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static void boe_bf060y8m_aj0_remove(struct mipi_dsi_device *dsi)
{
	struct boe_bf060y8m_aj0 *boe = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&boe->panel);
}

static const struct of_device_id boe_bf060y8m_aj0_of_match[] = {
	{ .compatible = "boe,bf060y8m-aj0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_bf060y8m_aj0_of_match);

static struct mipi_dsi_driver boe_bf060y8m_aj0_driver = {
	.probe = boe_bf060y8m_aj0_probe,
	.remove = boe_bf060y8m_aj0_remove,
	.driver = {
		.name = "panel-sw43404-boe-fhd-amoled",
		.of_match_table = boe_bf060y8m_aj0_of_match,
	},
};
module_mipi_dsi_driver(boe_bf060y8m_aj0_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@somainline.org>");
MODULE_DESCRIPTION("BOE BF060Y8M-AJ0 MIPI-DSI OLED panel");
MODULE_LICENSE("GPL v2");
