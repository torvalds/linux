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
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>

#include <plat/display.h>
#include <plat/nokia-dsi-panel.h>

/* DSI Virtual channel. Hardcoded for now. */
#define TCH 0

#define DCS_READ_NUM_ERRORS	0x05
#define DCS_READ_POWER_MODE	0x0a
#define DCS_READ_MADCTL		0x0b
#define DCS_READ_PIXEL_FORMAT	0x0c
#define DCS_RDDSDR		0x0f
#define DCS_SLEEP_IN		0x10
#define DCS_SLEEP_OUT		0x11
#define DCS_DISPLAY_OFF		0x28
#define DCS_DISPLAY_ON		0x29
#define DCS_COLUMN_ADDR		0x2a
#define DCS_PAGE_ADDR		0x2b
#define DCS_MEMORY_WRITE	0x2c
#define DCS_TEAR_OFF		0x34
#define DCS_TEAR_ON		0x35
#define DCS_MEM_ACC_CTRL	0x36
#define DCS_PIXEL_FORMAT	0x3a
#define DCS_BRIGHTNESS		0x51
#define DCS_CTRL_DISPLAY	0x53
#define DCS_WRITE_CABC		0x55
#define DCS_READ_CABC		0x56
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

#define TAAL_ESD_CHECK_PERIOD	msecs_to_jiffies(5000)

static irqreturn_t taal_te_isr(int irq, void *data);
static void taal_te_timeout_work_callback(struct work_struct *work);
static int _taal_enable_te(struct omap_dss_device *dssdev, bool enable);

struct panel_regulator {
	struct regulator *regulator;
	const char *name;
	int min_uV;
	int max_uV;
};

static void free_regulators(struct panel_regulator *regulators, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		/* disable/put in reverse order */
		regulator_disable(regulators[n - i - 1].regulator);
		regulator_put(regulators[n - i - 1].regulator);
	}
}

static int init_regulators(struct omap_dss_device *dssdev,
			struct panel_regulator *regulators, int n)
{
	int r, i, v;

	for (i = 0; i < n; i++) {
		struct regulator *reg;

		reg = regulator_get(&dssdev->dev, regulators[i].name);
		if (IS_ERR(reg)) {
			dev_err(&dssdev->dev, "failed to get regulator %s\n",
				regulators[i].name);
			r = PTR_ERR(reg);
			goto err;
		}

		/* FIXME: better handling of fixed vs. variable regulators */
		v = regulator_get_voltage(reg);
		if (v < regulators[i].min_uV || v > regulators[i].max_uV) {
			r = regulator_set_voltage(reg, regulators[i].min_uV,
						regulators[i].max_uV);
			if (r) {
				dev_err(&dssdev->dev,
					"failed to set regulator %s voltage\n",
					regulators[i].name);
				regulator_put(reg);
				goto err;
			}
		}

		r = regulator_enable(reg);
		if (r) {
			dev_err(&dssdev->dev, "failed to enable regulator %s\n",
				regulators[i].name);
			regulator_put(reg);
			goto err;
		}

		regulators[i].regulator = reg;
	}

	return 0;

err:
	free_regulators(regulators, i);

	return r;
}

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

	struct panel_regulator *regulators;
	int num_regulators;
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

	bool enabled;
	u8 rotate;
	bool mirror;

	bool te_enabled;

	atomic_t do_update;
	struct {
		u16 x;
		u16 y;
		u16 w;
		u16 h;
	} update_region;
	struct delayed_work te_timeout_work;

	bool use_dsi_bl;

	bool cabc_broken;
	unsigned cabc_mode;

	bool intro_printed;

	struct workqueue_struct *esd_wq;
	struct delayed_work esd_work;

	struct panel_config *panel_config;
};

static inline struct nokia_dsi_panel_data
*get_panel_data(const struct omap_dss_device *dssdev)
{
	return (struct nokia_dsi_panel_data *) dssdev->data;
}

static void taal_esd_work(struct work_struct *work);

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

static int taal_dcs_read_1(u8 dcs_cmd, u8 *data)
{
	int r;
	u8 buf[1];

	r = dsi_vc_dcs_read(TCH, dcs_cmd, buf, 1);

	if (r < 0)
		return r;

	*data = buf[0];

	return 0;
}

static int taal_dcs_write_0(u8 dcs_cmd)
{
	return dsi_vc_dcs_write(TCH, &dcs_cmd, 1);
}

static int taal_dcs_write_1(u8 dcs_cmd, u8 param)
{
	u8 buf[2];
	buf[0] = dcs_cmd;
	buf[1] = param;
	return dsi_vc_dcs_write(TCH, buf, 2);
}

static int taal_sleep_in(struct taal_data *td)

{
	u8 cmd;
	int r;

	hw_guard_wait(td);

	cmd = DCS_SLEEP_IN;
	r = dsi_vc_dcs_write_nosync(TCH, &cmd, 1);
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

	r = taal_dcs_write_0(DCS_SLEEP_OUT);
	if (r)
		return r;

	hw_guard_start(td, 120);

	if (td->panel_config->sleep.sleep_out)
		msleep(td->panel_config->sleep.sleep_out);

	return 0;
}

static int taal_get_id(u8 *id1, u8 *id2, u8 *id3)
{
	int r;

	r = taal_dcs_read_1(DCS_GET_ID1, id1);
	if (r)
		return r;
	r = taal_dcs_read_1(DCS_GET_ID2, id2);
	if (r)
		return r;
	r = taal_dcs_read_1(DCS_GET_ID3, id3);
	if (r)
		return r;

	return 0;
}

static int taal_set_addr_mode(u8 rotate, bool mirror)
{
	int r;
	u8 mode;
	int b5, b6, b7;

	r = taal_dcs_read_1(DCS_READ_MADCTL, &mode);
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

	return taal_dcs_write_1(DCS_MEM_ACC_CTRL, mode);
}

static int taal_set_update_window(u16 x, u16 y, u16 w, u16 h)
{
	int r;
	u16 x1 = x;
	u16 x2 = x + w - 1;
	u16 y1 = y;
	u16 y2 = y + h - 1;

	u8 buf[5];
	buf[0] = DCS_COLUMN_ADDR;
	buf[1] = (x1 >> 8) & 0xff;
	buf[2] = (x1 >> 0) & 0xff;
	buf[3] = (x2 >> 8) & 0xff;
	buf[4] = (x2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(TCH, buf, sizeof(buf));
	if (r)
		return r;

	buf[0] = DCS_PAGE_ADDR;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(TCH, buf, sizeof(buf));
	if (r)
		return r;

	dsi_vc_send_bta_sync(TCH);

	return r;
}

static int taal_bl_update_status(struct backlight_device *dev)
{
	struct omap_dss_device *dssdev = dev_get_drvdata(&dev->dev);
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&dssdev->dev, "update brightness to %d\n", level);

	mutex_lock(&td->lock);

	if (td->use_dsi_bl) {
		if (td->enabled) {
			dsi_bus_lock();
			r = taal_dcs_write_1(DCS_BRIGHTNESS, level);
			dsi_bus_unlock();
		} else {
			r = 0;
		}
	} else {
		if (!panel_data->set_backlight)
			r = -EINVAL;
		else
			r = panel_data->set_backlight(dssdev, level);
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

static struct backlight_ops taal_bl_ops = {
	.get_brightness = taal_bl_get_intensity,
	.update_status  = taal_bl_update_status,
};

static void taal_get_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	*timings = dssdev->panel.timings;
}

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
	u8 errors;
	int r;

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();
		r = taal_dcs_read_1(DCS_READ_NUM_ERRORS, &errors);
		dsi_bus_unlock();
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
		dsi_bus_lock();
		r = taal_get_id(&id1, &id2, &id3);
		dsi_bus_unlock();
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

	for (i = 0; i < ARRAY_SIZE(cabc_modes); i++) {
		if (sysfs_streq(cabc_modes[i], buf))
			break;
	}

	if (i == ARRAY_SIZE(cabc_modes))
		return -EINVAL;

	mutex_lock(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();
		if (!td->cabc_broken)
			taal_dcs_write_1(DCS_WRITE_CABC, i);
		dsi_bus_unlock();
	}

	td->cabc_mode = i;

	mutex_unlock(&td->lock);

	return count;
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

static DEVICE_ATTR(num_dsi_errors, S_IRUGO, taal_num_errors_show, NULL);
static DEVICE_ATTR(hw_revision, S_IRUGO, taal_hw_revision_show, NULL);
static DEVICE_ATTR(cabc_mode, S_IRUGO | S_IWUSR,
		show_cabc_mode, store_cabc_mode);
static DEVICE_ATTR(cabc_available_modes, S_IRUGO,
		show_cabc_available_modes, NULL);

static struct attribute *taal_attrs[] = {
	&dev_attr_num_dsi_errors.attr,
	&dev_attr_hw_revision.attr,
	&dev_attr_cabc_mode.attr,
	&dev_attr_cabc_available_modes.attr,
	NULL,
};

static struct attribute_group taal_attr_group = {
	.attrs = taal_attrs,
};

static void taal_hw_reset(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);

	if (panel_data->reset_gpio == -1)
		return;

	gpio_set_value(panel_data->reset_gpio, 1);
	if (td->panel_config->reset_sequence.high)
		udelay(td->panel_config->reset_sequence.high);
	/* reset the panel */
	gpio_set_value(panel_data->reset_gpio, 0);
	/* assert reset */
	if (td->panel_config->reset_sequence.low)
		udelay(td->panel_config->reset_sequence.low);
	gpio_set_value(panel_data->reset_gpio, 1);
	/* wait after releasing reset */
	if (td->panel_config->sleep.hw_reset)
		msleep(td->panel_config->sleep.hw_reset);
}

static int taal_probe(struct omap_dss_device *dssdev)
{
	struct backlight_properties props;
	struct taal_data *td;
	struct backlight_device *bldev;
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	struct panel_config *panel_config = NULL;
	int r, i;

	dev_dbg(&dssdev->dev, "probe\n");

	if (!panel_data || !panel_data->name) {
		r = -EINVAL;
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(panel_configs); i++) {
		if (strcmp(panel_data->name, panel_configs[i].name) == 0) {
			panel_config = &panel_configs[i];
			break;
		}
	}

	if (!panel_config) {
		r = -EINVAL;
		goto err;
	}

	dssdev->panel.config = OMAP_DSS_LCD_TFT;
	dssdev->panel.timings = panel_config->timings;
	dssdev->ctrl.pixel_size = 24;

	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (!td) {
		r = -ENOMEM;
		goto err;
	}
	td->dssdev = dssdev;
	td->panel_config = panel_config;

	mutex_init(&td->lock);

	atomic_set(&td->do_update, 0);

	r = init_regulators(dssdev, panel_config->regulators,
			panel_config->num_regulators);
	if (r)
		goto err_reg;

	td->esd_wq = create_singlethread_workqueue("taal_esd");
	if (td->esd_wq == NULL) {
		dev_err(&dssdev->dev, "can't create ESD workqueue\n");
		r = -ENOMEM;
		goto err_wq;
	}
	INIT_DELAYED_WORK_DEFERRABLE(&td->esd_work, taal_esd_work);

	dev_set_drvdata(&dssdev->dev, td);

	taal_hw_reset(dssdev);

	/* if no platform set_backlight() defined, presume DSI backlight
	 * control */
	memset(&props, 0, sizeof(struct backlight_properties));
	if (!panel_data->set_backlight)
		td->use_dsi_bl = true;

	if (td->use_dsi_bl)
		props.max_brightness = 255;
	else
		props.max_brightness = 127;
	bldev = backlight_device_register("taal", &dssdev->dev, dssdev,
					  &taal_bl_ops, &props);
	if (IS_ERR(bldev)) {
		r = PTR_ERR(bldev);
		goto err_bl;
	}

	td->bldev = bldev;

	bldev->props.fb_blank = FB_BLANK_UNBLANK;
	bldev->props.power = FB_BLANK_UNBLANK;
	if (td->use_dsi_bl)
		bldev->props.brightness = 255;
	else
		bldev->props.brightness = 127;

	taal_bl_update_status(bldev);

	if (panel_data->use_ext_te) {
		int gpio = panel_data->ext_te_gpio;

		r = gpio_request(gpio, "taal irq");
		if (r) {
			dev_err(&dssdev->dev, "GPIO request failed\n");
			goto err_gpio;
		}

		gpio_direction_input(gpio);

		r = request_irq(gpio_to_irq(gpio), taal_te_isr,
				IRQF_DISABLED | IRQF_TRIGGER_RISING,
				"taal vsync", dssdev);

		if (r) {
			dev_err(&dssdev->dev, "IRQ request failed\n");
			gpio_free(gpio);
			goto err_irq;
		}

		INIT_DELAYED_WORK_DEFERRABLE(&td->te_timeout_work,
					taal_te_timeout_work_callback);

		dev_dbg(&dssdev->dev, "Using GPIO TE\n");
	}

	r = sysfs_create_group(&dssdev->dev.kobj, &taal_attr_group);
	if (r) {
		dev_err(&dssdev->dev, "failed to create sysfs files\n");
		goto err_sysfs;
	}

	return 0;
err_sysfs:
	if (panel_data->use_ext_te)
		free_irq(gpio_to_irq(panel_data->ext_te_gpio), dssdev);
err_irq:
	if (panel_data->use_ext_te)
		gpio_free(panel_data->ext_te_gpio);
err_gpio:
	backlight_device_unregister(bldev);
err_bl:
	destroy_workqueue(td->esd_wq);
err_wq:
	free_regulators(panel_config->regulators, panel_config->num_regulators);
err_reg:
	kfree(td);
err:
	return r;
}

static void taal_remove(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	struct backlight_device *bldev;

	dev_dbg(&dssdev->dev, "remove\n");

	sysfs_remove_group(&dssdev->dev.kobj, &taal_attr_group);

	if (panel_data->use_ext_te) {
		int gpio = panel_data->ext_te_gpio;
		free_irq(gpio_to_irq(gpio), dssdev);
		gpio_free(gpio);
	}

	bldev = td->bldev;
	bldev->props.power = FB_BLANK_POWERDOWN;
	taal_bl_update_status(bldev);
	backlight_device_unregister(bldev);

	cancel_delayed_work(&td->esd_work);
	destroy_workqueue(td->esd_wq);

	/* reset, to be sure that the panel is in a valid state */
	taal_hw_reset(dssdev);

	free_regulators(td->panel_config->regulators,
			td->panel_config->num_regulators);

	kfree(td);
}

static int taal_power_on(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	r = omapdss_dsi_display_enable(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "failed to enable DSI\n");
		goto err0;
	}

	taal_hw_reset(dssdev);

	omapdss_dsi_vc_enable_hs(TCH, false);

	r = taal_sleep_out(td);
	if (r)
		goto err;

	r = taal_get_id(&id1, &id2, &id3);
	if (r)
		goto err;

	/* on early Taal revisions CABC is broken */
	if (td->panel_config->type == PANEL_TAAL &&
		(id2 == 0x00 || id2 == 0xff || id2 == 0x81))
		td->cabc_broken = true;

	r = taal_dcs_write_1(DCS_BRIGHTNESS, 0xff);
	if (r)
		goto err;

	r = taal_dcs_write_1(DCS_CTRL_DISPLAY,
			(1<<2) | (1<<5));	/* BL | BCTRL */
	if (r)
		goto err;

	r = taal_dcs_write_1(DCS_PIXEL_FORMAT, 0x7); /* 24bit/pixel */
	if (r)
		goto err;

	r = taal_set_addr_mode(td->rotate, td->mirror);
	if (r)
		goto err;

	if (!td->cabc_broken) {
		r = taal_dcs_write_1(DCS_WRITE_CABC, td->cabc_mode);
		if (r)
			goto err;
	}

	r = taal_dcs_write_0(DCS_DISPLAY_ON);
	if (r)
		goto err;

	r = _taal_enable_te(dssdev, td->te_enabled);
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

	omapdss_dsi_vc_enable_hs(TCH, true);

	return 0;
err:
	dev_err(&dssdev->dev, "error while enabling panel, issuing HW reset\n");

	taal_hw_reset(dssdev);

	omapdss_dsi_display_disable(dssdev);
err0:
	return r;
}

static void taal_power_off(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	r = taal_dcs_write_0(DCS_DISPLAY_OFF);
	if (!r) {
		r = taal_sleep_in(td);
		/* HACK: wait a bit so that the message goes through */
		msleep(10);
	}

	if (r) {
		dev_err(&dssdev->dev,
				"error disabling panel, issuing HW reset\n");
		taal_hw_reset(dssdev);
	}

	omapdss_dsi_display_disable(dssdev);

	td->enabled = 0;
}

static int taal_enable(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	dev_dbg(&dssdev->dev, "enable\n");

	mutex_lock(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	dsi_bus_lock();

	r = taal_power_on(dssdev);

	dsi_bus_unlock();

	if (r)
		goto err;

	if (panel_data->use_esd_check)
		queue_delayed_work(td->esd_wq, &td->esd_work,
				TAAL_ESD_CHECK_PERIOD);

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

	cancel_delayed_work(&td->esd_work);

	dsi_bus_lock();

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		taal_power_off(dssdev);

	dsi_bus_unlock();

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

	cancel_delayed_work(&td->esd_work);

	dsi_bus_lock();

	taal_power_off(dssdev);

	dsi_bus_unlock();

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
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	dev_dbg(&dssdev->dev, "resume\n");

	mutex_lock(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_SUSPENDED) {
		r = -EINVAL;
		goto err;
	}

	dsi_bus_lock();

	r = taal_power_on(dssdev);

	dsi_bus_unlock();

	if (r) {
		dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
	} else {
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
		if (panel_data->use_esd_check)
			queue_delayed_work(td->esd_wq, &td->esd_work,
					TAAL_ESD_CHECK_PERIOD);
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
	dsi_bus_unlock();
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

		r = omap_dsi_update(dssdev, TCH,
				td->update_region.x,
				td->update_region.y,
				td->update_region.w,
				td->update_region.h,
				taal_framedone_cb, dssdev);
		if (r)
			goto err;
	}

	return IRQ_HANDLED;
err:
	dev_err(&dssdev->dev, "start update failed\n");
	dsi_bus_unlock();
	return IRQ_HANDLED;
}

static void taal_te_timeout_work_callback(struct work_struct *work)
{
	struct taal_data *td = container_of(work, struct taal_data,
					te_timeout_work.work);
	struct omap_dss_device *dssdev = td->dssdev;

	dev_err(&dssdev->dev, "TE not received for 250ms!\n");

	atomic_set(&td->do_update, 0);
	dsi_bus_unlock();
}

static int taal_update(struct omap_dss_device *dssdev,
				    u16 x, u16 y, u16 w, u16 h)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	dev_dbg(&dssdev->dev, "update %d, %d, %d x %d\n", x, y, w, h);

	mutex_lock(&td->lock);
	dsi_bus_lock();

	if (!td->enabled) {
		r = 0;
		goto err;
	}

	r = omap_dsi_prepare_update(dssdev, &x, &y, &w, &h, true);
	if (r)
		goto err;

	r = taal_set_update_window(x, y, w, h);
	if (r)
		goto err;

	if (td->te_enabled && panel_data->use_ext_te) {
		td->update_region.x = x;
		td->update_region.y = y;
		td->update_region.w = w;
		td->update_region.h = h;
		barrier();
		schedule_delayed_work(&td->te_timeout_work,
				msecs_to_jiffies(250));
		atomic_set(&td->do_update, 1);
	} else {
		r = omap_dsi_update(dssdev, TCH, x, y, w, h,
				taal_framedone_cb, dssdev);
		if (r)
			goto err;
	}

	/* note: no bus_unlock here. unlock is in framedone_cb */
	mutex_unlock(&td->lock);
	return 0;
err:
	dsi_bus_unlock();
	mutex_unlock(&td->lock);
	return r;
}

static int taal_sync(struct omap_dss_device *dssdev)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "sync\n");

	mutex_lock(&td->lock);
	dsi_bus_lock();
	dsi_bus_unlock();
	mutex_unlock(&td->lock);

	dev_dbg(&dssdev->dev, "sync done\n");

	return 0;
}

static int _taal_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct taal_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	if (enable)
		r = taal_dcs_write_1(DCS_TEAR_ON, 0);
	else
		r = taal_dcs_write_0(DCS_TEAR_OFF);

	if (!panel_data->use_ext_te)
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

	dsi_bus_lock();

	if (td->enabled) {
		r = _taal_enable_te(dssdev, enable);
		if (r)
			goto err;
	}

	td->te_enabled = enable;

	dsi_bus_unlock();
end:
	mutex_unlock(&td->lock);

	return 0;
err:
	dsi_bus_unlock();
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
	int r;

	dev_dbg(&dssdev->dev, "rotate %d\n", rotate);

	mutex_lock(&td->lock);

	if (td->rotate == rotate)
		goto end;

	dsi_bus_lock();

	if (td->enabled) {
		r = taal_set_addr_mode(rotate, td->mirror);
		if (r)
			goto err;
	}

	td->rotate = rotate;

	dsi_bus_unlock();
end:
	mutex_unlock(&td->lock);
	return 0;
err:
	dsi_bus_unlock();
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

	dsi_bus_lock();
	if (td->enabled) {
		r = taal_set_addr_mode(td->rotate, enable);
		if (r)
			goto err;
	}

	td->mirror = enable;

	dsi_bus_unlock();
end:
	mutex_unlock(&td->lock);
	return 0;
err:
	dsi_bus_unlock();
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

	dsi_bus_lock();

	r = taal_dcs_read_1(DCS_GET_ID1, &id1);
	if (r)
		goto err2;
	r = taal_dcs_read_1(DCS_GET_ID2, &id2);
	if (r)
		goto err2;
	r = taal_dcs_read_1(DCS_GET_ID3, &id3);
	if (r)
		goto err2;

	dsi_bus_unlock();
	mutex_unlock(&td->lock);
	return 0;
err2:
	dsi_bus_unlock();
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

	dsi_bus_lock();

	/* plen 1 or 2 goes into short packet. until checksum error is fixed,
	 * use short packets. plen 32 works, but bigger packets seem to cause
	 * an error. */
	if (size % 2)
		plen = 1;
	else
		plen = 2;

	taal_set_update_window(x, y, w, h);

	r = dsi_vc_set_max_rx_packet_size(TCH, plen);
	if (r)
		goto err2;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = dsi_vc_dcs_read(TCH, dcs_cmd,
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
	dsi_vc_set_max_rx_packet_size(TCH, 1);
err2:
	dsi_bus_unlock();
err1:
	mutex_unlock(&td->lock);
	return r;
}

static void taal_esd_work(struct work_struct *work)
{
	struct taal_data *td = container_of(work, struct taal_data,
			esd_work.work);
	struct omap_dss_device *dssdev = td->dssdev;
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	u8 state1, state2;
	int r;

	mutex_lock(&td->lock);

	if (!td->enabled) {
		mutex_unlock(&td->lock);
		return;
	}

	dsi_bus_lock();

	r = taal_dcs_read_1(DCS_RDDSDR, &state1);
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

	r = taal_dcs_read_1(DCS_RDDSDR, &state2);
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
	if (td->te_enabled && panel_data->use_ext_te) {
		r = taal_dcs_write_1(DCS_TEAR_ON, 0);
		if (r)
			goto err;
	}

	dsi_bus_unlock();

	queue_delayed_work(td->esd_wq, &td->esd_work, TAAL_ESD_CHECK_PERIOD);

	mutex_unlock(&td->lock);
	return;
err:
	dev_err(&dssdev->dev, "performing LCD reset\n");

	taal_power_off(dssdev);
	taal_hw_reset(dssdev);
	taal_power_on(dssdev);

	dsi_bus_unlock();

	queue_delayed_work(td->esd_wq, &td->esd_work, TAAL_ESD_CHECK_PERIOD);

	mutex_unlock(&td->lock);
}

static int taal_set_update_mode(struct omap_dss_device *dssdev,
		enum omap_dss_update_mode mode)
{
	if (mode != OMAP_DSS_UPDATE_MANUAL)
		return -EINVAL;
	return 0;
}

static enum omap_dss_update_mode taal_get_update_mode(
		struct omap_dss_device *dssdev)
{
	return OMAP_DSS_UPDATE_MANUAL;
}

static struct omap_dss_driver taal_driver = {
	.probe		= taal_probe,
	.remove		= taal_remove,

	.enable		= taal_enable,
	.disable	= taal_disable,
	.suspend	= taal_suspend,
	.resume		= taal_resume,

	.set_update_mode = taal_set_update_mode,
	.get_update_mode = taal_get_update_mode,

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

	.get_timings	= taal_get_timings,

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
