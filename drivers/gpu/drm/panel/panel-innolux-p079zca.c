// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct innolux_panel;

struct panel_desc {
	const struct drm_display_mode *mode;
	unsigned int bpc;
	struct {
		unsigned int width;
		unsigned int height;
	} size;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct innolux_panel *innolux);
	unsigned int lanes;
	const char * const *supply_names;
	unsigned int num_supplies;
	unsigned int sleep_mode_delay;
	unsigned int power_down_delay;
};

struct innolux_panel {
	struct drm_panel base;
	struct mipi_dsi_device *link;
	const struct panel_desc *desc;

	struct regulator_bulk_data *supplies;
	struct gpio_desc *enable_gpio;
};

static inline struct innolux_panel *to_innolux_panel(struct drm_panel *panel)
{
	return container_of(panel, struct innolux_panel, base);
}

static int innolux_panel_unprepare(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	err = mipi_dsi_dcs_set_display_off(innolux->link);
	if (err < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", err);

	err = mipi_dsi_dcs_enter_sleep_mode(innolux->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", err);
		return err;
	}

	if (innolux->desc->sleep_mode_delay)
		msleep(innolux->desc->sleep_mode_delay);

	gpiod_set_value_cansleep(innolux->enable_gpio, 0);

	if (innolux->desc->power_down_delay)
		msleep(innolux->desc->power_down_delay);

	err = regulator_bulk_disable(innolux->desc->num_supplies,
				     innolux->supplies);
	if (err < 0)
		return err;

	return 0;
}

static int innolux_panel_prepare(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	gpiod_set_value_cansleep(innolux->enable_gpio, 0);

	err = regulator_bulk_enable(innolux->desc->num_supplies,
				    innolux->supplies);
	if (err < 0)
		return err;

	/* p079zca: t2 (20ms), p097pfg: t4 (15ms) */
	usleep_range(20000, 21000);

	gpiod_set_value_cansleep(innolux->enable_gpio, 1);

	/* p079zca: t4, p097pfg: t5 */
	usleep_range(20000, 21000);

	if (innolux->desc->init) {
		err = innolux->desc->init(innolux);
		if (err < 0)
			goto poweroff;
	}

	err = mipi_dsi_dcs_exit_sleep_mode(innolux->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", err);
		goto poweroff;
	}

	/* T6: 120ms - 1000ms*/
	msleep(120);

	err = mipi_dsi_dcs_set_display_on(innolux->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", err);
		goto poweroff;
	}

	/* T7: 5ms */
	usleep_range(5000, 6000);

	return 0;

poweroff:
	gpiod_set_value_cansleep(innolux->enable_gpio, 0);
	regulator_bulk_disable(innolux->desc->num_supplies, innolux->supplies);

	return err;
}

static const char * const innolux_p079zca_supply_names[] = {
	"power",
};

static const struct drm_display_mode innolux_p079zca_mode = {
	.clock = 56900,
	.hdisplay = 768,
	.hsync_start = 768 + 40,
	.hsync_end = 768 + 40 + 40,
	.htotal = 768 + 40 + 40 + 40,
	.vdisplay = 1024,
	.vsync_start = 1024 + 20,
	.vsync_end = 1024 + 20 + 4,
	.vtotal = 1024 + 20 + 4 + 20,
};

static const struct panel_desc innolux_p079zca_panel_desc = {
	.mode = &innolux_p079zca_mode,
	.bpc = 8,
	.size = {
		.width = 120,
		.height = 160,
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		 MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.supply_names = innolux_p079zca_supply_names,
	.num_supplies = ARRAY_SIZE(innolux_p079zca_supply_names),
	.power_down_delay = 80, /* T8: 80ms - 1000ms */
};

static const char * const innolux_p097pfg_supply_names[] = {
	"avdd",
	"avee",
};

static const struct drm_display_mode innolux_p097pfg_mode = {
	.clock = 229000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 100,
	.hsync_end = 1536 + 100 + 24,
	.htotal = 1536 + 100 + 24 + 100,
	.vdisplay = 2048,
	.vsync_start = 2048 + 100,
	.vsync_end = 2048 + 100 + 2,
	.vtotal = 2048 + 100 + 2 + 18,
};

static void innolux_panel_write_multi(struct mipi_dsi_multi_context *ctx,
				      const void *payload, size_t size)
{
	mipi_dsi_generic_write_multi(ctx, payload, size);

	/*
	 * Included by random guessing, because without this
	 * (or at least, some delay), the panel sometimes
	 * didn't appear to pick up the command sequence.
	 */
	mipi_dsi_dcs_nop_multi(ctx);
}

#define innolux_panel_init_cmd_multi(ctx, seq...)                 \
	do {                                                      \
		static const u8 d[] = { seq };                    \
		innolux_panel_write_multi(ctx, d, ARRAY_SIZE(d)); \
	} while (0)

#define innolux_panel_switch_page(ctx, page) \
	innolux_panel_init_cmd_multi(ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, (page))

/*
 * Display manufacturer failed to provide init sequencing according to
 * https://chromium-review.googlesource.com/c/chromiumos/third_party/coreboot/+/892065/
 * so the init sequence stems from a register dump of a working panel.
 */
static int innolux_p097pfg_init(struct innolux_panel *innolux)
{
	struct mipi_dsi_multi_context ctx = { .dsi = innolux->link };

	innolux_panel_switch_page(&ctx, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xb1, 0xe8, 0x11);
	innolux_panel_init_cmd_multi(&ctx, 0xb2, 0x25, 0x02);
	innolux_panel_init_cmd_multi(&ctx, 0xb5, 0x08, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xbc, 0x0f, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xb8, 0x03, 0x06, 0x00, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xbd, 0x01, 0x90, 0x14, 0x14);
	innolux_panel_init_cmd_multi(&ctx, 0x6f, 0x01);
	innolux_panel_init_cmd_multi(&ctx, 0xc0, 0x03);
	innolux_panel_init_cmd_multi(&ctx, 0x6f, 0x02);
	innolux_panel_init_cmd_multi(&ctx, 0xc1, 0x0d);
	innolux_panel_init_cmd_multi(&ctx, 0xd9, 0x01, 0x09, 0x70);
	innolux_panel_init_cmd_multi(&ctx, 0xc5, 0x12, 0x21, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xbb, 0x93, 0x93);

	innolux_panel_switch_page(&ctx, 0x01);
	innolux_panel_init_cmd_multi(&ctx, 0xb3, 0x3c, 0x3c);
	innolux_panel_init_cmd_multi(&ctx, 0xb4, 0x0f, 0x0f);
	innolux_panel_init_cmd_multi(&ctx, 0xb9, 0x45, 0x45);
	innolux_panel_init_cmd_multi(&ctx, 0xba, 0x14, 0x14);
	innolux_panel_init_cmd_multi(&ctx, 0xca, 0x02);
	innolux_panel_init_cmd_multi(&ctx, 0xce, 0x04);
	innolux_panel_init_cmd_multi(&ctx, 0xc3, 0x9b, 0x9b);
	innolux_panel_init_cmd_multi(&ctx, 0xd8, 0xc0, 0x03);
	innolux_panel_init_cmd_multi(&ctx, 0xbc, 0x82, 0x01);
	innolux_panel_init_cmd_multi(&ctx, 0xbd, 0x9e, 0x01);

	innolux_panel_switch_page(&ctx, 0x02);
	innolux_panel_init_cmd_multi(&ctx, 0xb0, 0x82);
	innolux_panel_init_cmd_multi(&ctx, 0xd1, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x82, 0x00, 0xa5,
				     0x00, 0xc1, 0x00, 0xea, 0x01, 0x0d, 0x01, 0x40);
	innolux_panel_init_cmd_multi(&ctx, 0xd2, 0x01, 0x6a, 0x01, 0xa8, 0x01, 0xdc, 0x02, 0x29,
				     0x02, 0x67, 0x02, 0x68, 0x02, 0xa8, 0x02, 0xf0);
	innolux_panel_init_cmd_multi(&ctx, 0xd3, 0x03, 0x19, 0x03, 0x49, 0x03, 0x67, 0x03, 0x8c,
				     0x03, 0xa6, 0x03, 0xc7, 0x03, 0xde, 0x03, 0xec);
	innolux_panel_init_cmd_multi(&ctx, 0xd4, 0x03, 0xff, 0x03, 0xff);
	innolux_panel_init_cmd_multi(&ctx, 0xe0, 0x00, 0x00, 0x00, 0x86, 0x00, 0xc5, 0x00, 0xe5,
				     0x00, 0xff, 0x01, 0x26, 0x01, 0x45, 0x01, 0x75);
	innolux_panel_init_cmd_multi(&ctx, 0xe1, 0x01, 0x9c, 0x01, 0xd5, 0x02, 0x05, 0x02, 0x4d,
				     0x02, 0x86, 0x02, 0x87, 0x02, 0xc3, 0x03, 0x03);
	innolux_panel_init_cmd_multi(&ctx, 0xe2, 0x03, 0x2a, 0x03, 0x56, 0x03, 0x72, 0x03, 0x94,
				     0x03, 0xac, 0x03, 0xcb, 0x03, 0xe0, 0x03, 0xed);
	innolux_panel_init_cmd_multi(&ctx, 0xe3, 0x03, 0xff, 0x03, 0xff);

	innolux_panel_switch_page(&ctx, 0x03);
	innolux_panel_init_cmd_multi(&ctx, 0xb0, 0x00, 0x00, 0x00, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xb1, 0x00, 0x00, 0x00, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xb2, 0x00, 0x00, 0x06, 0x04, 0x01, 0x40, 0x85);
	innolux_panel_init_cmd_multi(&ctx, 0xb3, 0x10, 0x07, 0xfc, 0x04, 0x01, 0x40, 0x80);
	innolux_panel_init_cmd_multi(&ctx, 0xb6, 0xf0, 0x08, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01,
				     0x40, 0x80);
	innolux_panel_init_cmd_multi(&ctx, 0xba, 0xc5, 0x07, 0x00, 0x04, 0x11, 0x25, 0x8c);
	innolux_panel_init_cmd_multi(&ctx, 0xbb, 0xc5, 0x07, 0x00, 0x03, 0x11, 0x25, 0x8c);
	innolux_panel_init_cmd_multi(&ctx, 0xc0, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x80, 0x80);
	innolux_panel_init_cmd_multi(&ctx, 0xc1, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x80, 0x80);
	innolux_panel_init_cmd_multi(&ctx, 0xc4, 0x00, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xef, 0x41);

	innolux_panel_switch_page(&ctx, 0x04);
	innolux_panel_init_cmd_multi(&ctx, 0xec, 0x4c);

	innolux_panel_switch_page(&ctx, 0x05);
	innolux_panel_init_cmd_multi(&ctx, 0xb0, 0x13, 0x03, 0x03, 0x01);
	innolux_panel_init_cmd_multi(&ctx, 0xb1, 0x30, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xb2, 0x02, 0x02, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xb3, 0x82, 0x23, 0x82, 0x9d);
	innolux_panel_init_cmd_multi(&ctx, 0xb4, 0xc5, 0x75, 0x24, 0x57);
	innolux_panel_init_cmd_multi(&ctx, 0xb5, 0x00, 0xd4, 0x72, 0x11, 0x11, 0xab, 0x0a);
	innolux_panel_init_cmd_multi(&ctx, 0xb6, 0x00, 0x00, 0xd5, 0x72, 0x24, 0x56);
	innolux_panel_init_cmd_multi(&ctx, 0xb7, 0x5c, 0xdc, 0x5c, 0x5c);
	innolux_panel_init_cmd_multi(&ctx, 0xb9, 0x0c, 0x00, 0x00, 0x01, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xc0, 0x75, 0x11, 0x11, 0x54, 0x05);
	innolux_panel_init_cmd_multi(&ctx, 0xc6, 0x00, 0x00, 0x00, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xd0, 0x00, 0x48, 0x08, 0x00, 0x00);
	innolux_panel_init_cmd_multi(&ctx, 0xd1, 0x00, 0x48, 0x09, 0x00, 0x00);

	innolux_panel_switch_page(&ctx, 0x06);
	innolux_panel_init_cmd_multi(&ctx, 0xb0, 0x02, 0x32, 0x32, 0x08, 0x2f);
	innolux_panel_init_cmd_multi(&ctx, 0xb1, 0x2e, 0x15, 0x14, 0x13, 0x12);
	innolux_panel_init_cmd_multi(&ctx, 0xb2, 0x11, 0x10, 0x00, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xb3, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xb4, 0x3d, 0x32);
	innolux_panel_init_cmd_multi(&ctx, 0xb5, 0x03, 0x32, 0x32, 0x09, 0x2f);
	innolux_panel_init_cmd_multi(&ctx, 0xb6, 0x2e, 0x1b, 0x1a, 0x19, 0x18);
	innolux_panel_init_cmd_multi(&ctx, 0xb7, 0x17, 0x16, 0x01, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xb8, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xb9, 0x3d, 0x32);
	innolux_panel_init_cmd_multi(&ctx, 0xc0, 0x01, 0x32, 0x32, 0x09, 0x2f);
	innolux_panel_init_cmd_multi(&ctx, 0xc1, 0x2e, 0x1a, 0x1b, 0x16, 0x17);
	innolux_panel_init_cmd_multi(&ctx, 0xc2, 0x18, 0x19, 0x03, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xc3, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xc4, 0x3d, 0x32);
	innolux_panel_init_cmd_multi(&ctx, 0xc5, 0x00, 0x32, 0x32, 0x08, 0x2f);
	innolux_panel_init_cmd_multi(&ctx, 0xc6, 0x2e, 0x14, 0x15, 0x10, 0x11);
	innolux_panel_init_cmd_multi(&ctx, 0xc7, 0x12, 0x13, 0x02, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xc8, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d);
	innolux_panel_init_cmd_multi(&ctx, 0xc9, 0x3d, 0x32);

	return ctx.accum_err;
}

static const struct panel_desc innolux_p097pfg_panel_desc = {
	.mode = &innolux_p097pfg_mode,
	.bpc = 8,
	.size = {
		.width = 147,
		.height = 196,
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		 MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.init = innolux_p097pfg_init,
	.lanes = 4,
	.supply_names = innolux_p097pfg_supply_names,
	.num_supplies = ARRAY_SIZE(innolux_p097pfg_supply_names),
	.sleep_mode_delay = 100, /* T15 */
};

static int innolux_panel_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	const struct drm_display_mode *m = innolux->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = innolux->desc->size.width;
	connector->display_info.height_mm = innolux->desc->size.height;
	connector->display_info.bpc = innolux->desc->bpc;

	return 1;
}

static const struct drm_panel_funcs innolux_panel_funcs = {
	.unprepare = innolux_panel_unprepare,
	.prepare = innolux_panel_prepare,
	.get_modes = innolux_panel_get_modes,
};

static const struct of_device_id innolux_of_match[] = {
	{ .compatible = "innolux,p079zca",
	  .data = &innolux_p079zca_panel_desc
	},
	{ .compatible = "innolux,p097pfg",
	  .data = &innolux_p097pfg_panel_desc
	},
	{ }
};
MODULE_DEVICE_TABLE(of, innolux_of_match);

static int innolux_panel_add(struct mipi_dsi_device *dsi,
			     const struct panel_desc *desc)
{
	struct innolux_panel *innolux;
	struct device *dev = &dsi->dev;
	int err, i;

	innolux = devm_drm_panel_alloc(dev, struct innolux_panel, base,
				       &innolux_panel_funcs,
				       DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(innolux))
		return PTR_ERR(innolux);

	innolux->desc = desc;

	innolux->supplies = devm_kcalloc(dev, desc->num_supplies,
					 sizeof(*innolux->supplies),
					 GFP_KERNEL);
	if (!innolux->supplies)
		return -ENOMEM;

	for (i = 0; i < desc->num_supplies; i++)
		innolux->supplies[i].supply = desc->supply_names[i];

	err = devm_regulator_bulk_get(dev, desc->num_supplies,
				      innolux->supplies);
	if (err < 0)
		return err;

	innolux->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						       GPIOD_OUT_HIGH);
	if (IS_ERR(innolux->enable_gpio)) {
		err = PTR_ERR(innolux->enable_gpio);
		dev_dbg(dev, "failed to get enable gpio: %d\n", err);
		innolux->enable_gpio = NULL;
	}

	err = drm_panel_of_backlight(&innolux->base);
	if (err)
		return err;

	drm_panel_add(&innolux->base);

	mipi_dsi_set_drvdata(dsi, innolux);
	innolux->link = dsi;

	return 0;
}

static void innolux_panel_del(struct innolux_panel *innolux)
{
	drm_panel_remove(&innolux->base);
}

static int innolux_panel_probe(struct mipi_dsi_device *dsi)
{
	const struct panel_desc *desc;
	struct innolux_panel *innolux;
	int err;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	err = innolux_panel_add(dsi, desc);
	if (err < 0)
		return err;

	err = mipi_dsi_attach(dsi);
	if (err < 0) {
		innolux = mipi_dsi_get_drvdata(dsi);
		innolux_panel_del(innolux);
		return err;
	}

	return 0;
}

static void innolux_panel_remove(struct mipi_dsi_device *dsi)
{
	struct innolux_panel *innolux = mipi_dsi_get_drvdata(dsi);
	int err;


	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	innolux_panel_del(innolux);
}

static struct mipi_dsi_driver innolux_panel_driver = {
	.driver = {
		.name = "panel-innolux-p079zca",
		.of_match_table = innolux_of_match,
	},
	.probe = innolux_panel_probe,
	.remove = innolux_panel_remove,
};
module_mipi_dsi_driver(innolux_panel_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("Innolux P079ZCA panel driver");
MODULE_LICENSE("GPL v2");
