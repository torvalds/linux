// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <drm/drmP.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>

#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

static const char * const regulator_names[] = {
	"vdda",
	"vdispp",
	"vdispn",
};

static unsigned long const regulator_enable_loads[] = {
	62000,
	100000,
	100000,
};

static unsigned long const regulator_disable_loads[] = {
	80,
	100,
	100,
};

struct cmd_set {
	u8 commands[4];
	u8 size;
};

struct nt35597_config {
	u32 width_mm;
	u32 height_mm;
	const char *panel_name;
	const struct cmd_set *panel_on_cmds;
	u32 num_on_cmds;
	const struct drm_display_mode *dm;
};

struct truly_nt35597 {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];

	struct gpio_desc *reset_gpio;
	struct gpio_desc *mode_gpio;

	struct backlight_device *backlight;

	struct mipi_dsi_device *dsi[2];

	const struct nt35597_config *config;
	bool prepared;
	bool enabled;
};

static inline struct truly_nt35597 *panel_to_ctx(struct drm_panel *panel)
{
	return container_of(panel, struct truly_nt35597, panel);
}

static const struct cmd_set qcom_2k_panel_magic_cmds[] = {
	/* CMD2_P0 */
	{ { 0xff, 0x20 }, 2 },
	{ { 0xfb, 0x01 }, 2 },
	{ { 0x00, 0x01 }, 2 },
	{ { 0x01, 0x55 }, 2 },
	{ { 0x02, 0x45 }, 2 },
	{ { 0x05, 0x40 }, 2 },
	{ { 0x06, 0x19 }, 2 },
	{ { 0x07, 0x1e }, 2 },
	{ { 0x0b, 0x73 }, 2 },
	{ { 0x0c, 0x73 }, 2 },
	{ { 0x0e, 0xb0 }, 2 },
	{ { 0x0f, 0xae }, 2 },
	{ { 0x11, 0xb8 }, 2 },
	{ { 0x13, 0x00 }, 2 },
	{ { 0x58, 0x80 }, 2 },
	{ { 0x59, 0x01 }, 2 },
	{ { 0x5a, 0x00 }, 2 },
	{ { 0x5b, 0x01 }, 2 },
	{ { 0x5c, 0x80 }, 2 },
	{ { 0x5d, 0x81 }, 2 },
	{ { 0x5e, 0x00 }, 2 },
	{ { 0x5f, 0x01 }, 2 },
	{ { 0x72, 0x11 }, 2 },
	{ { 0x68, 0x03 }, 2 },
	/* CMD2_P4 */
	{ { 0xFF, 0x24 }, 2 },
	{ { 0xFB, 0x01 }, 2 },
	{ { 0x00, 0x1C }, 2 },
	{ { 0x01, 0x0B }, 2 },
	{ { 0x02, 0x0C }, 2 },
	{ { 0x03, 0x01 }, 2 },
	{ { 0x04, 0x0F }, 2 },
	{ { 0x05, 0x10 }, 2 },
	{ { 0x06, 0x10 }, 2 },
	{ { 0x07, 0x10 }, 2 },
	{ { 0x08, 0x89 }, 2 },
	{ { 0x09, 0x8A }, 2 },
	{ { 0x0A, 0x13 }, 2 },
	{ { 0x0B, 0x13 }, 2 },
	{ { 0x0C, 0x15 }, 2 },
	{ { 0x0D, 0x15 }, 2 },
	{ { 0x0E, 0x17 }, 2 },
	{ { 0x0F, 0x17 }, 2 },
	{ { 0x10, 0x1C }, 2 },
	{ { 0x11, 0x0B }, 2 },
	{ { 0x12, 0x0C }, 2 },
	{ { 0x13, 0x01 }, 2 },
	{ { 0x14, 0x0F }, 2 },
	{ { 0x15, 0x10 }, 2 },
	{ { 0x16, 0x10 }, 2 },
	{ { 0x17, 0x10 }, 2 },
	{ { 0x18, 0x89 }, 2 },
	{ { 0x19, 0x8A }, 2 },
	{ { 0x1A, 0x13 }, 2 },
	{ { 0x1B, 0x13 }, 2 },
	{ { 0x1C, 0x15 }, 2 },
	{ { 0x1D, 0x15 }, 2 },
	{ { 0x1E, 0x17 }, 2 },
	{ { 0x1F, 0x17 }, 2 },
	/* STV */
	{ { 0x20, 0x40 }, 2 },
	{ { 0x21, 0x01 }, 2 },
	{ { 0x22, 0x00 }, 2 },
	{ { 0x23, 0x40 }, 2 },
	{ { 0x24, 0x40 }, 2 },
	{ { 0x25, 0x6D }, 2 },
	{ { 0x26, 0x40 }, 2 },
	{ { 0x27, 0x40 }, 2 },
	/* Vend */
	{ { 0xE0, 0x00 }, 2 },
	{ { 0xDC, 0x21 }, 2 },
	{ { 0xDD, 0x22 }, 2 },
	{ { 0xDE, 0x07 }, 2 },
	{ { 0xDF, 0x07 }, 2 },
	{ { 0xE3, 0x6D }, 2 },
	{ { 0xE1, 0x07 }, 2 },
	{ { 0xE2, 0x07 }, 2 },
	/* UD */
	{ { 0x29, 0xD8 }, 2 },
	{ { 0x2A, 0x2A }, 2 },
	/* CLK */
	{ { 0x4B, 0x03 }, 2 },
	{ { 0x4C, 0x11 }, 2 },
	{ { 0x4D, 0x10 }, 2 },
	{ { 0x4E, 0x01 }, 2 },
	{ { 0x4F, 0x01 }, 2 },
	{ { 0x50, 0x10 }, 2 },
	{ { 0x51, 0x00 }, 2 },
	{ { 0x52, 0x80 }, 2 },
	{ { 0x53, 0x00 }, 2 },
	{ { 0x56, 0x00 }, 2 },
	{ { 0x54, 0x07 }, 2 },
	{ { 0x58, 0x07 }, 2 },
	{ { 0x55, 0x25 }, 2 },
	/* Reset XDONB */
	{ { 0x5B, 0x43 }, 2 },
	{ { 0x5C, 0x00 }, 2 },
	{ { 0x5F, 0x73 }, 2 },
	{ { 0x60, 0x73 }, 2 },
	{ { 0x63, 0x22 }, 2 },
	{ { 0x64, 0x00 }, 2 },
	{ { 0x67, 0x08 }, 2 },
	{ { 0x68, 0x04 }, 2 },
	/* Resolution:1440x2560 */
	{ { 0x72, 0x02 }, 2 },
	/* mux */
	{ { 0x7A, 0x80 }, 2 },
	{ { 0x7B, 0x91 }, 2 },
	{ { 0x7C, 0xD8 }, 2 },
	{ { 0x7D, 0x60 }, 2 },
	{ { 0x7F, 0x15 }, 2 },
	{ { 0x75, 0x15 }, 2 },
	/* ABOFF */
	{ { 0xB3, 0xC0 }, 2 },
	{ { 0xB4, 0x00 }, 2 },
	{ { 0xB5, 0x00 }, 2 },
	/* Source EQ */
	{ { 0x78, 0x00 }, 2 },
	{ { 0x79, 0x00 }, 2 },
	{ { 0x80, 0x00 }, 2 },
	{ { 0x83, 0x00 }, 2 },
	/* FP BP */
	{ { 0x93, 0x0A }, 2 },
	{ { 0x94, 0x0A }, 2 },
	/* Inversion Type */
	{ { 0x8A, 0x00 }, 2 },
	{ { 0x9B, 0xFF }, 2 },
	/* IMGSWAP =1 @PortSwap=1 */
	{ { 0x9D, 0xB0 }, 2 },
	{ { 0x9F, 0x63 }, 2 },
	{ { 0x98, 0x10 }, 2 },
	/* FRM */
	{ { 0xEC, 0x00 }, 2 },
	/* CMD1 */
	{ { 0xFF, 0x10 }, 2 },
	/* VBP+VSA=,VFP = 10H */
	{ { 0x3B, 0x03, 0x0A, 0x0A }, 4 },
	/* FTE on */
	{ { 0x35, 0x00 }, 2 },
	/* EN_BK =1(auto black) */
	{ { 0xE5, 0x01 }, 2 },
	/* CMD mode(10) VDO mode(03) */
	{ { 0xBB, 0x03 }, 2 },
	/* Non Reload MTP */
	{ { 0xFB, 0x01 }, 2 },
};

static int truly_dcs_write(struct drm_panel *panel, u32 command)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ret = mipi_dsi_dcs_write(ctx->dsi[i], command, NULL, 0);
		if (ret < 0) {
			DRM_DEV_ERROR(ctx->dev,
				"cmd 0x%x failed for dsi = %d\n",
				command, i);
		}
	}

	return ret;
}

static int truly_dcs_write_buf(struct drm_panel *panel,
	u32 size, const u8 *buf)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ret = mipi_dsi_dcs_write_buffer(ctx->dsi[i], buf, size);
		if (ret < 0) {
			DRM_DEV_ERROR(ctx->dev,
				"failed to tx cmd [%d], err: %d\n", i, ret);
			return ret;
		}
	}

	return ret;
}

static int truly_35597_power_on(struct truly_nt35597 *ctx)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
					regulator_enable_loads[i]);
		if (ret)
			return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	/*
	 * Reset sequence of truly panel requires the panel to be
	 * out of reset for 10ms, followed by being held in reset
	 * for 10ms and then out again
	 */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 0);

	return 0;
}

static int truly_nt35597_power_off(struct truly_nt35597 *ctx)
{
	int ret = 0;
	int i;

	gpiod_set_value(ctx->reset_gpio, 1);

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
				regulator_disable_loads[i]);
		if (ret) {
			DRM_DEV_ERROR(ctx->dev,
				"regulator_set_load failed %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret) {
		DRM_DEV_ERROR(ctx->dev,
			"regulator_bulk_disable failed %d\n", ret);
	}
	return ret;
}

static int truly_nt35597_disable(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret;

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ret = backlight_disable(ctx->backlight);
		if (ret < 0)
			DRM_DEV_ERROR(ctx->dev, "backlight disable failed %d\n",
				ret);
	}

	ctx->enabled = false;
	return 0;
}

static int truly_nt35597_unprepare(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret = 0;

	if (!ctx->prepared)
		return 0;

	ctx->dsi[0]->mode_flags = 0;
	ctx->dsi[1]->mode_flags = 0;

	ret = truly_dcs_write(panel, MIPI_DCS_SET_DISPLAY_OFF);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"set_display_off cmd failed ret = %d\n",
			ret);
	}

	/* 120ms delay required here as per DCS spec */
	msleep(120);

	ret = truly_dcs_write(panel, MIPI_DCS_ENTER_SLEEP_MODE);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"enter_sleep cmd failed ret = %d\n", ret);
	}

	ret = truly_nt35597_power_off(ctx);
	if (ret < 0)
		DRM_DEV_ERROR(ctx->dev, "power_off failed ret = %d\n", ret);

	ctx->prepared = false;
	return ret;
}

static int truly_nt35597_prepare(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret;
	int i;
	const struct cmd_set *panel_on_cmds;
	const struct nt35597_config *config;
	u32 num_cmds;

	if (ctx->prepared)
		return 0;

	ret = truly_35597_power_on(ctx);
	if (ret < 0)
		return ret;

	ctx->dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	ctx->dsi[1]->mode_flags |= MIPI_DSI_MODE_LPM;

	config = ctx->config;
	panel_on_cmds = config->panel_on_cmds;
	num_cmds = config->num_on_cmds;

	for (i = 0; i < num_cmds; i++) {
		ret = truly_dcs_write_buf(panel,
				panel_on_cmds[i].size,
					panel_on_cmds[i].commands);
		if (ret < 0) {
			DRM_DEV_ERROR(ctx->dev,
				"cmd set tx failed i = %d ret = %d\n",
					i, ret);
			goto power_off;
		}
	}

	ret = truly_dcs_write(panel, MIPI_DCS_EXIT_SLEEP_MODE);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"exit_sleep_mode cmd failed ret = %d\n",
			ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending exit sleep DCS command */
	msleep(120);

	ret = truly_dcs_write(panel, MIPI_DCS_SET_DISPLAY_ON);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			"set_display_on cmd failed ret = %d\n", ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending set_display_on DCS command */
	msleep(120);

	ctx->prepared = true;

	return 0;

power_off:
	if (truly_nt35597_power_off(ctx))
		DRM_DEV_ERROR(ctx->dev, "power_off failed\n");
	return ret;
}

static int truly_nt35597_enable(struct drm_panel *panel)
{
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	int ret;

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ret = backlight_enable(ctx->backlight);
		if (ret < 0)
			DRM_DEV_ERROR(ctx->dev, "backlight enable failed %d\n",
						  ret);
	}

	ctx->enabled = true;

	return 0;
}

static int truly_nt35597_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct truly_nt35597 *ctx = panel_to_ctx(panel);
	struct drm_display_mode *mode;
	const struct nt35597_config *config;

	config = ctx->config;
	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_DEV_ERROR(ctx->dev,
			"failed to create a new display mode\n");
		return 0;
	}

	connector->display_info.width_mm = config->width_mm;
	connector->display_info.height_mm = config->height_mm;
	drm_mode_copy(mode, config->dm);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs truly_nt35597_drm_funcs = {
	.disable = truly_nt35597_disable,
	.unprepare = truly_nt35597_unprepare,
	.prepare = truly_nt35597_prepare,
	.enable = truly_nt35597_enable,
	.get_modes = truly_nt35597_get_modes,
};

static int truly_nt35597_panel_add(struct truly_nt35597 *ctx)
{
	struct device *dev = ctx->dev;
	int ret, i;
	const struct nt35597_config *config;

	config = ctx->config;
	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get reset gpio %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->mode_gpio = devm_gpiod_get(dev, "mode", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->mode_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get mode gpio %ld\n",
			PTR_ERR(ctx->mode_gpio));
		return PTR_ERR(ctx->mode_gpio);
	}

	/* dual port */
	gpiod_set_value(ctx->mode_gpio, 0);

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &truly_nt35597_drm_funcs;
	drm_panel_add(&ctx->panel);

	return 0;
}

static const struct drm_display_mode qcom_sdm845_mtp_2k_mode = {
	.name = "1440x2560",
	.clock = 268316,
	.hdisplay = 1440,
	.hsync_start = 1440 + 200,
	.hsync_end = 1440 + 200 + 32,
	.htotal = 1440 + 200 + 32 + 64,
	.vdisplay = 2560,
	.vsync_start = 2560 + 8,
	.vsync_end = 2560 + 8 + 1,
	.vtotal = 2560 + 8 + 1 + 7,
	.vrefresh = 60,
	.flags = 0,
};

static const struct nt35597_config nt35597_dir = {
	.width_mm = 74,
	.height_mm = 131,
	.panel_name = "qcom_sdm845_mtp_2k_panel",
	.dm = &qcom_sdm845_mtp_2k_mode,
	.panel_on_cmds = qcom_2k_panel_magic_cmds,
	.num_on_cmds = ARRAY_SIZE(qcom_2k_panel_magic_cmds),
};

static int truly_nt35597_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct truly_nt35597 *ctx;
	struct mipi_dsi_device *dsi1_device;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct mipi_dsi_device *dsi_dev;
	int ret = 0;
	int i;

	const struct mipi_dsi_device_info info = {
		.type = "trulynt35597",
		.channel = 0,
		.node = NULL,
	};

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;

	/*
	 * This device represents itself as one with two input ports which are
	 * fed by the output ports of the two DSI controllers . The DSI0 is
	 * the master controller and has most of the panel related info in its
	 * child node.
	 */

	ctx->config = of_device_get_match_data(dev);

	if (!ctx->config) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (!dsi1) {
		DRM_DEV_ERROR(dev,
			"failed to get remote node for dsi1_device\n");
		return -ENODEV;
	}

	dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
	of_node_put(dsi1);
	if (!dsi1_host) {
		DRM_DEV_ERROR(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	/* register the second DSI device */
	dsi1_device = mipi_dsi_device_register_full(dsi1_host, &info);
	if (IS_ERR(dsi1_device)) {
		DRM_DEV_ERROR(dev, "failed to create dsi device\n");
		return PTR_ERR(dsi1_device);
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->dsi[0] = dsi;
	ctx->dsi[1] = dsi1_device;

	ret = truly_nt35597_panel_add(ctx);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to add panel\n");
		goto err_panel_add;
	}

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		dsi_dev = ctx->dsi[i];
		dsi_dev->lanes = 4;
		dsi_dev->format = MIPI_DSI_FMT_RGB888;
		dsi_dev->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM |
			MIPI_DSI_CLOCK_NON_CONTINUOUS;
		ret = mipi_dsi_attach(dsi_dev);
		if (ret < 0) {
			DRM_DEV_ERROR(dev,
				"dsi attach failed i = %d\n", i);
			goto err_dsi_attach;
		}
	}

	return 0;

err_dsi_attach:
	drm_panel_remove(&ctx->panel);
err_panel_add:
	mipi_dsi_device_unregister(dsi1_device);
	return ret;
}

static int truly_nt35597_remove(struct mipi_dsi_device *dsi)
{
	struct truly_nt35597 *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->dsi[0])
		mipi_dsi_detach(ctx->dsi[0]);
	if (ctx->dsi[1]) {
		mipi_dsi_detach(ctx->dsi[1]);
		mipi_dsi_device_unregister(ctx->dsi[1]);
	}

	drm_panel_remove(&ctx->panel);
	return 0;
}

static const struct of_device_id truly_nt35597_of_match[] = {
	{
		.compatible = "truly,nt35597-2K-display",
		.data = &nt35597_dir,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, truly_nt35597_of_match);

static struct mipi_dsi_driver truly_nt35597_driver = {
	.driver = {
		.name = "panel-truly-nt35597",
		.of_match_table = truly_nt35597_of_match,
	},
	.probe = truly_nt35597_probe,
	.remove = truly_nt35597_remove,
};
module_mipi_dsi_driver(truly_nt35597_driver);

MODULE_DESCRIPTION("Truly NT35597 DSI Panel Driver");
MODULE_LICENSE("GPL v2");
