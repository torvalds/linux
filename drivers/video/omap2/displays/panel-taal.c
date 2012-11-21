/*
 * Taal DSI command mode panel
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*#define DEBUG*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <video/omapdss.h>
#include <video/omap-panel-nokia-dsi.h>
#include <video/mipi_display.h>

/* DSI Virtual channel. Hardcoded for now. */
#define TCH 0

#define DCS_READ_NUM_ERRORS	0x05
#define DCS_BRIGHTNESS		0x51
#define DCS_CTRL_DISPLAY	0x53
#define DCS_WRITE_CABC		0x55
#define DCS_READ_CABC		0x56
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

static irqreturn_t taal_te_isr(int irq, void *data);
static void taal_te_timeout_work_callback(struct work_struct *work);
static int _taal_enable_te(struct omap_dss_device *dssdev, bool enable);

static int taal_panel_reset(struct omap_dss_device *dssdev);

/**
 * struct panel_config - panel configuration
 * @name: panel name
 * @type: panel type
 * @timings: panel resolution
 * @sleep: various panel specific delays, passed to msleep() if non-zero
 * @reset_sequence: reset sequence timings, passed to udelay() if non-zero
 * @regulators: array of panel regulators
 * @num_regulators: number of regulators in the array
 */
struct panel_config {
	const char *name;
	int type;

	struct omap_video_timings timings;

	struct {
		unsigned int sleep_in;
		unsigned int sleep_out;
		unsigned int hw_reset;
		unsigned int enable_te;
	} sleep;

	struct {
		unsigned int high;
		unsigned int low;
	} reset_sequence;

};

enum {
	PANEL_TAAL,
};

static struct panel_config panel_configs[] = {
	{
		.name		= "taal",
		.type		= PANEL_TAAL,
		.timings	= {
			.x_res		= 864,
			.y_res		= 480,
		},
		.sleep		= {
			.sleep_in	= 5,
			.sleep_out	= 5,
			.hw_reset	= 5,
			.enable_te	= 100, /* possible panel bug */
		},
		.reset_sequence	= {
			.high		= 10,
			.low		= 10,
		},
	},
};

struct taal_data {
	struct mutex lock;

	struct backlight_device *bldev;

	unsigned long	hw_guard_end;	/* next value of jiffies when we can
					 * issue the next sleep in/out command
					 */
	unsigned long	hw_guard_wait;	/* max guard time in jiffies */

	struct omap_dss_device *dssdev;

	/* panel specific HW info */
	struct panel_config *panel_config;

	/* panel HW configuration from DT or platform data */
	int reset_gpio;
	int ext_te_gpio;

	bool use_dsi_backlight;

	struct omap_dsi_pin_config pin_config;

	/* runtime variables */
	bool enabled;
	u8 rotate;
	bool mirror;

	bool te_enabled;

	atomic_t do_update;
	int channel;

	struct delayed_work te_timeout_work;

	bool cabc_broken;
	unsigned cabc_mode;

	bool intro_printed;

	struct workqueue_struct *workqueue;

	struct delayed_work esd_work;
	unsigned esd_interval;

	bool ulps_enabled;
	unsigned ulps_timeout;
	struct delayed_work ulps_work;
};

static void taal_esd_work(struct work_struct *work);
static void taal_ulps_work(struct work_struct *work);

static void hw_guard_start(struct taal_data *td, int guard_msec)
{
	td->hw_guard_wait = msecs_to_jiffies(guard_msec);
	td->hw_guard_end = jiffies + td->hw_guard_wait;
}

static void hw_guard_wait(struct taal_data *td)
{
	unsigned long wait = td->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= td->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}

static int taal_dcs_read_1(struct taal_data *td, u8 dcs_cmd, u8 *data)
{
	int r;
	u8 buf[1];

	r = dsi_vc_dcs_read(td->dssdev, td->channel, dcs_cmd, buf, 1);

	if (r < 0)
		return r;

	*data = buf[0];

	return 0;
}

static int taal_dcs_write_0(struct taal_data *td, u8 dcs_cmd)
{
	return dsi_vc_dcs_write(td->dssdev, td->channel, &dcs_cmd, 1);
}

static int taal_dcs_write_1(struct taal_data *td, u8 dcs_cmd, u8 param)
{
	u8 buf[2];
	buf[0] = dcs_cmd;
	buf[1] = param;
	return dsi_vc_dcs_write(td->dssdev, td->channel, buf, 2);
}

static int taal_sleep_in(struct taal_data *td)

{
	u8 cmd;
	int r;

	hw_guard_wait(td);

	cmd = MIPI_DCS_ENTER_SLEEP_MODE;
	r = dsi_vc_dcs_write_nosync(td->dssdev, td->channel, &cmd, 1);
	if (r)
		return r;

	hw_guard_start(td, 120);

	if (td->panel_config->sleep.sleep_in)
		msleep(td->panel_config->sleep.sleep_in);

	return 0;
}

static int taal_sleep_out(struct taal_data *td)
{
	int r;

	hw_guard_wait(td);

	r = taal_dcs_write_0(td, MIPI_DCS_EXIT_SLEEP_MODE);
	if (r)
		return r;

	hw_guard_start(td, 120);

	if (td->panel_config->sleep.sleep_out)
		msleep(td->panel_config->sleep.sleep_out);

	return 0;
}

static int taal_get_id(struct taal_data *td, u8 *id1, u8 *id2, u8 *id3)
{
	int r;

	r = taal_dcs_read_1(td, DCS_GET_ID1, id1);
	if (r)
		return r;
	r = taal_dcs_read_1(td, DCS_GET_ID2, id2);
	if (r)
		return r;
	r = taal_dcs_read_1(td, DCS_GET_ID3, id3);
	if (r)
		return r;

	return 0;
}

static int taal_set_addr_mode(struct taal_data *td, u8 rotate, bool mirror)
{
	int r;
	u8 mode;
	int b5, b6, b7;

	r = taal_dcs_read_1(td, MIPI_DCS_GET_ADDRESS_MODE, &mode);
	if (r)
		return r;

	switch (rotate) {
	default:
	case 0:
		b7 = 0;
		b6 = 0;
		b5 = 0;
		break;
	case 1:
		b7 = 0;
		b6 = 1;
		b5 = 1;
		break;
	case 2:
		b7 = 1;
		b6 = 1;
		b5 = 0;
		break;
	case 3:
		b7 = 1;
		b6 = 0;
		b5 = 1;
		break;
	}

	if (mirror)
		b6 = !b6;

	mode &= ~((1<<7) | (1<<6) | (1<<5));
	mode |= (b7 << 7) | (b6 << 6) | (b5 << 5);

	return taal_dcs_write_1(td, MIPI_DCS_SET_ADDRESS_MODE, mode);
}

static int taal_set_update_window(struct taal_data *td,
		u16 x, u16 y, u16 w, u16 h)
{
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

	r = dsi_vc_dcs_write_nosync(td->dssdev, td->channel, buf, sizeof(buf));
	if (r)
		return r;

	buf[0] = MIPI_DCS_SET_PAGE_ADDRESS;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(td->dssdev, td->channel, buf, sizeof(buf));
	if (r)
		return r;

	dsi_vc_send_bta_sync(td->dssdev, td->channel);

	return r;
}

static void taal_queue_esd_work(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->esd_interval > 0)
		queue_delayed_work(td->workqueue, &td->esd_work,
				msecs_to_jiffies(td->esd_interval));
}

static void taal_cancel_esd_work(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	cancel_delayed_work(&td->esd_work);
}

static void taal_queue_ulps_work(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->ulps_timeout > 0)
		queue_delayed_work(td->workqueue, &td->ulps_work,
				msecs_to_jiffies(td->ulps_timeout));
}

static void taal_cancel_ulps_work(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	cancel_delayed_work(&td->ulps_work);
}

static int taal_enter_ulps(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	if (td->ulps_enabled)
		return 0;

	taal_cancel_ulps_work(dssdev);

	r = _taal_enable_te(dssdev, false);
	if (r)
		goto err;

	if (gpio_is_valid(td->ext_te_gpio))
		disable_irq(gpio_to_irq(td->ext_te_gpio));

	omapdss_dsi_display_disable(dssdev, false, true);

	td->ulps_enabled = true;

	return 0;

err:
	dev_err(&dssdev->dev, "enter ULPS failed");
	taal_panel_reset(dssdev);

	td->ulps_enabled = false;

	taal_queue_ulps_work(dssdev);

	return r;
}

static int taal_exit_ulps(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	if (!td->ulps_enabled)
		return 0;

	r = omapdss_dsi_display_enable(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "failed to enable DSI\n");
		goto err1;
	}

	omapdss_dsi_vc_enable_hs(dssdev, td->channel, true);

	r = _taal_enable_te(dssdev, true);
	if (r) {
		dev_err(&dssdev->dev, "failed to re-enable TE");
		goto err2;
	}

	if (gpio_is_valid(td->ext_te_gpio))
		enable_irq(gpio_to_irq(td->ext_te_gpio));

	taal_queue_ulps_work(dssdev);

	td->ulps_enabled = false;

	return 0;

err2:
	dev_err(&dssdev->dev, "failed to exit ULPS");

	r = taal_panel_reset(dssdev);
	if (!r) {
		if (gpio_is_valid(td->ext_te_gpio))
			enable_irq(gpio_to_irq(td->ext_te_gpio));
		td->ulps_enabled = false;
	}
err1:
	taal_queue_ulps_work(dssdev);

	return r;
}

static int taal_wake_up(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->ulps_enabled)
		return taal_exit_ulps(dssdev);

	taal_cancel_ulps_work(dssdev);
	taal_queue_ulps_work(dssdev);
	return 0;
}

static int taal_bl_update_status(struct backlight_device *dev)
{
	struct omap_dss_device *dssdev = dev_get_drvdata(&dev->dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&dssdev->dev, "update brightness to %d\n", level);

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock(dssdev);

		r = taal_wake_up(dssdev);
		if (!r)
			r = taal_dcs_write_1(td, DCS_BRIGHTNESS, level);

		dsi_bus_unlock(dssdev);
	} else {
		r = 0;
	}

	mutex_unlock(&td->lock);

	return r;
}

static int taal_bl_get_intensity(struct backlight_device *dev)
{
	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		return dev->props.brightness;

	return 0;
}

static const struct backlight_ops taal_bl_ops = {
	.get_brightness = taal_bl_get_intensity,
	.update_status  = taal_bl_update_status,
};

static void taal_get_resolution(struct omap_dss_device *dssdev,
		u16 *xres, u16 *yres)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->rotate == 0 || td->rotate == 2) {
		*xres = dssdev->panel.timings.x_res;
		*yres = dssdev->panel.timings.y_res;
	} else {
		*yres = dssdev->panel.timings.x_res;
		*xres = dssdev->panel.timings.y_res;
	}
}

static ssize_t taal_num_errors_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	u8 errors = 0;
	int r;

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock(dssdev);

		r = taal_wake_up(dssdev);
		if (!r)
			r = taal_dcs_read_1(td, DCS_READ_NUM_ERRORS, &errors);

		dsi_bus_unlock(dssdev);
	} else {
		r = -ENODEV;
	}

	mutex_unlock(&td->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", errors);
}

static ssize_t taal_hw_revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock(dssdev);

		r = taal_wake_up(dssdev);
		if (!r)
			r = taal_get_id(td, &id1, &id2, &id3);

		dsi_bus_unlock(dssdev);
	} else {
		r = -ENODEV;
	}

	mutex_unlock(&td->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x\n", id1, id2, id3);
}

static const char *cabc_modes[] = {
	"off",		/* used also always when CABC is not supported */
	"ui",
	"still-image",
	"moving-image",
};

static ssize_t show_cabc_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	const char *mode_str;
	int mode;
	int len;

	mode = td->cabc_mode;

	mode_str = "unknown";
	if (mode >= 0 && mode < ARRAY_SIZE(cabc_modes))
		mode_str = cabc_modes[mode];
	len = snprintf(buf, PAGE_SIZE, "%s\n", mode_str);

	return len < PAGE_SIZE - 1 ? len : PAGE_SIZE - 1;
}

static ssize_t store_cabc_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(cabc_modes); i++) {
		if (sysfs_streq(cabc_modes[i], buf))
			break;
	}

	if (i == ARRAY_SIZE(cabc_modes))
		return -EINVAL;

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock(dssdev);

		if (!td->cabc_broken) {
			r = taal_wake_up(dssdev);
			if (r)
				goto err;

			r = taal_dcs_write_1(td, DCS_WRITE_CABC, i);
			if (r)
				goto err;
		}

		dsi_bus_unlock(dssdev);
	}

	td->cabc_mode = i;

	mutex_unlock(&td->lock);

	return count;
err:
	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);
	return r;
}

static ssize_t show_cabc_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int len;
	int i;

	for (i = 0, len = 0;
	     len < PAGE_SIZE && i < ARRAY_SIZE(cabc_modes); i++)
		len += snprintf(&buf[len], PAGE_SIZE - len, "%s%s%s",
			i ? " " : "", cabc_modes[i],
			i == ARRAY_SIZE(cabc_modes) - 1 ? "\n" : "");

	return len < PAGE_SIZE ? len : PAGE_SIZE - 1;
}

static ssize_t taal_store_esd_interval(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	mutex_lock(&td->lock);
	taal_cancel_esd_work(dssdev);
	td->esd_interval = t;
	if (td->enabled)
		taal_queue_esd_work(dssdev);
	mutex_unlock(&td->lock);

	return count;
}

static ssize_t taal_show_esd_interval(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	mutex_lock(&td->lock);
	t = td->esd_interval;
	mutex_unlock(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t taal_store_ulps(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock(dssdev);

		if (t)
			r = taal_enter_ulps(dssdev);
		else
			r = taal_wake_up(dssdev);

		dsi_bus_unlock(dssdev);
	}

	mutex_unlock(&td->lock);

	if (r)
		return r;

	return count;
}

static ssize_t taal_show_ulps(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	mutex_lock(&td->lock);
	t = td->ulps_enabled;
	mutex_unlock(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t taal_store_ulps_timeout(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	mutex_lock(&td->lock);
	td->ulps_timeout = t;

	if (td->enabled) {
		/* taal_wake_up will restart the timer */
		dsi_bus_lock(dssdev);
		r = taal_wake_up(dssdev);
		dsi_bus_unlock(dssdev);
	}

	mutex_unlock(&td->lock);

	if (r)
		return r;

	return count;
}

static ssize_t taal_show_ulps_timeout(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	mutex_lock(&td->lock);
	t = td->ulps_timeout;
	mutex_unlock(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static DEVICE_ATTR(num_dsi_errors, S_IRUGO, taal_num_errors_show, NULL);
static DEVICE_ATTR(hw_revision, S_IRUGO, taal_hw_revision_show, NULL);
static DEVICE_ATTR(cabc_mode, S_IRUGO | S_IWUSR,
		show_cabc_mode, store_cabc_mode);
static DEVICE_ATTR(cabc_available_modes, S_IRUGO,
		show_cabc_available_modes, NULL);
static DEVICE_ATTR(esd_interval, S_IRUGO | S_IWUSR,
		taal_show_esd_interval, taal_store_esd_interval);
static DEVICE_ATTR(ulps, S_IRUGO | S_IWUSR,
		taal_show_ulps, taal_store_ulps);
static DEVICE_ATTR(ulps_timeout, S_IRUGO | S_IWUSR,
		taal_show_ulps_timeout, taal_store_ulps_timeout);

static struct attribute *taal_attrs[] = {
	&dev_attr_num_dsi_errors.attr,
	&dev_attr_hw_revision.attr,
	&dev_attr_cabc_mode.attr,
	&dev_attr_cabc_available_modes.attr,
	&dev_attr_esd_interval.attr,
	&dev_attr_ulps.attr,
	&dev_attr_ulps_timeout.attr,
	NULL,
};

static struct attribute_group taal_attr_group = {
	.attrs = taal_attrs,
};

static void taal_hw_reset(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	if (!gpio_is_valid(td->reset_gpio))
		return;

	gpio_set_value(td->reset_gpio, 1);
	if (td->panel_config->reset_sequence.high)
		udelay(td->panel_config->reset_sequence.high);
	/* reset the panel */
	gpio_set_value(td->reset_gpio, 0);
	/* assert reset */
	if (td->panel_config->reset_sequence.low)
		udelay(td->panel_config->reset_sequence.low);
	gpio_set_value(td->reset_gpio, 1);
	/* wait after releasing reset */
	if (td->panel_config->sleep.hw_reset)
		msleep(td->panel_config->sleep.hw_reset);
}

static void taal_probe_pdata(struct taal_data *td,
		const struct nokia_dsi_panel_data *pdata)
{
	td->reset_gpio = pdata->reset_gpio;

	if (pdata->use_ext_te)
		td->ext_te_gpio = pdata->ext_te_gpio;
	else
		td->ext_te_gpio = -1;

	td->esd_interval = pdata->esd_interval;
	td->ulps_timeout = pdata->ulps_timeout;

	td->use_dsi_backlight = pdata->use_dsi_backlight;

	td->pin_config = pdata->pin_config;
}

static int taal_probe(struct omap_dss_device *dssdev)
{
	struct backlight_properties props;
	struct taal_data *td;
	struct backlight_device *bldev = NULL;
	int r, i;
	const char *panel_name;

	dev_dbg(&dssdev->dev, "probe\n");

	td = devm_kzalloc(&dssdev->dev, sizeof(*td), GFP_KERNEL);
	if (!td)
		return -ENOMEM;

	dev_set_drvdata(&dssdev->dev, td);
	td->dssdev = dssdev;

	if (dssdev->data) {
		const struct nokia_dsi_panel_data *pdata = dssdev->data;

		taal_probe_pdata(td, pdata);

		panel_name = pdata->name;
	} else {
		return -ENODEV;
	}

	if (panel_name == NULL)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(panel_configs); i++) {
		if (strcmp(panel_name, panel_configs[i].name) == 0) {
			td->panel_config = &panel_configs[i];
			break;
		}
	}

	if (!td->panel_config)
		return -EINVAL;

	dssdev->panel.timings = td->panel_config->timings;
	dssdev->panel.dsi_pix_fmt = OMAP_DSS_DSI_FMT_RGB888;
	dssdev->caps = OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE |
		OMAP_DSS_DISPLAY_CAP_TEAR_ELIM;

	mutex_init(&td->lock);

	atomic_set(&td->do_update, 0);

	if (gpio_is_valid(td->reset_gpio)) {
		r = devm_gpio_request_one(&dssdev->dev, td->reset_gpio,
				GPIOF_OUT_INIT_LOW, "taal rst");
		if (r) {
			dev_err(&dssdev->dev, "failed to request reset gpio\n");
			return r;
		}
	}

	if (gpio_is_valid(td->ext_te_gpio)) {
		r = devm_gpio_request_one(&dssdev->dev, td->ext_te_gpio,
				GPIOF_IN, "taal irq");
		if (r) {
			dev_err(&dssdev->dev, "GPIO request failed\n");
			return r;
		}

		r = devm_request_irq(&dssdev->dev, gpio_to_irq(td->ext_te_gpio),
				taal_te_isr,
				IRQF_TRIGGER_RISING,
				"taal vsync", dssdev);

		if (r) {
			dev_err(&dssdev->dev, "IRQ request failed\n");
			return r;
		}

		INIT_DEFERRABLE_WORK(&td->te_timeout_work,
					taal_te_timeout_work_callback);

		dev_dbg(&dssdev->dev, "Using GPIO TE\n");
	}

	td->workqueue = create_singlethread_workqueue("taal_esd");
	if (td->workqueue == NULL) {
		dev_err(&dssdev->dev, "can't create ESD workqueue\n");
		return -ENOMEM;
	}
	INIT_DEFERRABLE_WORK(&td->esd_work, taal_esd_work);
	INIT_DELAYED_WORK(&td->ulps_work, taal_ulps_work);

	taal_hw_reset(dssdev);

	if (td->use_dsi_backlight) {
		memset(&props, 0, sizeof(struct backlight_properties));
		props.max_brightness = 255;

		props.type = BACKLIGHT_RAW;
		bldev = backlight_device_register(dev_name(&dssdev->dev),
				&dssdev->dev, dssdev, &taal_bl_ops, &props);
		if (IS_ERR(bldev)) {
			r = PTR_ERR(bldev);
			goto err_bl;
		}

		td->bldev = bldev;

		bldev->props.fb_blank = FB_BLANK_UNBLANK;
		bldev->props.power = FB_BLANK_UNBLANK;
		bldev->props.brightness = 255;

		taal_bl_update_status(bldev);
	}

	r = omap_dsi_request_vc(dssdev, &td->channel);
	if (r) {
		dev_err(&dssdev->dev, "failed to get virtual channel\n");
		goto err_req_vc;
	}

	r = omap_dsi_set_vc_id(dssdev, td->channel, TCH);
	if (r) {
		dev_err(&dssdev->dev, "failed to set VC_ID\n");
		goto err_vc_id;
	}

	r = sysfs_create_group(&dssdev->dev.kobj, &taal_attr_group);
	if (r) {
		dev_err(&dssdev->dev, "failed to create sysfs files\n");
		goto err_vc_id;
	}

	return 0;

err_vc_id:
	omap_dsi_release_vc(dssdev, td->channel);
err_req_vc:
	if (bldev != NULL)
		backlight_device_unregister(bldev);
err_bl:
	destroy_workqueue(td->workqueue);
	return r;
}

static void __exit taal_remove(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct backlight_device *bldev;

	dev_dbg(&dssdev->dev, "remove\n");

	sysfs_remove_group(&dssdev->dev.kobj, &taal_attr_group);
	omap_dsi_release_vc(dssdev, td->channel);

	bldev = td->bldev;
	if (bldev != NULL) {
		bldev->props.power = FB_BLANK_POWERDOWN;
		taal_bl_update_status(bldev);
		backlight_device_unregister(bldev);
	}

	taal_cancel_ulps_work(dssdev);
	taal_cancel_esd_work(dssdev);
	destroy_workqueue(td->workqueue);

	/* reset, to be sure that the panel is in a valid state */
	taal_hw_reset(dssdev);
}

static int taal_power_on(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	r = omapdss_dsi_configure_pins(dssdev, &td->pin_config);
	if (r) {
		dev_err(&dssdev->dev, "failed to configure DSI pins\n");
		goto err0;
	};

	omapdss_dsi_set_size(dssdev, dssdev->panel.timings.x_res,
		dssdev->panel.timings.y_res);
	omapdss_dsi_set_pixel_format(dssdev, OMAP_DSS_DSI_FMT_RGB888);
	omapdss_dsi_set_operation_mode(dssdev, OMAP_DSS_DSI_CMD_MODE);

	r = omapdss_dsi_set_clocks(dssdev, 216000000, 10000000);
	if (r) {
		dev_err(&dssdev->dev, "failed to set HS and LP clocks\n");
		goto err0;
	}

	r = omapdss_dsi_display_enable(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "failed to enable DSI\n");
		goto err0;
	}

	taal_hw_reset(dssdev);

	omapdss_dsi_vc_enable_hs(dssdev, td->channel, false);

	r = taal_sleep_out(td);
	if (r)
		goto err;

	r = taal_get_id(td, &id1, &id2, &id3);
	if (r)
		goto err;

	/* on early Taal revisions CABC is broken */
	if (td->panel_config->type == PANEL_TAAL &&
		(id2 == 0x00 || id2 == 0xff || id2 == 0x81))
		td->cabc_broken = true;

	r = taal_dcs_write_1(td, DCS_BRIGHTNESS, 0xff);
	if (r)
		goto err;

	r = taal_dcs_write_1(td, DCS_CTRL_DISPLAY,
			(1<<2) | (1<<5));	/* BL | BCTRL */
	if (r)
		goto err;

	r = taal_dcs_write_1(td, MIPI_DCS_SET_PIXEL_FORMAT,
		MIPI_DCS_PIXEL_FMT_24BIT);
	if (r)
		goto err;

	r = taal_set_addr_mode(td, td->rotate, td->mirror);
	if (r)
		goto err;

	if (!td->cabc_broken) {
		r = taal_dcs_write_1(td, DCS_WRITE_CABC, td->cabc_mode);
		if (r)
			goto err;
	}

	r = taal_dcs_write_0(td, MIPI_DCS_SET_DISPLAY_ON);
	if (r)
		goto err;

	r = _taal_enable_te(dssdev, td->te_enabled);
	if (r)
		goto err;

	r = dsi_enable_video_output(dssdev, td->channel);
	if (r)
		goto err;

	td->enabled = 1;

	if (!td->intro_printed) {
		dev_info(&dssdev->dev, "%s panel revision %02x.%02x.%02x\n",
			td->panel_config->name, id1, id2, id3);
		if (td->cabc_broken)
			dev_info(&dssdev->dev,
					"old Taal version, CABC disabled\n");
		td->intro_printed = true;
	}

	omapdss_dsi_vc_enable_hs(dssdev, td->channel, true);

	return 0;
err:
	dev_err(&dssdev->dev, "error while enabling panel, issuing HW reset\n");

	taal_hw_reset(dssdev);

	omapdss_dsi_display_disable(dssdev, true, false);
err0:
	return r;
}

static void taal_power_off(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dsi_disable_video_output(dssdev, td->channel);

	r = taal_dcs_write_0(td, MIPI_DCS_SET_DISPLAY_OFF);
	if (!r)
		r = taal_sleep_in(td);

	if (r) {
		dev_err(&dssdev->dev,
				"error disabling panel, issuing HW reset\n");
		taal_hw_reset(dssdev);
	}

	omapdss_dsi_display_disable(dssdev, true, false);

	td->enabled = 0;
}

static int taal_panel_reset(struct omap_dss_device *dssdev)
{
	dev_err(&dssdev->dev, "performing LCD reset\n");

	taal_power_off(dssdev);
	taal_hw_reset(dssdev);
	return taal_power_on(dssdev);
}

static int taal_enable(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "enable\n");

	mutex_lock(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	dsi_bus_lock(dssdev);

	r = taal_power_on(dssdev);

	dsi_bus_unlock(dssdev);

	if (r)
		goto err;

	taal_queue_esd_work(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	mutex_unlock(&td->lock);

	return 0;
err:
	dev_dbg(&dssdev->dev, "enable failed\n");
	mutex_unlock(&td->lock);
	return r;
}

static void taal_disable(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "disable\n");

	mutex_lock(&td->lock);

	taal_cancel_ulps_work(dssdev);
	taal_cancel_esd_work(dssdev);

	dsi_bus_lock(dssdev);

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
		int r;

		r = taal_wake_up(dssdev);
		if (!r)
			taal_power_off(dssdev);
	}

	dsi_bus_unlock(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&td->lock);
}

static int taal_suspend(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "suspend\n");

	mutex_lock(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		r = -EINVAL;
		goto err;
	}

	taal_cancel_ulps_work(dssdev);
	taal_cancel_esd_work(dssdev);

	dsi_bus_lock(dssdev);

	r = taal_wake_up(dssdev);
	if (!r)
		taal_power_off(dssdev);

	dsi_bus_unlock(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;

	mutex_unlock(&td->lock);

	return 0;
err:
	mutex_unlock(&td->lock);
	return r;
}

static int taal_resume(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "resume\n");

	mutex_lock(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_SUSPENDED) {
		r = -EINVAL;
		goto err;
	}

	dsi_bus_lock(dssdev);

	r = taal_power_on(dssdev);

	dsi_bus_unlock(dssdev);

	if (r) {
		dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
	} else {
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
		taal_queue_esd_work(dssdev);
	}

	mutex_unlock(&td->lock);

	return r;
err:
	mutex_unlock(&td->lock);
	return r;
}

static void taal_framedone_cb(int err, void *data)
{
	struct omap_dss_device *dssdev = data;
	dev_dbg(&dssdev->dev, "framedone, err %d\n", err);
	dsi_bus_unlock(dssdev);
}

static irqreturn_t taal_te_isr(int irq, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int old;
	int r;

	old = atomic_cmpxchg(&td->do_update, 1, 0);

	if (old) {
		cancel_delayed_work(&td->te_timeout_work);

		r = omap_dsi_update(dssdev, td->channel, taal_framedone_cb,
				dssdev);
		if (r)
			goto err;
	}

	return IRQ_HANDLED;
err:
	dev_err(&dssdev->dev, "start update failed\n");
	dsi_bus_unlock(dssdev);
	return IRQ_HANDLED;
}

static void taal_te_timeout_work_callback(struct work_struct *work)
{
	struct taal_data *td = container_of(work, struct taal_data,
					te_timeout_work.work);
	struct omap_dss_device *dssdev = td->dssdev;

	dev_err(&dssdev->dev, "TE not received for 250ms!\n");

	atomic_set(&td->do_update, 0);
	dsi_bus_unlock(dssdev);
}

static int taal_update(struct omap_dss_device *dssdev,
				    u16 x, u16 y, u16 w, u16 h)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "update %d, %d, %d x %d\n", x, y, w, h);

	mutex_lock(&td->lock);
	dsi_bus_lock(dssdev);

	r = taal_wake_up(dssdev);
	if (r)
		goto err;

	if (!td->enabled) {
		r = 0;
		goto err;
	}

	/* XXX no need to send this every frame, but dsi break if not done */
	r = taal_set_update_window(td, 0, 0,
			td->panel_config->timings.x_res,
			td->panel_config->timings.y_res);
	if (r)
		goto err;

	if (td->te_enabled && gpio_is_valid(td->ext_te_gpio)) {
		schedule_delayed_work(&td->te_timeout_work,
				msecs_to_jiffies(250));
		atomic_set(&td->do_update, 1);
	} else {
		r = omap_dsi_update(dssdev, td->channel, taal_framedone_cb,
				dssdev);
		if (r)
			goto err;
	}

	/* note: no bus_unlock here. unlock is in framedone_cb */
	mutex_unlock(&td->lock);
	return 0;
err:
	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);
	return r;
}

static int taal_sync(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "sync\n");

	mutex_lock(&td->lock);
	dsi_bus_lock(dssdev);
	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);

	dev_dbg(&dssdev->dev, "sync done\n");

	return 0;
}

static int _taal_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	if (enable)
		r = taal_dcs_write_1(td, MIPI_DCS_SET_TEAR_ON, 0);
	else
		r = taal_dcs_write_0(td, MIPI_DCS_SET_TEAR_OFF);

	if (!gpio_is_valid(td->ext_te_gpio))
		omapdss_dsi_enable_te(dssdev, enable);

	if (td->panel_config->sleep.enable_te)
		msleep(td->panel_config->sleep.enable_te);

	return r;
}

static int taal_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&td->lock);

	if (td->te_enabled == enable)
		goto end;

	dsi_bus_lock(dssdev);

	if (td->enabled) {
		r = taal_wake_up(dssdev);
		if (r)
			goto err;

		r = _taal_enable_te(dssdev, enable);
		if (r)
			goto err;
	}

	td->te_enabled = enable;

	dsi_bus_unlock(dssdev);
end:
	mutex_unlock(&td->lock);

	return 0;
err:
	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);

	return r;
}

static int taal_get_te(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&td->lock);
	r = td->te_enabled;
	mutex_unlock(&td->lock);

	return r;
}

static int taal_rotate(struct omap_dss_device *dssdev, u8 rotate)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	u16 dw, dh;
	int r;

	dev_dbg(&dssdev->dev, "rotate %d\n", rotate);

	mutex_lock(&td->lock);

	if (td->rotate == rotate)
		goto end;

	dsi_bus_lock(dssdev);

	if (td->enabled) {
		r = taal_wake_up(dssdev);
		if (r)
			goto err;

		r = taal_set_addr_mode(td, rotate, td->mirror);
		if (r)
			goto err;
	}

	if (rotate == 0 || rotate == 2) {
		dw = dssdev->panel.timings.x_res;
		dh = dssdev->panel.timings.y_res;
	} else {
		dw = dssdev->panel.timings.y_res;
		dh = dssdev->panel.timings.x_res;
	}

	omapdss_dsi_set_size(dssdev, dw, dh);

	td->rotate = rotate;

	dsi_bus_unlock(dssdev);
end:
	mutex_unlock(&td->lock);
	return 0;
err:
	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);
	return r;
}

static u8 taal_get_rotate(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&td->lock);
	r = td->rotate;
	mutex_unlock(&td->lock);

	return r;
}

static int taal_mirror(struct omap_dss_device *dssdev, bool enable)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "mirror %d\n", enable);

	mutex_lock(&td->lock);

	if (td->mirror == enable)
		goto end;

	dsi_bus_lock(dssdev);
	if (td->enabled) {
		r = taal_wake_up(dssdev);
		if (r)
			goto err;

		r = taal_set_addr_mode(td, td->rotate, enable);
		if (r)
			goto err;
	}

	td->mirror = enable;

	dsi_bus_unlock(dssdev);
end:
	mutex_unlock(&td->lock);
	return 0;
err:
	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);
	return r;
}

static bool taal_get_mirror(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&td->lock);
	r = td->mirror;
	mutex_unlock(&td->lock);

	return r;
}

static int taal_run_test(struct omap_dss_device *dssdev, int test_num)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	mutex_lock(&td->lock);

	if (!td->enabled) {
		r = -ENODEV;
		goto err1;
	}

	dsi_bus_lock(dssdev);

	r = taal_wake_up(dssdev);
	if (r)
		goto err2;

	r = taal_dcs_read_1(td, DCS_GET_ID1, &id1);
	if (r)
		goto err2;
	r = taal_dcs_read_1(td, DCS_GET_ID2, &id2);
	if (r)
		goto err2;
	r = taal_dcs_read_1(td, DCS_GET_ID3, &id3);
	if (r)
		goto err2;

	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);
	return 0;
err2:
	dsi_bus_unlock(dssdev);
err1:
	mutex_unlock(&td->lock);
	return r;
}

static int taal_memory_read(struct omap_dss_device *dssdev,
		void *buf, size_t size,
		u16 x, u16 y, u16 w, u16 h)
{
	int r;
	int first = 1;
	int plen;
	unsigned buf_used = 0;
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	if (size < w * h * 3)
		return -ENOMEM;

	mutex_lock(&td->lock);

	if (!td->enabled) {
		r = -ENODEV;
		goto err1;
	}

	size = min(w * h * 3,
			dssdev->panel.timings.x_res *
			dssdev->panel.timings.y_res * 3);

	dsi_bus_lock(dssdev);

	r = taal_wake_up(dssdev);
	if (r)
		goto err2;

	/* plen 1 or 2 goes into short packet. until checksum error is fixed,
	 * use short packets. plen 32 works, but bigger packets seem to cause
	 * an error. */
	if (size % 2)
		plen = 1;
	else
		plen = 2;

	taal_set_update_window(td, x, y, w, h);

	r = dsi_vc_set_max_rx_packet_size(dssdev, td->channel, plen);
	if (r)
		goto err2;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = dsi_vc_dcs_read(dssdev, td->channel, dcs_cmd,
				buf + buf_used, size - buf_used);

		if (r < 0) {
			dev_err(&dssdev->dev, "read error\n");
			goto err3;
		}

		buf_used += r;

		if (r < plen) {
			dev_err(&dssdev->dev, "short read\n");
			break;
		}

		if (signal_pending(current)) {
			dev_err(&dssdev->dev, "signal pending, "
					"aborting memory read\n");
			r = -ERESTARTSYS;
			goto err3;
		}
	}

	r = buf_used;

err3:
	dsi_vc_set_max_rx_packet_size(dssdev, td->channel, 1);
err2:
	dsi_bus_unlock(dssdev);
err1:
	mutex_unlock(&td->lock);
	return r;
}

static void taal_ulps_work(struct work_struct *work)
{
	struct taal_data *td = container_of(work, struct taal_data,
			ulps_work.work);
	struct omap_dss_device *dssdev = td->dssdev;

	mutex_lock(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE || !td->enabled) {
		mutex_unlock(&td->lock);
		return;
	}

	dsi_bus_lock(dssdev);

	taal_enter_ulps(dssdev);

	dsi_bus_unlock(dssdev);
	mutex_unlock(&td->lock);
}

static void taal_esd_work(struct work_struct *work)
{
	struct taal_data *td = container_of(work, struct taal_data,
			esd_work.work);
	struct omap_dss_device *dssdev = td->dssdev;
	u8 state1, state2;
	int r;

	mutex_lock(&td->lock);

	if (!td->enabled) {
		mutex_unlock(&td->lock);
		return;
	}

	dsi_bus_lock(dssdev);

	r = taal_wake_up(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "failed to exit ULPS\n");
		goto err;
	}

	r = taal_dcs_read_1(td, MIPI_DCS_GET_DIAGNOSTIC_RESULT, &state1);
	if (r) {
		dev_err(&dssdev->dev, "failed to read Taal status\n");
		goto err;
	}

	/* Run self diagnostics */
	r = taal_sleep_out(td);
	if (r) {
		dev_err(&dssdev->dev, "failed to run Taal self-diagnostics\n");
		goto err;
	}

	r = taal_dcs_read_1(td, MIPI_DCS_GET_DIAGNOSTIC_RESULT, &state2);
	if (r) {
		dev_err(&dssdev->dev, "failed to read Taal status\n");
		goto err;
	}

	/* Each sleep out command will trigger a self diagnostic and flip
	 * Bit6 if the test passes.
	 */
	if (!((state1 ^ state2) & (1 << 6))) {
		dev_err(&dssdev->dev, "LCD self diagnostics failed\n");
		goto err;
	}
	/* Self-diagnostics result is also shown on TE GPIO line. We need
	 * to re-enable TE after self diagnostics */
	if (td->te_enabled && gpio_is_valid(td->ext_te_gpio)) {
		r = taal_dcs_write_1(td, MIPI_DCS_SET_TEAR_ON, 0);
		if (r)
			goto err;
	}

	dsi_bus_unlock(dssdev);

	taal_queue_esd_work(dssdev);

	mutex_unlock(&td->lock);
	return;
err:
	dev_err(&dssdev->dev, "performing LCD reset\n");

	taal_panel_reset(dssdev);

	dsi_bus_unlock(dssdev);

	taal_queue_esd_work(dssdev);

	mutex_unlock(&td->lock);
}

static struct omap_dss_driver taal_driver = {
	.probe		= taal_probe,
	.remove		= __exit_p(taal_remove),

	.enable		= taal_enable,
	.disable	= taal_disable,
	.suspend	= taal_suspend,
	.resume		= taal_resume,

	.update		= taal_update,
	.sync		= taal_sync,

	.get_resolution	= taal_get_resolution,
	.get_recommended_bpp = omapdss_default_get_recommended_bpp,

	.enable_te	= taal_enable_te,
	.get_te		= taal_get_te,

	.set_rotate	= taal_rotate,
	.get_rotate	= taal_get_rotate,
	.set_mirror	= taal_mirror,
	.get_mirror	= taal_get_mirror,
	.run_test	= taal_run_test,
	.memory_read	= taal_memory_read,

	.driver         = {
		.name   = "taal",
		.owner  = THIS_MODULE,
	},
};

static int __init taal_init(void)
{
	omap_dss_register_driver(&taal_driver);

	return 0;
}

static void __exit taal_exit(void)
{
	omap_dss_unregister_driver(&taal_driver);
}

module_init(taal_init);
module_exit(taal_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("Taal Driver");
MODULE_LICENSE("GPL");
