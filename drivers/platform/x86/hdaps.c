/*
 * hdaps.c - driver for IBM's Hard Drive Active Protection System
 *
 * Copyright (C) 2005 Robert Love <rml@novell.com>
 * Copyright (C) 2005 Jesper Juhl <jj@chaosbits.net>
 *
 * The HardDisk Active Protection System (hdaps) is present in IBM ThinkPads
 * starting with the R40, T41, and X40.  It provides a basic two-axis
 * accelerometer and other data, such as the device's temperature.
 *
 * This driver is based on the document by Mark A. Smith available at
 * http://www.almaden.ibm.com/cs/people/marksmith/tpaps.html and a lot of trial
 * and error.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/thinkpad_ec.h>
#include <linux/pci_ids.h>
#include <linux/version.h>

/* Embedded controller accelerometer read command and its result: */
static const struct thinkpad_ec_row ec_accel_args =
	{ .mask = 0x0001, .val = {0x11} };
#define EC_ACCEL_IDX_READOUTS	0x1	/* readouts included in this read */
					/* First readout, if READOUTS>=1: */
#define EC_ACCEL_IDX_YPOS1	0x2	/*   y-axis position word */
#define EC_ACCEL_IDX_XPOS1	0x4	/*   x-axis position word */
#define EC_ACCEL_IDX_TEMP1	0x6	/*   device temperature in Celsius */
					/* Second readout, if READOUTS>=2: */
#define EC_ACCEL_IDX_XPOS2	0x7	/*   y-axis position word */
#define EC_ACCEL_IDX_YPOS2	0x9	/*   x-axis position word */
#define EC_ACCEL_IDX_TEMP2	0xb	/*   device temperature in Celsius */
#define EC_ACCEL_IDX_QUEUED	0xc	/* Number of queued readouts left */
#define EC_ACCEL_IDX_KMACT	0xd	/* keyboard or mouse activity */
#define EC_ACCEL_IDX_RETVAL	0xf	/* command return value, good=0x00 */

#define KEYBD_MASK		0x20	/* set if keyboard activity */
#define MOUSE_MASK		0x40	/* set if mouse activity */

#define READ_TIMEOUT_MSECS	100	/* wait this long for device read */
#define RETRY_MSECS		3	/* retry delay */

#define HDAPS_INPUT_FUZZ	4	/* input event threshold */
#define HDAPS_INPUT_FLAT	4
#define KMACT_REMEMBER_PERIOD   (HZ/10) /* keyboard/mouse persistance */

/* Input IDs */
#define HDAPS_INPUT_VENDOR	PCI_VENDOR_ID_IBM
#define HDAPS_INPUT_PRODUCT	0x5054 /* "TP", shared with thinkpad_acpi */
#define HDAPS_INPUT_JS_VERSION	0x6801 /* Joystick emulation input device */
#define HDAPS_INPUT_RAW_VERSION	0x4801 /* Raw accelerometer input device */

/* Axis orientation. */
/* The unnatural bit-representation of inversions is for backward
 * compatibility with the"invert=1" module parameter.             */
#define HDAPS_ORIENT_INVERT_XY  0x01   /* Invert both X and Y axes.       */
#define HDAPS_ORIENT_INVERT_X   0x02   /* Invert the X axis (uninvert if
					* already inverted by INVERT_XY). */
#define HDAPS_ORIENT_SWAP       0x04   /* Swap the axes. The swap occurs
					* before inverting X or Y.        */
#define HDAPS_ORIENT_MAX        0x07
#define HDAPS_ORIENT_UNDEFINED  0xFF   /* Placeholder during initialization */
#define HDAPS_ORIENT_INVERT_Y   (HDAPS_ORIENT_INVERT_XY | HDAPS_ORIENT_INVERT_X)

static struct timer_list hdaps_timer;
static struct platform_device *pdev;
static struct input_dev *hdaps_idev;     /* joystick-like device with fuzz */
static struct input_dev *hdaps_idev_raw; /* raw hdaps sensor readouts */
static unsigned int hdaps_invert = HDAPS_ORIENT_UNDEFINED;
static int needs_calibration;

/* Configuration: */
static int sampling_rate = 50;       /* Sampling rate  */
static int oversampling_ratio = 5;   /* Ratio between our sampling rate and
				      * EC accelerometer sampling rate      */
static int running_avg_filter_order = 2; /* EC running average filter order */

/* Latest state readout: */
static int pos_x, pos_y;      /* position */
static int temperature;       /* temperature */
static int stale_readout = 1; /* last read invalid */
static int rest_x, rest_y;    /* calibrated rest position */

/* Last time we saw keyboard and mouse activity: */
static u64 last_keyboard_jiffies = INITIAL_JIFFIES;
static u64 last_mouse_jiffies = INITIAL_JIFFIES;
static u64 last_update_jiffies = INITIAL_JIFFIES;

/* input device use count */
static int hdaps_users;
static DEFINE_MUTEX(hdaps_users_mtx);

/* Some models require an axis transformation to the standard representation */
static void transform_axes(int *x, int *y)
{
	if (hdaps_invert & HDAPS_ORIENT_SWAP) {
		int z;
		z = *x;
		*x = *y;
		*y = z;
	}
	if (hdaps_invert & HDAPS_ORIENT_INVERT_XY) {
		*x = -*x;
		*y = -*y;
	}
	if (hdaps_invert & HDAPS_ORIENT_INVERT_X)
		*x = -*x;
}

/**
 * __hdaps_update - query current state, with locks already acquired
 * @fast: if nonzero, do one quick attempt without retries.
 *
 * Query current accelerometer state and update global state variables.
 * Also prefetches the next query. Caller must hold controller lock.
 */
static int __hdaps_update(int fast)
{
	/* Read data: */
	struct thinkpad_ec_row data;
	int ret;

	data.mask = (1 << EC_ACCEL_IDX_READOUTS) | (1 << EC_ACCEL_IDX_KMACT) |
		    (3 << EC_ACCEL_IDX_YPOS1)    | (3 << EC_ACCEL_IDX_XPOS1) |
		    (1 << EC_ACCEL_IDX_TEMP1)    | (1 << EC_ACCEL_IDX_RETVAL);
	if (fast)
		ret = thinkpad_ec_try_read_row(&ec_accel_args, &data);
	else
		ret = thinkpad_ec_read_row(&ec_accel_args, &data);
	thinkpad_ec_prefetch_row(&ec_accel_args); /* Prefetch even if error */
	if (ret)
		return ret;

	/* Check status: */
	if (data.val[EC_ACCEL_IDX_RETVAL] != 0x00) {
		pr_warn("read RETVAL=0x%02x\n",
		       data.val[EC_ACCEL_IDX_RETVAL]);
		return -EIO;
	}

	if (data.val[EC_ACCEL_IDX_READOUTS] < 1)
		return -EBUSY; /* no pending readout, try again later */

	/* Parse position data: */
	pos_x = *(s16 *)(data.val+EC_ACCEL_IDX_XPOS1);
	pos_y = *(s16 *)(data.val+EC_ACCEL_IDX_YPOS1);
	transform_axes(&pos_x, &pos_y);

	/* Keyboard and mouse activity status is cleared as soon as it's read,
	 * so applications will eat each other's events. Thus we remember any
	 * event for KMACT_REMEMBER_PERIOD jiffies.
	 */
	if (data.val[EC_ACCEL_IDX_KMACT] & KEYBD_MASK)
		last_keyboard_jiffies = get_jiffies_64();
	if (data.val[EC_ACCEL_IDX_KMACT] & MOUSE_MASK)
		last_mouse_jiffies = get_jiffies_64();

	temperature = data.val[EC_ACCEL_IDX_TEMP1];

	last_update_jiffies = get_jiffies_64();
	stale_readout = 0;
	if (needs_calibration) {
		rest_x = pos_x;
		rest_y = pos_y;
		needs_calibration = 0;
	}

	return 0;
}

/**
 * hdaps_update - acquire locks and query current state
 *
 * Query current accelerometer state and update global state variables.
 * Also prefetches the next query.
 * Retries until timeout if the accelerometer is not in ready status (common).
 * Does its own locking.
 */
static int hdaps_update(void)
{
	u64 age = get_jiffies_64() - last_update_jiffies;
	int total, ret;

	if (!stale_readout && age < (9*HZ)/(10*sampling_rate))
		return 0; /* already updated recently */
	for (total = 0; total < READ_TIMEOUT_MSECS; total += RETRY_MSECS) {
		ret = thinkpad_ec_lock();
		if (ret)
			return ret;
		ret = __hdaps_update(0);
		thinkpad_ec_unlock();

		if (!ret)
			return 0;
		if (ret != -EBUSY)
			break;
		msleep(RETRY_MSECS);
	}
	return ret;
}

/**
 * hdaps_set_power - enable or disable power to the accelerometer.
 * Returns zero on success and negative error code on failure.  Can sleep.
 */
static int hdaps_set_power(int on)
{
	struct thinkpad_ec_row args =
		{ .mask = 0x0003, .val = {0x14, on?0x01:0x00} };
	struct thinkpad_ec_row data = { .mask = 0x8000 };
	int ret = thinkpad_ec_read_row(&args, &data);
	if (ret)
		return ret;
	if (data.val[0xF] != 0x00)
		return -EIO;
	return 0;
}

/**
 * hdaps_set_ec_config - set accelerometer parameters.
 * @ec_rate: embedded controller sampling rate
 * @order: embedded controller running average filter order
 * (Normally we have @ec_rate = sampling_rate * oversampling_ratio.)
 * Returns zero on success and negative error code on failure.  Can sleep.
 */
static int hdaps_set_ec_config(int ec_rate, int order)
{
	struct thinkpad_ec_row args = { .mask = 0x000F,
		.val = {0x10, (u8)ec_rate, (u8)(ec_rate>>8), order} };
	struct thinkpad_ec_row data = { .mask = 0x8000 };
	int ret = thinkpad_ec_read_row(&args, &data);
	pr_debug("setting ec_rate=%d, filter_order=%d\n", ec_rate, order);
	if (ret)
		return ret;
	if (data.val[0xF] == 0x03) {
		pr_warn("config param out of range\n");
		return -EINVAL;
	}
	if (data.val[0xF] == 0x06) {
		pr_warn("config change already pending\n");
		return -EBUSY;
	}
	if (data.val[0xF] != 0x00) {
		pr_warn("config change error, ret=%d\n",
		      data.val[0xF]);
		return -EIO;
	}
	return 0;
}

/**
 * hdaps_get_ec_config - get accelerometer parameters.
 * @ec_rate: embedded controller sampling rate
 * @order: embedded controller running average filter order
 * Returns zero on success and negative error code on failure.  Can sleep.
 */
static int hdaps_get_ec_config(int *ec_rate, int *order)
{
	const struct thinkpad_ec_row args =
		{ .mask = 0x0003, .val = {0x17, 0x82} };
	struct thinkpad_ec_row data = { .mask = 0x801F };
	int ret = thinkpad_ec_read_row(&args, &data);
	if (ret)
		return ret;
	if (data.val[0xF] != 0x00)
		return -EIO;
	if (!(data.val[0x1] & 0x01))
		return -ENXIO; /* accelerometer polling not enabled */
	if (data.val[0x1] & 0x02)
		return -EBUSY; /* config change in progress, retry later */
	*ec_rate = data.val[0x2] | ((int)(data.val[0x3]) << 8);
	*order = data.val[0x4];
	return 0;
}

/**
 * hdaps_get_ec_mode - get EC accelerometer mode
 * Returns zero on success and negative error code on failure.  Can sleep.
 */
static int hdaps_get_ec_mode(u8 *mode)
{
	const struct thinkpad_ec_row args =
		{ .mask = 0x0001, .val = {0x13} };
	struct thinkpad_ec_row data = { .mask = 0x8002 };
	int ret = thinkpad_ec_read_row(&args, &data);
	if (ret)
		return ret;
	if (data.val[0xF] != 0x00) {
		pr_warn("accelerometer not implemented (0x%02x)\n",
		       data.val[0xF]);
		return -EIO;
	}
	*mode = data.val[0x1];
	return 0;
}

/**
 * hdaps_check_ec - checks something about the EC.
 * Follows the clean-room spec for HDAPS; we don't know what it means.
 * Returns zero on success and negative error code on failure.  Can sleep.
 */
static int hdaps_check_ec(void)
{
	const struct thinkpad_ec_row args =
		{ .mask = 0x0003, .val = {0x17, 0x81} };
	struct thinkpad_ec_row data = { .mask = 0x800E };
	int ret = thinkpad_ec_read_row(&args, &data);
	if (ret)
		return  ret;
	if (!((data.val[0x1] == 0x00 && data.val[0x2] == 0x60) || /* cleanroom spec */
	      (data.val[0x1] == 0x01 && data.val[0x2] == 0x00)) || /* seen on T61 */
	    data.val[0x3] != 0x00 || data.val[0xF] != 0x00) {
		pr_warn("hdaps_check_ec: bad response (0x%x,0x%x,0x%x,0x%x)\n",
		       data.val[0x1], data.val[0x2],
		       data.val[0x3], data.val[0xF]);
		return -EIO;
	}
	return 0;
}

/**
 * hdaps_device_init - initialize the accelerometer.
 *
 * Call several embedded controller functions to test and initialize the
 * accelerometer.
 * Returns zero on success and negative error code on failure. Can sleep.
 */
#define FAILED_INIT(msg) pr_err("init failed at: %s\n", msg)
static int hdaps_device_init(void)
{
	int ret;
	u8 mode;

	ret = thinkpad_ec_lock();
	if (ret)
		return ret;

	if (hdaps_get_ec_mode(&mode))
		{ FAILED_INIT("hdaps_get_ec_mode failed"); goto bad; }

	pr_debug("initial mode latch is 0x%02x\n", mode);
	if (mode == 0x00)
		{ FAILED_INIT("accelerometer not available"); goto bad; }

	if (hdaps_check_ec())
		{ FAILED_INIT("hdaps_check_ec failed"); goto bad; }

	if (hdaps_set_power(1))
		{ FAILED_INIT("hdaps_set_power failed"); goto bad; }

	if (hdaps_set_ec_config(sampling_rate*oversampling_ratio,
				running_avg_filter_order))
		{ FAILED_INIT("hdaps_set_ec_config failed"); goto bad; }

	thinkpad_ec_invalidate();
	udelay(200);

	/* Just prefetch instead of reading, to avoid ~1sec delay on load */
	ret = thinkpad_ec_prefetch_row(&ec_accel_args);
	if (ret)
		{ FAILED_INIT("initial prefetch failed"); goto bad; }
	goto good;
bad:
	thinkpad_ec_invalidate();
	ret = -ENXIO;
good:
	stale_readout = 1;
	thinkpad_ec_unlock();
	return ret;
}

/**
 * hdaps_device_shutdown - power off the accelerometer
 * Returns nonzero on failure. Can sleep.
 */
static int hdaps_device_shutdown(void)
{
	int ret;
	ret = hdaps_set_power(0);
	if (ret) {
		pr_warn("cannot power off\n");
		return ret;
	}
	ret = hdaps_set_ec_config(0, 1);
	if (ret)
		pr_warn("cannot stop EC sampling\n");
	return ret;
}

/* Device model stuff */

static int hdaps_probe(struct platform_device *dev)
{
	int ret;

	ret = hdaps_device_init();
	if (ret)
		return ret;

	pr_info("device successfully initialized\n");
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hdaps_suspend(struct device *dev)
{
	/* Don't do hdaps polls until resume re-initializes the sensor. */
	del_timer_sync(&hdaps_timer);
	hdaps_device_shutdown(); /* ignore errors, effect is negligible */
	return 0;
}

static int hdaps_resume(struct device *dev)
{
	int ret = hdaps_device_init();
	if (ret)
		return ret;

	mutex_lock(&hdaps_users_mtx);
	if (hdaps_users)
		mod_timer(&hdaps_timer, jiffies + HZ/sampling_rate);
	mutex_unlock(&hdaps_users_mtx);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(hdaps_pm, hdaps_suspend, hdaps_resume);

static struct platform_driver hdaps_driver = {
	.probe = hdaps_probe,
	.driver	= {
		.name = "hdaps",
		.owner = THIS_MODULE,
		.pm = &hdaps_pm,
	},
};

/**
 * hdaps_calibrate - set our "resting" values.
 * Does its own locking.
 */
static void hdaps_calibrate(void)
{
	needs_calibration = 1;
	hdaps_update();
	/* If that fails, the mousedev poll will take care of things later. */
}

/* Timer handler for updating the input device. Runs in softirq context,
 * so avoid lenghty or blocking operations.
 */
static void hdaps_mousedev_poll(unsigned long unused)
{
	int ret;

	stale_readout = 1;

	/* Cannot sleep.  Try nonblockingly.  If we fail, try again later. */
	if (thinkpad_ec_try_lock())
		goto keep_active;

	ret = __hdaps_update(1); /* fast update, we're in softirq context */
	thinkpad_ec_unlock();
	/* Any of "successful", "not yet ready" and "not prefetched"? */
	if (ret != 0 && ret != -EBUSY && ret != -ENODATA) {
		pr_err("poll failed, disabling updates\n");
		return;
	}

keep_active:
	/* Even if we failed now, pos_x,y may have been updated earlier: */
	input_report_abs(hdaps_idev, ABS_X, pos_x - rest_x);
	input_report_abs(hdaps_idev, ABS_Y, pos_y - rest_y);
	input_sync(hdaps_idev);
	input_report_abs(hdaps_idev_raw, ABS_X, pos_x);
	input_report_abs(hdaps_idev_raw, ABS_Y, pos_y);
	input_sync(hdaps_idev_raw);
	mod_timer(&hdaps_timer, jiffies + HZ/sampling_rate);
}


/* Sysfs Files */

static ssize_t hdaps_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret = hdaps_update();
	if (ret)
		return ret;
	return sprintf(buf, "(%d,%d)\n", pos_x, pos_y);
}

static ssize_t hdaps_temp1_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = hdaps_update();
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", temperature);
}

static ssize_t hdaps_keyboard_activity_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	int ret = hdaps_update();
	if (ret)
		return ret;
	return sprintf(buf, "%u\n",
	   get_jiffies_64() < last_keyboard_jiffies + KMACT_REMEMBER_PERIOD);
}

static ssize_t hdaps_mouse_activity_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int ret = hdaps_update();
	if (ret)
		return ret;
	return sprintf(buf, "%u\n",
	   get_jiffies_64() < last_mouse_jiffies + KMACT_REMEMBER_PERIOD);
}

static ssize_t hdaps_calibrate_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(%d,%d)\n", rest_x, rest_y);
}

static ssize_t hdaps_calibrate_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	hdaps_calibrate();
	return count;
}

static ssize_t hdaps_invert_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", hdaps_invert);
}

static ssize_t hdaps_invert_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int invert;

	if (sscanf(buf, "%d", &invert) != 1 ||
	    invert < 0 || invert > HDAPS_ORIENT_MAX)
		return -EINVAL;

	hdaps_invert = invert;
	hdaps_calibrate();

	return count;
}

static ssize_t hdaps_sampling_rate_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", sampling_rate);
}

static ssize_t hdaps_sampling_rate_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int rate, ret;
	if (sscanf(buf, "%d", &rate) != 1 || rate > HZ || rate <= 0) {
		pr_warn("must have 0<input_sampling_rate<=HZ=%d\n", HZ);
		return -EINVAL;
	}
	ret = hdaps_set_ec_config(rate*oversampling_ratio,
				  running_avg_filter_order);
	if (ret)
		return ret;
	sampling_rate = rate;
	return count;
}

static ssize_t hdaps_oversampling_ratio_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	int ec_rate, order;
	int ret = hdaps_get_ec_config(&ec_rate, &order);
	if (ret)
		return ret;
	return sprintf(buf, "%u\n", ec_rate / sampling_rate);
}

static ssize_t hdaps_oversampling_ratio_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ratio, ret;
	if (sscanf(buf, "%d", &ratio) != 1 || ratio < 1)
		return -EINVAL;
	ret = hdaps_set_ec_config(sampling_rate*ratio,
				  running_avg_filter_order);
	if (ret)
		return ret;
	oversampling_ratio = ratio;
	return count;
}

static ssize_t hdaps_running_avg_filter_order_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	int rate, order;
	int ret = hdaps_get_ec_config(&rate, &order);
	if (ret)
		return ret;
	return sprintf(buf, "%u\n", order);
}

static ssize_t hdaps_running_avg_filter_order_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int order, ret;
	if (sscanf(buf, "%d", &order) != 1)
		return -EINVAL;
	ret = hdaps_set_ec_config(sampling_rate*oversampling_ratio, order);
	if (ret)
		return ret;
	running_avg_filter_order = order;
	return count;
}

static int hdaps_mousedev_open(struct input_dev *dev)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	mutex_lock(&hdaps_users_mtx);
	if (hdaps_users++ == 0) /* first input user */
		mod_timer(&hdaps_timer, jiffies + HZ/sampling_rate);
	mutex_unlock(&hdaps_users_mtx);
	return 0;
}

static void hdaps_mousedev_close(struct input_dev *dev)
{
	mutex_lock(&hdaps_users_mtx);
	if (--hdaps_users == 0) /* no input users left */
		del_timer_sync(&hdaps_timer);
	mutex_unlock(&hdaps_users_mtx);

	module_put(THIS_MODULE);
}

static DEVICE_ATTR(position, 0444, hdaps_position_show, NULL);
static DEVICE_ATTR(temp1, 0444, hdaps_temp1_show, NULL);
  /* "temp1" instead of "temperature" is hwmon convention */
static DEVICE_ATTR(keyboard_activity, 0444,
		   hdaps_keyboard_activity_show, NULL);
static DEVICE_ATTR(mouse_activity, 0444, hdaps_mouse_activity_show, NULL);
static DEVICE_ATTR(calibrate, 0644,
		   hdaps_calibrate_show, hdaps_calibrate_store);
static DEVICE_ATTR(invert, 0644, hdaps_invert_show, hdaps_invert_store);
static DEVICE_ATTR(sampling_rate, 0644,
		   hdaps_sampling_rate_show, hdaps_sampling_rate_store);
static DEVICE_ATTR(oversampling_ratio, 0644,
		   hdaps_oversampling_ratio_show,
		   hdaps_oversampling_ratio_store);
static DEVICE_ATTR(running_avg_filter_order, 0644,
		   hdaps_running_avg_filter_order_show,
		   hdaps_running_avg_filter_order_store);

static struct attribute *hdaps_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_temp1.attr,
	&dev_attr_keyboard_activity.attr,
	&dev_attr_mouse_activity.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_invert.attr,
	&dev_attr_sampling_rate.attr,
	&dev_attr_oversampling_ratio.attr,
	&dev_attr_running_avg_filter_order.attr,
	NULL,
};

static struct attribute_group hdaps_attribute_group = {
	.attrs = hdaps_attributes,
};


/* Module stuff */

/* hdaps_dmi_match_invert - found an inverted match. */
static int __init hdaps_dmi_match_invert(const struct dmi_system_id *id)
{
	unsigned int orient = (kernel_ulong_t) id->driver_data;
	hdaps_invert = orient;
	pr_info("%s detected, setting orientation %u\n", id->ident, orient);
	return 1; /* stop enumeration */
}

#define HDAPS_DMI_MATCH_INVERT(vendor, model, orient) { \
	.ident = vendor " " model,			\
	.callback = hdaps_dmi_match_invert,		\
	.driver_data = (void *)(orient),		\
	.matches = {					\
		DMI_MATCH(DMI_BOARD_VENDOR, vendor),	\
		DMI_MATCH(DMI_PRODUCT_VERSION, model)	\
	}						\
}

/* List of models with abnormal axis configuration.
   Note that HDAPS_DMI_MATCH_NORMAL("ThinkPad T42") would match
   "ThinkPad T42p", and enumeration stops after first match,
   so the order of the entries matters. */
struct dmi_system_id __initdata hdaps_whitelist[] = {
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad R50p", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad R60", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad T41p", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad T42p", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad X40", HDAPS_ORIENT_INVERT_Y),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad X41", HDAPS_ORIENT_INVERT_Y),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad R60", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad R61", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T60", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T61", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X60 Tablet", HDAPS_ORIENT_INVERT_Y),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X60s", HDAPS_ORIENT_INVERT_Y),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X60", HDAPS_ORIENT_SWAP | HDAPS_ORIENT_INVERT_X),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X61", HDAPS_ORIENT_SWAP | HDAPS_ORIENT_INVERT_X),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T400s", HDAPS_ORIENT_INVERT_X),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T400", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T410", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T500", HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X200", HDAPS_ORIENT_SWAP | HDAPS_ORIENT_INVERT_X | HDAPS_ORIENT_INVERT_Y),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X201 Tablet", HDAPS_ORIENT_SWAP | HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X201s", HDAPS_ORIENT_SWAP | HDAPS_ORIENT_INVERT_XY),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X201", HDAPS_ORIENT_SWAP | HDAPS_ORIENT_INVERT_X),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X220", HDAPS_ORIENT_SWAP),
	{ .ident = NULL }
};

static int __init hdaps_init(void)
{
	int ret;

	/* Determine axis orientation orientation */
	if (hdaps_invert == HDAPS_ORIENT_UNDEFINED) /* set by module param? */
		if (dmi_check_system(hdaps_whitelist) < 1) /* in whitelist? */
			hdaps_invert = 0; /* default */

	/* Init timer before platform_driver_register, in case of suspend */
	init_timer(&hdaps_timer);
	hdaps_timer.function = hdaps_mousedev_poll;
	ret = platform_driver_register(&hdaps_driver);
	if (ret)
		goto out;

	pdev = platform_device_register_simple("hdaps", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &hdaps_attribute_group);
	if (ret)
		goto out_device;

	hdaps_idev = input_allocate_device();
	if (!hdaps_idev) {
		ret = -ENOMEM;
		goto out_group;
	}

	hdaps_idev_raw = input_allocate_device();
	if (!hdaps_idev_raw) {
		ret = -ENOMEM;
		goto out_idev_first;
	}

	/* calibration for the input device (deferred to avoid delay) */
	needs_calibration = 1;

	/* initialize the joystick-like fuzzed input device */
	hdaps_idev->name = "ThinkPad HDAPS joystick emulation";
	hdaps_idev->phys = "hdaps/input0";
	hdaps_idev->id.bustype = BUS_HOST;
	hdaps_idev->id.vendor  = HDAPS_INPUT_VENDOR;
	hdaps_idev->id.product = HDAPS_INPUT_PRODUCT;
	hdaps_idev->id.version = HDAPS_INPUT_JS_VERSION;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	hdaps_idev->cdev.dev = &pdev->dev;
#endif
	hdaps_idev->evbit[0] = BIT(EV_ABS);
	hdaps_idev->open = hdaps_mousedev_open;
	hdaps_idev->close = hdaps_mousedev_close;
	input_set_abs_params(hdaps_idev, ABS_X,
			-256, 256, HDAPS_INPUT_FUZZ, HDAPS_INPUT_FLAT);
	input_set_abs_params(hdaps_idev, ABS_Y,
			-256, 256, HDAPS_INPUT_FUZZ, HDAPS_INPUT_FLAT);

	ret = input_register_device(hdaps_idev);
	if (ret)
		goto out_idev;

	/* initialize the raw data input device */
	hdaps_idev_raw->name = "ThinkPad HDAPS accelerometer data";
	hdaps_idev_raw->phys = "hdaps/input1";
	hdaps_idev_raw->id.bustype = BUS_HOST;
	hdaps_idev_raw->id.vendor  = HDAPS_INPUT_VENDOR;
	hdaps_idev_raw->id.product = HDAPS_INPUT_PRODUCT;
	hdaps_idev_raw->id.version = HDAPS_INPUT_RAW_VERSION;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	hdaps_idev_raw->cdev.dev = &pdev->dev;
#endif
	hdaps_idev_raw->evbit[0] = BIT(EV_ABS);
	hdaps_idev_raw->open = hdaps_mousedev_open;
	hdaps_idev_raw->close = hdaps_mousedev_close;
	input_set_abs_params(hdaps_idev_raw, ABS_X, -32768, 32767, 0, 0);
	input_set_abs_params(hdaps_idev_raw, ABS_Y, -32768, 32767, 0, 0);

	ret = input_register_device(hdaps_idev_raw);
	if (ret)
		goto out_idev_reg_first;

	pr_info("driver successfully loaded.\n");
	return 0;

out_idev_reg_first:
	input_unregister_device(hdaps_idev);
out_idev:
	input_free_device(hdaps_idev_raw);
out_idev_first:
	input_free_device(hdaps_idev);
out_group:
	sysfs_remove_group(&pdev->dev.kobj, &hdaps_attribute_group);
out_device:
	platform_device_unregister(pdev);
out_driver:
	platform_driver_unregister(&hdaps_driver);
	hdaps_device_shutdown();
out:
	pr_warn("driver init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit hdaps_exit(void)
{
	input_unregister_device(hdaps_idev_raw);
	input_unregister_device(hdaps_idev);
	hdaps_device_shutdown(); /* ignore errors, effect is negligible */
	sysfs_remove_group(&pdev->dev.kobj, &hdaps_attribute_group);
	platform_device_unregister(pdev);
	platform_driver_unregister(&hdaps_driver);

	pr_info("driver unloaded\n");
}

module_init(hdaps_init);
module_exit(hdaps_exit);

module_param_named(invert, hdaps_invert, uint, 0);
MODULE_PARM_DESC(invert, "axis orientation code");

MODULE_AUTHOR("Robert Love");
MODULE_DESCRIPTION("IBM Hard Drive Active Protection System (HDAPS) driver");
MODULE_LICENSE("GPL v2");
