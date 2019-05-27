// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct panel_init_cmd {
	size_t len;
	const char *data;
};

#define _INIT_CMD(...) { \
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

struct panel_desc {
	const struct drm_display_mode *mode;
	unsigned int bpc;
	struct {
		unsigned int width;
		unsigned int height;
	} size;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
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

	struct backlight_device *backlight;
	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;
	struct gpio_desc *enable_gpio;

	bool prepared;
	bool enabled;
};

static inline struct innolux_panel *to_innolux_panel(struct drm_panel *panel)
{
	return container_of(panel, struct innolux_panel, base);
}

static int innolux_panel_disable(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);

	if (!innolux->enabled)
		return 0;

	backlight_disable(innolux->backlight);

	innolux->enabled = false;

	return 0;
}

static int innolux_panel_unprepare(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	if (!innolux->prepared)
		return 0;

	err = mipi_dsi_dcs_set_display_off(innolux->link);
	if (err < 0)
		DRM_DEV_ERROR(panel->dev, "failed to set display off: %d\n",
			      err);

	err = mipi_dsi_dcs_enter_sleep_mode(innolux->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to enter sleep mode: %d\n",
			      err);
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

	innolux->prepared = false;

	return 0;
}

static int innolux_panel_prepare(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	if (innolux->prepared)
		return 0;

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

	if (innolux->desc->init_cmds) {
		const struct panel_init_cmd *cmds =
					innolux->desc->init_cmds;
		unsigned int i;

		for (i = 0; cmds[i].len != 0; i++) {
			const struct panel_init_cmd *cmd = &cmds[i];

			err = mipi_dsi_generic_write(innolux->link, cmd->data,
						     cmd->len);
			if (err < 0) {
				dev_err(panel->dev,
					"failed to write command %u\n", i);
				goto poweroff;
			}

			/*
			 * Included by random guessing, because without this
			 * (or at least, some delay), the panel sometimes
			 * didn't appear to pick up the command sequence.
			 */
			err = mipi_dsi_dcs_nop(innolux->link);
			if (err < 0) {
				dev_err(panel->dev,
					"failed to send DCS nop: %d\n", err);
				goto poweroff;
			}
		}
	}

	err = mipi_dsi_dcs_exit_sleep_mode(innolux->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to exit sleep mode: %d\n",
			      err);
		goto poweroff;
	}

	/* T6: 120ms - 1000ms*/
	msleep(120);

	err = mipi_dsi_dcs_set_display_on(innolux->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to set display on: %d\n",
			      err);
		goto poweroff;
	}

	/* T7: 5ms */
	usleep_range(5000, 6000);

	innolux->prepared = true;

	return 0;

poweroff:
	gpiod_set_value_cansleep(innolux->enable_gpio, 0);
	regulator_bulk_disable(innolux->desc->num_supplies, innolux->supplies);

	return err;
}

static int innolux_panel_enable(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int ret;

	if (innolux->enabled)
		return 0;

	ret = backlight_enable(innolux->backlight);
	if (ret) {
		DRM_DEV_ERROR(panel->drm->dev,
			      "Failed to enable backlight %d\n", ret);
		return ret;
	}

	innolux->enabled = true;

	return 0;
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
	.vrefresh = 60,
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
	.vrefresh = 60,
};

/*
 * Display manufacturer failed to provide init sequencing according to
 * https://chromium-review.googlesource.com/c/chromiumos/third_party/coreboot/+/892065/
 * so the init sequence stems from a register dump of a working panel.
 */
static const struct panel_init_cmd innolux_p097pfg_init_cmds[] = {
	/* page 0 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	_INIT_CMD(0xB1, 0xE8, 0x11),
	_INIT_CMD(0xB2, 0x25, 0x02),
	_INIT_CMD(0xB5, 0x08, 0x00),
	_INIT_CMD(0xBC, 0x0F, 0x00),
	_INIT_CMD(0xB8, 0x03, 0x06, 0x00, 0x00),
	_INIT_CMD(0xBD, 0x01, 0x90, 0x14, 0x14),
	_INIT_CMD(0x6F, 0x01),
	_INIT_CMD(0xC0, 0x03),
	_INIT_CMD(0x6F, 0x02),
	_INIT_CMD(0xC1, 0x0D),
	_INIT_CMD(0xD9, 0x01, 0x09, 0x70),
	_INIT_CMD(0xC5, 0x12, 0x21, 0x00),
	_INIT_CMD(0xBB, 0x93, 0x93),

	/* page 1 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01),
	_INIT_CMD(0xB3, 0x3C, 0x3C),
	_INIT_CMD(0xB4, 0x0F, 0x0F),
	_INIT_CMD(0xB9, 0x45, 0x45),
	_INIT_CMD(0xBA, 0x14, 0x14),
	_INIT_CMD(0xCA, 0x02),
	_INIT_CMD(0xCE, 0x04),
	_INIT_CMD(0xC3, 0x9B, 0x9B),
	_INIT_CMD(0xD8, 0xC0, 0x03),
	_INIT_CMD(0xBC, 0x82, 0x01),
	_INIT_CMD(0xBD, 0x9E, 0x01),

	/* page 2 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02),
	_INIT_CMD(0xB0, 0x82),
	_INIT_CMD(0xD1, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x82, 0x00, 0xA5,
		  0x00, 0xC1, 0x00, 0xEA, 0x01, 0x0D, 0x01, 0x40),
	_INIT_CMD(0xD2, 0x01, 0x6A, 0x01, 0xA8, 0x01, 0xDC, 0x02, 0x29,
		  0x02, 0x67, 0x02, 0x68, 0x02, 0xA8, 0x02, 0xF0),
	_INIT_CMD(0xD3, 0x03, 0x19, 0x03, 0x49, 0x03, 0x67, 0x03, 0x8C,
		  0x03, 0xA6, 0x03, 0xC7, 0x03, 0xDE, 0x03, 0xEC),
	_INIT_CMD(0xD4, 0x03, 0xFF, 0x03, 0xFF),
	_INIT_CMD(0xE0, 0x00, 0x00, 0x00, 0x86, 0x00, 0xC5, 0x00, 0xE5,
		  0x00, 0xFF, 0x01, 0x26, 0x01, 0x45, 0x01, 0x75),
	_INIT_CMD(0xE1, 0x01, 0x9C, 0x01, 0xD5, 0x02, 0x05, 0x02, 0x4D,
		  0x02, 0x86, 0x02, 0x87, 0x02, 0xC3, 0x03, 0x03),
	_INIT_CMD(0xE2, 0x03, 0x2A, 0x03, 0x56, 0x03, 0x72, 0x03, 0x94,
		  0x03, 0xAC, 0x03, 0xCB, 0x03, 0xE0, 0x03, 0xED),
	_INIT_CMD(0xE3, 0x03, 0xFF, 0x03, 0xFF),

	/* page 3 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03),
	_INIT_CMD(0xB0, 0x00, 0x00, 0x00, 0x00),
	_INIT_CMD(0xB1, 0x00, 0x00, 0x00, 0x00),
	_INIT_CMD(0xB2, 0x00, 0x00, 0x06, 0x04, 0x01, 0x40, 0x85),
	_INIT_CMD(0xB3, 0x10, 0x07, 0xFC, 0x04, 0x01, 0x40, 0x80),
	_INIT_CMD(0xB6, 0xF0, 0x08, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01,
		  0x40, 0x80),
	_INIT_CMD(0xBA, 0xC5, 0x07, 0x00, 0x04, 0x11, 0x25, 0x8C),
	_INIT_CMD(0xBB, 0xC5, 0x07, 0x00, 0x03, 0x11, 0x25, 0x8C),
	_INIT_CMD(0xC0, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x80, 0x80),
	_INIT_CMD(0xC1, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x80, 0x80),
	_INIT_CMD(0xC4, 0x00, 0x00),
	_INIT_CMD(0xEF, 0x41),

	/* page 4 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x04),
	_INIT_CMD(0xEC, 0x4C),

	/* page 5 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x05),
	_INIT_CMD(0xB0, 0x13, 0x03, 0x03, 0x01),
	_INIT_CMD(0xB1, 0x30, 0x00),
	_INIT_CMD(0xB2, 0x02, 0x02, 0x00),
	_INIT_CMD(0xB3, 0x82, 0x23, 0x82, 0x9D),
	_INIT_CMD(0xB4, 0xC5, 0x75, 0x24, 0x57),
	_INIT_CMD(0xB5, 0x00, 0xD4, 0x72, 0x11, 0x11, 0xAB, 0x0A),
	_INIT_CMD(0xB6, 0x00, 0x00, 0xD5, 0x72, 0x24, 0x56),
	_INIT_CMD(0xB7, 0x5C, 0xDC, 0x5C, 0x5C),
	_INIT_CMD(0xB9, 0x0C, 0x00, 0x00, 0x01, 0x00),
	_INIT_CMD(0xC0, 0x75, 0x11, 0x11, 0x54, 0x05),
	_INIT_CMD(0xC6, 0x00, 0x00, 0x00, 0x00),
	_INIT_CMD(0xD0, 0x00, 0x48, 0x08, 0x00, 0x00),
	_INIT_CMD(0xD1, 0x00, 0x48, 0x09, 0x00, 0x00),

	/* page 6 */
	_INIT_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x06),
	_INIT_CMD(0xB0, 0x02, 0x32, 0x32, 0x08, 0x2F),
	_INIT_CMD(0xB1, 0x2E, 0x15, 0x14, 0x13, 0x12),
	_INIT_CMD(0xB2, 0x11, 0x10, 0x00, 0x3D, 0x3D),
	_INIT_CMD(0xB3, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D),
	_INIT_CMD(0xB4, 0x3D, 0x32),
	_INIT_CMD(0xB5, 0x03, 0x32, 0x32, 0x09, 0x2F),
	_INIT_CMD(0xB6, 0x2E, 0x1B, 0x1A, 0x19, 0x18),
	_INIT_CMD(0xB7, 0x17, 0x16, 0x01, 0x3D, 0x3D),
	_INIT_CMD(0xB8, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D),
	_INIT_CMD(0xB9, 0x3D, 0x32),
	_INIT_CMD(0xC0, 0x01, 0x32, 0x32, 0x09, 0x2F),
	_INIT_CMD(0xC1, 0x2E, 0x1A, 0x1B, 0x16, 0x17),
	_INIT_CMD(0xC2, 0x18, 0x19, 0x03, 0x3D, 0x3D),
	_INIT_CMD(0xC3, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D),
	_INIT_CMD(0xC4, 0x3D, 0x32),
	_INIT_CMD(0xC5, 0x00, 0x32, 0x32, 0x08, 0x2F),
	_INIT_CMD(0xC6, 0x2E, 0x14, 0x15, 0x10, 0x11),
	_INIT_CMD(0xC7, 0x12, 0x13, 0x02, 0x3D, 0x3D),
	_INIT_CMD(0xC8, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D),
	_INIT_CMD(0xC9, 0x3D, 0x32),

	{},
};

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
	.init_cmds = innolux_p097pfg_init_cmds,
	.lanes = 4,
	.supply_names = innolux_p097pfg_supply_names,
	.num_supplies = ARRAY_SIZE(innolux_p097pfg_supply_names),
	.sleep_mode_delay = 100, /* T15 */
};

static int innolux_panel_get_modes(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	const struct drm_display_mode *m = innolux->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, m);
	if (!mode) {
		DRM_DEV_ERROR(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			      m->hdisplay, m->vdisplay, m->vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm =
			innolux->desc->size.width;
	panel->connector->display_info.height_mm =
			innolux->desc->size.height;
	panel->connector->display_info.bpc = innolux->desc->bpc;

	return 1;
}

static const struct drm_panel_funcs innolux_panel_funcs = {
	.disable = innolux_panel_disable,
	.unprepare = innolux_panel_unprepare,
	.prepare = innolux_panel_prepare,
	.enable = innolux_panel_enable,
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

	innolux = devm_kzalloc(dev, sizeof(*innolux), GFP_KERNEL);
	if (!innolux)
		return -ENOMEM;

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

	innolux->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(innolux->backlight))
		return PTR_ERR(innolux->backlight);

	drm_panel_init(&innolux->base);
	innolux->base.funcs = &innolux_panel_funcs;
	innolux->base.dev = dev;

	err = drm_panel_add(&innolux->base);
	if (err < 0)
		return err;

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
	int err;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	err = innolux_panel_add(dsi, desc);
	if (err < 0)
		return err;

	return mipi_dsi_attach(dsi);
}

static int innolux_panel_remove(struct mipi_dsi_device *dsi)
{
	struct innolux_panel *innolux = mipi_dsi_get_drvdata(dsi);
	int err;

	err = innolux_panel_unprepare(&innolux->base);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to unprepare panel: %d\n",
			      err);

	err = innolux_panel_disable(&innolux->base);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to disable panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to detach from DSI host: %d\n",
			      err);

	innolux_panel_del(innolux);

	return 0;
}

static void innolux_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct innolux_panel *innolux = mipi_dsi_get_drvdata(dsi);

	innolux_panel_unprepare(&innolux->base);
	innolux_panel_disable(&innolux->base);
}

static struct mipi_dsi_driver innolux_panel_driver = {
	.driver = {
		.name = "panel-innolux-p079zca",
		.of_match_table = innolux_of_match,
	},
	.probe = innolux_panel_probe,
	.remove = innolux_panel_remove,
	.shutdown = innolux_panel_shutdown,
};
module_mipi_dsi_driver(innolux_panel_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("Innolux P079ZCA panel driver");
MODULE_LICENSE("GPL v2");
