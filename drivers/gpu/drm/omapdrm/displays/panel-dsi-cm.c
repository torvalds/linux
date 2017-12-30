/*
 * Generic DSI Command Mode panel driver
 *
 * Copyright (C) 2013 Texas Instruments
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
#include <linux/of_gpio.h>

#include <video/mipi_display.h>

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
	struct omap_dss_device *in;

	struct videomode vm;

	struct platform_device *pdev;

	struct mutex lock;

	struct backlight_device *bldev;

	unsigned long	hw_guard_end;	/* next value of jiffies when we can
					 * issue the next sleep in/out command
					 */
	unsigned long	hw_guard_wait;	/* max guard time in jiffies */

	/* panel HW configuration from DT or platform data */
	int reset_gpio;
	int ext_te_gpio;

	bool use_dsi_backlight;

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
	unsigned ulps_timeout;
	struct delayed_work ulps_work;
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static irqreturn_t dsicm_te_isr(int irq, void *data);
static void dsicm_te_timeout_work_callback(struct work_struct *work);
static int _dsicm_enable_te(struct panel_drv_data *ddata, bool enable);

static int dsicm_panel_reset(struct panel_drv_data *ddata);

static void dsicm_ulps_work(struct work_struct *work);

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
	struct omap_dss_device *in = ddata->in;
	int r;
	u8 buf[1];

	r = in->ops.dsi->dcs_read(in, ddata->channel, dcs_cmd, buf, 1);

	if (r < 0)
		return r;

	*data = buf[0];

	return 0;
}

static int dsicm_dcs_write_0(struct panel_drv_data *ddata, u8 dcs_cmd)
{
	struct omap_dss_device *in = ddata->in;
	return in->ops.dsi->dcs_write(in, ddata->channel, &dcs_cmd, 1);
}

static int dsicm_dcs_write_1(struct panel_drv_data *ddata, u8 dcs_cmd, u8 param)
{
	struct omap_dss_device *in = ddata->in;
	u8 buf[2] = { dcs_cmd, param };

	return in->ops.dsi->dcs_write(in, ddata->channel, buf, 2);
}

static int dsicm_sleep_in(struct panel_drv_data *ddata)

{
	struct omap_dss_device *in = ddata->in;
	u8 cmd;
	int r;

	hw_guard_wait(ddata);

	cmd = MIPI_DCS_ENTER_SLEEP_MODE;
	r = in->ops.dsi->dcs_write_nosync(in, ddata->channel, &cmd, 1);
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
	struct omap_dss_device *in = ddata->in;
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

	r = in->ops.dsi->dcs_write_nosync(in, ddata->channel, buf, sizeof(buf));
	if (r)
		return r;

	buf[0] = MIPI_DCS_SET_PAGE_ADDRESS;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	r = in->ops.dsi->dcs_write_nosync(in, ddata->channel, buf, sizeof(buf));
	if (r)
		return r;

	in->ops.dsi->bta_sync(in, ddata->channel);

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
	struct omap_dss_device *in = ddata->in;
	int r;

	if (ddata->ulps_enabled)
		return 0;

	dsicm_cancel_ulps_work(ddata);

	r = _dsicm_enable_te(ddata, false);
	if (r)
		goto err;

	if (gpio_is_valid(ddata->ext_te_gpio))
		disable_irq(gpio_to_irq(ddata->ext_te_gpio));

	in->ops.dsi->disable(in, false, true);

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
	struct omap_dss_device *in = ddata->in;
	int r;

	if (!ddata->ulps_enabled)
		return 0;

	r = in->ops.dsi->enable(in);
	if (r) {
		dev_err(&ddata->pdev->dev, "failed to enable DSI\n");
		goto err1;
	}

	in->ops.dsi->enable_hs(in, ddata->channel, true);

	r = _dsicm_enable_te(ddata, true);
	if (r) {
		dev_err(&ddata->pdev->dev, "failed to re-enable TE");
		goto err2;
	}

	if (gpio_is_valid(ddata->ext_te_gpio))
		enable_irq(gpio_to_irq(ddata->ext_te_gpio));

	dsicm_queue_ulps_work(ddata);

	ddata->ulps_enabled = false;

	return 0;

err2:
	dev_err(&ddata->pdev->dev, "failed to exit ULPS");

	r = dsicm_panel_reset(ddata);
	if (!r) {
		if (gpio_is_valid(ddata->ext_te_gpio))
			enable_irq(gpio_to_irq(ddata->ext_te_gpio));
		ddata->ulps_enabled = false;
	}
err1:
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
	struct omap_dss_device *in = ddata->in;
	int r;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&ddata->pdev->dev, "update brightness to %d\n", level);

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		in->ops.dsi->bus_lock(in);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_dcs_write_1(ddata, DCS_BRIGHTNESS, level);

		in->ops.dsi->bus_unlock(in);
	} else {
		r = 0;
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
	struct omap_dss_device *in = ddata->in;
	u8 errors = 0;
	int r;

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		in->ops.dsi->bus_lock(in);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_dcs_read_1(ddata, DCS_READ_NUM_ERRORS,
					&errors);

		in->ops.dsi->bus_unlock(in);
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
	struct omap_dss_device *in = ddata->in;
	u8 id1, id2, id3;
	int r;

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		in->ops.dsi->bus_lock(in);

		r = dsicm_wake_up(ddata);
		if (!r)
			r = dsicm_get_id(ddata, &id1, &id2, &id3);

		in->ops.dsi->bus_unlock(in);
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
	struct omap_dss_device *in = ddata->in;
	unsigned long t;
	int r;

	r = kstrtoul(buf, 0, &t);
	if (r)
		return r;

	mutex_lock(&ddata->lock);

	if (ddata->enabled) {
		in->ops.dsi->bus_lock(in);

		if (t)
			r = dsicm_enter_ulps(ddata);
		else
			r = dsicm_wake_up(ddata);

		in->ops.dsi->bus_unlock(in);
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
	unsigned t;

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
	struct omap_dss_device *in = ddata->in;
	unsigned long t;
	int r;

	r = kstrtoul(buf, 0, &t);
	if (r)
		return r;

	mutex_lock(&ddata->lock);
	ddata->ulps_timeout = t;

	if (ddata->enabled) {
		/* dsicm_wake_up will restart the timer */
		in->ops.dsi->bus_lock(in);
		r = dsicm_wake_up(ddata);
		in->ops.dsi->bus_unlock(in);
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
	unsigned t;

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
	if (!gpio_is_valid(ddata->reset_gpio))
		return;

	gpio_set_value(ddata->reset_gpio, 1);
	udelay(10);
	/* reset the panel */
	gpio_set_value(ddata->reset_gpio, 0);
	/* assert reset */
	udelay(10);
	gpio_set_value(ddata->reset_gpio, 1);
	/* wait after releasing reset */
	usleep_range(5000, 10000);
}

static int dsicm_power_on(struct panel_drv_data *ddata)
{
	struct omap_dss_device *in = ddata->in;
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

	if (ddata->pin_config.num_pins > 0) {
		r = in->ops.dsi->configure_pins(in, &ddata->pin_config);
		if (r) {
			dev_err(&ddata->pdev->dev,
				"failed to configure DSI pins\n");
			goto err0;
		}
	}

	r = in->ops.dsi->set_config(in, &dsi_config);
	if (r) {
		dev_err(&ddata->pdev->dev, "failed to configure DSI\n");
		goto err0;
	}

	r = in->ops.dsi->enable(in);
	if (r) {
		dev_err(&ddata->pdev->dev, "failed to enable DSI\n");
		goto err0;
	}

	dsicm_hw_reset(ddata);

	in->ops.dsi->enable_hs(in, ddata->channel, false);

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

	r = in->ops.dsi->enable_video_output(in, ddata->channel);
	if (r)
		goto err;

	ddata->enabled = 1;

	if (!ddata->intro_printed) {
		dev_info(&ddata->pdev->dev, "panel revision %02x.%02x.%02x\n",
			id1, id2, id3);
		ddata->intro_printed = true;
	}

	in->ops.dsi->enable_hs(in, ddata->channel, true);

	return 0;
err:
	dev_err(&ddata->pdev->dev, "error while enabling panel, issuing HW reset\n");

	dsicm_hw_reset(ddata);

	in->ops.dsi->disable(in, true, false);
err0:
	return r;
}

static void dsicm_power_off(struct panel_drv_data *ddata)
{
	struct omap_dss_device *in = ddata->in;
	int r;

	in->ops.dsi->disable_video_output(in, ddata->channel);

	r = dsicm_dcs_write_0(ddata, MIPI_DCS_SET_DISPLAY_OFF);
	if (!r)
		r = dsicm_sleep_in(ddata);

	if (r) {
		dev_err(&ddata->pdev->dev,
				"error disabling panel, issuing HW reset\n");
		dsicm_hw_reset(ddata);
	}

	in->ops.dsi->disable(in, true, false);

	ddata->enabled = 0;
}

static int dsicm_panel_reset(struct panel_drv_data *ddata)
{
	dev_err(&ddata->pdev->dev, "performing LCD reset\n");

	dsicm_power_off(ddata);
	dsicm_hw_reset(ddata);
	return dsicm_power_on(ddata);
}

static int dsicm_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	struct device *dev = &ddata->pdev->dev;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.dsi->connect(in, dssdev);
	if (r) {
		dev_err(dev, "Failed to connect to video source\n");
		return r;
	}

	r = in->ops.dsi->request_vc(ddata->in, &ddata->channel);
	if (r) {
		dev_err(dev, "failed to get virtual channel\n");
		goto err_req_vc;
	}

	r = in->ops.dsi->set_vc_id(ddata->in, ddata->channel, TCH);
	if (r) {
		dev_err(dev, "failed to set VC_ID\n");
		goto err_vc_id;
	}

	return 0;

err_vc_id:
	in->ops.dsi->release_vc(ddata->in, ddata->channel);
err_req_vc:
	in->ops.dsi->disconnect(in, dssdev);
	return r;
}

static void dsicm_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dsi->release_vc(in, ddata->channel);
	in->ops.dsi->disconnect(in, dssdev);
}

static int dsicm_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(&ddata->pdev->dev, "enable\n");

	mutex_lock(&ddata->lock);

	if (!omapdss_device_is_connected(dssdev)) {
		r = -ENODEV;
		goto err;
	}

	if (omapdss_device_is_enabled(dssdev)) {
		r = 0;
		goto err;
	}

	in->ops.dsi->bus_lock(in);

	r = dsicm_power_on(ddata);

	in->ops.dsi->bus_unlock(in);

	if (r)
		goto err;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	mutex_unlock(&ddata->lock);

	return 0;
err:
	dev_dbg(&ddata->pdev->dev, "enable failed\n");
	mutex_unlock(&ddata->lock);
	return r;
}

static void dsicm_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(&ddata->pdev->dev, "disable\n");

	mutex_lock(&ddata->lock);

	dsicm_cancel_ulps_work(ddata);

	in->ops.dsi->bus_lock(in);

	if (omapdss_device_is_enabled(dssdev)) {
		r = dsicm_wake_up(ddata);
		if (!r)
			dsicm_power_off(ddata);
	}

	in->ops.dsi->bus_unlock(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&ddata->lock);
}

static void dsicm_framedone_cb(int err, void *data)
{
	struct panel_drv_data *ddata = data;
	struct omap_dss_device *in = ddata->in;

	dev_dbg(&ddata->pdev->dev, "framedone, err %d\n", err);
	in->ops.dsi->bus_unlock(ddata->in);
}

static irqreturn_t dsicm_te_isr(int irq, void *data)
{
	struct panel_drv_data *ddata = data;
	struct omap_dss_device *in = ddata->in;
	int old;
	int r;

	old = atomic_cmpxchg(&ddata->do_update, 1, 0);

	if (old) {
		cancel_delayed_work(&ddata->te_timeout_work);

		r = in->ops.dsi->update(in, ddata->channel, dsicm_framedone_cb,
				ddata);
		if (r)
			goto err;
	}

	return IRQ_HANDLED;
err:
	dev_err(&ddata->pdev->dev, "start update failed\n");
	in->ops.dsi->bus_unlock(in);
	return IRQ_HANDLED;
}

static void dsicm_te_timeout_work_callback(struct work_struct *work)
{
	struct panel_drv_data *ddata = container_of(work, struct panel_drv_data,
					te_timeout_work.work);
	struct omap_dss_device *in = ddata->in;

	dev_err(&ddata->pdev->dev, "TE not received for 250ms!\n");

	atomic_set(&ddata->do_update, 0);
	in->ops.dsi->bus_unlock(in);
}

static int dsicm_update(struct omap_dss_device *dssdev,
				    u16 x, u16 y, u16 w, u16 h)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(&ddata->pdev->dev, "update %d, %d, %d x %d\n", x, y, w, h);

	mutex_lock(&ddata->lock);
	in->ops.dsi->bus_lock(in);

	r = dsicm_wake_up(ddata);
	if (r)
		goto err;

	if (!ddata->enabled) {
		r = 0;
		goto err;
	}

	/* XXX no need to send this every frame, but dsi break if not done */
	r = dsicm_set_update_window(ddata, 0, 0,
			dssdev->panel.vm.hactive,
			dssdev->panel.vm.vactive);
	if (r)
		goto err;

	if (ddata->te_enabled && gpio_is_valid(ddata->ext_te_gpio)) {
		schedule_delayed_work(&ddata->te_timeout_work,
				msecs_to_jiffies(250));
		atomic_set(&ddata->do_update, 1);
	} else {
		r = in->ops.dsi->update(in, ddata->channel, dsicm_framedone_cb,
				ddata);
		if (r)
			goto err;
	}

	/* note: no bus_unlock here. unlock is in framedone_cb */
	mutex_unlock(&ddata->lock);
	return 0;
err:
	in->ops.dsi->bus_unlock(in);
	mutex_unlock(&ddata->lock);
	return r;
}

static int dsicm_sync(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(&ddata->pdev->dev, "sync\n");

	mutex_lock(&ddata->lock);
	in->ops.dsi->bus_lock(in);
	in->ops.dsi->bus_unlock(in);
	mutex_unlock(&ddata->lock);

	dev_dbg(&ddata->pdev->dev, "sync done\n");

	return 0;
}

static int _dsicm_enable_te(struct panel_drv_data *ddata, bool enable)
{
	struct omap_dss_device *in = ddata->in;
	int r;

	if (enable)
		r = dsicm_dcs_write_1(ddata, MIPI_DCS_SET_TEAR_ON, 0);
	else
		r = dsicm_dcs_write_0(ddata, MIPI_DCS_SET_TEAR_OFF);

	if (!gpio_is_valid(ddata->ext_te_gpio))
		in->ops.dsi->enable_te(in, enable);

	/* possible panel bug */
	msleep(100);

	return r;
}

static int dsicm_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	mutex_lock(&ddata->lock);

	if (ddata->te_enabled == enable)
		goto end;

	in->ops.dsi->bus_lock(in);

	if (ddata->enabled) {
		r = dsicm_wake_up(ddata);
		if (r)
			goto err;

		r = _dsicm_enable_te(ddata, enable);
		if (r)
			goto err;
	}

	ddata->te_enabled = enable;

	in->ops.dsi->bus_unlock(in);
end:
	mutex_unlock(&ddata->lock);

	return 0;
err:
	in->ops.dsi->bus_unlock(in);
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
	struct omap_dss_device *in = ddata->in;
	int r;
	int first = 1;
	int plen;
	unsigned buf_used = 0;

	if (size < w * h * 3)
		return -ENOMEM;

	mutex_lock(&ddata->lock);

	if (!ddata->enabled) {
		r = -ENODEV;
		goto err1;
	}

	size = min((u32)w * h * 3,
		   dssdev->panel.vm.hactive * dssdev->panel.vm.vactive * 3);

	in->ops.dsi->bus_lock(in);

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

	r = in->ops.dsi->set_max_rx_packet_size(in, ddata->channel, plen);
	if (r)
		goto err2;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = in->ops.dsi->dcs_read(in, ddata->channel, dcs_cmd,
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
	in->ops.dsi->set_max_rx_packet_size(in, ddata->channel, 1);
err2:
	in->ops.dsi->bus_unlock(in);
err1:
	mutex_unlock(&ddata->lock);
	return r;
}

static void dsicm_ulps_work(struct work_struct *work)
{
	struct panel_drv_data *ddata = container_of(work, struct panel_drv_data,
			ulps_work.work);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	mutex_lock(&ddata->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE || !ddata->enabled) {
		mutex_unlock(&ddata->lock);
		return;
	}

	in->ops.dsi->bus_lock(in);

	dsicm_enter_ulps(ddata);

	in->ops.dsi->bus_unlock(in);
	mutex_unlock(&ddata->lock);
}

static struct omap_dss_driver dsicm_ops = {
	.connect	= dsicm_connect,
	.disconnect	= dsicm_disconnect,

	.enable		= dsicm_enable,
	.disable	= dsicm_disable,

	.update		= dsicm_update,
	.sync		= dsicm_sync,

	.enable_te	= dsicm_enable_te,
	.get_te		= dsicm_get_te,

	.memory_read	= dsicm_memory_read,
};

static int dsicm_probe_of(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *in;
	int gpio;

	gpio = of_get_named_gpio(node, "reset-gpios", 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "failed to parse reset gpio\n");
		return gpio;
	}
	ddata->reset_gpio = gpio;

	gpio = of_get_named_gpio(node, "te-gpios", 0);
	if (gpio_is_valid(gpio) || gpio == -ENOENT) {
		ddata->ext_te_gpio = gpio;
	} else {
		dev_err(&pdev->dev, "failed to parse TE gpio\n");
		return gpio;
	}

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	/* TODO: ulps, backlight */

	return 0;
}

static int dsicm_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
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

	if (!pdev->dev.of_node)
		return -ENODEV;

	r = dsicm_probe_of(pdev);
	if (r)
		return r;

	ddata->vm.hactive = 864;
	ddata->vm.vactive = 480;
	ddata->vm.pixelclock = 864 * 480 * 60;

	dssdev = &ddata->dssdev;
	dssdev->dev = dev;
	dssdev->driver = &dsicm_ops;
	dssdev->panel.vm = ddata->vm;
	dssdev->type = OMAP_DISPLAY_TYPE_DSI;
	dssdev->owner = THIS_MODULE;

	dssdev->panel.dsi_pix_fmt = OMAP_DSS_DSI_FMT_RGB888;
	dssdev->caps = OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE |
		OMAP_DSS_DISPLAY_CAP_TEAR_ELIM;

	r = omapdss_register_display(dssdev);
	if (r) {
		dev_err(dev, "Failed to register panel\n");
		goto err_reg;
	}

	mutex_init(&ddata->lock);

	atomic_set(&ddata->do_update, 0);

	if (gpio_is_valid(ddata->reset_gpio)) {
		r = devm_gpio_request_one(dev, ddata->reset_gpio,
				GPIOF_OUT_INIT_LOW, "taal rst");
		if (r) {
			dev_err(dev, "failed to request reset gpio\n");
			return r;
		}
	}

	if (gpio_is_valid(ddata->ext_te_gpio)) {
		r = devm_gpio_request_one(dev, ddata->ext_te_gpio,
				GPIOF_IN, "taal irq");
		if (r) {
			dev_err(dev, "GPIO request failed\n");
			return r;
		}

		r = devm_request_irq(dev, gpio_to_irq(ddata->ext_te_gpio),
				dsicm_te_isr,
				IRQF_TRIGGER_RISING,
				"taal vsync", ddata);

		if (r) {
			dev_err(dev, "IRQ request failed\n");
			return r;
		}

		INIT_DEFERRABLE_WORK(&ddata->te_timeout_work,
					dsicm_te_timeout_work_callback);

		dev_dbg(dev, "Using GPIO TE\n");
	}

	ddata->workqueue = create_singlethread_workqueue("dsicm_wq");
	if (ddata->workqueue == NULL) {
		dev_err(dev, "can't create workqueue\n");
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&ddata->ulps_work, dsicm_ulps_work);

	dsicm_hw_reset(ddata);

	if (ddata->use_dsi_backlight) {
		memset(&props, 0, sizeof(props));
		props.max_brightness = 255;

		props.type = BACKLIGHT_RAW;
		bldev = backlight_device_register(dev_name(dev),
				dev, ddata, &dsicm_bl_ops, &props);
		if (IS_ERR(bldev)) {
			r = PTR_ERR(bldev);
			goto err_bl;
		}

		ddata->bldev = bldev;

		bldev->props.fb_blank = FB_BLANK_UNBLANK;
		bldev->props.power = FB_BLANK_UNBLANK;
		bldev->props.brightness = 255;

		dsicm_bl_update_status(bldev);
	}

	r = sysfs_create_group(&dev->kobj, &dsicm_attr_group);
	if (r) {
		dev_err(dev, "failed to create sysfs files\n");
		goto err_sysfs_create;
	}

	return 0;

err_sysfs_create:
	backlight_device_unregister(bldev);
err_bl:
	destroy_workqueue(ddata->workqueue);
err_reg:
	return r;
}

static int __exit dsicm_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct backlight_device *bldev;

	dev_dbg(&pdev->dev, "remove\n");

	omapdss_unregister_display(dssdev);

	dsicm_disable(dssdev);
	dsicm_disconnect(dssdev);

	sysfs_remove_group(&pdev->dev.kobj, &dsicm_attr_group);

	bldev = ddata->bldev;
	if (bldev != NULL) {
		bldev->props.power = FB_BLANK_POWERDOWN;
		dsicm_bl_update_status(bldev);
		backlight_device_unregister(bldev);
	}

	omap_dss_put_device(ddata->in);

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
