// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA Corporation
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

struct sharp_panel {
	struct drm_panel base;
	/* the datasheet refers to them as DSI-LINK1 and DSI-LINK2 */
	struct mipi_dsi_device *link1;
	struct mipi_dsi_device *link2;

	struct regulator *supply;

	const struct drm_display_mode *mode;
};

static inline struct sharp_panel *to_sharp_panel(struct drm_panel *panel)
{
	return container_of(panel, struct sharp_panel, base);
}

static void sharp_wait_frames(struct sharp_panel *sharp, unsigned int frames)
{
	unsigned int refresh = drm_mode_vrefresh(sharp->mode);

	if (WARN_ON(frames > refresh))
		return;

	msleep(1000 / (refresh / frames));
}

static int sharp_panel_write(struct sharp_panel *sharp, u16 offset, u8 value)
{
	u8 payload[3] = { offset >> 8, offset & 0xff, value };
	struct mipi_dsi_device *dsi = sharp->link1;
	ssize_t err;

	err = mipi_dsi_generic_write(dsi, payload, sizeof(payload));
	if (err < 0) {
		dev_err(&dsi->dev, "failed to write %02x to %04x: %zd\n",
			value, offset, err);
		return err;
	}

	err = mipi_dsi_dcs_nop(dsi);
	if (err < 0) {
		dev_err(&dsi->dev, "failed to send DCS nop: %zd\n", err);
		return err;
	}

	usleep_range(10, 20);

	return 0;
}

static __maybe_unused int sharp_panel_read(struct sharp_panel *sharp,
					   u16 offset, u8 *value)
{
	ssize_t err;

	cpu_to_be16s(&offset);

	err = mipi_dsi_generic_read(sharp->link1, &offset, sizeof(offset),
				    value, sizeof(*value));
	if (err < 0)
		dev_err(&sharp->link1->dev, "failed to read from %04x: %zd\n",
			offset, err);

	return err;
}

static int sharp_panel_unprepare(struct drm_panel *panel)
{
	struct sharp_panel *sharp = to_sharp_panel(panel);
	int err;

	sharp_wait_frames(sharp, 4);

	err = mipi_dsi_dcs_set_display_off(sharp->link1);
	if (err < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", err);

	err = mipi_dsi_dcs_enter_sleep_mode(sharp->link1);
	if (err < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", err);

	msleep(120);

	regulator_disable(sharp->supply);

	return 0;
}

static int sharp_setup_symmetrical_split(struct mipi_dsi_device *left,
					 struct mipi_dsi_device *right,
					 const struct drm_display_mode *mode)
{
	int err;

	err = mipi_dsi_dcs_set_column_address(left, 0, mode->hdisplay / 2 - 1);
	if (err < 0) {
		dev_err(&left->dev, "failed to set column address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(left, 0, mode->vdisplay - 1);
	if (err < 0) {
		dev_err(&left->dev, "failed to set page address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_column_address(right, mode->hdisplay / 2,
					      mode->hdisplay - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set column address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(right, 0, mode->vdisplay - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set page address: %d\n", err);
		return err;
	}

	return 0;
}

static int sharp_panel_prepare(struct drm_panel *panel)
{
	struct sharp_panel *sharp = to_sharp_panel(panel);
	u8 format = MIPI_DCS_PIXEL_FMT_24BIT;
	int err;

	err = regulator_enable(sharp->supply);
	if (err < 0)
		return err;

	/*
	 * According to the datasheet, the panel needs around 10 ms to fully
	 * power up. At least another 120 ms is required before exiting sleep
	 * mode to make sure the panel is ready. Throw in another 20 ms for
	 * good measure.
	 */
	msleep(150);

	err = mipi_dsi_dcs_exit_sleep_mode(sharp->link1);
	if (err < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", err);
		goto poweroff;
	}

	/*
	 * The MIPI DCS specification mandates this delay only between the
	 * exit_sleep_mode and enter_sleep_mode commands, so it isn't strictly
	 * necessary here.
	 */
	/*
	msleep(120);
	*/

	/* set left-right mode */
	err = sharp_panel_write(sharp, 0x1000, 0x2a);
	if (err < 0) {
		dev_err(panel->dev, "failed to set left-right mode: %d\n", err);
		goto poweroff;
	}

	/* enable command mode */
	err = sharp_panel_write(sharp, 0x1001, 0x01);
	if (err < 0) {
		dev_err(panel->dev, "failed to enable command mode: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_pixel_format(sharp->link1, format);
	if (err < 0) {
		dev_err(panel->dev, "failed to set pixel format: %d\n", err);
		goto poweroff;
	}

	/*
	 * TODO: The device supports both left-right and even-odd split
	 * configurations, but this driver currently supports only the left-
	 * right split. To support a different mode a mechanism needs to be
	 * put in place to communicate the configuration back to the DSI host
	 * controller.
	 */
	err = sharp_setup_symmetrical_split(sharp->link1, sharp->link2,
					    sharp->mode);
	if (err < 0) {
		dev_err(panel->dev, "failed to set up symmetrical split: %d\n",
			err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_display_on(sharp->link1);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", err);
		goto poweroff;
	}

	/* wait for 6 frames before continuing */
	sharp_wait_frames(sharp, 6);

	return 0;

poweroff:
	regulator_disable(sharp->supply);
	return err;
}

static const struct drm_display_mode default_mode = {
	.clock = 278000,
	.hdisplay = 2560,
	.hsync_start = 2560 + 128,
	.hsync_end = 2560 + 128 + 64,
	.htotal = 2560 + 128 + 64 + 64,
	.vdisplay = 1600,
	.vsync_start = 1600 + 4,
	.vsync_end = 1600 + 4 + 8,
	.vtotal = 1600 + 4 + 8 + 32,
};

static int sharp_panel_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 217;
	connector->display_info.height_mm = 136;

	return 1;
}

static const struct drm_panel_funcs sharp_panel_funcs = {
	.unprepare = sharp_panel_unprepare,
	.prepare = sharp_panel_prepare,
	.get_modes = sharp_panel_get_modes,
};

static const struct of_device_id sharp_of_match[] = {
	{ .compatible = "sharp,lq101r1sx01", },
	{ }
};
MODULE_DEVICE_TABLE(of, sharp_of_match);

static int sharp_panel_add(struct sharp_panel *sharp)
{
	int ret;

	sharp->mode = &default_mode;

	sharp->supply = devm_regulator_get(&sharp->link1->dev, "power");
	if (IS_ERR(sharp->supply))
		return PTR_ERR(sharp->supply);

	ret = drm_panel_of_backlight(&sharp->base);
	if (ret)
		return ret;

	drm_panel_add(&sharp->base);

	return 0;
}

static void sharp_panel_del(struct sharp_panel *sharp)
{
	if (sharp->base.dev)
		drm_panel_remove(&sharp->base);

	if (sharp->link2)
		put_device(&sharp->link2->dev);
}

static int sharp_panel_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_device *secondary = NULL;
	struct sharp_panel *sharp;
	struct device_node *np;
	int err;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM;

	/* Find DSI-LINK1 */
	np = of_parse_phandle(dsi->dev.of_node, "link2", 0);
	if (np) {
		secondary = of_find_mipi_dsi_device_by_node(np);
		of_node_put(np);

		if (!secondary)
			return -EPROBE_DEFER;
	}

	/* register a panel for only the DSI-LINK1 interface */
	if (secondary) {
		sharp = devm_drm_panel_alloc(&dsi->dev, __typeof(*sharp), base,
					     &sharp_panel_funcs,
					     DRM_MODE_CONNECTOR_DSI);
		if (IS_ERR(sharp)) {
			put_device(&secondary->dev);
			return PTR_ERR(sharp);
		}

		mipi_dsi_set_drvdata(dsi, sharp);

		sharp->link2 = secondary;
		sharp->link1 = dsi;

		err = sharp_panel_add(sharp);
		if (err < 0) {
			put_device(&secondary->dev);
			return err;
		}
	}

	err = mipi_dsi_attach(dsi);
	if (err < 0) {
		if (secondary)
			sharp_panel_del(sharp);

		return err;
	}

	return 0;
}

static void sharp_panel_remove(struct mipi_dsi_device *dsi)
{
	struct sharp_panel *sharp = mipi_dsi_get_drvdata(dsi);
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	/* only detach from host for the DSI-LINK2 interface */
	if (sharp)
		sharp_panel_del(sharp);
}

static struct mipi_dsi_driver sharp_panel_driver = {
	.driver = {
		.name = "panel-sharp-lq101r1sx01",
		.of_match_table = sharp_of_match,
	},
	.probe = sharp_panel_probe,
	.remove = sharp_panel_remove,
};
module_mipi_dsi_driver(sharp_panel_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("Sharp LQ101R1SX01 panel driver");
MODULE_LICENSE("GPL v2");
