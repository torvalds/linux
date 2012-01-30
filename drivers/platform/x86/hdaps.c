/*
 * hdaps.c - driver for IBM's Hard Drive Active Protection System
 *
 * Copyright (C) 2005 Robert Love <rml@novell.com>
 * Copyright (C) 2005 Jesper Juhl <jesper.juhl@gmail.com>
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
#include <linux/input-polldev.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/io.h>

#define HDAPS_LOW_PORT		0x1600	/* first port used by hdaps */
#define HDAPS_NR_PORTS		0x30	/* number of ports: 0x1600 - 0x162f */

#define HDAPS_PORT_STATE	0x1611	/* device state */
#define HDAPS_PORT_YPOS		0x1612	/* y-axis position */
#define	HDAPS_PORT_XPOS		0x1614	/* x-axis position */
#define HDAPS_PORT_TEMP1	0x1616	/* device temperature, in Celsius */
#define HDAPS_PORT_YVAR		0x1617	/* y-axis variance (what is this?) */
#define HDAPS_PORT_XVAR		0x1619	/* x-axis variance (what is this?) */
#define HDAPS_PORT_TEMP2	0x161b	/* device temperature (again?) */
#define HDAPS_PORT_UNKNOWN	0x161c	/* what is this? */
#define HDAPS_PORT_KMACT	0x161d	/* keyboard or mouse activity */

#define STATE_FRESH		0x50	/* accelerometer data is fresh */

#define KEYBD_MASK		0x20	/* set if keyboard activity */
#define MOUSE_MASK		0x40	/* set if mouse activity */
#define KEYBD_ISSET(n)		(!! (n & KEYBD_MASK))	/* keyboard used? */
#define MOUSE_ISSET(n)		(!! (n & MOUSE_MASK))	/* mouse used? */

#define INIT_TIMEOUT_MSECS	4000	/* wait up to 4s for device init ... */
#define INIT_WAIT_MSECS		200	/* ... in 200ms increments */

#define HDAPS_POLL_INTERVAL	50	/* poll for input every 1/20s (50 ms)*/
#define HDAPS_INPUT_FUZZ	4	/* input event threshold */
#define HDAPS_INPUT_FLAT	4

#define HDAPS_X_AXIS		(1 << 0)
#define HDAPS_Y_AXIS		(1 << 1)
#define HDAPS_BOTH_AXES		(HDAPS_X_AXIS | HDAPS_Y_AXIS)

static struct platform_device *pdev;
static struct input_polled_dev *hdaps_idev;
static unsigned int hdaps_invert;
static u8 km_activity;
static int rest_x;
static int rest_y;

static DEFINE_MUTEX(hdaps_mtx);

/*
 * __get_latch - Get the value from a given port.  Callers must hold hdaps_mtx.
 */
static inline u8 __get_latch(u16 port)
{
	return inb(port) & 0xff;
}

/*
 * __check_latch - Check a port latch for a given value.  Returns zero if the
 * port contains the given value.  Callers must hold hdaps_mtx.
 */
static inline int __check_latch(u16 port, u8 val)
{
	if (__get_latch(port) == val)
		return 0;
	return -EINVAL;
}

/*
 * __wait_latch - Wait up to 100us for a port latch to get a certain value,
 * returning zero if the value is obtained.  Callers must hold hdaps_mtx.
 */
static int __wait_latch(u16 port, u8 val)
{
	unsigned int i;

	for (i = 0; i < 20; i++) {
		if (!__check_latch(port, val))
			return 0;
		udelay(5);
	}

	return -EIO;
}

/*
 * __device_refresh - request a refresh from the accelerometer.  Does not wait
 * for refresh to complete.  Callers must hold hdaps_mtx.
 */
static void __device_refresh(void)
{
	udelay(200);
	if (inb(0x1604) != STATE_FRESH) {
		outb(0x11, 0x1610);
		outb(0x01, 0x161f);
	}
}

/*
 * __device_refresh_sync - request a synchronous refresh from the
 * accelerometer.  We wait for the refresh to complete.  Returns zero if
 * successful and nonzero on error.  Callers must hold hdaps_mtx.
 */
static int __device_refresh_sync(void)
{
	__device_refresh();
	return __wait_latch(0x1604, STATE_FRESH);
}

/*
 * __device_complete - indicate to the accelerometer that we are done reading
 * data, and then initiate an async refresh.  Callers must hold hdaps_mtx.
 */
static inline void __device_complete(void)
{
	inb(0x161f);
	inb(0x1604);
	__device_refresh();
}

/*
 * hdaps_readb_one - reads a byte from a single I/O port, placing the value in
 * the given pointer.  Returns zero on success or a negative error on failure.
 * Can sleep.
 */
static int hdaps_readb_one(unsigned int port, u8 *val)
{
	int ret;

	mutex_lock(&hdaps_mtx);

	/* do a sync refresh -- we need to be sure that we read fresh data */
	ret = __device_refresh_sync();
	if (ret)
		goto out;

	*val = inb(port);
	__device_complete();

out:
	mutex_unlock(&hdaps_mtx);
	return ret;
}

/* __hdaps_read_pair - internal lockless helper for hdaps_read_pair(). */
static int __hdaps_read_pair(unsigned int port1, unsigned int port2,
			     int *x, int *y)
{
	/* do a sync refresh -- we need to be sure that we read fresh data */
	if (__device_refresh_sync())
		return -EIO;

	*y = inw(port2);
	*x = inw(port1);
	km_activity = inb(HDAPS_PORT_KMACT);
	__device_complete();

	/* hdaps_invert is a bitvector to negate the axes */
	if (hdaps_invert & HDAPS_X_AXIS)
		*x = -*x;
	if (hdaps_invert & HDAPS_Y_AXIS)
		*y = -*y;

	return 0;
}

/*
 * hdaps_read_pair - reads the values from a pair of ports, placing the values
 * in the given pointers.  Returns zero on success.  Can sleep.
 */
static int hdaps_read_pair(unsigned int port1, unsigned int port2,
			   int *val1, int *val2)
{
	int ret;

	mutex_lock(&hdaps_mtx);
	ret = __hdaps_read_pair(port1, port2, val1, val2);
	mutex_unlock(&hdaps_mtx);

	return ret;
}

/*
 * hdaps_device_init - initialize the accelerometer.  Returns zero on success
 * and negative error code on failure.  Can sleep.
 */
static int hdaps_device_init(void)
{
	int total, ret = -ENXIO;

	mutex_lock(&hdaps_mtx);

	outb(0x13, 0x1610);
	outb(0x01, 0x161f);
	if (__wait_latch(0x161f, 0x00))
		goto out;

	/*
	 * Most ThinkPads return 0x01.
	 *
	 * Others--namely the R50p, T41p, and T42p--return 0x03.  These laptops
	 * have "inverted" axises.
	 *
	 * The 0x02 value occurs when the chip has been previously initialized.
	 */
	if (__check_latch(0x1611, 0x03) &&
		     __check_latch(0x1611, 0x02) &&
		     __check_latch(0x1611, 0x01))
		goto out;

	printk(KERN_DEBUG "hdaps: initial latch check good (0x%02x)\n",
	       __get_latch(0x1611));

	outb(0x17, 0x1610);
	outb(0x81, 0x1611);
	outb(0x01, 0x161f);
	if (__wait_latch(0x161f, 0x00))
		goto out;
	if (__wait_latch(0x1611, 0x00))
		goto out;
	if (__wait_latch(0x1612, 0x60))
		goto out;
	if (__wait_latch(0x1613, 0x00))
		goto out;
	outb(0x14, 0x1610);
	outb(0x01, 0x1611);
	outb(0x01, 0x161f);
	if (__wait_latch(0x161f, 0x00))
		goto out;
	outb(0x10, 0x1610);
	outb(0xc8, 0x1611);
	outb(0x00, 0x1612);
	outb(0x02, 0x1613);
	outb(0x01, 0x161f);
	if (__wait_latch(0x161f, 0x00))
		goto out;
	if (__device_refresh_sync())
		goto out;
	if (__wait_latch(0x1611, 0x00))
		goto out;

	/* we have done our dance, now let's wait for the applause */
	for (total = INIT_TIMEOUT_MSECS; total > 0; total -= INIT_WAIT_MSECS) {
		int x, y;

		/* a read of the device helps push it into action */
		__hdaps_read_pair(HDAPS_PORT_XPOS, HDAPS_PORT_YPOS, &x, &y);
		if (!__wait_latch(0x1611, 0x02)) {
			ret = 0;
			break;
		}

		msleep(INIT_WAIT_MSECS);
	}

out:
	mutex_unlock(&hdaps_mtx);
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

static int hdaps_resume(struct platform_device *dev)
{
	return hdaps_device_init();
}

static struct platform_driver hdaps_driver = {
	.probe = hdaps_probe,
	.resume = hdaps_resume,
	.driver	= {
		.name = "hdaps",
		.owner = THIS_MODULE,
	},
};

/*
 * hdaps_calibrate - Set our "resting" values.  Callers must hold hdaps_mtx.
 */
static void hdaps_calibrate(void)
{
	__hdaps_read_pair(HDAPS_PORT_XPOS, HDAPS_PORT_YPOS, &rest_x, &rest_y);
}

static void hdaps_mousedev_poll(struct input_polled_dev *dev)
{
	struct input_dev *input_dev = dev->input;
	int x, y;

	mutex_lock(&hdaps_mtx);

	if (__hdaps_read_pair(HDAPS_PORT_XPOS, HDAPS_PORT_YPOS, &x, &y))
		goto out;

	input_report_abs(input_dev, ABS_X, x - rest_x);
	input_report_abs(input_dev, ABS_Y, y - rest_y);
	input_sync(input_dev);

out:
	mutex_unlock(&hdaps_mtx);
}


/* Sysfs Files */

static ssize_t hdaps_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret, x, y;

	ret = hdaps_read_pair(HDAPS_PORT_XPOS, HDAPS_PORT_YPOS, &x, &y);
	if (ret)
		return ret;

	return sprintf(buf, "(%d,%d)\n", x, y);
}

static ssize_t hdaps_variance_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret, x, y;

	ret = hdaps_read_pair(HDAPS_PORT_XVAR, HDAPS_PORT_YVAR, &x, &y);
	if (ret)
		return ret;

	return sprintf(buf, "(%d,%d)\n", x, y);
}

static ssize_t hdaps_temp1_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 uninitialized_var(temp);
	int ret;

	ret = hdaps_readb_one(HDAPS_PORT_TEMP1, &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", temp);
}

static ssize_t hdaps_temp2_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 uninitialized_var(temp);
	int ret;

	ret = hdaps_readb_one(HDAPS_PORT_TEMP2, &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", temp);
}

static ssize_t hdaps_keyboard_activity_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return sprintf(buf, "%u\n", KEYBD_ISSET(km_activity));
}

static ssize_t hdaps_mouse_activity_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sprintf(buf, "%u\n", MOUSE_ISSET(km_activity));
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
	mutex_lock(&hdaps_mtx);
	hdaps_calibrate();
	mutex_unlock(&hdaps_mtx);

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
	    invert < 0 || invert > HDAPS_BOTH_AXES)
		return -EINVAL;

	hdaps_invert = invert;
	hdaps_calibrate();

	return count;
}

static DEVICE_ATTR(position, 0444, hdaps_position_show, NULL);
static DEVICE_ATTR(variance, 0444, hdaps_variance_show, NULL);
static DEVICE_ATTR(temp1, 0444, hdaps_temp1_show, NULL);
static DEVICE_ATTR(temp2, 0444, hdaps_temp2_show, NULL);
static DEVICE_ATTR(keyboard_activity, 0444, hdaps_keyboard_activity_show, NULL);
static DEVICE_ATTR(mouse_activity, 0444, hdaps_mouse_activity_show, NULL);
static DEVICE_ATTR(calibrate, 0644, hdaps_calibrate_show,hdaps_calibrate_store);
static DEVICE_ATTR(invert, 0644, hdaps_invert_show, hdaps_invert_store);

static struct attribute *hdaps_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_variance.attr,
	&dev_attr_temp1.attr,
	&dev_attr_temp2.attr,
	&dev_attr_keyboard_activity.attr,
	&dev_attr_mouse_activity.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_invert.attr,
	NULL,
};

static struct attribute_group hdaps_attribute_group = {
	.attrs = hdaps_attributes,
};


/* Module stuff */

/* hdaps_dmi_match - found a match.  return one, short-circuiting the hunt. */
static int __init hdaps_dmi_match(const struct dmi_system_id *id)
{
	pr_info("%s detected\n", id->ident);
	return 1;
}

/* hdaps_dmi_match_invert - found an inverted match. */
static int __init hdaps_dmi_match_invert(const struct dmi_system_id *id)
{
	hdaps_invert = (unsigned long)id->driver_data;
	pr_info("inverting axis (%u) readings\n", hdaps_invert);
	return hdaps_dmi_match(id);
}

#define HDAPS_DMI_MATCH_INVERT(vendor, model, axes) {	\
	.ident = vendor " " model,			\
	.callback = hdaps_dmi_match_invert,		\
	.driver_data = (void *)axes,			\
	.matches = {					\
		DMI_MATCH(DMI_BOARD_VENDOR, vendor),	\
		DMI_MATCH(DMI_PRODUCT_VERSION, model)	\
	}						\
}

#define HDAPS_DMI_MATCH_NORMAL(vendor, model)		\
	HDAPS_DMI_MATCH_INVERT(vendor, model, 0)

/* Note that HDAPS_DMI_MATCH_NORMAL("ThinkPad T42") would match
   "ThinkPad T42p", so the order of the entries matters.
   If your ThinkPad is not recognized, please update to latest
   BIOS. This is especially the case for some R52 ThinkPads. */
static struct dmi_system_id __initdata hdaps_whitelist[] = {
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad R50p", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad R50"),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad R51"),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad R52"),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad R61i", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad R61", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad T41p", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad T41"),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad T42p", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad T42"),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad T43"),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T400", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T60", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T61p", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad T61", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad X40"),
	HDAPS_DMI_MATCH_INVERT("IBM", "ThinkPad X41", HDAPS_Y_AXIS),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X60", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X61s", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad X61", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_NORMAL("IBM", "ThinkPad Z60m"),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad Z61m", HDAPS_BOTH_AXES),
	HDAPS_DMI_MATCH_INVERT("LENOVO", "ThinkPad Z61p", HDAPS_BOTH_AXES),
	{ .ident = NULL }
};

static int __init hdaps_init(void)
{
	struct input_dev *idev;
	int ret;

	if (!dmi_check_system(hdaps_whitelist)) {
		pr_warn("supported laptop not found!\n");
		ret = -ENODEV;
		goto out;
	}

	if (!request_region(HDAPS_LOW_PORT, HDAPS_NR_PORTS, "hdaps")) {
		ret = -ENXIO;
		goto out;
	}

	ret = platform_driver_register(&hdaps_driver);
	if (ret)
		goto out_region;

	pdev = platform_device_register_simple("hdaps", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &hdaps_attribute_group);
	if (ret)
		goto out_device;

	hdaps_idev = input_allocate_polled_device();
	if (!hdaps_idev) {
		ret = -ENOMEM;
		goto out_group;
	}

	hdaps_idev->poll = hdaps_mousedev_poll;
	hdaps_idev->poll_interval = HDAPS_POLL_INTERVAL;

	/* initial calibrate for the input device */
	hdaps_calibrate();

	/* initialize the input class */
	idev = hdaps_idev->input;
	idev->name = "hdaps";
	idev->phys = "isa1600/input0";
	idev->id.bustype = BUS_ISA;
	idev->dev.parent = &pdev->dev;
	idev->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(idev, ABS_X,
			-256, 256, HDAPS_INPUT_FUZZ, HDAPS_INPUT_FLAT);
	input_set_abs_params(idev, ABS_Y,
			-256, 256, HDAPS_INPUT_FUZZ, HDAPS_INPUT_FLAT);

	ret = input_register_polled_device(hdaps_idev);
	if (ret)
		goto out_idev;

	pr_info("driver successfully loaded\n");
	return 0;

out_idev:
	input_free_polled_device(hdaps_idev);
out_group:
	sysfs_remove_group(&pdev->dev.kobj, &hdaps_attribute_group);
out_device:
	platform_device_unregister(pdev);
out_driver:
	platform_driver_unregister(&hdaps_driver);
out_region:
	release_region(HDAPS_LOW_PORT, HDAPS_NR_PORTS);
out:
	pr_warn("driver init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit hdaps_exit(void)
{
	input_unregister_polled_device(hdaps_idev);
	input_free_polled_device(hdaps_idev);
	sysfs_remove_group(&pdev->dev.kobj, &hdaps_attribute_group);
	platform_device_unregister(pdev);
	platform_driver_unregister(&hdaps_driver);
	release_region(HDAPS_LOW_PORT, HDAPS_NR_PORTS);

	pr_info("driver unloaded\n");
}

module_init(hdaps_init);
module_exit(hdaps_exit);

module_param_named(invert, hdaps_invert, int, 0);
MODULE_PARM_DESC(invert, "invert data along each axis. 1 invert x-axis, "
		 "2 invert y-axis, 3 invert both axes.");

MODULE_AUTHOR("Robert Love");
MODULE_DESCRIPTION("IBM Hard Drive Active Protection System (HDAPS) driver");
MODULE_LICENSE("GPL v2");
