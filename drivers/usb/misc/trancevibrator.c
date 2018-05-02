// SPDX-License-Identifier: GPL-2.0+
/*
 * PlayStation 2 Trance Vibrator driver
 *
 * Copyright (C) 2006 Sam Hocevar <sam@zoy.org>
 */

/* Standard include files */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DRIVER_AUTHOR "Sam Hocevar, sam@zoy.org"
#define DRIVER_DESC "PlayStation 2 Trance Vibrator driver"

#define TRANCEVIBRATOR_VENDOR_ID	0x0b49	/* ASCII Corporation */
#define TRANCEVIBRATOR_PRODUCT_ID	0x064f	/* Trance Vibrator */

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(TRANCEVIBRATOR_VENDOR_ID, TRANCEVIBRATOR_PRODUCT_ID) },
	{ },
};
MODULE_DEVICE_TABLE (usb, id_table);

/* Driver-local specific stuff */
struct trancevibrator {
	struct usb_device *udev;
	unsigned int speed;
};

static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct trancevibrator *tv = usb_get_intfdata(intf);

	return sprintf(buf, "%d\n", tv->speed);
}

static ssize_t speed_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct trancevibrator *tv = usb_get_intfdata(intf);
	int temp, retval, old;

	temp = simple_strtoul(buf, NULL, 10);
	if (temp > 255)
		temp = 255;
	else if (temp < 0)
		temp = 0;
	old = tv->speed;
	tv->speed = temp;

	dev_dbg(&tv->udev->dev, "speed = %d\n", tv->speed);

	/* Set speed */
	retval = usb_control_msg(tv->udev, usb_sndctrlpipe(tv->udev, 0),
				 0x01, /* vendor request: set speed */
				 USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_OTHER,
				 tv->speed, /* speed value */
				 0, NULL, 0, USB_CTRL_GET_TIMEOUT);
	if (retval) {
		tv->speed = old;
		dev_dbg(&tv->udev->dev, "retval = %d\n", retval);
		return retval;
	}
	return count;
}

static DEVICE_ATTR_RW(speed);

static int tv_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct trancevibrator *dev;
	int retval;

	dev = kzalloc(sizeof(struct trancevibrator), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	dev->udev = usb_get_dev(udev);
	usb_set_intfdata(interface, dev);
	retval = device_create_file(&interface->dev, &dev_attr_speed);
	if (retval)
		goto error_create_file;

	return 0;

error_create_file:
	usb_put_dev(udev);
	usb_set_intfdata(interface, NULL);
error:
	kfree(dev);
	return retval;
}

static void tv_disconnect(struct usb_interface *interface)
{
	struct trancevibrator *dev;

	dev = usb_get_intfdata (interface);
	device_remove_file(&interface->dev, &dev_attr_speed);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
}

/* USB subsystem object */
static struct usb_driver tv_driver = {
	.name =		"trancevibrator",
	.probe =	tv_probe,
	.disconnect =	tv_disconnect,
	.id_table =	id_table,
};

module_usb_driver(tv_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
