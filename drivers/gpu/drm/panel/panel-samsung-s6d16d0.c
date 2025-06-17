// SPDX-License-Identifier: GPL-2.0+
/*
 * MIPI-DSI Samsung s6d16d0 panel driver. This is a 864x480
 * AMOLED panel with a command-only DSI interface.
 */

#include <drm/drm_modes.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

struct s6d16d0 {
	struct device *dev;
	struct drm_panel panel;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

/*
 * The timings are not very helpful as the display is used in
 * command mode.
 */
static const struct drm_display_mode samsung_s6d16d0_mode = {
	/* HS clock, (htotal*vtotal*vrefresh)/1000 */
	.clock = 420160,
	.hdisplay = 864,
	.hsync_start = 864 + 154,
	.hsync_end = 864 + 154 + 16,
	.htotal = 864 + 154 + 16 + 32,
	.vdisplay = 480,
	.vsync_start = 480 + 1,
	.vsync_end = 480 + 1 + 1,
	.vtotal = 480 + 1 + 1 + 1,
	.width_mm = 84,
	.height_mm = 48,
};

static inline struct s6d16d0 *panel_to_s6d16d0(struct drm_panel *panel)
{
	return container_of(panel, struct s6d16d0, panel);
}

static int s6d16d0_unprepare(struct drm_panel *panel)
{
	struct s6d16d0 *s6 = panel_to_s6d16d0(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(s6->dev);
	int ret;

	/* Enter sleep mode */
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(s6->dev, "failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	/* Assert RESET */
	gpiod_set_value_cansleep(s6->reset_gpio, 1);
	regulator_disable(s6->supply);

	return 0;
}

static int s6d16d0_prepare(struct drm_panel *panel)
{
	struct s6d16d0 *s6 = panel_to_s6d16d0(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(s6->dev);
	int ret;

	ret = regulator_enable(s6->supply);
	if (ret) {
		dev_err(s6->dev, "failed to enable supply (%d)\n", ret);
		return ret;
	}

	/* Assert RESET */
	gpiod_set_value_cansleep(s6->reset_gpio, 1);
	udelay(10);
	/* De-assert RESET */
	gpiod_set_value_cansleep(s6->reset_gpio, 0);
	msleep(120);

	/* Enabe tearing mode: send TE (tearing effect) at VBLANK */
	ret = mipi_dsi_dcs_set_tear_on(dsi,
				       MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret) {
		dev_err(s6->dev, "failed to enable vblank TE (%d)\n", ret);
		return ret;
	}
	/* Exit sleep mode and power on */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(s6->dev, "failed to exit sleep mode (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int s6d16d0_enable(struct drm_panel *panel)
{
	struct s6d16d0 *s6 = panel_to_s6d16d0(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(s6->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(s6->dev, "failed to turn display on (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int s6d16d0_disable(struct drm_panel *panel)
{
	struct s6d16d0 *s6 = panel_to_s6d16d0(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(s6->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		dev_err(s6->dev, "failed to turn display off (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int s6d16d0_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &samsung_s6d16d0_mode);
	if (!mode) {
		dev_err(panel->dev, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs s6d16d0_drm_funcs = {
	.disable = s6d16d0_disable,
	.unprepare = s6d16d0_unprepare,
	.prepare = s6d16d0_prepare,
	.enable = s6d16d0_enable,
	.get_modes = s6d16d0_get_modes,
};

static int s6d16d0_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6d16d0 *s6;
	int ret;

	s6 = devm_drm_panel_alloc(dev, struct s6d16d0, panel,
				  &s6d16d0_drm_funcs,
				  DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(s6))
		return PTR_ERR(s6);

	mipi_dsi_set_drvdata(dsi, s6);
	s6->dev = dev;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->hs_rate = 420160000;
	dsi->lp_rate = 19200000;
	/*
	 * This display uses command mode so no MIPI_DSI_MODE_VIDEO
	 * or MIPI_DSI_MODE_VIDEO_SYNC_PULSE
	 *
	 * As we only send commands we do not need to be continuously
	 * clocked.
	 */
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	s6->supply = devm_regulator_get(dev, "vdd1");
	if (IS_ERR(s6->supply))
		return PTR_ERR(s6->supply);

	/* This asserts RESET by default */
	s6->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(s6->reset_gpio)) {
		ret = PTR_ERR(s6->reset_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to request GPIO (%d)\n", ret);
		return ret;
	}

	drm_panel_add(&s6->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&s6->panel);

	return ret;
}

static void s6d16d0_remove(struct mipi_dsi_device *dsi)
{
	struct s6d16d0 *s6 = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&s6->panel);
}

static const struct of_device_id s6d16d0_of_match[] = {
	{ .compatible = "samsung,s6d16d0" },
	{ }
};
MODULE_DEVICE_TABLE(of, s6d16d0_of_match);

static struct mipi_dsi_driver s6d16d0_driver = {
	.probe = s6d16d0_probe,
	.remove = s6d16d0_remove,
	.driver = {
		.name = "panel-samsung-s6d16d0",
		.of_match_table = s6d16d0_of_match,
	},
};
module_mipi_dsi_driver(s6d16d0_driver);

MODULE_AUTHOR("Linus Wallei <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("MIPI-DSI s6d16d0 Panel Driver");
MODULE_LICENSE("GPL v2");
