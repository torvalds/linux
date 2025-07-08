#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/module.h>


#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#define mipi_dsi_dcs_write_lmi_multi(ctx, cmd, ...) ({ \
	struct mipi_dsi_device *dsi__ = (ctx)-> dsi; \
        u8 __data[] = {__VA_ARGS__}; \
        int ret__ = mipi_dsi_dcs_write(dsi__, cmd, __data, sizeof(__data)); \
        if (ret__ < 0) \
                pr_err("lmi DCS write %02x failed: %d\n", cmd, ret__); \
        ret__; \
})

struct ams667uu01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct ams667uu01 *to_ams667uu01(struct drm_panel *panel)
{
	return container_of(panel, struct ams667uu01, panel);
}

static void ams667uu01_reset(struct ams667uu01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ams667uu01_on(struct ams667uu01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xb7, 0x01, 0x4b);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x095f);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xe9,
				     0x11, 0x75, 0xa5, 0x8e, 0x76, 0xa6, 0x37,
				     0xbe, 0x00, 0x32, 0x32);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xe1,
				     0x00, 0x00, 0x02, 0x02, 0x42, 0x02);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xe2,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xb0, 0x0c);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xe1, 0x19);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xb0, 0x09);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xee, 0x07);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_lmi_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 67);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ams667uu01_off(struct ams667uu01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 32);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int ams667uu01_prepare(struct drm_panel *panel)
{
	struct ams667uu01 *ctx = to_ams667uu01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ams667uu01_reset(ctx);

	ret = ams667uu01_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int ams667uu01_unprepare(struct drm_panel *panel)
{
	struct ams667uu01 *ctx = to_ams667uu01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ams667uu01_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode ams667uu01_mode = {
	.clock = (1080 + 64 + 20 + 64) * (2400 + 34 + 20 + 34) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 64,
	.hsync_end = 1080 + 64 + 20,
	.htotal = 1080 + 64 + 20 + 64,
	.vdisplay = 2400,
	.vsync_start = 2400 + 34,
	.vsync_end = 2400 + 34 + 20,
	.vtotal = 2400 + 34 + 20 + 34,
	.width_mm = 695,
	.height_mm = 1545,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ams667uu01_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ams667uu01_mode);
}

static const struct drm_panel_funcs ams667uu01_panel_funcs = {
	.prepare = ams667uu01_prepare,
	.unprepare = ams667uu01_unprepare,
	.get_modes = ams667uu01_get_modes,
};

static int ams667uu01_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int ams667uu01_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops ams667uu01_bl_ops = {
	.update_status = ams667uu01_bl_update_status,
	.get_brightness = ams667uu01_bl_get_brightness,
};

static struct backlight_device *
ams667uu01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2047,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &ams667uu01_bl_ops, &props);
}

static int ams667uu01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ams667uu01 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ams667uu01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ams667uu01_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void ams667uu01_remove(struct mipi_dsi_device *dsi)
{
	struct ams667uu01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams667uu01_of_match[] = {
	{ .compatible = "xiaomi,lmi-ams667uu01" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ams667uu01_of_match);

static struct mipi_dsi_driver ams667uu01_driver = {
	.probe = ams667uu01_probe,
	.remove = ams667uu01_remove,
	.driver = {
		.name = "panel-samsung-ams667uu01",
		.of_match_table = ams667uu01_of_match,
	},
};
module_mipi_dsi_driver(ams667uu01_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for xiaomi j11 38 08 0a fhd panel");
MODULE_LICENSE("GPL");
