// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

struct ili9488_desc {
	const struct drm_display_mode *display_mode;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	unsigned int bpc;
	void (*init_sequence)(struct mipi_dsi_multi_context *ctx);
};

struct ili9488 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset;
	struct regulator_bulk_data supplies[2];
	const struct ili9488_desc *desc;
	enum drm_panel_orientation orientation;
};

static const char * const regulator_names[] = {
	"vci",
	"iovcc",
};

static void e35gh_i_mw800cb_init(struct mipi_dsi_multi_context *ctx)
{
	/* Gamma control 1,2 */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xE0, 0x00, 0x10, 0x14, 0x01, 0x0E, 0x04, 0x33,
				     0x56, 0x48, 0x03, 0x0C, 0x0B, 0x2B, 0x34, 0x0F);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xE1, 0x00, 0x12, 0x18, 0x05, 0x12, 0x06, 0x40,
				     0x34, 0x57, 0x06, 0x10, 0x0C, 0x3B, 0x3F, 0x0F);
	/* Power control 1,2 */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xC0, 0x0F, 0x0C);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xC1, 0x41);
	/* VCOM Control */
	mipi_dsi_dcs_write_seq_multi(ctx, 0xC5, 0x00, 0x25, 0x80);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x36, 0x48);
	/* Interface pixel format 18bpp */
	mipi_dsi_dcs_write_seq_multi(ctx, 0x3A, 0x66);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xB0, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xB1, 0xA0);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xB4, 0x02);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xB6, 0x02, 0x02, 0x3B);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xE9, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xF7, 0xA9, 0x51, 0x2C, 0x82);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x21);
}

static const struct drm_display_mode e35gh_i_mw800cb_display_mode = {
	.clock = 14400,

	.hdisplay = 320,
	.hsync_start = 320 + 60,
	.hsync_end = 320 + 60 + 20,
	.htotal = 320 + 60 + 20 + 42,

	.vdisplay = 480,
	.vsync_start = 480 + 20,
	.vsync_end = 480 + 20 + 10,
	.vtotal = 480 + 20 + 10 + 33,

	.width_mm = 48,
	.height_mm = 73,

	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static inline struct ili9488 *panel_to_ili9488(struct drm_panel *panel)
{
	return container_of(panel, struct ili9488, panel);
}

static int ili9488_power_on(struct ili9488 *ili)
{
	struct mipi_dsi_device *dsi = ili->dsi;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ili->supplies), ili->supplies);
	if (ret < 0) {
		dev_err(&dsi->dev, "regulator bulk enable failed: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(ili->reset, 0);
	usleep_range(1000, 5000);
	gpiod_set_value_cansleep(ili->reset, 1);
	usleep_range(1000, 5000);
	gpiod_set_value_cansleep(ili->reset, 0);
	usleep_range(5000, 10000);

	return 0;
}

static int ili9488_power_off(struct ili9488 *ili)
{
	struct mipi_dsi_device *dsi = ili->dsi;
	int ret;

	gpiod_set_value_cansleep(ili->reset, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(ili->supplies), ili->supplies);
	if (ret)
		dev_err(&dsi->dev, "regulator bulk disable failed: %d\n", ret);

	return ret;
}

static int ili9488_activate(struct ili9488 *ili)
{
	struct mipi_dsi_multi_context ctx = { .dsi = ili->dsi };

	if (ili->desc->init_sequence)
		ili->desc->init_sequence(&ctx);

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&ctx);

	return ctx.accum_err;
}

static int ili9488_prepare(struct drm_panel *panel)
{
	struct ili9488 *ili = panel_to_ili9488(panel);
	int ret;

	ret = ili9488_power_on(ili);
	if (ret)
		return ret;

	ret = ili9488_activate(ili);
	if (ret) {
		ili9488_power_off(ili);
		return ret;
	}

	return 0;
}

static int ili9488_deactivate(struct ili9488 *ili)
{
	struct mipi_dsi_multi_context ctx = { .dsi = ili->dsi };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	return ctx.accum_err;
}

static int ili9488_unprepare(struct drm_panel *panel)
{
	struct ili9488 *ili = panel_to_ili9488(panel);
	struct mipi_dsi_device *dsi = ili->dsi;
	int ret;

	ili9488_deactivate(ili);
	ret = ili9488_power_off(ili);
	if (ret < 0)
		dev_err(&dsi->dev, "power off failed: %d\n", ret);

	return ret;
}

static int ili9488_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct ili9488 *ili = panel_to_ili9488(panel);
	const struct drm_display_mode *mode = ili->desc->display_mode;

	connector->display_info.bpc = ili->desc->bpc;

	return drm_connector_helper_get_modes_fixed(connector, mode);
}

static enum drm_panel_orientation ili9488_get_orientation(struct drm_panel *panel)
{
	struct ili9488 *ili = panel_to_ili9488(panel);

	return ili->orientation;
}

static const struct drm_panel_funcs ili9488_funcs = {
	.prepare	= ili9488_prepare,
	.unprepare	= ili9488_unprepare,
	.get_modes	= ili9488_get_modes,
	.get_orientation = ili9488_get_orientation,
};

static int ili9488_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ili9488 *ili;
	int i, ret;

	ili = devm_drm_panel_alloc(dev, struct ili9488, panel, &ili9488_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ili))
		return PTR_ERR(ili);

	ili->desc = device_get_match_data(dev);
	ili->dsi = dsi;

	dsi->mode_flags = ili->desc->mode_flags;
	dsi->format = ili->desc->format;
	dsi->lanes = ili->desc->lanes;

	ili->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ili->reset))
		return dev_err_probe(dev, PTR_ERR(ili->reset),
				     "failed to get reset-gpios\n");

	for (i = 0; i < ARRAY_SIZE(ili->supplies); i++)
		ili->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ili->supplies),
				      ili->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ret = drm_of_get_panel_orientation(dev->of_node, &ili->orientation);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get orientation\n");

	ret = drm_panel_of_backlight(&ili->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

	ili->panel.prepare_prev_first = true;

	ret = devm_drm_panel_add(dev, &ili->panel);
	if (ret)
		return ret;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to attach to DSI host\n");

	return 0;
}

static const struct ili9488_desc e35gh_i_mw800cb_desc = {
	.init_sequence = e35gh_i_mw800cb_init,
	.display_mode = &e35gh_i_mw800cb_display_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB666_PACKED,
	.lanes = 1,
	.bpc = 6,
};

static const struct of_device_id ili9488_of_match[] = {
	{ .compatible = "focuslcds,e35gh-i-mw800cb", .data = &e35gh_i_mw800cb_desc },
	{ }
};

MODULE_DEVICE_TABLE(of, ili9488_of_match);

static struct mipi_dsi_driver ili9488_dsi_driver = {
	.probe	= ili9488_dsi_probe,
	.driver = {
		.name		= "ili9488-dsi",
		.of_match_table	= ili9488_of_match,
	},
};
module_mipi_dsi_driver(ili9488_dsi_driver);

MODULE_AUTHOR("Igor Reznichenko <igor@reznichenko.net>");
MODULE_DESCRIPTION("Ilitek ILI9488 Controller Driver");
MODULE_LICENSE("GPL");
