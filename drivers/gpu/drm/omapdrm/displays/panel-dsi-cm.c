// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic DSI Command Mode panel driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

/* #define DEBUG */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>

#include <video/mipi_display.h>
#include <video/of_display_timing.h>

#include "../dss/omapdss.h"

#define DCS_READ_NUM_ERRORS	0x05
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

struct panel_drv_data {
	struct mipi_dsi_device *dsi;

	struct omap_dss_device dssdev;
	struct omap_dss_device *src;

	struct videomode vm;

	struct mutex lock;

	struct backlight_device *bldev;
	struct backlight_device *extbldev;

	unsigned long	hw_guard_end;	/* next value of jiffies when we can
					 * issue the next sleep in/out command
					 */
	unsigned long	hw_guard_wait;	/* max guard time in jiffies */

	/* panel HW configuration from DT or platform data */
	struct gpio_desc *reset_gpio;
	struct gpio_desc *ext_te_gpio;

	struct regulator *vpnl;
	struct regulator *vddi;

	bool use_dsi_backlight;

	int width_mm;
	int height_mm;

	/* runtime variables */
	bool enabled;

	bool te_enabled;

	atomic_t do_update;

	struct delayed_work te_timeout_work;

	bool intro_printed;

	struct workqueue_struct *workqueue;

	bool ulps_enabled;
	unsigned int ulps_timeout;
	struct delayed_work ulps_work;
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static irqreturn_t dsicm_te_isr(int irq, void *data);
static void dsicm_te_timeout_work_callback(struct work_struct *work);
static int _dsicm_enable_te(struct panel_drv_data *ddata, bool enable);

static int dsicm_panel_reset(struct panel_drv_data *ddata);

static void dsicm_ulps_work(struct work_struct *work);

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

static int dsicm_set_update_window(struct panel_drv_data *ddata,
		u16 x, u16 y, u16 w, u16 h)
{
	struct mipi_dsi_device *dsi = ddata->dsi;
	int r;
	u16 x1 = x;
	u16 x2 = x + w - 1;
	u16 y1 = y;
	u16 y2 = y + h - 1;

	r = mipi_dsi_dcs_set_column_address(dsi, x1, x2);
	if (r < 0)
		return r;

	r = mipi_dsi_dcs_set_page_address(dsi, y1, y2);
	if (r < 0)
		return r;

	return 0;
}

static void dsicm_queue_ulps_work(struct panel_drv_data *ddata)
{
	if (ddata->ulps_timeout > 0)
		queue_delayed_work(ddata->workqueue, &ddata->ulps_work,
				msecs_to_jiffies(ddata->ulps_timeout));
}

static void dsicm_cancel_ulps_work(struct panel_drv_data *ddata)
{
	cancel_delayed_work(&ddata->ulps_work);
}

static int dsicm_enter_ulps(struct panel_drv_data *ddata)
{
	struct omap_dss_device *src = ddata->src;
	int r;

	if (ddata->ulps_enabled)
		return 0;

	dsicm_cancel_ulps_work(ddata);

	r = _dsicm_enable_te(ddata, false);
	if (r)
		goto err;

	if (ddata->ext_te_gpio)
		disable_irq(gpiod_to_irq(ddata->ext_te_gpio));

	src->ops->dsi.disable(src, false, true);

	ddata->ulps_enabled = true;

	return 0;

err:
	dev_err(&ddata->dsi->dev, "enter ULPS failed");
	dsicm_panel_reset(ddata);

	ddata->ulps_enabled = false;

	dsicm_queue_ulps_work(ddata);

	return r;
}

static int dsicm_exit_ulps(struct panel_drv_data *ddata)
{
	struct omap_dss_device *src = ddata->src;
	int r;

	if (!ddata->ulps_enabled)
		return 0;

	src->ops->enable(src);
	src->ops->dsi.enable_hs(src, ddata->dsi->channel, true);

	r = _dsicm_enable_te(ddata, true);
	if (r) {
		dev_err(&ddata->dsi->dev, "failed to re-enable TE");
		goto err2;
	}

	if (ddata->ext_te_gpio)
		enable_irq(gpiod_to_irq(ddata->ext_te_gpio));

	dsicm_queue_ulps_work(ddata);

	ddata->ulps_enabled = false;

	return 0;

err2:
	dev_err(&ddata->dsi->dev, "failed to exit ULPS");

	r = dsicm_panel_reset(ddata);
	if (!r) {
		if (ddata->ext_te_gpio)
			enable_irq(gpiod_to_irq(ddata->ext_te_gpio));
		ddata->ulps_enabled = false;
	}

	dsicm_queue_ulps_work(ddata);

	return r;
}

static int dsicm_wake_up(struct panel_drv_data *ddata)
{
	if (ddata->ulps_enabled)
		return dsicm_exit_ulps(ddata);

	dsicm_cancel_ulps_work(ddata);
	dsicm_queue_ulps_work(ddata);
	return 0;
}

static int dsicm_bl_update_status(struct backlight_device *dev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dev->dev);
	struct omap_dss_device *src = ddata->src;
	int r = 0;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&ddata->dsi->dev, "update brightness to %d\n", level);

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		src->ops->dsi.bus_lock(src);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_dcs_write_1(
				ddata, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, level);

		src->ops->dsi.bus_unlock(src);
	}

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

static ssize_t dsicm_num_errors_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	struct omap_dss_device *src = ddata->src;
	u8 errors = 0;
	int r;

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		src->ops->dsi.bus_lock(src);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_dcs_read_1(ddata, DCS_READ_NUM_ERRORS,
					&errors);

		src->ops->dsi.bus_unlock(src);
	} else {
		r = -ENODEV;
	}

	mutex_unlock(&ddata->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", errors);
}

static ssize_t dsicm_hw_revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	struct omap_dss_device *src = ddata->src;
	u8 id1, id2, id3;
	int r;

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		src->ops->dsi.bus_lock(src);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_get_id(ddata, &id1, &id2, &id3);

		src->ops->dsi.bus_unlock(src);
	} else {
		r = -ENODEV;
	}

	mutex_unlock(&ddata->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x\n", id1, id2, id3);
}

static ssize_t dsicm_store_ulps(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	struct omap_dss_device *src = ddata->src;
	unsigned long t;
	int r;

	r = kstrtoul(buf, 0, &t);
	if (r)
		return r;

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		src->ops->dsi.bus_lock(src);

		if (t)
			r = dsicm_enter_ulps(ddata);
		else
			r = dsicm_wake_up(ddata);

		src->ops->dsi.bus_unlock(src);
	}

	mutex_unlock(&ddata->lock);

	if (r)
		return r;

	return count;
}

static ssize_t dsicm_show_ulps(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	unsigned int t;

	mutex_lock(&ddata->lock);
	t = ddata->ulps_enabled;
	mutex_unlock(&ddata->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t dsicm_store_ulps_timeout(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	struct omap_dss_device *src = ddata->src;
	unsigned long t;
	int r;

	r = kstrtoul(buf, 0, &t);
	if (r)
		return r;

	mutex_lock(&ddata->lock);
	ddata->ulps_timeout = t;

	if (ddata->enabled) {
		/* dsicm_wake_up will restart the timer */
		src->ops->dsi.bus_lock(src);
		r = dsicm_wake_up(ddata);
		src->ops->dsi.bus_unlock(src);
	}

	mutex_unlock(&ddata->lock);

	if (r)
		return r;

	return count;
}

static ssize_t dsicm_show_ulps_timeout(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	unsigned int t;

	mutex_lock(&ddata->lock);
	t = ddata->ulps_timeout;
	mutex_unlock(&ddata->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static DEVICE_ATTR(num_dsi_errors, S_IRUGO, dsicm_num_errors_show, NULL);
static DEVICE_ATTR(hw_revision, S_IRUGO, dsicm_hw_revision_show, NULL);
static DEVICE_ATTR(ulps, S_IRUGO | S_IWUSR,
		dsicm_show_ulps, dsicm_store_ulps);
static DEVICE_ATTR(ulps_timeout, S_IRUGO | S_IWUSR,
		dsicm_show_ulps_timeout, dsicm_store_ulps_timeout);

static struct attribute *dsicm_attrs[] = {
	&dev_attr_num_dsi_errors.attr,
	&dev_attr_hw_revision.attr,
	&dev_attr_ulps.attr,
	&dev_attr_ulps_timeout.attr,
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
	struct omap_dss_device *src = ddata->src;
	u8 id1, id2, id3;
	int r;
	struct omap_dss_dsi_config dsi_config = {
		.mode = OMAP_DSS_DSI_CMD_MODE,
		.pixel_format = MIPI_DSI_FMT_RGB888,
		.vm = &ddata->vm,
		.hs_clk_min = 150000000,
		.hs_clk_max = 300000000,
		.lp_clk_min = 7000000,
		.lp_clk_max = 10000000,
	};

	if (ddata->vpnl) {
		r = regulator_enable(ddata->vpnl);
		if (r) {
			dev_err(&ddata->dsi->dev,
				"failed to enable VPNL: %d\n", r);
			return r;
		}
	}

	if (ddata->vddi) {
		r = regulator_enable(ddata->vddi);
		if (r) {
			dev_err(&ddata->dsi->dev,
				"failed to enable VDDI: %d\n", r);
			goto err_vpnl;
		}
	}

	r = src->ops->dsi.set_config(src, &dsi_config);
	if (r) {
		dev_err(&ddata->dsi->dev, "failed to configure DSI\n");
		goto err_vddi;
	}

	src->ops->enable(src);

	dsicm_hw_reset(ddata);

	src->ops->dsi.enable_hs(src, ddata->dsi->channel, false);

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

	r = mipi_dsi_dcs_set_display_on(ddata->dsi);
	if (r)
		goto err;

	r = _dsicm_enable_te(ddata, ddata->te_enabled);
	if (r)
		goto err;

	r = src->ops->dsi.enable_video_output(src, ddata->dsi->channel);
	if (r)
		goto err;

	ddata->enabled = true;

	if (!ddata->intro_printed) {
		dev_info(&ddata->dsi->dev, "panel revision %02x.%02x.%02x\n",
			id1, id2, id3);
		ddata->intro_printed = true;
	}

	src->ops->dsi.enable_hs(src, ddata->dsi->channel, true);

	return 0;
err:
	dev_err(&ddata->dsi->dev, "error while enabling panel, issuing HW reset\n");

	dsicm_hw_reset(ddata);

	src->ops->dsi.disable(src, true, false);
err_vddi:
	if (ddata->vddi)
		regulator_disable(ddata->vddi);
err_vpnl:
	if (ddata->vpnl)
		regulator_disable(ddata->vpnl);

	return r;
}

static void dsicm_power_off(struct panel_drv_data *ddata)
{
	struct omap_dss_device *src = ddata->src;
	int r;

	src->ops->dsi.disable_video_output(src, ddata->dsi->channel);

	r = mipi_dsi_dcs_set_display_off(ddata->dsi);
	if (!r)
		r = dsicm_sleep_in(ddata);

	if (r) {
		dev_err(&ddata->dsi->dev,
				"error disabling panel, issuing HW reset\n");
		dsicm_hw_reset(ddata);
	}

	src->ops->dsi.disable(src, true, false);

	if (ddata->vddi)
		regulator_disable(ddata->vddi);
	if (ddata->vpnl)
		regulator_disable(ddata->vpnl);

	ddata->enabled = false;
}

static int dsicm_panel_reset(struct panel_drv_data *ddata)
{
	dev_err(&ddata->dsi->dev, "performing LCD reset\n");

	dsicm_power_off(ddata);
	dsicm_hw_reset(ddata);
	return dsicm_power_on(ddata);
}

static int dsicm_connect(struct omap_dss_device *src,
			 struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dst);

	ddata->src = src;
	return 0;
}

static void dsicm_disconnect(struct omap_dss_device *src,
			     struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dst);

	ddata->src = NULL;
}

static void dsicm_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;
	int r;

	mutex_lock(&ddata->lock);

	src->ops->dsi.bus_lock(src);

	r = dsicm_power_on(ddata);

	src->ops->dsi.bus_unlock(src);

	if (r)
		goto err;

	mutex_unlock(&ddata->lock);

	dsicm_bl_power(ddata, true);

	return;
err:
	dev_dbg(&ddata->dsi->dev, "enable failed (%d)\n", r);
	mutex_unlock(&ddata->lock);
}

static void dsicm_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;
	int r;

	dsicm_bl_power(ddata, false);

	mutex_lock(&ddata->lock);

	dsicm_cancel_ulps_work(ddata);

	src->ops->dsi.bus_lock(src);

	r = dsicm_wake_up(ddata);
	if (!r)
		dsicm_power_off(ddata);

	src->ops->dsi.bus_unlock(src);

	mutex_unlock(&ddata->lock);
}

static void dsicm_framedone_cb(int err, void *data)
{
	struct panel_drv_data *ddata = data;
	struct omap_dss_device *src = ddata->src;

	dev_dbg(&ddata->dsi->dev, "framedone, err %d\n", err);
	src->ops->dsi.bus_unlock(src);
}

static irqreturn_t dsicm_te_isr(int irq, void *data)
{
	struct panel_drv_data *ddata = data;
	struct omap_dss_device *src = ddata->src;
	int old;
	int r;

	old = atomic_cmpxchg(&ddata->do_update, 1, 0);

	if (old) {
		cancel_delayed_work(&ddata->te_timeout_work);

		r = src->ops->dsi.update(src, ddata->dsi->channel, dsicm_framedone_cb,
				ddata);
		if (r)
			goto err;
	}

	return IRQ_HANDLED;
err:
	dev_err(&ddata->dsi->dev, "start update failed\n");
	src->ops->dsi.bus_unlock(src);
	return IRQ_HANDLED;
}

static void dsicm_te_timeout_work_callback(struct work_struct *work)
{
	struct panel_drv_data *ddata = container_of(work, struct panel_drv_data,
					te_timeout_work.work);
	struct omap_dss_device *src = ddata->src;

	dev_err(&ddata->dsi->dev, "TE not received for 250ms!\n");

	atomic_set(&ddata->do_update, 0);
	src->ops->dsi.bus_unlock(src);
}

static int dsicm_update(struct omap_dss_device *dssdev,
				    u16 x, u16 y, u16 w, u16 h)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;
	int r;

	dev_dbg(&ddata->dsi->dev, "update %d, %d, %d x %d\n", x, y, w, h);

	mutex_lock(&ddata->lock);
	src->ops->dsi.bus_lock(src);

	r = dsicm_wake_up(ddata);
	if (r)
		goto err;

	if (!ddata->enabled) {
		r = 0;
		goto err;
	}

	/* XXX no need to send this every frame, but dsi break if not done */
	r = dsicm_set_update_window(ddata, 0, 0, ddata->vm.hactive,
				    ddata->vm.vactive);
	if (r)
		goto err;

	if (ddata->te_enabled && ddata->ext_te_gpio) {
		schedule_delayed_work(&ddata->te_timeout_work,
				msecs_to_jiffies(250));
		atomic_set(&ddata->do_update, 1);
	} else {
		r = src->ops->dsi.update(src, ddata->dsi->channel, dsicm_framedone_cb,
				ddata);
		if (r)
			goto err;
	}

	/* note: no bus_unlock here. unlock is src framedone_cb */
	mutex_unlock(&ddata->lock);
	return 0;
err:
	src->ops->dsi.bus_unlock(src);
	mutex_unlock(&ddata->lock);
	return r;
}

static int _dsicm_enable_te(struct panel_drv_data *ddata, bool enable)
{
	struct omap_dss_device *src = ddata->src;
	struct mipi_dsi_device *dsi = ddata->dsi;
	int r;

	if (enable)
		r = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	else
		r = mipi_dsi_dcs_set_tear_off(dsi);

	if (!ddata->ext_te_gpio)
		src->ops->dsi.enable_te(src, enable);

	/* possible panel bug */
	msleep(100);

	return r;
}

static void dsicm_ulps_work(struct work_struct *work)
{
	struct panel_drv_data *ddata = container_of(work, struct panel_drv_data,
			ulps_work.work);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *src = ddata->src;

	mutex_lock(&ddata->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE || !ddata->enabled) {
		mutex_unlock(&ddata->lock);
		return;
	}

	src->ops->dsi.bus_lock(src);

	dsicm_enter_ulps(ddata);

	src->ops->dsi.bus_unlock(src);
	mutex_unlock(&ddata->lock);
}

static int dsicm_get_modes(struct omap_dss_device *dssdev,
			   struct drm_connector *connector)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	connector->display_info.width_mm = ddata->width_mm;
	connector->display_info.height_mm = ddata->height_mm;

	return omapdss_display_get_modes(connector, &ddata->vm);
}

static int dsicm_check_timings(struct omap_dss_device *dssdev,
			       struct drm_display_mode *mode)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	int ret = 0;

	if (mode->hdisplay != ddata->vm.hactive)
		ret = -EINVAL;

	if (mode->vdisplay != ddata->vm.vactive)
		ret = -EINVAL;

	if (ret) {
		dev_warn(dssdev->dev, "wrong resolution: %d x %d",
			 mode->hdisplay, mode->vdisplay);
		dev_warn(dssdev->dev, "panel resolution: %d x %d",
			 ddata->vm.hactive, ddata->vm.vactive);
	}

	return ret;
}

static const struct omap_dss_device_ops dsicm_ops = {
	.connect	= dsicm_connect,
	.disconnect	= dsicm_disconnect,

	.enable		= dsicm_enable,
	.disable	= dsicm_disable,

	.get_modes	= dsicm_get_modes,
	.check_timings	= dsicm_check_timings,
};

static const struct omap_dss_driver dsicm_dss_driver = {
	.update		= dsicm_update,
};

static int dsicm_probe_of(struct mipi_dsi_device *dsi)
{
	struct device_node *node = dsi->dev.of_node;
	struct backlight_device *backlight;
	struct panel_drv_data *ddata = mipi_dsi_get_drvdata(dsi);
	struct display_timing timing;
	int err;

	ddata->reset_gpio = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ddata->reset_gpio)) {
		err = PTR_ERR(ddata->reset_gpio);
		dev_err(&dsi->dev, "reset gpio request failed: %d", err);
		return err;
	}

	ddata->ext_te_gpio = devm_gpiod_get_optional(&dsi->dev, "te",
						     GPIOD_IN);
	if (IS_ERR(ddata->ext_te_gpio)) {
		err = PTR_ERR(ddata->ext_te_gpio);
		dev_err(&dsi->dev, "TE gpio request failed: %d", err);
		return err;
	}

	err = of_get_display_timing(node, "panel-timing", &timing);
	if (!err) {
		videomode_from_timing(&timing, &ddata->vm);
		if (!ddata->vm.pixelclock)
			ddata->vm.pixelclock =
				ddata->vm.hactive * ddata->vm.vactive * 60;
	} else {
		dev_warn(&dsi->dev,
			 "failed to get video timing, using defaults\n");
	}

	ddata->width_mm = 0;
	of_property_read_u32(node, "width-mm", &ddata->width_mm);

	ddata->height_mm = 0;
	of_property_read_u32(node, "height-mm", &ddata->height_mm);

	ddata->vpnl = devm_regulator_get_optional(&dsi->dev, "vpnl");
	if (IS_ERR(ddata->vpnl)) {
		err = PTR_ERR(ddata->vpnl);
		if (err == -EPROBE_DEFER)
			return err;
		ddata->vpnl = NULL;
	}

	ddata->vddi = devm_regulator_get_optional(&dsi->dev, "vddi");
	if (IS_ERR(ddata->vddi)) {
		err = PTR_ERR(ddata->vddi);
		if (err == -EPROBE_DEFER)
			return err;
		ddata->vddi = NULL;
	}

	backlight = devm_of_find_backlight(&dsi->dev);
	if (IS_ERR(backlight))
		return PTR_ERR(backlight);

	/* If no backlight device is found assume native backlight support */
	if (backlight)
		ddata->extbldev = backlight;
	else
		ddata->use_dsi_backlight = true;

	/* TODO: ulps */

	return 0;
}

static int dsicm_probe(struct mipi_dsi_device *dsi)
{
	struct panel_drv_data *ddata;
	struct backlight_device *bldev = NULL;
	struct device *dev = &dsi->dev;
	struct omap_dss_device *dssdev;
	int r;

	dev_dbg(dev, "probe\n");

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ddata);
	ddata->dsi = dsi;

	ddata->vm.hactive = 864;
	ddata->vm.vactive = 480;
	ddata->vm.pixelclock = 864 * 480 * 60;

	r = dsicm_probe_of(dsi);
	if (r)
		return r;

	dssdev = &ddata->dssdev;
	dssdev->dev = dev;
	dssdev->ops = &dsicm_ops;
	dssdev->driver = &dsicm_dss_driver;
	dssdev->type = OMAP_DISPLAY_TYPE_DSI;
	dssdev->display = true;
	dssdev->owner = THIS_MODULE;
	dssdev->of_port = 0;
	dssdev->ops_flags = OMAP_DSS_DEVICE_OP_MODES;

	dssdev->caps = OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE |
		OMAP_DSS_DISPLAY_CAP_TEAR_ELIM;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	mutex_init(&ddata->lock);

	atomic_set(&ddata->do_update, 0);

	if (ddata->ext_te_gpio) {
		r = devm_request_irq(dev, gpiod_to_irq(ddata->ext_te_gpio),
				dsicm_te_isr,
				IRQF_TRIGGER_RISING,
				"taal vsync", ddata);

		if (r) {
			dev_err(dev, "IRQ request failed\n");
			goto err_reg;
		}

		INIT_DEFERRABLE_WORK(&ddata->te_timeout_work,
					dsicm_te_timeout_work_callback);

		dev_dbg(dev, "Using GPIO TE\n");
	}

	ddata->workqueue = create_singlethread_workqueue("dsicm_wq");
	if (!ddata->workqueue) {
		r = -ENOMEM;
		goto err_reg;
	}
	INIT_DELAYED_WORK(&ddata->ulps_work, dsicm_ulps_work);

	dsicm_hw_reset(ddata);

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
	dsi->hs_rate = 300000000;
	dsi->lp_rate = 10000000;

	r = mipi_dsi_attach(dsi);
	if (r < 0)
		goto err_dsi_attach;

	return 0;

err_dsi_attach:
	sysfs_remove_group(&dsi->dev.kobj, &dsicm_attr_group);
err_bl:
	destroy_workqueue(ddata->workqueue);
err_reg:
	if (ddata->extbldev)
		put_device(&ddata->extbldev->dev);

	return r;
}

static int __exit dsicm_remove(struct mipi_dsi_device *dsi)
{
	struct panel_drv_data *ddata = mipi_dsi_get_drvdata(dsi);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	dev_dbg(&dsi->dev, "remove\n");

	mipi_dsi_detach(dsi);

	omapdss_device_unregister(dssdev);

	if (omapdss_device_is_enabled(dssdev))
		dsicm_disable(dssdev);
	omapdss_device_disconnect(ddata->src, dssdev);

	sysfs_remove_group(&dsi->dev.kobj, &dsicm_attr_group);

	if (ddata->extbldev)
		put_device(&ddata->extbldev->dev);

	dsicm_cancel_ulps_work(ddata);
	destroy_workqueue(ddata->workqueue);

	/* reset, to be sure that the panel is in a valid state */
	dsicm_hw_reset(ddata);

	return 0;
}

static const struct of_device_id dsicm_of_match[] = {
	{ .compatible = "omapdss,panel-dsi-cm", },
	{},
};

MODULE_DEVICE_TABLE(of, dsicm_of_match);

static struct mipi_dsi_driver dsicm_driver = {
	.probe = dsicm_probe,
	.remove = __exit_p(dsicm_remove),
	.driver = {
		.name = "panel-dsi-cm",
		.of_match_table = dsicm_of_match,
		.suppress_bind_attrs = true,
	},
};
module_mipi_dsi_driver(dsicm_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Generic DSI Command Mode Panel Driver");
MODULE_LICENSE("GPL");
