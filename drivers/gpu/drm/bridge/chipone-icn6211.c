// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Amarula Solutions(India)
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#define HACTIVE_LI		0x20
#define VACTIVE_LI		0x21
#define VACTIVE_HACTIVE_HI	0x22
#define HFP_LI			0x23
#define HSYNC_LI		0x24
#define HBP_LI			0x25
#define HFP_HSW_HBP_HI		0x26
#define VFP			0x27
#define VSYNC			0x28
#define VBP			0x29

struct chipone {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	struct gpio_desc *enable_gpio;
	struct regulator *vdd1;
	struct regulator *vdd2;
	struct regulator *vdd3;
};

static inline struct chipone *bridge_to_chipone(struct drm_bridge *bridge)
{
	return container_of(bridge, struct chipone, bridge);
}

static struct drm_display_mode *bridge_to_mode(struct drm_bridge *bridge)
{
	return &bridge->encoder->crtc->state->adjusted_mode;
}

static inline int chipone_dsi_write(struct chipone *icn,  const void *seq,
				    size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(icn->dev);

	return mipi_dsi_generic_write(dsi, seq, len);
}

#define ICN6211_DSI(icn, seq...)				\
	{							\
		const u8 d[] = { seq };				\
		chipone_dsi_write(icn, d, ARRAY_SIZE(d));	\
	}

static void chipone_enable(struct drm_bridge *bridge)
{
	struct chipone *icn = bridge_to_chipone(bridge);
	struct drm_display_mode *mode = bridge_to_mode(bridge);

	ICN6211_DSI(icn, 0x7a, 0xc1);

	ICN6211_DSI(icn, HACTIVE_LI, mode->hdisplay & 0xff);

	ICN6211_DSI(icn, VACTIVE_LI, mode->vdisplay & 0xff);

	/**
	 * lsb nibble: 2nd nibble of hdisplay
	 * msb nibble: 2nd nibble of vdisplay
	 */
	ICN6211_DSI(icn, VACTIVE_HACTIVE_HI,
		    ((mode->hdisplay >> 8) & 0xf) |
		    (((mode->vdisplay >> 8) & 0xf) << 4));

	ICN6211_DSI(icn, HFP_LI, mode->hsync_start - mode->hdisplay);

	ICN6211_DSI(icn, HSYNC_LI, mode->hsync_end - mode->hsync_start);

	ICN6211_DSI(icn, HBP_LI, mode->htotal - mode->hsync_end);

	ICN6211_DSI(icn, HFP_HSW_HBP_HI, 0x00);

	ICN6211_DSI(icn, VFP, mode->vsync_start - mode->vdisplay);

	ICN6211_DSI(icn, VSYNC, mode->vsync_end - mode->vsync_start);

	ICN6211_DSI(icn, VBP, mode->vtotal - mode->vsync_end);

	/* dsi specific sequence */
	ICN6211_DSI(icn, MIPI_DCS_SET_TEAR_OFF, 0x80);
	ICN6211_DSI(icn, MIPI_DCS_SET_ADDRESS_MODE, 0x28);
	ICN6211_DSI(icn, 0xb5, 0xa0);
	ICN6211_DSI(icn, 0x5c, 0xff);
	ICN6211_DSI(icn, MIPI_DCS_SET_COLUMN_ADDRESS, 0x01);
	ICN6211_DSI(icn, MIPI_DCS_GET_POWER_SAVE, 0x92);
	ICN6211_DSI(icn, 0x6b, 0x71);
	ICN6211_DSI(icn, 0x69, 0x2b);
	ICN6211_DSI(icn, MIPI_DCS_ENTER_SLEEP_MODE, 0x40);
	ICN6211_DSI(icn, MIPI_DCS_EXIT_SLEEP_MODE, 0x98);

	/* icn6211 specific sequence */
	ICN6211_DSI(icn, 0xb6, 0x20);
	ICN6211_DSI(icn, 0x51, 0x20);
	ICN6211_DSI(icn, 0x09, 0x10);

	usleep_range(10000, 11000);
}

static void chipone_pre_enable(struct drm_bridge *bridge)
{
	struct chipone *icn = bridge_to_chipone(bridge);
	int ret;

	if (icn->vdd1) {
		ret = regulator_enable(icn->vdd1);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD1 regulator: %d\n", ret);
	}

	if (icn->vdd2) {
		ret = regulator_enable(icn->vdd2);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD2 regulator: %d\n", ret);
	}

	if (icn->vdd3) {
		ret = regulator_enable(icn->vdd3);
		if (ret)
			DRM_DEV_ERROR(icn->dev,
				      "failed to enable VDD3 regulator: %d\n", ret);
	}

	gpiod_set_value(icn->enable_gpio, 1);

	usleep_range(10000, 11000);
}

static void chipone_post_disable(struct drm_bridge *bridge)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	if (icn->vdd1)
		regulator_disable(icn->vdd1);

	if (icn->vdd2)
		regulator_disable(icn->vdd2);

	if (icn->vdd3)
		regulator_disable(icn->vdd3);

	gpiod_set_value(icn->enable_gpio, 0);
}

static int chipone_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	struct chipone *icn = bridge_to_chipone(bridge);

	return drm_bridge_attach(bridge->encoder, icn->panel_bridge, bridge, flags);
}

static const struct drm_bridge_funcs chipone_bridge_funcs = {
	.attach = chipone_attach,
	.post_disable = chipone_post_disable,
	.pre_enable = chipone_pre_enable,
	.enable = chipone_enable,
};

static int chipone_parse_dt(struct chipone *icn)
{
	struct device *dev = icn->dev;
	struct drm_panel *panel;
	int ret;

	icn->vdd1 = devm_regulator_get_optional(dev, "vdd1");
	if (IS_ERR(icn->vdd1)) {
		ret = PTR_ERR(icn->vdd1);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd1 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD1 regulator: %d\n", ret);
	}

	icn->vdd2 = devm_regulator_get_optional(dev, "vdd2");
	if (IS_ERR(icn->vdd2)) {
		ret = PTR_ERR(icn->vdd2);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd2 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD2 regulator: %d\n", ret);
	}

	icn->vdd3 = devm_regulator_get_optional(dev, "vdd3");
	if (IS_ERR(icn->vdd3)) {
		ret = PTR_ERR(icn->vdd3);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		icn->vdd3 = NULL;
		DRM_DEV_DEBUG(dev, "failed to get VDD3 regulator: %d\n", ret);
	}

	icn->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(icn->enable_gpio)) {
		DRM_DEV_ERROR(dev, "failed to get enable GPIO\n");
		return PTR_ERR(icn->enable_gpio);
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, NULL);
	if (ret)
		return ret;

	icn->panel_bridge = devm_drm_panel_bridge_add(dev, panel);
	if (IS_ERR(icn->panel_bridge))
		return PTR_ERR(icn->panel_bridge);

	return 0;
}

static int chipone_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct chipone *icn;
	int ret;

	icn = devm_kzalloc(dev, sizeof(struct chipone), GFP_KERNEL);
	if (!icn)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, icn);
	icn->dev = dev;

	ret = chipone_parse_dt(icn);
	if (ret)
		return ret;

	icn->bridge.funcs = &chipone_bridge_funcs;
	icn->bridge.type = DRM_MODE_CONNECTOR_DPI;
	icn->bridge.of_node = dev->of_node;

	drm_bridge_add(&icn->bridge);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_bridge_remove(&icn->bridge);
		dev_err(dev, "failed to attach dsi\n");
	}

	return ret;
}

static int chipone_remove(struct mipi_dsi_device *dsi)
{
	struct chipone *icn = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&icn->bridge);

	return 0;
}

static const struct of_device_id chipone_of_match[] = {
	{ .compatible = "chipone,icn6211", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, chipone_of_match);

static struct mipi_dsi_driver chipone_driver = {
	.probe = chipone_probe,
	.remove = chipone_remove,
	.driver = {
		.name = "chipone-icn6211",
		.owner = THIS_MODULE,
		.of_match_table = chipone_of_match,
	},
};
module_mipi_dsi_driver(chipone_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Chipone ICN6211 MIPI-DSI to RGB Converter Bridge");
MODULE_LICENSE("GPL");
