// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic DSI Command Mode panel driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

#define DCS_REGULATOR_SUPPLY_NUM 2

static const struct of_device_id dsicm_of_match[];

struct dsic_panel_data {
	u32 xres;
	u32 yres;
	u32 refresh;
	u32 width_mm;
	u32 height_mm;
	u32 max_hs_rate;
	u32 max_lp_rate;
};

struct panel_drv_data {
	struct mipi_dsi_device *dsi;
	struct drm_panel panel;
	struct drm_display_mode mode;

	struct mutex lock;

	struct backlight_device *bldev;
	struct backlight_device *extbldev;

	unsigned long	hw_guard_end;	/* next value of jiffies when we can
					 * issue the next sleep in/out command
					 */
	unsigned long	hw_guard_wait;	/* max guard time in jiffies */

	const struct dsic_panel_data *panel_data;

	struct gpio_desc *reset_gpio;

	struct regulator_bulk_data supplies[DCS_REGULATOR_SUPPLY_NUM];

	bool use_dsi_backlight;

	/* runtime variables */
	bool enabled;

	bool intro_printed;
};

static inline struct panel_drv_data *panel_to_ddata(struct drm_panel *panel)
{
	return container_of(panel, struct panel_drv_data, panel);
}

static void dsicm_bl_power(struct panel_drv_data *ddata, bool enable)
{
	struct backlight_device *backlight;

	if (ddata->bldev)
		backlight = ddata->bldev;
	else if (ddata->extbldev)
		backlight = ddata->extbldev;
	else
		return;

	if (enable) {
		backlight->props.fb_blank = FB_BLANK_UNBLANK;
		backlight->props.state = ~(BL_CORE_FBBLANK | BL_CORE_SUSPENDED);
		backlight->props.power = FB_BLANK_UNBLANK;
	} else {
		backlight->props.fb_blank = FB_BLANK_NORMAL;
		backlight->props.power = FB_BLANK_POWERDOWN;
		backlight->props.state |= BL_CORE_FBBLANK | BL_CORE_SUSPENDED;
	}

	backlight_update_status(backlight);
}

static void hw_guard_start(struct panel_drv_data *ddata, int guard_msec)
{
	ddata->hw_guard_wait = msecs_to_jiffies(guard_msec);
	ddata->hw_guard_end = jiffies + ddata->hw_guard_wait;
}

static void hw_guard_wait(struct panel_drv_data *ddata)
{
	unsigned long wait = ddata->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= ddata->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}

static int dsicm_dcs_read_1(struct panel_drv_data *ddata, u8 dcs_cmd, u8 *data)
{
	return mipi_dsi_dcs_read(ddata->dsi, dcs_cmd, data, 1);
}

static int dsicm_dcs_write_1(struct panel_drv_data *ddata, u8 dcs_cmd, u8 param)
{
	return mipi_dsi_dcs_write(ddata->dsi, dcs_cmd, &param, 1);
}

static int dsicm_sleep_in(struct panel_drv_data *ddata)

{
	int r;

	hw_guard_wait(ddata);

	r = mipi_dsi_dcs_enter_sleep_mode(ddata->dsi);
	if (r)
		return r;

	hw_guard_start(ddata, 120);

	usleep_range(5000, 10000);

	return 0;
}

static int dsicm_sleep_out(struct panel_drv_data *ddata)
{
	int r;

	hw_guard_wait(ddata);

	r = mipi_dsi_dcs_exit_sleep_mode(ddata->dsi);
	if (r)
		return r;

	hw_guard_start(ddata, 120);

	usleep_range(5000, 10000);

	return 0;
}

static int dsicm_get_id(struct panel_drv_data *ddata, u8 *id1, u8 *id2, u8 *id3)
{
	int r;

	r = dsicm_dcs_read_1(ddata, DCS_GET_ID1, id1);
	if (r)
		return r;
	r = dsicm_dcs_read_1(ddata, DCS_GET_ID2, id2);
	if (r)
		return r;
	r = dsicm_dcs_read_1(ddata, DCS_GET_ID3, id3);
	if (r)
		return r;

	return 0;
}

static int dsicm_set_update_window(struct panel_drv_data *ddata)
{
	struct mipi_dsi_device *dsi = ddata->dsi;
	int r;

	r = mipi_dsi_dcs_set_column_address(dsi, 0, ddata->mode.hdisplay - 1);
	if (r < 0)
		return r;

	r = mipi_dsi_dcs_set_page_address(dsi, 0, ddata->mode.vdisplay - 1);
	if (r < 0)
		return r;

	return 0;
}

static int dsicm_bl_update_status(struct backlight_device *dev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dev->dev);
	int r = 0;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&ddata->dsi->dev, "update brightness to %d\n", level);

	mutex_lock(&ddata->lock);

	if (ddata->enabled)
		r = dsicm_dcs_write_1(ddata, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				      level);

	mutex_unlock(&ddata->lock);

	return r;
}

static int dsicm_bl_get_intensity(struct backlight_device *dev)
{
	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		return dev->props.brightness;

	return 0;
}

static const struct backlight_ops dsicm_bl_ops = {
	.get_brightness = dsicm_bl_get_intensity,
	.update_status  = dsicm_bl_update_status,
};

static ssize_t num_dsi_errors_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	u8 errors = 0;
	int r = -ENODEV;

	mutex_lock(&ddata->lock);

	if (ddata->enabled)
		r = dsicm_dcs_read_1(ddata, MIPI_DCS_GET_ERROR_COUNT_ON_DSI, &errors);

	mutex_unlock(&ddata->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", errors);
}

static ssize_t hw_revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	u8 id1, id2, id3;
	int r = -ENODEV;

	mutex_lock(&ddata->lock);

	if (ddata->enabled)
		r = dsicm_get_id(ddata, &id1, &id2, &id3);

	mutex_unlock(&ddata->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x\n", id1, id2, id3);
}

static DEVICE_ATTR_RO(num_dsi_errors);
static DEVICE_ATTR_RO(hw_revision);

static struct attribute *dsicm_attrs[] = {
	&dev_attr_num_dsi_errors.attr,
	&dev_attr_hw_revision.attr,
	NULL,
};

static const struct attribute_group dsicm_attr_group = {
	.attrs = dsicm_attrs,
};

static void dsicm_hw_reset(struct panel_drv_data *ddata)
{
	gpiod_set_value(ddata->reset_gpio, 1);
	udelay(10);
	/* reset the panel */
	gpiod_set_value(ddata->reset_gpio, 0);
	/* assert reset */
	udelay(10);
	gpiod_set_value(ddata->reset_gpio, 1);
	/* wait after releasing reset */
	usleep_range(5000, 10000);
}

static int dsicm_power_on(struct panel_drv_data *ddata)
{
	u8 id1, id2, id3;
	int r;

	dsicm_hw_reset(ddata);

	ddata->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	r = dsicm_sleep_out(ddata);
	if (r)
		goto err;

	r = dsicm_get_id(ddata, &id1, &id2, &id3);
	if (r)
		goto err;

	r = dsicm_dcs_write_1(ddata, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0xff);
	if (r)
		goto err;

	r = dsicm_dcs_write_1(ddata, MIPI_DCS_WRITE_CONTROL_DISPLAY,
			(1<<2) | (1<<5));	/* BL | BCTRL */
	if (r)
		goto err;

	r = mipi_dsi_dcs_set_pixel_format(ddata->dsi, MIPI_DCS_PIXEL_FMT_24BIT);
	if (r)
		goto err;

	r = dsicm_set_update_window(ddata);
	if (r)
		goto err;

	r = mipi_dsi_dcs_set_display_on(ddata->dsi);
	if (r)
		goto err;

	r = mipi_dsi_dcs_set_tear_on(ddata->dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (r)
		goto err;

	/* possible panel bug */
	msleep(100);

	ddata->enabled = true;

	if (!ddata->intro_printed) {
		dev_info(&ddata->dsi->dev, "panel revision %02x.%02x.%02x\n",
			id1, id2, id3);
		ddata->intro_printed = true;
	}

	ddata->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	return 0;
err:
	dev_err(&ddata->dsi->dev, "error while enabling panel, issuing HW reset\n");

	dsicm_hw_reset(ddata);

	return r;
}

static int dsicm_power_off(struct panel_drv_data *ddata)
{
	int r;

	ddata->enabled = false;

	r = mipi_dsi_dcs_set_display_off(ddata->dsi);
	if (!r)
		r = dsicm_sleep_in(ddata);

	if (r) {
		dev_err(&ddata->dsi->dev,
				"error disabling panel, issuing HW reset\n");
		dsicm_hw_reset(ddata);
	}

	return r;
}

static int dsicm_prepare(struct drm_panel *panel)
{
	struct panel_drv_data *ddata = panel_to_ddata(panel);
	int r;

	r = regulator_bulk_enable(ARRAY_SIZE(ddata->supplies), ddata->supplies);
	if (r)
		dev_err(&ddata->dsi->dev, "failed to enable supplies: %d\n", r);

	return r;
}

static int dsicm_enable(struct drm_panel *panel)
{
	struct panel_drv_data *ddata = panel_to_ddata(panel);
	int r;

	mutex_lock(&ddata->lock);

	r = dsicm_power_on(ddata);
	if (r)
		goto err;

	mutex_unlock(&ddata->lock);

	dsicm_bl_power(ddata, true);

	return 0;
err:
	dev_err(&ddata->dsi->dev, "enable failed (%d)\n", r);
	mutex_unlock(&ddata->lock);
	return r;
}

static int dsicm_unprepare(struct drm_panel *panel)
{
	struct panel_drv_data *ddata = panel_to_ddata(panel);
	int r;

	r = regulator_bulk_disable(ARRAY_SIZE(ddata->supplies), ddata->supplies);
	if (r)
		dev_err(&ddata->dsi->dev, "failed to disable supplies: %d\n", r);

	return r;
}

static int dsicm_disable(struct drm_panel *panel)
{
	struct panel_drv_data *ddata = panel_to_ddata(panel);
	int r;

	dsicm_bl_power(ddata, false);

	mutex_lock(&ddata->lock);

	r = dsicm_power_off(ddata);

	mutex_unlock(&ddata->lock);

	return r;
}

static int dsicm_get_modes(struct drm_panel *panel,
			   struct drm_connector *connector)
{
	struct panel_drv_data *ddata = panel_to_ddata(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ddata->mode);
	if (!mode) {
		dev_err(&ddata->dsi->dev, "failed to add mode %ux%ux@%u kHz\n",
			ddata->mode.hdisplay, ddata->mode.vdisplay,
			ddata->mode.clock);
		return -ENOMEM;
	}

	connector->display_info.width_mm = ddata->panel_data->width_mm;
	connector->display_info.height_mm = ddata->panel_data->height_mm;

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs dsicm_panel_funcs = {
	.unprepare = dsicm_unprepare,
	.disable = dsicm_disable,
	.prepare = dsicm_prepare,
	.enable = dsicm_enable,
	.get_modes = dsicm_get_modes,
};

static int dsicm_probe_of(struct mipi_dsi_device *dsi)
{
	struct backlight_device *backlight;
	struct panel_drv_data *ddata = mipi_dsi_get_drvdata(dsi);
	int err;
	struct drm_display_mode *mode = &ddata->mode;

	ddata->reset_gpio = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ddata->reset_gpio)) {
		err = PTR_ERR(ddata->reset_gpio);
		dev_err(&dsi->dev, "reset gpio request failed: %d", err);
		return err;
	}

	mode->hdisplay = mode->hsync_start = mode->hsync_end = mode->htotal =
		ddata->panel_data->xres;
	mode->vdisplay = mode->vsync_start = mode->vsync_end = mode->vtotal =
		ddata->panel_data->yres;
	mode->clock = ddata->panel_data->xres * ddata->panel_data->yres *
		ddata->panel_data->refresh / 1000;
	mode->width_mm = ddata->panel_data->width_mm;
	mode->height_mm = ddata->panel_data->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);

	ddata->supplies[0].supply = "vpnl";
	ddata->supplies[1].supply = "vddi";
	err = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(ddata->supplies),
				      ddata->supplies);
	if (err)
		return err;

	backlight = devm_of_find_backlight(&dsi->dev);
	if (IS_ERR(backlight))
		return PTR_ERR(backlight);

	/* If no backlight device is found assume native backlight support */
	if (backlight)
		ddata->extbldev = backlight;
	else
		ddata->use_dsi_backlight = true;

	return 0;
}

static int dsicm_probe(struct mipi_dsi_device *dsi)
{
	struct panel_drv_data *ddata;
	struct backlight_device *bldev = NULL;
	struct device *dev = &dsi->dev;
	int r;

	dev_dbg(dev, "probe\n");

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ddata);
	ddata->dsi = dsi;

	ddata->panel_data = of_device_get_match_data(dev);
	if (!ddata->panel_data)
		return -ENODEV;

	r = dsicm_probe_of(dsi);
	if (r)
		return r;

	mutex_init(&ddata->lock);

	dsicm_hw_reset(ddata);

	drm_panel_init(&ddata->panel, dev, &dsicm_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	if (ddata->use_dsi_backlight) {
		struct backlight_properties props = { 0 };
		props.max_brightness = 255;
		props.type = BACKLIGHT_RAW;

		bldev = devm_backlight_device_register(dev, dev_name(dev),
			dev, ddata, &dsicm_bl_ops, &props);
		if (IS_ERR(bldev)) {
			r = PTR_ERR(bldev);
			goto err_bl;
		}

		ddata->bldev = bldev;
	}

	r = sysfs_create_group(&dev->kobj, &dsicm_attr_group);
	if (r) {
		dev_err(dev, "failed to create sysfs files\n");
		goto err_bl;
	}

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_EOT_PACKET;
	dsi->hs_rate = ddata->panel_data->max_hs_rate;
	dsi->lp_rate = ddata->panel_data->max_lp_rate;

	drm_panel_add(&ddata->panel);

	r = mipi_dsi_attach(dsi);
	if (r < 0)
		goto err_dsi_attach;

	return 0;

err_dsi_attach:
	drm_panel_remove(&ddata->panel);
	sysfs_remove_group(&dsi->dev.kobj, &dsicm_attr_group);
err_bl:
	if (ddata->extbldev)
		put_device(&ddata->extbldev->dev);

	return r;
}

static int dsicm_remove(struct mipi_dsi_device *dsi)
{
	struct panel_drv_data *ddata = mipi_dsi_get_drvdata(dsi);

	dev_dbg(&dsi->dev, "remove\n");

	mipi_dsi_detach(dsi);

	drm_panel_remove(&ddata->panel);

	sysfs_remove_group(&dsi->dev.kobj, &dsicm_attr_group);

	if (ddata->extbldev)
		put_device(&ddata->extbldev->dev);

	return 0;
}

static const struct dsic_panel_data taal_data = {
	.xres = 864,
	.yres = 480,
	.refresh = 60,
	.width_mm = 0,
	.height_mm = 0,
	.max_hs_rate = 300000000,
	.max_lp_rate = 10000000,
};

static const struct dsic_panel_data himalaya_data = {
	.xres = 480,
	.yres = 864,
	.refresh = 60,
	.width_mm = 49,
	.height_mm = 88,
	.max_hs_rate = 300000000,
	.max_lp_rate = 10000000,
};

static const struct dsic_panel_data droid4_data = {
	.xres = 540,
	.yres = 960,
	.refresh = 60,
	.width_mm = 50,
	.height_mm = 89,
	.max_hs_rate = 300000000,
	.max_lp_rate = 10000000,
};

static const struct of_device_id dsicm_of_match[] = {
	{ .compatible = "tpo,taal", .data = &taal_data },
	{ .compatible = "nokia,himalaya", &himalaya_data },
	{ .compatible = "motorola,droid4-panel", &droid4_data },
	{},
};

MODULE_DEVICE_TABLE(of, dsicm_of_match);

static struct mipi_dsi_driver dsicm_driver = {
	.probe = dsicm_probe,
	.remove = dsicm_remove,
	.driver = {
		.name = "panel-dsi-cm",
		.of_match_table = dsicm_of_match,
	},
};
module_mipi_dsi_driver(dsicm_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Generic DSI Command Mode Panel Driver");
MODULE_LICENSE("GPL");
