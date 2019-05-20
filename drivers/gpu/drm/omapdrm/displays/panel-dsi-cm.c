/*
 * Generic DSI Command Mode panel driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/* #define DEBUG */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>

#include <video/mipi_display.h>
#include <video/of_display_timing.h>

#include "../dss/omapdss.h"

/* DSI Virtual channel. Hardcoded for now. */
#define TCH 0

#define DCS_READ_NUM_ERRORS	0x05
#define DCS_BRIGHTNESS		0x51
#define DCS_CTRL_DISPLAY	0x53
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *src;

	struct videomode vm;

	struct platform_device *pdev;

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

	struct omap_dsi_pin_config pin_config;

	/* runtime variables */
	bool enabled;

	bool te_enabled;

	atomic_t do_update;
	int channel;

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
	struct omap_dss_device *src = ddata->src;
	int r;
	u8 buf[1];

	r = src->ops->dsi.dcs_read(src, ddata->channel, dcs_cmd, buf, 1);

	if (r < 0)
		return r;

	*data = buf[0];

	return 0;
}

static int dsicm_dcs_write_0(struct panel_drv_data *ddata, u8 dcs_cmd)
{
	struct omap_dss_device *src = ddata->src;

	return src->ops->dsi.dcs_write(src, ddata->channel, &dcs_cmd, 1);
}

static int dsicm_dcs_write_1(struct panel_drv_data *ddata, u8 dcs_cmd, u8 param)
{
	struct omap_dss_device *src = ddata->src;
	u8 buf[2] = { dcs_cmd, param };

	return src->ops->dsi.dcs_write(src, ddata->channel, buf, 2);
}

static int dsicm_sleep_in(struct panel_drv_data *ddata)

{
	struct omap_dss_device *src = ddata->src;
	u8 cmd;
	int r;

	hw_guard_wait(ddata);

	cmd = MIPI_DCS_ENTER_SLEEP_MODE;
	r = src->ops->dsi.dcs_write_nosync(src, ddata->channel, &cmd, 1);
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

	r = dsicm_dcs_write_0(ddata, MIPI_DCS_EXIT_SLEEP_MODE);
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
	struct omap_dss_device *src = ddata->src;
	int r;
	u16 x1 = x;
	u16 x2 = x + w - 1;
	u16 y1 = y;
	u16 y2 = y + h - 1;

	u8 buf[5];
	buf[0] = MIPI_DCS_SET_COLUMN_ADDRESS;
	buf[1] = (x1 >> 8) & 0xff;
	buf[2] = (x1 >> 0) & 0xff;
	buf[3] = (x2 >> 8) & 0xff;
	buf[4] = (x2 >> 0) & 0xff;

	r = src->ops->dsi.dcs_write_nosync(src, ddata->channel, buf, sizeof(buf));
	if (r)
		return r;

	buf[0] = MIPI_DCS_SET_PAGE_ADDRESS;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	r = src->ops->dsi.dcs_write_nosync(src, ddata->channel, buf, sizeof(buf));
	if (r)
		return r;

	src->ops->dsi.bta_sync(src, ddata->channel);

	return r;
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
	dev_err(&ddata->pdev->dev, "enter ULPS failed");
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
	src->ops->dsi.enable_hs(src, ddata->channel, true);

	r = _dsicm_enable_te(ddata, true);
	if (r) {
		dev_err(&ddata->pdev->dev, "failed to re-enable TE");
		goto err2;
	}

	if (ddata->ext_te_gpio)
		enable_irq(gpiod_to_irq(ddata->ext_te_gpio));

	dsicm_queue_ulps_work(ddata);

	ddata->ulps_enabled = false;

	return 0;

err2:
	dev_err(&ddata->pdev->dev, "failed to exit ULPS");

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

	dev_dbg(&ddata->pdev->dev, "update brightness to %d\n", level);

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		src->ops->dsi.bus_lock(src);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_dcs_write_1(ddata, DCS_BRIGHTNESS, level);

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
	struct platform_device *pdev = to_platform_device(dev);
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
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
	struct platform_device *pdev = to_platform_device(dev);
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
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
	struct platform_device *pdev = to_platform_device(dev);
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
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
	struct platform_device *pdev = to_platform_device(dev);
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
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
	struct platform_device *pdev = to_platform_device(dev);
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
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
	struct platform_device *pdev = to_platform_device(dev);
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
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
		.pixel_format = OMAP_DSS_DSI_FMT_RGB888,
		.vm = &ddata->vm,
		.hs_clk_min = 150000000,
		.hs_clk_max = 300000000,
		.lp_clk_min = 7000000,
		.lp_clk_max = 10000000,
	};

	if (ddata->vpnl) {
		r = regulator_enable(ddata->vpnl);
		if (r) {
			dev_err(&ddata->pdev->dev,
				"failed to enable VPNL: %d\n", r);
			return r;
		}
	}

	if (ddata->vddi) {
		r = regulator_enable(ddata->vddi);
		if (r) {
			dev_err(&ddata->pdev->dev,
				"failed to enable VDDI: %d\n", r);
			goto err_vpnl;
		}
	}

	if (ddata->pin_config.num_pins > 0) {
		r = src->ops->dsi.configure_pins(src, &ddata->pin_config);
		if (r) {
			dev_err(&ddata->pdev->dev,
				"failed to configure DSI pins\n");
			goto err_vddi;
		}
	}

	r = src->ops->dsi.set_config(src, &dsi_config);
	if (r) {
		dev_err(&ddata->pdev->dev, "failed to configure DSI\n");
		goto err_vddi;
	}

	src->ops->enable(src);

	dsicm_hw_reset(ddata);

	src->ops->dsi.enable_hs(src, ddata->channel, false);

	r = dsicm_sleep_out(ddata);
	if (r)
		goto err;

	r = dsicm_get_id(ddata, &id1, &id2, &id3);
	if (r)
		goto err;

	r = dsicm_dcs_write_1(ddata, DCS_BRIGHTNESS, 0xff);
	if (r)
		goto err;

	r = dsicm_dcs_write_1(ddata, DCS_CTRL_DISPLAY,
			(1<<2) | (1<<5));	/* BL | BCTRL */
	if (r)
		goto err;

	r = dsicm_dcs_write_1(ddata, MIPI_DCS_SET_PIXEL_FORMAT,
		MIPI_DCS_PIXEL_FMT_24BIT);
	if (r)
		goto err;

	r = dsicm_dcs_write_0(ddata, MIPI_DCS_SET_DISPLAY_ON);
	if (r)
		goto err;

	r = _dsicm_enable_te(ddata, ddata->te_enabled);
	if (r)
		goto err;

	r = src->ops->dsi.enable_video_output(src, ddata->channel);
	if (r)
		goto err;

	ddata->enabled = 1;

	if (!ddata->intro_printed) {
		dev_info(&ddata->pdev->dev, "panel revision %02x.%02x.%02x\n",
			id1, id2, id3);
		ddata->intro_printed = true;
	}

	src->ops->dsi.enable_hs(src, ddata->channel, true);

	return 0;
err:
	dev_err(&ddata->pdev->dev, "error while enabling panel, issuing HW reset\n");

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

	src->ops->dsi.disable_video_output(src, ddata->channel);

	r = dsicm_dcs_write_0(ddata, MIPI_DCS_SET_DISPLAY_OFF);
	if (!r)
		r = dsicm_sleep_in(ddata);

	if (r) {
		dev_err(&ddata->pdev->dev,
				"error disabling panel, issuing HW reset\n");
		dsicm_hw_reset(ddata);
	}

	src->ops->dsi.disable(src, true, false);

	if (ddata->vddi)
		regulator_disable(ddata->vddi);
	if (ddata->vpnl)
		regulator_disable(ddata->vpnl);

	ddata->enabled = 0;
}

static int dsicm_panel_reset(struct panel_drv_data *ddata)
{
	dev_err(&ddata->pdev->dev, "performing LCD reset\n");

	dsicm_power_off(ddata);
	dsicm_hw_reset(ddata);
	return dsicm_power_on(ddata);
}

static int dsicm_connect(struct omap_dss_device *src,
			 struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dst);
	struct device *dev = &ddata->pdev->dev;
	int r;

	r = src->ops->dsi.request_vc(src, &ddata->channel);
	if (r) {
		dev_err(dev, "failed to get virtual channel\n");
		return r;
	}

	r = src->ops->dsi.set_vc_id(src, ddata->channel, TCH);
	if (r) {
		dev_err(dev, "failed to set VC_ID\n");
		src->ops->dsi.release_vc(src, ddata->channel);
		return r;
	}

	ddata->src = src;
	return 0;
}

static void dsicm_disconnect(struct omap_dss_device *src,
			     struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dst);

	src->ops->dsi.release_vc(src, ddata->channel);
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
	dev_dbg(&ddata->pdev->dev, "enable failed (%d)\n", r);
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

	dev_dbg(&ddata->pdev->dev, "framedone, err %d\n", err);
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

		r = src->ops->dsi.update(src, ddata->channel, dsicm_framedone_cb,
				ddata);
		if (r)
			goto err;
	}

	return IRQ_HANDLED;
err:
	dev_err(&ddata->pdev->dev, "start update failed\n");
	src->ops->dsi.bus_unlock(src);
	return IRQ_HANDLED;
}

static void dsicm_te_timeout_work_callback(struct work_struct *work)
{
	struct panel_drv_data *ddata = container_of(work, struct panel_drv_data,
					te_timeout_work.work);
	struct omap_dss_device *src = ddata->src;

	dev_err(&ddata->pdev->dev, "TE not received for 250ms!\n");

	atomic_set(&ddata->do_update, 0);
	src->ops->dsi.bus_unlock(src);
}

static int dsicm_update(struct omap_dss_device *dssdev,
				    u16 x, u16 y, u16 w, u16 h)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;
	int r;

	dev_dbg(&ddata->pdev->dev, "update %d, %d, %d x %d\n", x, y, w, h);

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
		r = src->ops->dsi.update(src, ddata->channel, dsicm_framedone_cb,
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

static int dsicm_sync(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;

	dev_dbg(&ddata->pdev->dev, "sync\n");

	mutex_lock(&ddata->lock);
	src->ops->dsi.bus_lock(src);
	src->ops->dsi.bus_unlock(src);
	mutex_unlock(&ddata->lock);

	dev_dbg(&ddata->pdev->dev, "sync done\n");

	return 0;
}

static int _dsicm_enable_te(struct panel_drv_data *ddata, bool enable)
{
	struct omap_dss_device *src = ddata->src;
	int r;

	if (enable)
		r = dsicm_dcs_write_1(ddata, MIPI_DCS_SET_TEAR_ON, 0);
	else
		r = dsicm_dcs_write_0(ddata, MIPI_DCS_SET_TEAR_OFF);

	if (!ddata->ext_te_gpio)
		src->ops->dsi.enable_te(src, enable);

	/* possible panel bug */
	msleep(100);

	return r;
}

static int dsicm_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;
	int r;

	mutex_lock(&ddata->lock);

	if (ddata->te_enabled == enable)
		goto end;

	src->ops->dsi.bus_lock(src);

	if (ddata->enabled) {
		r = dsicm_wake_up(ddata);
		if (r)
			goto err;

		r = _dsicm_enable_te(ddata, enable);
		if (r)
			goto err;
	}

	ddata->te_enabled = enable;

	src->ops->dsi.bus_unlock(src);
end:
	mutex_unlock(&ddata->lock);

	return 0;
err:
	src->ops->dsi.bus_unlock(src);
	mutex_unlock(&ddata->lock);

	return r;
}

static int dsicm_get_te(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	int r;

	mutex_lock(&ddata->lock);
	r = ddata->te_enabled;
	mutex_unlock(&ddata->lock);

	return r;
}

static int dsicm_memory_read(struct omap_dss_device *dssdev,
		void *buf, size_t size,
		u16 x, u16 y, u16 w, u16 h)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = ddata->src;
	int r;
	int first = 1;
	int plen;
	unsigned int buf_used = 0;

	if (size < w * h * 3)
		return -ENOMEM;

	mutex_lock(&ddata->lock);

	if (!ddata->enabled) {
		r = -ENODEV;
		goto err1;
	}

	size = min((u32)w * h * 3,
		   ddata->vm.hactive * ddata->vm.vactive * 3);

	src->ops->dsi.bus_lock(src);

	r = dsicm_wake_up(ddata);
	if (r)
		goto err2;

	/* plen 1 or 2 goes into short packet. until checksum error is fixed,
	 * use short packets. plen 32 works, but bigger packets seem to cause
	 * an error. */
	if (size % 2)
		plen = 1;
	else
		plen = 2;

	dsicm_set_update_window(ddata, x, y, w, h);

	r = src->ops->dsi.set_max_rx_packet_size(src, ddata->channel, plen);
	if (r)
		goto err2;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = src->ops->dsi.dcs_read(src, ddata->channel, dcs_cmd,
				buf + buf_used, size - buf_used);

		if (r < 0) {
			dev_err(dssdev->dev, "read error\n");
			goto err3;
		}

		buf_used += r;

		if (r < plen) {
			dev_err(&ddata->pdev->dev, "short read\n");
			break;
		}

		if (signal_pending(current)) {
			dev_err(&ddata->pdev->dev, "signal pending, "
					"aborting memory read\n");
			r = -ERESTARTSYS;
			goto err3;
		}
	}

	r = buf_used;

err3:
	src->ops->dsi.set_max_rx_packet_size(src, ddata->channel, 1);
err2:
	src->ops->dsi.bus_unlock(src);
err1:
	mutex_unlock(&ddata->lock);
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
	.sync		= dsicm_sync,

	.enable_te	= dsicm_enable_te,
	.get_te		= dsicm_get_te,

	.memory_read	= dsicm_memory_read,
};

static int dsicm_probe_of(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *backlight;
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct display_timing timing;
	int err;

	ddata->reset_gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ddata->reset_gpio)) {
		err = PTR_ERR(ddata->reset_gpio);
		dev_err(&pdev->dev, "reset gpio request failed: %d", err);
		return err;
	}

	ddata->ext_te_gpio = devm_gpiod_get_optional(&pdev->dev, "te",
						     GPIOD_IN);
	if (IS_ERR(ddata->ext_te_gpio)) {
		err = PTR_ERR(ddata->ext_te_gpio);
		dev_err(&pdev->dev, "TE gpio request failed: %d", err);
		return err;
	}

	err = of_get_display_timing(node, "panel-timing", &timing);
	if (!err) {
		videomode_from_timing(&timing, &ddata->vm);
		if (!ddata->vm.pixelclock)
			ddata->vm.pixelclock =
				ddata->vm.hactive * ddata->vm.vactive * 60;
	} else {
		dev_warn(&pdev->dev,
			 "failed to get video timing, using defaults\n");
	}

	ddata->width_mm = 0;
	of_property_read_u32(node, "width-mm", &ddata->width_mm);

	ddata->height_mm = 0;
	of_property_read_u32(node, "height-mm", &ddata->height_mm);

	ddata->vpnl = devm_regulator_get_optional(&pdev->dev, "vpnl");
	if (IS_ERR(ddata->vpnl)) {
		err = PTR_ERR(ddata->vpnl);
		if (err == -EPROBE_DEFER)
			return err;
		ddata->vpnl = NULL;
	}

	ddata->vddi = devm_regulator_get_optional(&pdev->dev, "vddi");
	if (IS_ERR(ddata->vddi)) {
		err = PTR_ERR(ddata->vddi);
		if (err == -EPROBE_DEFER)
			return err;
		ddata->vddi = NULL;
	}

	backlight = of_parse_phandle(node, "backlight", 0);
	if (backlight) {
		ddata->extbldev = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ddata->extbldev)
			return -EPROBE_DEFER;
	} else {
		/* assume native backlight support */
		ddata->use_dsi_backlight = true;
	}

	/* TODO: ulps */

	return 0;
}

static int dsicm_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct backlight_device *bldev = NULL;
	struct device *dev = &pdev->dev;
	struct omap_dss_device *dssdev;
	int r;

	dev_dbg(dev, "probe\n");

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);
	ddata->pdev = pdev;

	ddata->vm.hactive = 864;
	ddata->vm.vactive = 480;
	ddata->vm.pixelclock = 864 * 480 * 60;

	r = dsicm_probe_of(pdev);
	if (r)
		return r;

	dssdev = &ddata->dssdev;
	dssdev->dev = dev;
	dssdev->ops = &dsicm_ops;
	dssdev->driver = &dsicm_dss_driver;
	dssdev->type = OMAP_DISPLAY_TYPE_DSI;
	dssdev->display = true;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(0);
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

	return 0;

err_bl:
	destroy_workqueue(ddata->workqueue);
err_reg:
	if (ddata->extbldev)
		put_device(&ddata->extbldev->dev);

	return r;
}

static int __exit dsicm_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	dev_dbg(&pdev->dev, "remove\n");

	omapdss_device_unregister(dssdev);

	if (omapdss_device_is_enabled(dssdev))
		dsicm_disable(dssdev);
	omapdss_device_disconnect(ddata->src, dssdev);

	sysfs_remove_group(&pdev->dev.kobj, &dsicm_attr_group);

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

static struct platform_driver dsicm_driver = {
	.probe = dsicm_probe,
	.remove = __exit_p(dsicm_remove),
	.driver = {
		.name = "panel-dsi-cm",
		.of_match_table = dsicm_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(dsicm_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Generic DSI Command Mode Panel Driver");
MODULE_LICENSE("GPL");
