/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

/**
 * @modes: Pointer to array of fixed modes appropriate for this panel.  If
 *         only one mode then this can just be the address of this the mode.
 *         NOTE: cannot be used with "timings" and also if this is specified
 *         then you cannot override the mode in the device tree.
 * @num_modes: Number of elements in modes array.
 * @timings: Pointer to array of display timings.  NOTE: cannot be used with
 *           "modes" and also these will be used to validate a device tree
 *           override if one is present.
 * @num_timings: Number of elements in timings array.
 * @bpc: Bits per color.
 * @size: Structure containing the physical size of this panel.
 * @delay: Structure containing various delay values for this panel.
 * @bus_format: See MEDIA_BUS_FMT_... defines.
 * @bus_flags: See DRM_BUS_FLAG_... defines.
 */
struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct display_timing *timings;
	unsigned int num_timings;

	unsigned int bpc;

	/**
	 * @width: width (in millimeters) of the panel's active display area
	 * @height: height (in millimeters) of the panel's active display area
	 */
	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *           become ready and start receiving video data
	 * @hpd_absent_delay: Add this to the prepare delay if we know Hot
	 *                    Plug Detect isn't used.
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *          display the first valid frame after starting to receive
	 *          video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *           turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *             to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int hpd_absent_delay;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;

	u32 bus_format;
	u32 bus_flags;
	int connector_type;
};

struct panel_simple {
	struct drm_panel base;
	bool prepared;
	bool enabled;
	bool no_hpd;

	const struct panel_desc *desc;

	struct regulator *supply;
	struct i2c_adapter *ddc;

	struct gpio_desc *enable_gpio;

	struct drm_display_mode override_mode;
};

static inline struct panel_simple *to_panel_simple(struct drm_panel *panel)
{
	return container_of(panel, struct panel_simple, base);
}

static unsigned int panel_simple_get_timings_modes(struct panel_simple *panel,
						   struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	for (i = 0; i < panel->desc->num_timings; i++) {
		const struct display_timing *dt = &panel->desc->timings[i];
		struct videomode vm;

		videomode_from_timing(dt, &vm);
		mode = drm_mode_create(connector->dev);
		if (!mode) {
			dev_err(panel->base.dev, "failed to add mode %ux%u\n",
				dt->hactive.typ, dt->vactive.typ);
			continue;
		}

		drm_display_mode_from_videomode(&vm, mode);

		mode->type |= DRM_MODE_TYPE_DRIVER;

		if (panel->desc->num_timings == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
		num++;
	}

	return num;
}

static unsigned int panel_simple_get_display_modes(struct panel_simple *panel,
						   struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	for (i = 0; i < panel->desc->num_modes; i++) {
		const struct drm_display_mode *m = &panel->desc->modes[i];

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->base.dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, m->vrefresh);
			continue;
		}

		mode->type |= DRM_MODE_TYPE_DRIVER;

		if (panel->desc->num_modes == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	return num;
}

static int panel_simple_get_non_edid_modes(struct panel_simple *panel,
					   struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	bool has_override = panel->override_mode.type;
	unsigned int num = 0;

	if (!panel->desc)
		return 0;

	if (has_override) {
		mode = drm_mode_duplicate(connector->dev,
					  &panel->override_mode);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num = 1;
		} else {
			dev_err(panel->base.dev, "failed to add override mode\n");
		}
	}

	/* Only add timings if override was not there or failed to validate */
	if (num == 0 && panel->desc->num_timings)
		num = panel_simple_get_timings_modes(panel, connector);

	/*
	 * Only add fixed modes if timings/override added no mode.
	 *
	 * We should only ever have either the display timings specified
	 * or a fixed mode. Anything else is rather bogus.
	 */
	WARN_ON(panel->desc->num_timings && panel->desc->num_modes);
	if (num == 0)
		num = panel_simple_get_display_modes(panel, connector);

	connector->display_info.bpc = panel->desc->bpc;
	connector->display_info.width_mm = panel->desc->size.width;
	connector->display_info.height_mm = panel->desc->size.height;
	if (panel->desc->bus_format)
		drm_display_info_set_bus_formats(&connector->display_info,
						 &panel->desc->bus_format, 1);
	connector->display_info.bus_flags = panel->desc->bus_flags;

	return num;
}

static int panel_simple_disable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);

	if (!p->enabled)
		return 0;

	if (p->desc->delay.disable)
		msleep(p->desc->delay.disable);

	p->enabled = false;

	return 0;
}

static int panel_simple_unprepare(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);

	if (!p->prepared)
		return 0;

	gpiod_set_value_cansleep(p->enable_gpio, 0);

	regulator_disable(p->supply);

	if (p->desc->delay.unprepare)
		msleep(p->desc->delay.unprepare);

	p->prepared = false;

	return 0;
}

static int panel_simple_prepare(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	unsigned int delay;
	int err;

	if (p->prepared)
		return 0;

	err = regulator_enable(p->supply);
	if (err < 0) {
		dev_err(panel->dev, "failed to enable supply: %d\n", err);
		return err;
	}

	gpiod_set_value_cansleep(p->enable_gpio, 1);

	delay = p->desc->delay.prepare;
	if (p->no_hpd)
		delay += p->desc->delay.hpd_absent_delay;
	if (delay)
		msleep(delay);

	p->prepared = true;

	return 0;
}

static int panel_simple_enable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);

	if (p->enabled)
		return 0;

	if (p->desc->delay.enable)
		msleep(p->desc->delay.enable);

	p->enabled = true;

	return 0;
}

static int panel_simple_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct panel_simple *p = to_panel_simple(panel);
	int num = 0;

	/* probe EDID if a DDC bus is available */
	if (p->ddc) {
		struct edid *edid = drm_get_edid(connector, p->ddc);

		drm_connector_update_edid_property(connector, edid);
		if (edid) {
			num += drm_add_edid_modes(connector, edid);
			kfree(edid);
		}
	}

	/* add hard-coded panel modes */
	num += panel_simple_get_non_edid_modes(p, connector);

	return num;
}

static int panel_simple_get_timings(struct drm_panel *panel,
				    unsigned int num_timings,
				    struct display_timing *timings)
{
	struct panel_simple *p = to_panel_simple(panel);
	unsigned int i;

	if (p->desc->num_timings < num_timings)
		num_timings = p->desc->num_timings;

	if (timings)
		for (i = 0; i < num_timings; i++)
			timings[i] = p->desc->timings[i];

	return p->desc->num_timings;
}

static const struct drm_panel_funcs panel_simple_funcs = {
	.disable = panel_simple_disable,
	.unprepare = panel_simple_unprepare,
	.prepare = panel_simple_prepare,
	.enable = panel_simple_enable,
	.get_modes = panel_simple_get_modes,
	.get_timings = panel_simple_get_timings,
};

#define PANEL_SIMPLE_BOUNDS_CHECK(to_check, bounds, field) \
	(to_check->field.typ >= bounds->field.min && \
	 to_check->field.typ <= bounds->field.max)
static void panel_simple_parse_panel_timing_node(struct device *dev,
						 struct panel_simple *panel,
						 const struct display_timing *ot)
{
	const struct panel_desc *desc = panel->desc;
	struct videomode vm;
	unsigned int i;

	if (WARN_ON(desc->num_modes)) {
		dev_err(dev, "Reject override mode: panel has a fixed mode\n");
		return;
	}
	if (WARN_ON(!desc->num_timings)) {
		dev_err(dev, "Reject override mode: no timings specified\n");
		return;
	}

	for (i = 0; i < panel->desc->num_timings; i++) {
		const struct display_timing *dt = &panel->desc->timings[i];

		if (!PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, hactive) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, hfront_porch) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, hback_porch) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, hsync_len) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, vactive) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, vfront_porch) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, vback_porch) ||
		    !PANEL_SIMPLE_BOUNDS_CHECK(ot, dt, vsync_len))
			continue;

		if (ot->flags != dt->flags)
			continue;

		videomode_from_timing(ot, &vm);
		drm_display_mode_from_videomode(&vm, &panel->override_mode);
		panel->override_mode.type |= DRM_MODE_TYPE_DRIVER |
					     DRM_MODE_TYPE_PREFERRED;
		break;
	}

	if (WARN_ON(!panel->override_mode.type))
		dev_err(dev, "Reject override mode: No display_timing found\n");
}

static int panel_simple_probe(struct device *dev, const struct panel_desc *desc)
{
	struct panel_simple *panel;
	struct display_timing dt;
	struct device_node *ddc;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->enabled = false;
	panel->prepared = false;
	panel->desc = desc;

	panel->no_hpd = of_property_read_bool(dev->of_node, "no-hpd");

	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply))
		return PTR_ERR(panel->supply);

	panel->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(panel->enable_gpio)) {
		err = PTR_ERR(panel->enable_gpio);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to request GPIO: %d\n", err);
		return err;
	}

	ddc = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
	if (ddc) {
		panel->ddc = of_find_i2c_adapter_by_node(ddc);
		of_node_put(ddc);

		if (!panel->ddc)
			return -EPROBE_DEFER;
	}

	if (!of_get_display_timing(dev->of_node, "panel-timing", &dt))
		panel_simple_parse_panel_timing_node(dev, panel, &dt);

	drm_panel_init(&panel->base, dev, &panel_simple_funcs,
		       desc->connector_type);

	err = drm_panel_of_backlight(&panel->base);
	if (err)
		goto free_ddc;

	err = drm_panel_add(&panel->base);
	if (err < 0)
		goto free_ddc;

	dev_set_drvdata(dev, panel);

	return 0;

free_ddc:
	if (panel->ddc)
		put_device(&panel->ddc->dev);

	return err;
}

static int panel_simple_remove(struct device *dev)
{
	struct panel_simple *panel = dev_get_drvdata(dev);

	drm_panel_remove(&panel->base);
	drm_panel_disable(&panel->base);
	drm_panel_unprepare(&panel->base);

	if (panel->ddc)
		put_device(&panel->ddc->dev);

	return 0;
}

static void panel_simple_shutdown(struct device *dev)
{
	struct panel_simple *panel = dev_get_drvdata(dev);

	drm_panel_disable(&panel->base);
	drm_panel_unprepare(&panel->base);
}

static const struct drm_display_mode ampire_am_480272h3tmqw_t01h_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 10,
	.vtotal = 272 + 2 + 10 + 2,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc ampire_am_480272h3tmqw_t01h = {
	.modes = &ampire_am_480272h3tmqw_t01h_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 105,
		.height = 67,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct drm_display_mode ampire_am800480r3tmqwa1h_mode = {
	.clock = 33333,
	.hdisplay = 800,
	.hsync_start = 800 + 0,
	.hsync_end = 800 + 0 + 255,
	.htotal = 800 + 0 + 255 + 0,
	.vdisplay = 480,
	.vsync_start = 480 + 2,
	.vsync_end = 480 + 2 + 45,
	.vtotal = 480 + 2 + 45 + 0,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc ampire_am800480r3tmqwa1h = {
	.modes = &ampire_am800480r3tmqwa1h_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct display_timing santek_st0700i5y_rbslw_f_timing = {
	.pixelclock = { 26400000, 33300000, 46800000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 16, 210, 354 },
	.hback_porch = { 45, 36, 6 },
	.hsync_len = { 1, 10, 40 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 7, 22, 147 },
	.vback_porch = { 22, 13, 3 },
	.vsync_len = { 1, 10, 20 },
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE
};

static const struct panel_desc armadeus_st0700_adapt = {
	.timings = &santek_st0700i5y_rbslw_f_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 86,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_POSEDGE,
};

static const struct drm_display_mode auo_b101aw03_mode = {
	.clock = 51450,
	.hdisplay = 1024,
	.hsync_start = 1024 + 156,
	.hsync_end = 1024 + 156 + 8,
	.htotal = 1024 + 156 + 8 + 156,
	.vdisplay = 600,
	.vsync_start = 600 + 16,
	.vsync_end = 600 + 16 + 6,
	.vtotal = 600 + 16 + 6 + 16,
	.vrefresh = 60,
};

static const struct panel_desc auo_b101aw03 = {
	.modes = &auo_b101aw03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct display_timing auo_b101ean01_timing = {
	.pixelclock = { 65300000, 72500000, 75000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 18, 119, 119 },
	.hback_porch = { 21, 21, 21 },
	.hsync_len = { 32, 32, 32 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 4, 4, 4 },
	.vback_porch = { 8, 8, 8 },
	.vsync_len = { 18, 20, 20 },
};

static const struct panel_desc auo_b101ean01 = {
	.timings = &auo_b101ean01_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 217,
		.height = 136,
	},
};

static const struct drm_display_mode auo_b101xtn01_mode = {
	.clock = 72000,
	.hdisplay = 1366,
	.hsync_start = 1366 + 20,
	.hsync_end = 1366 + 20 + 70,
	.htotal = 1366 + 20 + 70,
	.vdisplay = 768,
	.vsync_start = 768 + 14,
	.vsync_end = 768 + 14 + 42,
	.vtotal = 768 + 14 + 42,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc auo_b101xtn01 = {
	.modes = &auo_b101xtn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode auo_b116xak01_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 10,
	.vdisplay = 768,
	.vsync_start = 768 + 4,
	.vsync_end = 768 + 4 + 6,
	.vtotal = 768 + 4 + 6 + 15,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc auo_b116xak01 = {
	.modes = &auo_b116xak01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
	.delay = {
		.hpd_absent_delay = 200,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
	.connector_type = DRM_MODE_CONNECTOR_eDP,
};

static const struct drm_display_mode auo_b116xw03_mode = {
	.clock = 70589,
	.hdisplay = 1366,
	.hsync_start = 1366 + 40,
	.hsync_end = 1366 + 40 + 40,
	.htotal = 1366 + 40 + 40 + 32,
	.vdisplay = 768,
	.vsync_start = 768 + 10,
	.vsync_end = 768 + 10 + 12,
	.vtotal = 768 + 10 + 12 + 6,
	.vrefresh = 60,
};

static const struct panel_desc auo_b116xw03 = {
	.modes = &auo_b116xw03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
};

static const struct drm_display_mode auo_b133xtn01_mode = {
	.clock = 69500,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 20,
	.vdisplay = 768,
	.vsync_start = 768 + 3,
	.vsync_end = 768 + 3 + 6,
	.vtotal = 768 + 3 + 6 + 13,
	.vrefresh = 60,
};

static const struct panel_desc auo_b133xtn01 = {
	.modes = &auo_b133xtn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 293,
		.height = 165,
	},
};

static const struct drm_display_mode auo_b133htn01_mode = {
	.clock = 150660,
	.hdisplay = 1920,
	.hsync_start = 1920 + 172,
	.hsync_end = 1920 + 172 + 80,
	.htotal = 1920 + 172 + 80 + 60,
	.vdisplay = 1080,
	.vsync_start = 1080 + 25,
	.vsync_end = 1080 + 25 + 10,
	.vtotal = 1080 + 25 + 10 + 10,
	.vrefresh = 60,
};

static const struct panel_desc auo_b133htn01 = {
	.modes = &auo_b133htn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 293,
		.height = 165,
	},
	.delay = {
		.prepare = 105,
		.enable = 20,
		.unprepare = 50,
	},
};

static const struct display_timing auo_g070vvn01_timings = {
	.pixelclock = { 33300000, 34209000, 45000000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 20, 40, 200 },
	.hback_porch = { 87, 40, 1 },
	.hsync_len = { 1, 48, 87 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 5, 13, 200 },
	.vback_porch = { 31, 31, 29 },
	.vsync_len = { 1, 1, 3 },
};

static const struct panel_desc auo_g070vvn01 = {
	.timings = &auo_g070vvn01_timings,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 152,
		.height = 91,
	},
	.delay = {
		.prepare = 200,
		.enable = 50,
		.disable = 50,
		.unprepare = 1000,
	},
};

static const struct drm_display_mode auo_g101evn010_mode = {
	.clock = 68930,
	.hdisplay = 1280,
	.hsync_start = 1280 + 82,
	.hsync_end = 1280 + 82 + 2,
	.htotal = 1280 + 82 + 2 + 84,
	.vdisplay = 800,
	.vsync_start = 800 + 8,
	.vsync_end = 800 + 8 + 2,
	.vtotal = 800 + 8 + 2 + 6,
	.vrefresh = 60,
};

static const struct panel_desc auo_g101evn010 = {
	.modes = &auo_g101evn010_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 216,
		.height = 135,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode auo_g104sn02_mode = {
	.clock = 40000,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 216,
	.htotal = 800 + 40 + 216 + 128,
	.vdisplay = 600,
	.vsync_start = 600 + 10,
	.vsync_end = 600 + 10 + 35,
	.vtotal = 600 + 10 + 35 + 2,
	.vrefresh = 60,
};

static const struct panel_desc auo_g104sn02 = {
	.modes = &auo_g104sn02_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 211,
		.height = 158,
	},
};

static const struct display_timing auo_g133han01_timings = {
	.pixelclock = { 134000000, 141200000, 149000000 },
	.hactive = { 1920, 1920, 1920 },
	.hfront_porch = { 39, 58, 77 },
	.hback_porch = { 59, 88, 117 },
	.hsync_len = { 28, 42, 56 },
	.vactive = { 1080, 1080, 1080 },
	.vfront_porch = { 3, 8, 11 },
	.vback_porch = { 5, 14, 19 },
	.vsync_len = { 4, 14, 19 },
};

static const struct panel_desc auo_g133han01 = {
	.timings = &auo_g133han01_timings,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 293,
		.height = 165,
	},
	.delay = {
		.prepare = 200,
		.enable = 50,
		.disable = 50,
		.unprepare = 1000,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing auo_g185han01_timings = {
	.pixelclock = { 120000000, 144000000, 175000000 },
	.hactive = { 1920, 1920, 1920 },
	.hfront_porch = { 36, 120, 148 },
	.hback_porch = { 24, 88, 108 },
	.hsync_len = { 20, 48, 64 },
	.vactive = { 1080, 1080, 1080 },
	.vfront_porch = { 6, 10, 40 },
	.vback_porch = { 2, 5, 20 },
	.vsync_len = { 2, 5, 20 },
};

static const struct panel_desc auo_g185han01 = {
	.timings = &auo_g185han01_timings,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 409,
		.height = 230,
	},
	.delay = {
		.prepare = 50,
		.enable = 200,
		.disable = 110,
		.unprepare = 1000,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing auo_p320hvn03_timings = {
	.pixelclock = { 106000000, 148500000, 164000000 },
	.hactive = { 1920, 1920, 1920 },
	.hfront_porch = { 25, 50, 130 },
	.hback_porch = { 25, 50, 130 },
	.hsync_len = { 20, 40, 105 },
	.vactive = { 1080, 1080, 1080 },
	.vfront_porch = { 8, 17, 150 },
	.vback_porch = { 8, 17, 150 },
	.vsync_len = { 4, 11, 100 },
};

static const struct panel_desc auo_p320hvn03 = {
	.timings = &auo_p320hvn03_timings,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 698,
		.height = 393,
	},
	.delay = {
		.prepare = 1,
		.enable = 450,
		.unprepare = 500,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode auo_t215hvn01_mode = {
	.clock = 148800,
	.hdisplay = 1920,
	.hsync_start = 1920 + 88,
	.hsync_end = 1920 + 88 + 44,
	.htotal = 1920 + 88 + 44 + 148,
	.vdisplay = 1080,
	.vsync_start = 1080 + 4,
	.vsync_end = 1080 + 4 + 5,
	.vtotal = 1080 + 4 + 5 + 36,
	.vrefresh = 60,
};

static const struct panel_desc auo_t215hvn01 = {
	.modes = &auo_t215hvn01_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 430,
		.height = 270,
	},
	.delay = {
		.disable = 5,
		.unprepare = 1000,
	}
};

static const struct drm_display_mode avic_tm070ddh03_mode = {
	.clock = 51200,
	.hdisplay = 1024,
	.hsync_start = 1024 + 160,
	.hsync_end = 1024 + 160 + 4,
	.htotal = 1024 + 160 + 4 + 156,
	.vdisplay = 600,
	.vsync_start = 600 + 17,
	.vsync_end = 600 + 17 + 1,
	.vtotal = 600 + 17 + 1 + 17,
	.vrefresh = 60,
};

static const struct panel_desc avic_tm070ddh03 = {
	.modes = &avic_tm070ddh03_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 154,
		.height = 90,
	},
	.delay = {
		.prepare = 20,
		.enable = 200,
		.disable = 200,
	},
};

static const struct drm_display_mode bananapi_s070wv20_ct16_mode = {
	.clock = 30000,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 48,
	.htotal = 800 + 40 + 48 + 40,
	.vdisplay = 480,
	.vsync_start = 480 + 13,
	.vsync_end = 480 + 13 + 3,
	.vtotal = 480 + 13 + 3 + 29,
};

static const struct panel_desc bananapi_s070wv20_ct16 = {
	.modes = &bananapi_s070wv20_ct16_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 86,
	},
};

static const struct drm_display_mode boe_hv070wsa_mode = {
	.clock = 42105,
	.hdisplay = 1024,
	.hsync_start = 1024 + 30,
	.hsync_end = 1024 + 30 + 30,
	.htotal = 1024 + 30 + 30 + 30,
	.vdisplay = 600,
	.vsync_start = 600 + 10,
	.vsync_end = 600 + 10 + 10,
	.vtotal = 600 + 10 + 10 + 10,
	.vrefresh = 60,
};

static const struct panel_desc boe_hv070wsa = {
	.modes = &boe_hv070wsa_mode,
	.num_modes = 1,
	.size = {
		.width = 154,
		.height = 90,
	},
};

static const struct drm_display_mode boe_nv101wxmn51_modes[] = {
	{
		.clock = 71900,
		.hdisplay = 1280,
		.hsync_start = 1280 + 48,
		.hsync_end = 1280 + 48 + 32,
		.htotal = 1280 + 48 + 32 + 80,
		.vdisplay = 800,
		.vsync_start = 800 + 3,
		.vsync_end = 800 + 3 + 5,
		.vtotal = 800 + 3 + 5 + 24,
		.vrefresh = 60,
	},
	{
		.clock = 57500,
		.hdisplay = 1280,
		.hsync_start = 1280 + 48,
		.hsync_end = 1280 + 48 + 32,
		.htotal = 1280 + 48 + 32 + 80,
		.vdisplay = 800,
		.vsync_start = 800 + 3,
		.vsync_end = 800 + 3 + 5,
		.vtotal = 800 + 3 + 5 + 24,
		.vrefresh = 48,
	},
};

static const struct panel_desc boe_nv101wxmn51 = {
	.modes = boe_nv101wxmn51_modes,
	.num_modes = ARRAY_SIZE(boe_nv101wxmn51_modes),
	.bpc = 8,
	.size = {
		.width = 217,
		.height = 136,
	},
	.delay = {
		.prepare = 210,
		.enable = 50,
		.unprepare = 160,
	},
};

static const struct drm_display_mode boe_nv140fhmn49_modes[] = {
	{
		.clock = 148500,
		.hdisplay = 1920,
		.hsync_start = 1920 + 48,
		.hsync_end = 1920 + 48 + 32,
		.htotal = 2200,
		.vdisplay = 1080,
		.vsync_start = 1080 + 3,
		.vsync_end = 1080 + 3 + 5,
		.vtotal = 1125,
		.vrefresh = 60,
	},
};

static const struct panel_desc boe_nv140fhmn49 = {
	.modes = boe_nv140fhmn49_modes,
	.num_modes = ARRAY_SIZE(boe_nv140fhmn49_modes),
	.bpc = 6,
	.size = {
		.width = 309,
		.height = 174,
	},
	.delay = {
		.prepare = 210,
		.enable = 50,
		.unprepare = 160,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
	.connector_type = DRM_MODE_CONNECTOR_eDP,
};

static const struct drm_display_mode cdtech_s043wq26h_ct7_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 5,
	.hsync_end = 480 + 5 + 5,
	.htotal = 480 + 5 + 5 + 40,
	.vdisplay = 272,
	.vsync_start = 272 + 8,
	.vsync_end = 272 + 8 + 8,
	.vtotal = 272 + 8 + 8 + 8,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc cdtech_s043wq26h_ct7 = {
	.modes = &cdtech_s043wq26h_ct7_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct drm_display_mode cdtech_s070wv95_ct16_mode = {
	.clock = 35000,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 40,
	.htotal = 800 + 40 + 40 + 48,
	.vdisplay = 480,
	.vsync_start = 480 + 29,
	.vsync_end = 480 + 29 + 13,
	.vtotal = 480 + 29 + 13 + 3,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc cdtech_s070wv95_ct16 = {
	.modes = &cdtech_s070wv95_ct16_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 154,
		.height = 85,
	},
};

static const struct drm_display_mode chunghwa_claa070wp03xg_mode = {
	.clock = 66770,
	.hdisplay = 800,
	.hsync_start = 800 + 49,
	.hsync_end = 800 + 49 + 33,
	.htotal = 800 + 49 + 33 + 17,
	.vdisplay = 1280,
	.vsync_start = 1280 + 1,
	.vsync_end = 1280 + 1 + 7,
	.vtotal = 1280 + 1 + 7 + 15,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc chunghwa_claa070wp03xg = {
	.modes = &chunghwa_claa070wp03xg_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 94,
		.height = 150,
	},
};

static const struct drm_display_mode chunghwa_claa101wa01a_mode = {
	.clock = 72070,
	.hdisplay = 1366,
	.hsync_start = 1366 + 58,
	.hsync_end = 1366 + 58 + 58,
	.htotal = 1366 + 58 + 58 + 58,
	.vdisplay = 768,
	.vsync_start = 768 + 4,
	.vsync_end = 768 + 4 + 4,
	.vtotal = 768 + 4 + 4 + 4,
	.vrefresh = 60,
};

static const struct panel_desc chunghwa_claa101wa01a = {
	.modes = &chunghwa_claa101wa01a_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 220,
		.height = 120,
	},
};

static const struct drm_display_mode chunghwa_claa101wb01_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 20,
	.vdisplay = 768,
	.vsync_start = 768 + 16,
	.vsync_end = 768 + 16 + 8,
	.vtotal = 768 + 16 + 8 + 16,
	.vrefresh = 60,
};

static const struct panel_desc chunghwa_claa101wb01 = {
	.modes = &chunghwa_claa101wb01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode dataimage_scf0700c48ggu18_mode = {
	.clock = 33260,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 128,
	.htotal = 800 + 40 + 128 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 2,
	.vtotal = 480 + 10 + 2 + 33,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc dataimage_scf0700c48ggu18 = {
	.modes = &dataimage_scf0700c48ggu18_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct display_timing dlc_dlc0700yzg_1_timing = {
	.pixelclock = { 45000000, 51200000, 57000000 },
	.hactive = { 1024, 1024, 1024 },
	.hfront_porch = { 100, 106, 113 },
	.hback_porch = { 100, 106, 113 },
	.hsync_len = { 100, 108, 114 },
	.vactive = { 600, 600, 600 },
	.vfront_porch = { 8, 11, 15 },
	.vback_porch = { 8, 11, 15 },
	.vsync_len = { 9, 13, 15 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc dlc_dlc0700yzg_1 = {
	.timings = &dlc_dlc0700yzg_1_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 86,
	},
	.delay = {
		.prepare = 30,
		.enable = 200,
		.disable = 200,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing dlc_dlc1010gig_timing = {
	.pixelclock = { 68900000, 71100000, 73400000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 43, 53, 63 },
	.hback_porch = { 43, 53, 63 },
	.hsync_len = { 44, 54, 64 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 5, 8, 11 },
	.vback_porch = { 5, 8, 11 },
	.vsync_len = { 5, 7, 11 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc dlc_dlc1010gig = {
	.timings = &dlc_dlc1010gig_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 216,
		.height = 135,
	},
	.delay = {
		.prepare = 60,
		.enable = 150,
		.disable = 100,
		.unprepare = 60,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode edt_et035012dm6_mode = {
	.clock = 6500,
	.hdisplay = 320,
	.hsync_start = 320 + 20,
	.hsync_end = 320 + 20 + 30,
	.htotal = 320 + 20 + 68,
	.vdisplay = 240,
	.vsync_start = 240 + 4,
	.vsync_end = 240 + 4 + 4,
	.vtotal = 240 + 4 + 4 + 14,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc edt_et035012dm6 = {
	.modes = &edt_et035012dm6_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 70,
		.height = 52,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_LOW | DRM_BUS_FLAG_PIXDATA_NEGEDGE,
};

static const struct drm_display_mode edt_etm0430g0dh6_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 10,
	.vtotal = 272 + 2 + 10 + 2,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc edt_etm0430g0dh6 = {
	.modes = &edt_etm0430g0dh6_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 95,
		.height = 54,
	},
};

static const struct drm_display_mode edt_et057090dhu_mode = {
	.clock = 25175,
	.hdisplay = 640,
	.hsync_start = 640 + 16,
	.hsync_end = 640 + 16 + 30,
	.htotal = 640 + 16 + 30 + 114,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 3,
	.vtotal = 480 + 10 + 3 + 32,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc edt_et057090dhu = {
	.modes = &edt_et057090dhu_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 115,
		.height = 86,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
};

static const struct drm_display_mode edt_etm0700g0dh6_mode = {
	.clock = 33260,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 128,
	.htotal = 800 + 40 + 128 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 2,
	.vtotal = 480 + 10 + 2 + 33,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc edt_etm0700g0dh6 = {
	.modes = &edt_etm0700g0dh6_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
};

static const struct panel_desc edt_etm0700g0bdh6 = {
	.modes = &edt_etm0700g0dh6_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct display_timing evervision_vgg804821_timing = {
	.pixelclock = { 27600000, 33300000, 50000000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 40, 66, 70 },
	.hback_porch = { 40, 67, 70 },
	.hsync_len = { 40, 67, 70 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 6, 10, 10 },
	.vback_porch = { 7, 11, 11 },
	.vsync_len = { 7, 11, 11 },
	.flags = DISPLAY_FLAGS_HSYNC_HIGH | DISPLAY_FLAGS_VSYNC_HIGH |
		 DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_NEGEDGE |
		 DISPLAY_FLAGS_SYNC_NEGEDGE,
};

static const struct panel_desc evervision_vgg804821 = {
	.timings = &evervision_vgg804821_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 108,
		.height = 64,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_NEGEDGE,
};

static const struct drm_display_mode foxlink_fl500wvr00_a0t_mode = {
	.clock = 32260,
	.hdisplay = 800,
	.hsync_start = 800 + 168,
	.hsync_end = 800 + 168 + 64,
	.htotal = 800 + 168 + 64 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 37,
	.vsync_end = 480 + 37 + 2,
	.vtotal = 480 + 37 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc foxlink_fl500wvr00_a0t = {
	.modes = &foxlink_fl500wvr00_a0t_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 108,
		.height = 65,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct drm_display_mode friendlyarm_hd702e_mode = {
	.clock		= 67185,
	.hdisplay	= 800,
	.hsync_start	= 800 + 20,
	.hsync_end	= 800 + 20 + 24,
	.htotal		= 800 + 20 + 24 + 20,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 4,
	.vsync_end	= 1280 + 4 + 8,
	.vtotal		= 1280 + 4 + 8 + 4,
	.vrefresh	= 60,
	.flags		= DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc friendlyarm_hd702e = {
	.modes = &friendlyarm_hd702e_mode,
	.num_modes = 1,
	.size = {
		.width	= 94,
		.height	= 151,
	},
};

static const struct drm_display_mode giantplus_gpg482739qs5_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 5,
	.hsync_end = 480 + 5 + 1,
	.htotal = 480 + 5 + 1 + 40,
	.vdisplay = 272,
	.vsync_start = 272 + 8,
	.vsync_end = 272 + 8 + 1,
	.vtotal = 272 + 8 + 1 + 8,
	.vrefresh = 60,
};

static const struct panel_desc giantplus_gpg482739qs5 = {
	.modes = &giantplus_gpg482739qs5_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct display_timing giantplus_gpm940b0_timing = {
	.pixelclock = { 13500000, 27000000, 27500000 },
	.hactive = { 320, 320, 320 },
	.hfront_porch = { 14, 686, 718 },
	.hback_porch = { 50, 70, 255 },
	.hsync_len = { 1, 1, 1 },
	.vactive = { 240, 240, 240 },
	.vfront_porch = { 1, 1, 179 },
	.vback_porch = { 1, 21, 31 },
	.vsync_len = { 1, 1, 6 },
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
};

static const struct panel_desc giantplus_gpm940b0 = {
	.timings = &giantplus_gpm940b0_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 60,
		.height = 45,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_3X8,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_NEGEDGE,
};

static const struct display_timing hannstar_hsd070pww1_timing = {
	.pixelclock = { 64300000, 71100000, 82000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 1, 1, 10 },
	.hback_porch = { 1, 1, 10 },
	/*
	 * According to the data sheet, the minimum horizontal blanking interval
	 * is 54 clocks (1 + 52 + 1), but tests with a Nitrogen6X have shown the
	 * minimum working horizontal blanking interval to be 60 clocks.
	 */
	.hsync_len = { 58, 158, 661 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 1, 1, 10 },
	.vback_porch = { 1, 1, 10 },
	.vsync_len = { 1, 21, 203 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc hannstar_hsd070pww1 = {
	.timings = &hannstar_hsd070pww1_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 151,
		.height = 94,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing hannstar_hsd100pxn1_timing = {
	.pixelclock = { 55000000, 65000000, 75000000 },
	.hactive = { 1024, 1024, 1024 },
	.hfront_porch = { 40, 40, 40 },
	.hback_porch = { 220, 220, 220 },
	.hsync_len = { 20, 60, 100 },
	.vactive = { 768, 768, 768 },
	.vfront_porch = { 7, 7, 7 },
	.vback_porch = { 21, 21, 21 },
	.vsync_len = { 10, 10, 10 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc hannstar_hsd100pxn1 = {
	.timings = &hannstar_hsd100pxn1_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 203,
		.height = 152,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode hitachi_tx23d38vm0caa_mode = {
	.clock = 33333,
	.hdisplay = 800,
	.hsync_start = 800 + 85,
	.hsync_end = 800 + 85 + 86,
	.htotal = 800 + 85 + 86 + 85,
	.vdisplay = 480,
	.vsync_start = 480 + 16,
	.vsync_end = 480 + 16 + 13,
	.vtotal = 480 + 16 + 13 + 16,
	.vrefresh = 60,
};

static const struct panel_desc hitachi_tx23d38vm0caa = {
	.modes = &hitachi_tx23d38vm0caa_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 195,
		.height = 117,
	},
	.delay = {
		.enable = 160,
		.disable = 160,
	},
};

static const struct drm_display_mode innolux_at043tn24_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 10,
	.vtotal = 272 + 2 + 10 + 2,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc innolux_at043tn24 = {
	.modes = &innolux_at043tn24_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct drm_display_mode innolux_at070tn92_mode = {
	.clock = 33333,
	.hdisplay = 800,
	.hsync_start = 800 + 210,
	.hsync_end = 800 + 210 + 20,
	.htotal = 800 + 210 + 20 + 46,
	.vdisplay = 480,
	.vsync_start = 480 + 22,
	.vsync_end = 480 + 22 + 10,
	.vtotal = 480 + 22 + 23 + 10,
	.vrefresh = 60,
};

static const struct panel_desc innolux_at070tn92 = {
	.modes = &innolux_at070tn92_mode,
	.num_modes = 1,
	.size = {
		.width = 154,
		.height = 86,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct display_timing innolux_g070y2_l01_timing = {
	.pixelclock = { 28000000, 29500000, 32000000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 61, 91, 141 },
	.hback_porch = { 60, 90, 140 },
	.hsync_len = { 12, 12, 12 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 4, 9, 30 },
	.vback_porch = { 4, 8, 28 },
	.vsync_len = { 2, 2, 2 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc innolux_g070y2_l01 = {
	.timings = &innolux_g070y2_l01_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.delay = {
		.prepare = 10,
		.enable = 100,
		.disable = 100,
		.unprepare = 800,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing innolux_g101ice_l01_timing = {
	.pixelclock = { 60400000, 71100000, 74700000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 41, 80, 100 },
	.hback_porch = { 40, 79, 99 },
	.hsync_len = { 1, 1, 1 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 5, 11, 14 },
	.vback_porch = { 4, 11, 14 },
	.vsync_len = { 1, 1, 1 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc innolux_g101ice_l01 = {
	.timings = &innolux_g101ice_l01_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 217,
		.height = 135,
	},
	.delay = {
		.enable = 200,
		.disable = 200,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing innolux_g121i1_l01_timing = {
	.pixelclock = { 67450000, 71000000, 74550000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 40, 80, 160 },
	.hback_porch = { 39, 79, 159 },
	.hsync_len = { 1, 1, 1 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 5, 11, 100 },
	.vback_porch = { 4, 11, 99 },
	.vsync_len = { 1, 1, 1 },
};

static const struct panel_desc innolux_g121i1_l01 = {
	.timings = &innolux_g121i1_l01_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 261,
		.height = 163,
	},
	.delay = {
		.enable = 200,
		.disable = 20,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode innolux_g121x1_l03_mode = {
	.clock = 65000,
	.hdisplay = 1024,
	.hsync_start = 1024 + 0,
	.hsync_end = 1024 + 1,
	.htotal = 1024 + 0 + 1 + 320,
	.vdisplay = 768,
	.vsync_start = 768 + 38,
	.vsync_end = 768 + 38 + 1,
	.vtotal = 768 + 38 + 1 + 0,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc innolux_g121x1_l03 = {
	.modes = &innolux_g121x1_l03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 246,
		.height = 185,
	},
	.delay = {
		.enable = 200,
		.unprepare = 200,
		.disable = 400,
	},
};

/*
 * Datasheet specifies that at 60 Hz refresh rate:
 * - total horizontal time: { 1506, 1592, 1716 }
 * - total vertical time: { 788, 800, 868 }
 *
 * ...but doesn't go into exactly how that should be split into a front
 * porch, back porch, or sync length.  For now we'll leave a single setting
 * here which allows a bit of tweaking of the pixel clock at the expense of
 * refresh rate.
 */
static const struct display_timing innolux_n116bge_timing = {
	.pixelclock = { 72600000, 76420000, 80240000 },
	.hactive = { 1366, 1366, 1366 },
	.hfront_porch = { 136, 136, 136 },
	.hback_porch = { 60, 60, 60 },
	.hsync_len = { 30, 30, 30 },
	.vactive = { 768, 768, 768 },
	.vfront_porch = { 8, 8, 8 },
	.vback_porch = { 12, 12, 12 },
	.vsync_len = { 12, 12, 12 },
	.flags = DISPLAY_FLAGS_VSYNC_LOW | DISPLAY_FLAGS_HSYNC_LOW,
};

static const struct panel_desc innolux_n116bge = {
	.timings = &innolux_n116bge_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
};

static const struct drm_display_mode innolux_n156bge_l21_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 16,
	.hsync_end = 1366 + 16 + 34,
	.htotal = 1366 + 16 + 34 + 50,
	.vdisplay = 768,
	.vsync_start = 768 + 2,
	.vsync_end = 768 + 2 + 6,
	.vtotal = 768 + 2 + 6 + 12,
	.vrefresh = 60,
};

static const struct panel_desc innolux_n156bge_l21 = {
	.modes = &innolux_n156bge_l21_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 344,
		.height = 193,
	},
};

static const struct drm_display_mode innolux_p120zdg_bf1_mode = {
	.clock = 206016,
	.hdisplay = 2160,
	.hsync_start = 2160 + 48,
	.hsync_end = 2160 + 48 + 32,
	.htotal = 2160 + 48 + 32 + 80,
	.vdisplay = 1440,
	.vsync_start = 1440 + 3,
	.vsync_end = 1440 + 3 + 10,
	.vtotal = 1440 + 3 + 10 + 27,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc innolux_p120zdg_bf1 = {
	.modes = &innolux_p120zdg_bf1_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 254,
		.height = 169,
	},
	.delay = {
		.hpd_absent_delay = 200,
		.unprepare = 500,
	},
};

static const struct drm_display_mode innolux_zj070na_01p_mode = {
	.clock = 51501,
	.hdisplay = 1024,
	.hsync_start = 1024 + 128,
	.hsync_end = 1024 + 128 + 64,
	.htotal = 1024 + 128 + 64 + 128,
	.vdisplay = 600,
	.vsync_start = 600 + 16,
	.vsync_end = 600 + 16 + 4,
	.vtotal = 600 + 16 + 4 + 16,
	.vrefresh = 60,
};

static const struct panel_desc innolux_zj070na_01p = {
	.modes = &innolux_zj070na_01p_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 90,
	},
};

static const struct display_timing koe_tx14d24vm1bpa_timing = {
	.pixelclock = { 5580000, 5850000, 6200000 },
	.hactive = { 320, 320, 320 },
	.hfront_porch = { 30, 30, 30 },
	.hback_porch = { 30, 30, 30 },
	.hsync_len = { 1, 5, 17 },
	.vactive = { 240, 240, 240 },
	.vfront_porch = { 6, 6, 6 },
	.vback_porch = { 5, 5, 5 },
	.vsync_len = { 1, 2, 11 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc koe_tx14d24vm1bpa = {
	.timings = &koe_tx14d24vm1bpa_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 115,
		.height = 86,
	},
};

static const struct display_timing koe_tx31d200vm0baa_timing = {
	.pixelclock = { 39600000, 43200000, 48000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 16, 36, 56 },
	.hback_porch = { 16, 36, 56 },
	.hsync_len = { 8, 8, 8 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 6, 21, 33 },
	.vback_porch = { 6, 21, 33 },
	.vsync_len = { 8, 8, 8 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc koe_tx31d200vm0baa = {
	.timings = &koe_tx31d200vm0baa_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 292,
		.height = 109,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing kyo_tcg121xglp_timing = {
	.pixelclock = { 52000000, 65000000, 71000000 },
	.hactive = { 1024, 1024, 1024 },
	.hfront_porch = { 2, 2, 2 },
	.hback_porch = { 2, 2, 2 },
	.hsync_len = { 86, 124, 244 },
	.vactive = { 768, 768, 768 },
	.vfront_porch = { 2, 2, 2 },
	.vback_porch = { 2, 2, 2 },
	.vsync_len = { 6, 34, 73 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc kyo_tcg121xglp = {
	.timings = &kyo_tcg121xglp_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 246,
		.height = 184,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode lemaker_bl035_rgb_002_mode = {
	.clock = 7000,
	.hdisplay = 320,
	.hsync_start = 320 + 20,
	.hsync_end = 320 + 20 + 30,
	.htotal = 320 + 20 + 30 + 38,
	.vdisplay = 240,
	.vsync_start = 240 + 4,
	.vsync_end = 240 + 4 + 3,
	.vtotal = 240 + 4 + 3 + 15,
	.vrefresh = 60,
};

static const struct panel_desc lemaker_bl035_rgb_002 = {
	.modes = &lemaker_bl035_rgb_002_mode,
	.num_modes = 1,
	.size = {
		.width = 70,
		.height = 52,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_LOW,
};

static const struct drm_display_mode lg_lb070wv8_mode = {
	.clock = 33246,
	.hdisplay = 800,
	.hsync_start = 800 + 88,
	.hsync_end = 800 + 88 + 80,
	.htotal = 800 + 88 + 80 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 25,
	.vtotal = 480 + 10 + 25 + 10,
	.vrefresh = 60,
};

static const struct panel_desc lg_lb070wv8 = {
	.modes = &lg_lb070wv8_mode,
	.num_modes = 1,
	.bpc = 16,
	.size = {
		.width = 151,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode lg_lp079qx1_sp0v_mode = {
	.clock = 200000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 12,
	.hsync_end = 1536 + 12 + 16,
	.htotal = 1536 + 12 + 16 + 48,
	.vdisplay = 2048,
	.vsync_start = 2048 + 8,
	.vsync_end = 2048 + 8 + 4,
	.vtotal = 2048 + 8 + 4 + 8,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc lg_lp079qx1_sp0v = {
	.modes = &lg_lp079qx1_sp0v_mode,
	.num_modes = 1,
	.size = {
		.width = 129,
		.height = 171,
	},
};

static const struct drm_display_mode lg_lp097qx1_spa1_mode = {
	.clock = 205210,
	.hdisplay = 2048,
	.hsync_start = 2048 + 150,
	.hsync_end = 2048 + 150 + 5,
	.htotal = 2048 + 150 + 5 + 5,
	.vdisplay = 1536,
	.vsync_start = 1536 + 3,
	.vsync_end = 1536 + 3 + 1,
	.vtotal = 1536 + 3 + 1 + 9,
	.vrefresh = 60,
};

static const struct panel_desc lg_lp097qx1_spa1 = {
	.modes = &lg_lp097qx1_spa1_mode,
	.num_modes = 1,
	.size = {
		.width = 208,
		.height = 147,
	},
};

static const struct drm_display_mode lg_lp120up1_mode = {
	.clock = 162300,
	.hdisplay = 1920,
	.hsync_start = 1920 + 40,
	.hsync_end = 1920 + 40 + 40,
	.htotal = 1920 + 40 + 40+ 80,
	.vdisplay = 1280,
	.vsync_start = 1280 + 4,
	.vsync_end = 1280 + 4 + 4,
	.vtotal = 1280 + 4 + 4 + 12,
	.vrefresh = 60,
};

static const struct panel_desc lg_lp120up1 = {
	.modes = &lg_lp120up1_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 267,
		.height = 183,
	},
};

static const struct drm_display_mode lg_lp129qe_mode = {
	.clock = 285250,
	.hdisplay = 2560,
	.hsync_start = 2560 + 48,
	.hsync_end = 2560 + 48 + 32,
	.htotal = 2560 + 48 + 32 + 80,
	.vdisplay = 1700,
	.vsync_start = 1700 + 3,
	.vsync_end = 1700 + 3 + 10,
	.vtotal = 1700 + 3 + 10 + 36,
	.vrefresh = 60,
};

static const struct panel_desc lg_lp129qe = {
	.modes = &lg_lp129qe_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 272,
		.height = 181,
	},
};

static const struct drm_display_mode mitsubishi_aa070mc01_mode = {
	.clock = 30400,
	.hdisplay = 800,
	.hsync_start = 800 + 0,
	.hsync_end = 800 + 1,
	.htotal = 800 + 0 + 1 + 160,
	.vdisplay = 480,
	.vsync_start = 480 + 0,
	.vsync_end = 480 + 48 + 1,
	.vtotal = 480 + 48 + 1 + 0,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct drm_display_mode logicpd_type_28_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 3,
	.hsync_end = 480 + 3 + 42,
	.htotal = 480 + 3 + 42 + 2,

	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 11,
	.vtotal = 272 + 2 + 11 + 3,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc logicpd_type_28 = {
	.modes = &logicpd_type_28_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 105,
		.height = 67,
	},
	.delay = {
		.prepare = 200,
		.enable = 200,
		.unprepare = 200,
		.disable = 200,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE |
		     DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE,
};

static const struct panel_desc mitsubishi_aa070mc01 = {
	.modes = &mitsubishi_aa070mc01_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 152,
		.height = 91,
	},

	.delay = {
		.enable = 200,
		.unprepare = 200,
		.disable = 400,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH,
};

static const struct display_timing nec_nl12880bc20_05_timing = {
	.pixelclock = { 67000000, 71000000, 75000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 2, 30, 30 },
	.hback_porch = { 6, 100, 100 },
	.hsync_len = { 2, 30, 30 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 5, 5, 5 },
	.vback_porch = { 11, 11, 11 },
	.vsync_len = { 7, 7, 7 },
};

static const struct panel_desc nec_nl12880bc20_05 = {
	.timings = &nec_nl12880bc20_05_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 261,
		.height = 163,
	},
	.delay = {
		.enable = 50,
		.disable = 50,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode nec_nl4827hc19_05b_mode = {
	.clock = 10870,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 4,
	.vtotal = 272 + 2 + 4 + 2,
	.vrefresh = 74,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc nec_nl4827hc19_05b = {
	.modes = &nec_nl4827hc19_05b_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct drm_display_mode netron_dy_e231732_mode = {
	.clock = 66000,
	.hdisplay = 1024,
	.hsync_start = 1024 + 160,
	.hsync_end = 1024 + 160 + 70,
	.htotal = 1024 + 160 + 70 + 90,
	.vdisplay = 600,
	.vsync_start = 600 + 127,
	.vsync_end = 600 + 127 + 20,
	.vtotal = 600 + 127 + 20 + 3,
	.vrefresh = 60,
};

static const struct panel_desc netron_dy_e231732 = {
	.modes = &netron_dy_e231732_mode,
	.num_modes = 1,
	.size = {
		.width = 154,
		.height = 87,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode newhaven_nhd_43_480272ef_atxl_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 10,
	.vtotal = 272 + 2 + 10 + 2,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc newhaven_nhd_43_480272ef_atxl = {
	.modes = &newhaven_nhd_43_480272ef_atxl_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE |
		     DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE,
};

static const struct display_timing nlt_nl192108ac18_02d_timing = {
	.pixelclock = { 130000000, 148350000, 163000000 },
	.hactive = { 1920, 1920, 1920 },
	.hfront_porch = { 80, 100, 100 },
	.hback_porch = { 100, 120, 120 },
	.hsync_len = { 50, 60, 60 },
	.vactive = { 1080, 1080, 1080 },
	.vfront_porch = { 12, 30, 30 },
	.vback_porch = { 4, 10, 10 },
	.vsync_len = { 4, 5, 5 },
};

static const struct panel_desc nlt_nl192108ac18_02d = {
	.timings = &nlt_nl192108ac18_02d_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 344,
		.height = 194,
	},
	.delay = {
		.unprepare = 500,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode nvd_9128_mode = {
	.clock = 29500,
	.hdisplay = 800,
	.hsync_start = 800 + 130,
	.hsync_end = 800 + 130 + 98,
	.htotal = 800 + 0 + 130 + 98,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 50,
	.vtotal = 480 + 0 + 10 + 50,
};

static const struct panel_desc nvd_9128 = {
	.modes = &nvd_9128_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 156,
		.height = 88,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing okaya_rs800480t_7x0gp_timing = {
	.pixelclock = { 30000000, 30000000, 40000000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 40, 40, 40 },
	.hback_porch = { 40, 40, 40 },
	.hsync_len = { 1, 48, 48 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 13, 13, 13 },
	.vback_porch = { 29, 29, 29 },
	.vsync_len = { 3, 3, 3 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc okaya_rs800480t_7x0gp = {
	.timings = &okaya_rs800480t_7x0gp_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 87,
	},
	.delay = {
		.prepare = 41,
		.enable = 50,
		.unprepare = 41,
		.disable = 50,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode olimex_lcd_olinuxino_43ts_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 5,
	.hsync_end = 480 + 5 + 30,
	.htotal = 480 + 5 + 30 + 10,
	.vdisplay = 272,
	.vsync_start = 272 + 8,
	.vsync_end = 272 + 8 + 5,
	.vtotal = 272 + 8 + 5 + 3,
	.vrefresh = 60,
};

static const struct panel_desc olimex_lcd_olinuxino_43ts = {
	.modes = &olimex_lcd_olinuxino_43ts_mode,
	.num_modes = 1,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

/*
 * 800x480 CVT. The panel appears to be quite accepting, at least as far as
 * pixel clocks, but this is the timing that was being used in the Adafruit
 * installation instructions.
 */
static const struct drm_display_mode ontat_yx700wv03_mode = {
	.clock = 29500,
	.hdisplay = 800,
	.hsync_start = 824,
	.hsync_end = 896,
	.htotal = 992,
	.vdisplay = 480,
	.vsync_start = 483,
	.vsync_end = 493,
	.vtotal = 500,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

/*
 * Specification at:
 * https://www.adafruit.com/images/product-files/2406/c3163.pdf
 */
static const struct panel_desc ontat_yx700wv03 = {
	.modes = &ontat_yx700wv03_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 154,
		.height = 83,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode ortustech_com37h3m_mode  = {
	.clock = 22153,
	.hdisplay = 480,
	.hsync_start = 480 + 8,
	.hsync_end = 480 + 8 + 10,
	.htotal = 480 + 8 + 10 + 10,
	.vdisplay = 640,
	.vsync_start = 640 + 4,
	.vsync_end = 640 + 4 + 3,
	.vtotal = 640 + 4 + 3 + 4,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc ortustech_com37h3m = {
	.modes = &ortustech_com37h3m_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 56,	/* 56.16mm */
		.height = 75,	/* 74.88mm */
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_POSEDGE |
		     DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE,
};

static const struct drm_display_mode ortustech_com43h4m85ulc_mode  = {
	.clock = 25000,
	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 10,
	.htotal = 480 + 10 + 10 + 15,
	.vdisplay = 800,
	.vsync_start = 800 + 3,
	.vsync_end = 800 + 3 + 3,
	.vtotal = 800 + 3 + 3 + 3,
	.vrefresh = 60,
};

static const struct panel_desc ortustech_com43h4m85ulc = {
	.modes = &ortustech_com43h4m85ulc_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 56,
		.height = 93,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct drm_display_mode osddisplays_osd070t1718_19ts_mode  = {
	.clock = 33000,
	.hdisplay = 800,
	.hsync_start = 800 + 210,
	.hsync_end = 800 + 210 + 30,
	.htotal = 800 + 210 + 30 + 16,
	.vdisplay = 480,
	.vsync_start = 480 + 22,
	.vsync_end = 480 + 22 + 13,
	.vtotal = 480 + 22 + 13 + 10,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc osddisplays_osd070t1718_19ts = {
	.modes = &osddisplays_osd070t1718_19ts_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	.connector_type = DRM_MODE_CONNECTOR_DPI,
};

static const struct drm_display_mode pda_91_00156_a0_mode = {
	.clock = 33300,
	.hdisplay = 800,
	.hsync_start = 800 + 1,
	.hsync_end = 800 + 1 + 64,
	.htotal = 800 + 1 + 64 + 64,
	.vdisplay = 480,
	.vsync_start = 480 + 1,
	.vsync_end = 480 + 1 + 23,
	.vtotal = 480 + 1 + 23 + 22,
	.vrefresh = 60,
};

static const struct panel_desc pda_91_00156_a0  = {
	.modes = &pda_91_00156_a0_mode,
	.num_modes = 1,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};


static const struct drm_display_mode qd43003c0_40_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 8,
	.hsync_end = 480 + 8 + 4,
	.htotal = 480 + 8 + 4 + 39,
	.vdisplay = 272,
	.vsync_start = 272 + 4,
	.vsync_end = 272 + 4 + 10,
	.vtotal = 272 + 4 + 10 + 2,
	.vrefresh = 60,
};

static const struct panel_desc qd43003c0_40 = {
	.modes = &qd43003c0_40_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 53,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct display_timing rocktech_rk070er9427_timing = {
	.pixelclock = { 26400000, 33300000, 46800000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 16, 210, 354 },
	.hback_porch = { 46, 46, 46 },
	.hsync_len = { 1, 1, 1 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 7, 22, 147 },
	.vback_porch = { 23, 23, 23 },
	.vsync_len = { 1, 1, 1 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc rocktech_rk070er9427 = {
	.timings = &rocktech_rk070er9427_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 86,
	},
	.delay = {
		.prepare = 41,
		.enable = 50,
		.unprepare = 41,
		.disable = 50,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode samsung_lsn122dl01_c01_mode = {
	.clock = 271560,
	.hdisplay = 2560,
	.hsync_start = 2560 + 48,
	.hsync_end = 2560 + 48 + 32,
	.htotal = 2560 + 48 + 32 + 80,
	.vdisplay = 1600,
	.vsync_start = 1600 + 2,
	.vsync_end = 1600 + 2 + 5,
	.vtotal = 1600 + 2 + 5 + 57,
	.vrefresh = 60,
};

static const struct panel_desc samsung_lsn122dl01_c01 = {
	.modes = &samsung_lsn122dl01_c01_mode,
	.num_modes = 1,
	.size = {
		.width = 263,
		.height = 164,
	},
};

static const struct drm_display_mode samsung_ltn101nt05_mode = {
	.clock = 54030,
	.hdisplay = 1024,
	.hsync_start = 1024 + 24,
	.hsync_end = 1024 + 24 + 136,
	.htotal = 1024 + 24 + 136 + 160,
	.vdisplay = 600,
	.vsync_start = 600 + 3,
	.vsync_end = 600 + 3 + 6,
	.vtotal = 600 + 3 + 6 + 61,
	.vrefresh = 60,
};

static const struct panel_desc samsung_ltn101nt05 = {
	.modes = &samsung_ltn101nt05_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode samsung_ltn140at29_301_mode = {
	.clock = 76300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 64,
	.hsync_end = 1366 + 64 + 48,
	.htotal = 1366 + 64 + 48 + 128,
	.vdisplay = 768,
	.vsync_start = 768 + 2,
	.vsync_end = 768 + 2 + 5,
	.vtotal = 768 + 2 + 5 + 17,
	.vrefresh = 60,
};

static const struct panel_desc samsung_ltn140at29_301 = {
	.modes = &samsung_ltn140at29_301_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 320,
		.height = 187,
	},
};

static const struct display_timing satoz_sat050at40h12r2_timing = {
	.pixelclock = {33300000, 33300000, 50000000},
	.hactive = {800, 800, 800},
	.hfront_porch = {16, 210, 354},
	.hback_porch = {46, 46, 46},
	.hsync_len = {1, 1, 40},
	.vactive = {480, 480, 480},
	.vfront_porch = {7, 22, 147},
	.vback_porch = {23, 23, 23},
	.vsync_len = {1, 1, 20},
};

static const struct panel_desc satoz_sat050at40h12r2 = {
	.timings = &satoz_sat050at40h12r2_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 108,
		.height = 65,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode sharp_ld_d5116z01b_mode = {
	.clock = 168480,
	.hdisplay = 1920,
	.hsync_start = 1920 + 48,
	.hsync_end = 1920 + 48 + 32,
	.htotal = 1920 + 48 + 32 + 80,
	.vdisplay = 1280,
	.vsync_start = 1280 + 3,
	.vsync_end = 1280 + 3 + 10,
	.vtotal = 1280 + 3 + 10 + 57,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc sharp_ld_d5116z01b = {
	.modes = &sharp_ld_d5116z01b_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 260,
		.height = 120,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DATA_MSB_TO_LSB,
};

static const struct drm_display_mode sharp_lq070y3dg3b_mode = {
	.clock = 33260,
	.hdisplay = 800,
	.hsync_start = 800 + 64,
	.hsync_end = 800 + 64 + 128,
	.htotal = 800 + 64 + 128 + 64,
	.vdisplay = 480,
	.vsync_start = 480 + 8,
	.vsync_end = 480 + 8 + 2,
	.vtotal = 480 + 8 + 2 + 35,
	.vrefresh = 60,
	.flags = DISPLAY_FLAGS_PIXDATA_POSEDGE,
};

static const struct panel_desc sharp_lq070y3dg3b = {
	.modes = &sharp_lq070y3dg3b_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 152,	/* 152.4mm */
		.height = 91,	/* 91.4mm */
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_POSEDGE |
		     DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE,
};

static const struct drm_display_mode sharp_lq035q7db03_mode = {
	.clock = 5500,
	.hdisplay = 240,
	.hsync_start = 240 + 16,
	.hsync_end = 240 + 16 + 7,
	.htotal = 240 + 16 + 7 + 5,
	.vdisplay = 320,
	.vsync_start = 320 + 9,
	.vsync_end = 320 + 9 + 1,
	.vtotal = 320 + 9 + 1 + 7,
	.vrefresh = 60,
};

static const struct panel_desc sharp_lq035q7db03 = {
	.modes = &sharp_lq035q7db03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 54,
		.height = 72,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct display_timing sharp_lq101k1ly04_timing = {
	.pixelclock = { 60000000, 65000000, 80000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 20, 20, 20 },
	.hback_porch = { 20, 20, 20 },
	.hsync_len = { 10, 10, 10 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 4, 4, 4 },
	.vback_porch = { 4, 4, 4 },
	.vsync_len = { 4, 4, 4 },
	.flags = DISPLAY_FLAGS_PIXDATA_POSEDGE,
};

static const struct panel_desc sharp_lq101k1ly04 = {
	.timings = &sharp_lq101k1ly04_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 217,
		.height = 136,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing sharp_lq123p1jx31_timing = {
	.pixelclock = { 252750000, 252750000, 266604720 },
	.hactive = { 2400, 2400, 2400 },
	.hfront_porch = { 48, 48, 48 },
	.hback_porch = { 80, 80, 84 },
	.hsync_len = { 32, 32, 32 },
	.vactive = { 1600, 1600, 1600 },
	.vfront_porch = { 3, 3, 3 },
	.vback_porch = { 33, 33, 120 },
	.vsync_len = { 10, 10, 10 },
	.flags = DISPLAY_FLAGS_VSYNC_LOW | DISPLAY_FLAGS_HSYNC_LOW,
};

static const struct panel_desc sharp_lq123p1jx31 = {
	.timings = &sharp_lq123p1jx31_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 259,
		.height = 173,
	},
	.delay = {
		.prepare = 110,
		.enable = 50,
		.unprepare = 550,
	},
};

static const struct drm_display_mode sharp_lq150x1lg11_mode = {
	.clock = 71100,
	.hdisplay = 1024,
	.hsync_start = 1024 + 168,
	.hsync_end = 1024 + 168 + 64,
	.htotal = 1024 + 168 + 64 + 88,
	.vdisplay = 768,
	.vsync_start = 768 + 37,
	.vsync_end = 768 + 37 + 2,
	.vtotal = 768 + 37 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc sharp_lq150x1lg11 = {
	.modes = &sharp_lq150x1lg11_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 304,
		.height = 228,
	},
	.bus_format = MEDIA_BUS_FMT_RGB565_1X16,
};

static const struct display_timing sharp_ls020b1dd01d_timing = {
	.pixelclock = { 2000000, 4200000, 5000000 },
	.hactive = { 240, 240, 240 },
	.hfront_porch = { 66, 66, 66 },
	.hback_porch = { 1, 1, 1 },
	.hsync_len = { 1, 1, 1 },
	.vactive = { 160, 160, 160 },
	.vfront_porch = { 52, 52, 52 },
	.vback_porch = { 6, 6, 6 },
	.vsync_len = { 10, 10, 10 },
	.flags = DISPLAY_FLAGS_HSYNC_HIGH | DISPLAY_FLAGS_VSYNC_LOW,
};

static const struct panel_desc sharp_ls020b1dd01d = {
	.timings = &sharp_ls020b1dd01d_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 42,
		.height = 28,
	},
	.bus_format = MEDIA_BUS_FMT_RGB565_1X16,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH
		   | DRM_BUS_FLAG_PIXDATA_NEGEDGE
		   | DRM_BUS_FLAG_SHARP_SIGNALS,
};

static const struct drm_display_mode shelly_sca07010_bfn_lnn_mode = {
	.clock = 33300,
	.hdisplay = 800,
	.hsync_start = 800 + 1,
	.hsync_end = 800 + 1 + 64,
	.htotal = 800 + 1 + 64 + 64,
	.vdisplay = 480,
	.vsync_start = 480 + 1,
	.vsync_end = 480 + 1 + 23,
	.vtotal = 480 + 1 + 23 + 22,
	.vrefresh = 60,
};

static const struct panel_desc shelly_sca07010_bfn_lnn = {
	.modes = &shelly_sca07010_bfn_lnn_mode,
	.num_modes = 1,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode starry_kr122ea0sra_mode = {
	.clock = 147000,
	.hdisplay = 1920,
	.hsync_start = 1920 + 16,
	.hsync_end = 1920 + 16 + 16,
	.htotal = 1920 + 16 + 16 + 32,
	.vdisplay = 1200,
	.vsync_start = 1200 + 15,
	.vsync_end = 1200 + 15 + 2,
	.vtotal = 1200 + 15 + 2 + 18,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc starry_kr122ea0sra = {
	.modes = &starry_kr122ea0sra_mode,
	.num_modes = 1,
	.size = {
		.width = 263,
		.height = 164,
	},
	.delay = {
		.prepare = 10 + 200,
		.enable = 50,
		.unprepare = 10 + 500,
	},
};

static const struct drm_display_mode tfc_s9700rtwv43tr_01b_mode = {
	.clock = 30000,
	.hdisplay = 800,
	.hsync_start = 800 + 39,
	.hsync_end = 800 + 39 + 47,
	.htotal = 800 + 39 + 47 + 39,
	.vdisplay = 480,
	.vsync_start = 480 + 13,
	.vsync_end = 480 + 13 + 2,
	.vtotal = 480 + 13 + 2 + 29,
	.vrefresh = 62,
};

static const struct panel_desc tfc_s9700rtwv43tr_01b = {
	.modes = &tfc_s9700rtwv43tr_01b_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 155,
		.height = 90,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_POSEDGE,
};

static const struct display_timing tianma_tm070jdhg30_timing = {
	.pixelclock = { 62600000, 68200000, 78100000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 15, 64, 159 },
	.hback_porch = { 5, 5, 5 },
	.hsync_len = { 1, 1, 256 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 3, 40, 99 },
	.vback_porch = { 2, 2, 2 },
	.vsync_len = { 1, 1, 128 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc tianma_tm070jdhg30 = {
	.timings = &tianma_tm070jdhg30_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 151,
		.height = 95,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct display_timing tianma_tm070rvhg71_timing = {
	.pixelclock = { 27700000, 29200000, 39600000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 12, 40, 212 },
	.hback_porch = { 88, 88, 88 },
	.hsync_len = { 1, 1, 40 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 1, 13, 88 },
	.vback_porch = { 32, 32, 32 },
	.vsync_len = { 1, 1, 3 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc tianma_tm070rvhg71 = {
	.timings = &tianma_tm070rvhg71_timing,
	.num_timings = 1,
	.bpc = 8,
	.size = {
		.width = 154,
		.height = 86,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode ti_nspire_cx_lcd_mode[] = {
	{
		.clock = 10000,
		.hdisplay = 320,
		.hsync_start = 320 + 50,
		.hsync_end = 320 + 50 + 6,
		.htotal = 320 + 50 + 6 + 38,
		.vdisplay = 240,
		.vsync_start = 240 + 3,
		.vsync_end = 240 + 3 + 1,
		.vtotal = 240 + 3 + 1 + 17,
		.vrefresh = 60,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	},
};

static const struct panel_desc ti_nspire_cx_lcd_panel = {
	.modes = ti_nspire_cx_lcd_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 65,
		.height = 49,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_NEGEDGE,
};

static const struct drm_display_mode ti_nspire_classic_lcd_mode[] = {
	{
		.clock = 10000,
		.hdisplay = 320,
		.hsync_start = 320 + 6,
		.hsync_end = 320 + 6 + 6,
		.htotal = 320 + 6 + 6 + 6,
		.vdisplay = 240,
		.vsync_start = 240 + 0,
		.vsync_end = 240 + 0 + 1,
		.vtotal = 240 + 0 + 1 + 0,
		.vrefresh = 60,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	},
};

static const struct panel_desc ti_nspire_classic_lcd_panel = {
	.modes = ti_nspire_classic_lcd_mode,
	.num_modes = 1,
	/* The grayscale panel has 8 bit for the color .. Y (black) */
	.bpc = 8,
	.size = {
		.width = 71,
		.height = 53,
	},
	/* This is the grayscale bus format */
	.bus_format = MEDIA_BUS_FMT_Y8_1X8,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_POSEDGE,
};

static const struct drm_display_mode toshiba_lt089ac29000_mode = {
	.clock = 79500,
	.hdisplay = 1280,
	.hsync_start = 1280 + 192,
	.hsync_end = 1280 + 192 + 128,
	.htotal = 1280 + 192 + 128 + 64,
	.vdisplay = 768,
	.vsync_start = 768 + 20,
	.vsync_end = 768 + 20 + 7,
	.vtotal = 768 + 20 + 7 + 3,
	.vrefresh = 60,
};

static const struct panel_desc toshiba_lt089ac29000 = {
	.modes = &toshiba_lt089ac29000_mode,
	.num_modes = 1,
	.size = {
		.width = 194,
		.height = 116,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct drm_display_mode tpk_f07a_0102_mode = {
	.clock = 33260,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 128,
	.htotal = 800 + 40 + 128 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 2,
	.vtotal = 480 + 10 + 2 + 33,
	.vrefresh = 60,
};

static const struct panel_desc tpk_f07a_0102 = {
	.modes = &tpk_f07a_0102_mode,
	.num_modes = 1,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE,
};

static const struct drm_display_mode tpk_f10a_0102_mode = {
	.clock = 45000,
	.hdisplay = 1024,
	.hsync_start = 1024 + 176,
	.hsync_end = 1024 + 176 + 5,
	.htotal = 1024 + 176 + 5 + 88,
	.vdisplay = 600,
	.vsync_start = 600 + 20,
	.vsync_end = 600 + 20 + 5,
	.vtotal = 600 + 20 + 5 + 25,
	.vrefresh = 60,
};

static const struct panel_desc tpk_f10a_0102 = {
	.modes = &tpk_f10a_0102_mode,
	.num_modes = 1,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct display_timing urt_umsh_8596md_timing = {
	.pixelclock = { 33260000, 33260000, 33260000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 41, 41, 41 },
	.hback_porch = { 216 - 128, 216 - 128, 216 - 128 },
	.hsync_len = { 71, 128, 128 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 10, 10, 10 },
	.vback_porch = { 35 - 2, 35 - 2, 35 - 2 },
	.vsync_len = { 2, 2, 2 },
	.flags = DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_NEGEDGE |
		DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
};

static const struct panel_desc urt_umsh_8596md_lvds = {
	.timings = &urt_umsh_8596md_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
	.connector_type = DRM_MODE_CONNECTOR_LVDS,
};

static const struct panel_desc urt_umsh_8596md_parallel = {
	.timings = &urt_umsh_8596md_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode vl050_8048nt_c01_mode = {
	.clock = 33333,
	.hdisplay = 800,
	.hsync_start = 800 + 210,
	.hsync_end = 800 + 210 + 20,
	.htotal = 800 + 210 + 20 + 46,
	.vdisplay =  480,
	.vsync_start = 480 + 22,
	.vsync_end = 480 + 22 + 10,
	.vtotal = 480 + 22 + 10 + 23,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc vl050_8048nt_c01 = {
	.modes = &vl050_8048nt_c01_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 120,
		.height = 76,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_POSEDGE,
};

static const struct drm_display_mode winstar_wf35ltiacd_mode = {
	.clock = 6410,
	.hdisplay = 320,
	.hsync_start = 320 + 20,
	.hsync_end = 320 + 20 + 30,
	.htotal = 320 + 20 + 30 + 38,
	.vdisplay = 240,
	.vsync_start = 240 + 4,
	.vsync_end = 240 + 4 + 3,
	.vtotal = 240 + 4 + 3 + 15,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc winstar_wf35ltiacd = {
	.modes = &winstar_wf35ltiacd_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 70,
		.height = 53,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct drm_display_mode arm_rtsm_mode[] = {
	{
		.clock = 65000,
		.hdisplay = 1024,
		.hsync_start = 1024 + 24,
		.hsync_end = 1024 + 24 + 136,
		.htotal = 1024 + 24 + 136 + 160,
		.vdisplay = 768,
		.vsync_start = 768 + 3,
		.vsync_end = 768 + 3 + 6,
		.vtotal = 768 + 3 + 6 + 29,
		.vrefresh = 60,
		.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
	},
};

static const struct panel_desc arm_rtsm = {
	.modes = arm_rtsm_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 400,
		.height = 300,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct of_device_id platform_of_match[] = {
	{
		.compatible = "ampire,am-480272h3tmqw-t01h",
		.data = &ampire_am_480272h3tmqw_t01h,
	}, {
		.compatible = "ampire,am800480r3tmqwa1h",
		.data = &ampire_am800480r3tmqwa1h,
	}, {
		.compatible = "arm,rtsm-display",
		.data = &arm_rtsm,
	}, {
		.compatible = "armadeus,st0700-adapt",
		.data = &armadeus_st0700_adapt,
	}, {
		.compatible = "auo,b101aw03",
		.data = &auo_b101aw03,
	}, {
		.compatible = "auo,b101ean01",
		.data = &auo_b101ean01,
	}, {
		.compatible = "auo,b101xtn01",
		.data = &auo_b101xtn01,
	}, {
		.compatible = "auo,b116xa01",
		.data = &auo_b116xak01,
	}, {
		.compatible = "auo,b116xw03",
		.data = &auo_b116xw03,
	}, {
		.compatible = "auo,b133htn01",
		.data = &auo_b133htn01,
	}, {
		.compatible = "auo,b133xtn01",
		.data = &auo_b133xtn01,
	}, {
		.compatible = "auo,g070vvn01",
		.data = &auo_g070vvn01,
	}, {
		.compatible = "auo,g101evn010",
		.data = &auo_g101evn010,
	}, {
		.compatible = "auo,g104sn02",
		.data = &auo_g104sn02,
	}, {
		.compatible = "auo,g133han01",
		.data = &auo_g133han01,
	}, {
		.compatible = "auo,g185han01",
		.data = &auo_g185han01,
	}, {
		.compatible = "auo,p320hvn03",
		.data = &auo_p320hvn03,
	}, {
		.compatible = "auo,t215hvn01",
		.data = &auo_t215hvn01,
	}, {
		.compatible = "avic,tm070ddh03",
		.data = &avic_tm070ddh03,
	}, {
		.compatible = "bananapi,s070wv20-ct16",
		.data = &bananapi_s070wv20_ct16,
	}, {
		.compatible = "boe,hv070wsa-100",
		.data = &boe_hv070wsa
	}, {
		.compatible = "boe,nv101wxmn51",
		.data = &boe_nv101wxmn51,
	}, {
		.compatible = "boe,nv140fhmn49",
		.data = &boe_nv140fhmn49,
	}, {
		.compatible = "cdtech,s043wq26h-ct7",
		.data = &cdtech_s043wq26h_ct7,
	}, {
		.compatible = "cdtech,s070wv95-ct16",
		.data = &cdtech_s070wv95_ct16,
	}, {
		.compatible = "chunghwa,claa070wp03xg",
		.data = &chunghwa_claa070wp03xg,
	}, {
		.compatible = "chunghwa,claa101wa01a",
		.data = &chunghwa_claa101wa01a
	}, {
		.compatible = "chunghwa,claa101wb01",
		.data = &chunghwa_claa101wb01
	}, {
		.compatible = "dataimage,scf0700c48ggu18",
		.data = &dataimage_scf0700c48ggu18,
	}, {
		.compatible = "dlc,dlc0700yzg-1",
		.data = &dlc_dlc0700yzg_1,
	}, {
		.compatible = "dlc,dlc1010gig",
		.data = &dlc_dlc1010gig,
	}, {
		.compatible = "edt,et035012dm6",
		.data = &edt_et035012dm6,
	}, {
		.compatible = "edt,etm0430g0dh6",
		.data = &edt_etm0430g0dh6,
	}, {
		.compatible = "edt,et057090dhu",
		.data = &edt_et057090dhu,
	}, {
		.compatible = "edt,et070080dh6",
		.data = &edt_etm0700g0dh6,
	}, {
		.compatible = "edt,etm0700g0dh6",
		.data = &edt_etm0700g0dh6,
	}, {
		.compatible = "edt,etm0700g0bdh6",
		.data = &edt_etm0700g0bdh6,
	}, {
		.compatible = "edt,etm0700g0edh6",
		.data = &edt_etm0700g0bdh6,
	}, {
		.compatible = "evervision,vgg804821",
		.data = &evervision_vgg804821,
	}, {
		.compatible = "foxlink,fl500wvr00-a0t",
		.data = &foxlink_fl500wvr00_a0t,
	}, {
		.compatible = "friendlyarm,hd702e",
		.data = &friendlyarm_hd702e,
	}, {
		.compatible = "giantplus,gpg482739qs5",
		.data = &giantplus_gpg482739qs5
	}, {
		.compatible = "giantplus,gpm940b0",
		.data = &giantplus_gpm940b0,
	}, {
		.compatible = "hannstar,hsd070pww1",
		.data = &hannstar_hsd070pww1,
	}, {
		.compatible = "hannstar,hsd100pxn1",
		.data = &hannstar_hsd100pxn1,
	}, {
		.compatible = "hit,tx23d38vm0caa",
		.data = &hitachi_tx23d38vm0caa
	}, {
		.compatible = "innolux,at043tn24",
		.data = &innolux_at043tn24,
	}, {
		.compatible = "innolux,at070tn92",
		.data = &innolux_at070tn92,
	}, {
		.compatible = "innolux,g070y2-l01",
		.data = &innolux_g070y2_l01,
	}, {
		.compatible = "innolux,g101ice-l01",
		.data = &innolux_g101ice_l01
	}, {
		.compatible = "innolux,g121i1-l01",
		.data = &innolux_g121i1_l01
	}, {
		.compatible = "innolux,g121x1-l03",
		.data = &innolux_g121x1_l03,
	}, {
		.compatible = "innolux,n116bge",
		.data = &innolux_n116bge,
	}, {
		.compatible = "innolux,n156bge-l21",
		.data = &innolux_n156bge_l21,
	}, {
		.compatible = "innolux,p120zdg-bf1",
		.data = &innolux_p120zdg_bf1,
	}, {
		.compatible = "innolux,zj070na-01p",
		.data = &innolux_zj070na_01p,
	}, {
		.compatible = "koe,tx14d24vm1bpa",
		.data = &koe_tx14d24vm1bpa,
	}, {
		.compatible = "koe,tx31d200vm0baa",
		.data = &koe_tx31d200vm0baa,
	}, {
		.compatible = "kyo,tcg121xglp",
		.data = &kyo_tcg121xglp,
	}, {
		.compatible = "lemaker,bl035-rgb-002",
		.data = &lemaker_bl035_rgb_002,
	}, {
		.compatible = "lg,lb070wv8",
		.data = &lg_lb070wv8,
	}, {
		.compatible = "lg,lp079qx1-sp0v",
		.data = &lg_lp079qx1_sp0v,
	}, {
		.compatible = "lg,lp097qx1-spa1",
		.data = &lg_lp097qx1_spa1,
	}, {
		.compatible = "lg,lp120up1",
		.data = &lg_lp120up1,
	}, {
		.compatible = "lg,lp129qe",
		.data = &lg_lp129qe,
	}, {
		.compatible = "logicpd,type28",
		.data = &logicpd_type_28,
	}, {
		.compatible = "mitsubishi,aa070mc01-ca1",
		.data = &mitsubishi_aa070mc01,
	}, {
		.compatible = "nec,nl12880bc20-05",
		.data = &nec_nl12880bc20_05,
	}, {
		.compatible = "nec,nl4827hc19-05b",
		.data = &nec_nl4827hc19_05b,
	}, {
		.compatible = "netron-dy,e231732",
		.data = &netron_dy_e231732,
	}, {
		.compatible = "newhaven,nhd-4.3-480272ef-atxl",
		.data = &newhaven_nhd_43_480272ef_atxl,
	}, {
		.compatible = "nlt,nl192108ac18-02d",
		.data = &nlt_nl192108ac18_02d,
	}, {
		.compatible = "nvd,9128",
		.data = &nvd_9128,
	}, {
		.compatible = "okaya,rs800480t-7x0gp",
		.data = &okaya_rs800480t_7x0gp,
	}, {
		.compatible = "olimex,lcd-olinuxino-43-ts",
		.data = &olimex_lcd_olinuxino_43ts,
	}, {
		.compatible = "ontat,yx700wv03",
		.data = &ontat_yx700wv03,
	}, {
		.compatible = "ortustech,com37h3m05dtc",
		.data = &ortustech_com37h3m,
	}, {
		.compatible = "ortustech,com37h3m99dtc",
		.data = &ortustech_com37h3m,
	}, {
		.compatible = "ortustech,com43h4m85ulc",
		.data = &ortustech_com43h4m85ulc,
	}, {
		.compatible = "osddisplays,osd070t1718-19ts",
		.data = &osddisplays_osd070t1718_19ts,
	}, {
		.compatible = "pda,91-00156-a0",
		.data = &pda_91_00156_a0,
	}, {
		.compatible = "qiaodian,qd43003c0-40",
		.data = &qd43003c0_40,
	}, {
		.compatible = "rocktech,rk070er9427",
		.data = &rocktech_rk070er9427,
	}, {
		.compatible = "samsung,lsn122dl01-c01",
		.data = &samsung_lsn122dl01_c01,
	}, {
		.compatible = "samsung,ltn101nt05",
		.data = &samsung_ltn101nt05,
	}, {
		.compatible = "samsung,ltn140at29-301",
		.data = &samsung_ltn140at29_301,
	}, {
		.compatible = "satoz,sat050at40h12r2",
		.data = &satoz_sat050at40h12r2,
	}, {
		.compatible = "sharp,ld-d5116z01b",
		.data = &sharp_ld_d5116z01b,
	}, {
		.compatible = "sharp,lq035q7db03",
		.data = &sharp_lq035q7db03,
	}, {
		.compatible = "sharp,lq070y3dg3b",
		.data = &sharp_lq070y3dg3b,
	}, {
		.compatible = "sharp,lq101k1ly04",
		.data = &sharp_lq101k1ly04,
	}, {
		.compatible = "sharp,lq123p1jx31",
		.data = &sharp_lq123p1jx31,
	}, {
		.compatible = "sharp,lq150x1lg11",
		.data = &sharp_lq150x1lg11,
	}, {
		.compatible = "sharp,ls020b1dd01d",
		.data = &sharp_ls020b1dd01d,
	}, {
		.compatible = "shelly,sca07010-bfn-lnn",
		.data = &shelly_sca07010_bfn_lnn,
	}, {
		.compatible = "starry,kr122ea0sra",
		.data = &starry_kr122ea0sra,
	}, {
		.compatible = "tfc,s9700rtwv43tr-01b",
		.data = &tfc_s9700rtwv43tr_01b,
	}, {
		.compatible = "tianma,tm070jdhg30",
		.data = &tianma_tm070jdhg30,
	}, {
		.compatible = "tianma,tm070rvhg71",
		.data = &tianma_tm070rvhg71,
	}, {
		.compatible = "ti,nspire-cx-lcd-panel",
		.data = &ti_nspire_cx_lcd_panel,
	}, {
		.compatible = "ti,nspire-classic-lcd-panel",
		.data = &ti_nspire_classic_lcd_panel,
	}, {
		.compatible = "toshiba,lt089ac29000",
		.data = &toshiba_lt089ac29000,
	}, {
		.compatible = "tpk,f07a-0102",
		.data = &tpk_f07a_0102,
	}, {
		.compatible = "tpk,f10a-0102",
		.data = &tpk_f10a_0102,
	}, {
		.compatible = "urt,umsh-8596md-t",
		.data = &urt_umsh_8596md_parallel,
	}, {
		.compatible = "urt,umsh-8596md-1t",
		.data = &urt_umsh_8596md_parallel,
	}, {
		.compatible = "urt,umsh-8596md-7t",
		.data = &urt_umsh_8596md_parallel,
	}, {
		.compatible = "urt,umsh-8596md-11t",
		.data = &urt_umsh_8596md_lvds,
	}, {
		.compatible = "urt,umsh-8596md-19t",
		.data = &urt_umsh_8596md_lvds,
	}, {
		.compatible = "urt,umsh-8596md-20t",
		.data = &urt_umsh_8596md_parallel,
	}, {
		.compatible = "vxt,vl050-8048nt-c01",
		.data = &vl050_8048nt_c01,
	}, {
		.compatible = "winstar,wf35ltiacd",
		.data = &winstar_wf35ltiacd,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, platform_of_match);

static int panel_simple_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;

	id = of_match_node(platform_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	return panel_simple_probe(&pdev->dev, id->data);
}

static int panel_simple_platform_remove(struct platform_device *pdev)
{
	return panel_simple_remove(&pdev->dev);
}

static void panel_simple_platform_shutdown(struct platform_device *pdev)
{
	panel_simple_shutdown(&pdev->dev);
}

static struct platform_driver panel_simple_platform_driver = {
	.driver = {
		.name = "panel-simple",
		.of_match_table = platform_of_match,
	},
	.probe = panel_simple_platform_probe,
	.remove = panel_simple_platform_remove,
	.shutdown = panel_simple_platform_shutdown,
};

struct panel_desc_dsi {
	struct panel_desc desc;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
};

static const struct drm_display_mode auo_b080uan01_mode = {
	.clock = 154500,
	.hdisplay = 1200,
	.hsync_start = 1200 + 62,
	.hsync_end = 1200 + 62 + 4,
	.htotal = 1200 + 62 + 4 + 62,
	.vdisplay = 1920,
	.vsync_start = 1920 + 9,
	.vsync_end = 1920 + 9 + 2,
	.vtotal = 1920 + 9 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc_dsi auo_b080uan01 = {
	.desc = {
		.modes = &auo_b080uan01_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 108,
			.height = 272,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode boe_tv080wum_nl0_mode = {
	.clock = 160000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 120,
	.hsync_end = 1200 + 120 + 20,
	.htotal = 1200 + 120 + 20 + 21,
	.vdisplay = 1920,
	.vsync_start = 1920 + 21,
	.vsync_end = 1920 + 21 + 3,
	.vtotal = 1920 + 21 + 3 + 18,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc_dsi boe_tv080wum_nl0 = {
	.desc = {
		.modes = &boe_tv080wum_nl0_mode,
		.num_modes = 1,
		.size = {
			.width = 107,
			.height = 172,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO |
		 MIPI_DSI_MODE_VIDEO_BURST |
		 MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode lg_ld070wx3_sl01_mode = {
	.clock = 71000,
	.hdisplay = 800,
	.hsync_start = 800 + 32,
	.hsync_end = 800 + 32 + 1,
	.htotal = 800 + 32 + 1 + 57,
	.vdisplay = 1280,
	.vsync_start = 1280 + 28,
	.vsync_end = 1280 + 28 + 1,
	.vtotal = 1280 + 28 + 1 + 14,
	.vrefresh = 60,
};

static const struct panel_desc_dsi lg_ld070wx3_sl01 = {
	.desc = {
		.modes = &lg_ld070wx3_sl01_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 94,
			.height = 151,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode lg_lh500wx1_sd03_mode = {
	.clock = 67000,
	.hdisplay = 720,
	.hsync_start = 720 + 12,
	.hsync_end = 720 + 12 + 4,
	.htotal = 720 + 12 + 4 + 112,
	.vdisplay = 1280,
	.vsync_start = 1280 + 8,
	.vsync_end = 1280 + 8 + 4,
	.vtotal = 1280 + 8 + 4 + 12,
	.vrefresh = 60,
};

static const struct panel_desc_dsi lg_lh500wx1_sd03 = {
	.desc = {
		.modes = &lg_lh500wx1_sd03_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 62,
			.height = 110,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode panasonic_vvx10f004b00_mode = {
	.clock = 157200,
	.hdisplay = 1920,
	.hsync_start = 1920 + 154,
	.hsync_end = 1920 + 154 + 16,
	.htotal = 1920 + 154 + 16 + 32,
	.vdisplay = 1200,
	.vsync_start = 1200 + 17,
	.vsync_end = 1200 + 17 + 2,
	.vtotal = 1200 + 17 + 2 + 16,
	.vrefresh = 60,
};

static const struct panel_desc_dsi panasonic_vvx10f004b00 = {
	.desc = {
		.modes = &panasonic_vvx10f004b00_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 217,
			.height = 136,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		 MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode lg_acx467akm_7_mode = {
	.clock = 150000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 2,
	.hsync_end = 1080 + 2 + 2,
	.htotal = 1080 + 2 + 2 + 2,
	.vdisplay = 1920,
	.vsync_start = 1920 + 2,
	.vsync_end = 1920 + 2 + 2,
	.vtotal = 1920 + 2 + 2 + 2,
	.vrefresh = 60,
};

static const struct panel_desc_dsi lg_acx467akm_7 = {
	.desc = {
		.modes = &lg_acx467akm_7_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 62,
			.height = 110,
		},
	},
	.flags = 0,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode osd101t2045_53ts_mode = {
	.clock = 154500,
	.hdisplay = 1920,
	.hsync_start = 1920 + 112,
	.hsync_end = 1920 + 112 + 16,
	.htotal = 1920 + 112 + 16 + 32,
	.vdisplay = 1200,
	.vsync_start = 1200 + 16,
	.vsync_end = 1200 + 16 + 2,
	.vtotal = 1200 + 16 + 2 + 16,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc_dsi osd101t2045_53ts = {
	.desc = {
		.modes = &osd101t2045_53ts_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 217,
			.height = 136,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		 MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		 MIPI_DSI_MODE_EOT_PACKET,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct of_device_id dsi_of_match[] = {
	{
		.compatible = "auo,b080uan01",
		.data = &auo_b080uan01
	}, {
		.compatible = "boe,tv080wum-nl0",
		.data = &boe_tv080wum_nl0
	}, {
		.compatible = "lg,ld070wx3-sl01",
		.data = &lg_ld070wx3_sl01
	}, {
		.compatible = "lg,lh500wx1-sd03",
		.data = &lg_lh500wx1_sd03
	}, {
		.compatible = "panasonic,vvx10f004b00",
		.data = &panasonic_vvx10f004b00
	}, {
		.compatible = "lg,acx467akm-7",
		.data = &lg_acx467akm_7
	}, {
		.compatible = "osddisplays,osd101t2045-53ts",
		.data = &osd101t2045_53ts
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, dsi_of_match);

static int panel_simple_dsi_probe(struct mipi_dsi_device *dsi)
{
	const struct panel_desc_dsi *desc;
	const struct of_device_id *id;
	int err;

	id = of_match_node(dsi_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	desc = id->data;

	err = panel_simple_probe(&dsi->dev, &desc->desc);
	if (err < 0)
		return err;

	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	err = mipi_dsi_attach(dsi);
	if (err) {
		struct panel_simple *panel = dev_get_drvdata(&dsi->dev);

		drm_panel_remove(&panel->base);
	}

	return err;
}

static int panel_simple_dsi_remove(struct mipi_dsi_device *dsi)
{
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	return panel_simple_remove(&dsi->dev);
}

static void panel_simple_dsi_shutdown(struct mipi_dsi_device *dsi)
{
	panel_simple_shutdown(&dsi->dev);
}

static struct mipi_dsi_driver panel_simple_dsi_driver = {
	.driver = {
		.name = "panel-simple-dsi",
		.of_match_table = dsi_of_match,
	},
	.probe = panel_simple_dsi_probe,
	.remove = panel_simple_dsi_remove,
	.shutdown = panel_simple_dsi_shutdown,
};

static int __init panel_simple_init(void)
{
	int err;

	err = platform_driver_register(&panel_simple_platform_driver);
	if (err < 0)
		return err;

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI)) {
		err = mipi_dsi_driver_register(&panel_simple_dsi_driver);
		if (err < 0)
			return err;
	}

	return 0;
}
module_init(panel_simple_init);

static void __exit panel_simple_exit(void)
{
	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_unregister(&panel_simple_dsi_driver);

	platform_driver_unregister(&panel_simple_platform_driver);
}
module_exit(panel_simple_exit);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("DRM Driver for Simple Panels");
MODULE_LICENSE("GPL and additional rights");
