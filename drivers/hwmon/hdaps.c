/*
 * drivers/hwmon/hdaps.c - driver for IBM's Hard Drive Active Protection System
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

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <asm/io.h>

#define HDAPS_LOW_PORT		0x1600	/* first port used by hdaps */
#define HDAPS_NR_PORTS		0x30	/* number of ports: 0x1600 - 0x162f */

#define HDAPS_PORT_STATE	0x1611	/* device state */
#define HDAPS_PORT_YPOS		0x1612	/* y-axis position */
#define	HDAPS_PORT_XPOS		0x1614	/* x-axis position */
#define HDAPS_PORT_TEMP1	0x1616	/* device temperature, in celcius */
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

#define HDAPS_POLL_PERIOD	(HZ/20)	/* poll for input every 1/20s */
#define HDAPS_INPUT_FUZZ	4	/* input event threshold */

static struct timer_list hdaps_timer;
static struct platform_device *pdev;
static unsigned int hdaps_invert;
static u8 km_activity;
static int rest_x;
static int rest_y;

static DECLARE_MUTEX(hdaps_sem);

/*
 * __get_latch - Get the value from a given port.  Callers must hold hdaps_sem.
 */
static inline u8 __get_latch(u16 port)
{
	return inb(port) & 0xff;
}

/*
 * __check_latch - Check a port latch for a given value.  Returns zero if the
 * port contains the given value.  Callers must hold hdaps_sem.
 */
static inline int __check_latch(u16 port, u8 val)
{
	if (__get_latch(port) == val)
		return 0;
	return -EINVAL;
}

/*
 * __wait_latch - Wait up to 100us for a port latch to get a certain value,
 * returning zero if the value is obtained.  Callers must hold hdaps_sem.
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
 * for refresh to complete.  Callers must hold hdaps_sem.
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
 * successful and nonzero on error.  Callers must hold hdaps_sem.
 */
static int __device_refresh_sync(void)
{
	__device_refresh();
	return __wait_latch(0x1604, STATE_FRESH);
}

/*
 * __device_complete - indicate to the accelerometer that we are done reading
 * data, and then initiate an async refresh.  Callers must hold hdaps_sem.
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

	down(&hdaps_sem);

	/* do a sync refresh -- we need to be sure that we read fresh data */
	ret = __device_refresh_sync();
	if (ret)
		goto out;

	*val = inb(port);
	__device_complete();

out:
	up(&hdaps_sem);
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

	/* if hdaps_invert is set, negate the two values */
	if (hdaps_invert) {
		*x = -*x;
		*y = -*y;
	}

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

	down(&hdaps_sem);
	ret = __hdaps_read_pair(port1, port2, val1, val2);
	up(&hdaps_sem);

	return ret;
}

/*
 * hdaps_device_init - initialize the accelerometer.  Returns zero on success
 * and negative error code on failure.  Can sleep.
 */
static int hdaps_device_init(void)
{
	int total, ret = -ENXIO;

	down(&hdaps_sem);

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

	printk(KERN_DEBUG "hdaps: initial latch check good (0x%02x).\n",
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
	up(&hdaps_sem);
	return ret;
}


/* Device model stuff */

static int hdaps_probe(struct device *dev)
{
	int ret;

	ret = hdaps_device_init();
	if (ret)
		return ret;

	printk(KERN_INFO "hdaps: device successfully initialized.\n");
	return 0;
}

static int hdaps_resume(struct device *dev)
{
	return hdaps_device_init();
}

static struct device_driver hdaps_driver = {
	.name = "hdaps",
	.bus = &platform_bus_type,
	.owner = THIS_MODULE,
	.probe = hdaps_probe,
	.resume = hdaps_resume
};

/* Input class stuff */

static struct input_dev hdaps_idev = {
	.name = "hdaps",
	.evbit = { BIT(EV_ABS) },
	.absbit = { BIT(ABS_X) | BIT(ABS_Y) },
	.absmin  = { [ABS_X] = -256, [ABS_Y] = -256 },
	.absmax  = { [ABS_X] = 256, [ABS_Y] = 256 },
	.absfuzz = { [ABS_X] = HDAPS_INPUT_FUZZ, [ABS_Y] = HDAPS_INPUT_FUZZ },
	.absflat = { [ABS_X] = HDAPS_INPUT_FUZZ, [ABS_Y] = HDAPS_INPUT_FUZZ },
};

/*
 * hdaps_calibrate - Set our "resting" values.  Callers must hold hdaps_sem.
 */
static void hdaps_calibrate(void)
{
	__hdaps_read_pair(HDAPS_PORT_XPOS, HDAPS_PORT_YPOS, &rest_x, &rest_y);
}

static void hdaps_mousedev_poll(unsigned long unused)
{
	int x, y;

	/* Cannot sleep.  Try nonblockingly.  If we fail, try again later. */
	if (down_trylock(&hdaps_sem)) {
		mod_timer(&hdaps_timer,jiffies + HDAPS_POLL_PERIOD);
		return;
	}

	if (__hdaps_read_pair(HDAPS_PORT_XPOS, HDAPS_PORT_YPOS, &x, &y))
		goto out;

	input_report_abs(&hdaps_idev, ABS_X, x - rest_x);
	input_report_abs(&hdaps_idev, ABS_Y, y - rest_y);
	input_sync(&hdaps_idev);

	mod_timer(&hdaps_timer, jiffies + HDAPS_POLL_PERIOD);

out:
	up(&hdaps_sem);
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
	u8 temp;
	int ret;

	ret = hdaps_readb_one(HDAPS_PORT_TEMP1, &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", temp);
}

static ssize_t hdaps_temp2_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 temp;
	int ret;

	ret = hdaps_readb_one(HDAPS_PORT_TEMP2, &temp);
	if (ret < 0)
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
	down(&hdaps_sem);
	hdaps_calibrate();
	up(&hdaps_sem);

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

	if (sscanf(buf, "%d", &invert) != 1 || (invert != 1 && invert != 0))
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
static int hdaps_dmi_match(struct dmi_system_id *id)
{
	printk(KERN_INFO "hdaps: %s detected.\n", id->ident);
	return 1;
}

/* hdaps_dmi_match_invert - found an inverted match. */
static int hdaps_dmi_match_invert(struct dmi_system_id *id)
{
	hdaps_invert = 1;
	printk(KERN_INFO "hdaps: inverting axis readings.\n");
	return hdaps_dmi_match(id);
}

#define HDAPS_DMI_MATCH_NORMAL(model)	{		\
	.ident = "IBM " model,				\
	.callback = hdaps_dmi_match,			\
	.matches = {					\
		DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),	\
		DMI_MATCH(DMI_PRODUCT_VERSION, model)	\
	}						\
}

#define HDAPS_DMI_MATCH_INVERT(model)	{		\
	.ident = "IBM " model,				\
	.callback = hdaps_dmi_match_invert,		\
	.matches = {					\
		DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),	\
		DMI_MATCH(DMI_PRODUCT_VERSION, model)	\
	}						\
}

static int __init hdaps_init(void)
{
	int ret;

	/* Note that DMI_MATCH(...,"ThinkPad T42") will match "ThinkPad T42p" */
	struct dmi_system_id hdaps_whitelist[] = {
		HDAPS_DMI_MATCH_INVERT("ThinkPad R50p"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad R50"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad R51"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad R52"),
		HDAPS_DMI_MATCH_INVERT("ThinkPad T41p"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad T41"),
		HDAPS_DMI_MATCH_INVERT("ThinkPad T42p"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad T42"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad T43"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad X40"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad X41 Tablet"),
		HDAPS_DMI_MATCH_NORMAL("ThinkPad X41"),
		{ .ident = NULL }
	};

	if (!dmi_check_system(hdaps_whitelist)) {
		printk(KERN_WARNING "hdaps: supported laptop not found!\n");
		ret = -ENXIO;
		goto out;
	}

	if (!request_region(HDAPS_LOW_PORT, HDAPS_NR_PORTS, "hdaps")) {
		ret = -ENXIO;
		goto out;
	}

	ret = driver_register(&hdaps_driver);
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

	/* initial calibrate for the input device */
	hdaps_calibrate();

	/* initialize the input class */
	hdaps_idev.dev = &pdev->dev;
	input_register_device(&hdaps_idev);

	/* start up our timer for the input device */
	init_timer(&hdaps_timer);
	hdaps_timer.function = hdaps_mousedev_poll;
	hdaps_timer.expires = jiffies + HDAPS_POLL_PERIOD;
	add_timer(&hdaps_timer);

	printk(KERN_INFO "hdaps: driver successfully loaded.\n");
	return 0;

out_device:
	platform_device_unregister(pdev);
out_driver:
	driver_unregister(&hdaps_driver);
out_region:
	release_region(HDAPS_LOW_PORT, HDAPS_NR_PORTS);
out:
	printk(KERN_WARNING "hdaps: driver init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit hdaps_exit(void)
{
	del_timer_sync(&hdaps_timer);
	input_unregister_device(&hdaps_idev);
	sysfs_remove_group(&pdev->dev.kobj, &hdaps_attribute_group);
	platform_device_unregister(pdev);
	driver_unregister(&hdaps_driver);
	release_region(HDAPS_LOW_PORT, HDAPS_NR_PORTS);

	printk(KERN_INFO "hdaps: driver unloaded.\n");
}

module_init(hdaps_init);
module_exit(hdaps_exit);

module_param_named(invert, hdaps_invert, bool, 0);
MODULE_PARM_DESC(invert, "invert data along each axis");

MODULE_AUTHOR("Robert Love");
MODULE_DESCRIPTION("IBM Hard Drive Active Protection System (HDAPS) driver");
MODULE_LICENSE("GPL v2");
