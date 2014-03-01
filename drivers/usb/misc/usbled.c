/*
 * USB LED driver
 *
 * Copyright (C) 2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>


#define DRIVER_AUTHOR "Greg Kroah-Hartman, greg@kroah.com"
#define DRIVER_DESC "USB LED Driver"

enum led_type {
	DELCOM_VISUAL_SIGNAL_INDICATOR,
	DREAM_CHEEKY_WEBMAIL_NOTIFIER,
};

/* table of devices that work with this driver */
static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0fc5, 0x1223),
			.driver_info = DELCOM_VISUAL_SIGNAL_INDICATOR },
	{ USB_DEVICE(0x1d34, 0x0004),
			.driver_info = DREAM_CHEEKY_WEBMAIL_NOTIFIER },
	{ USB_DEVICE(0x1d34, 0x000a),
			.driver_info = DREAM_CHEEKY_WEBMAIL_NOTIFIER },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct usb_led {
	struct usb_device	*udev;
	unsigned char		blue;
	unsigned char		red;
	unsigned char		green;
	enum led_type		type;
};

static void change_color(struct usb_led *led)
{
	int retval = 0;
	unsigned char *buffer;

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&led->udev->dev, "out of memory\n");
		return;
	}

	switch (led->type) {
	case DELCOM_VISUAL_SIGNAL_INDICATOR: {
		unsigned char color = 0x07;

		if (led->blue)
			color &= ~0x04;
		if (led->red)
			color &= ~0x02;
		if (led->green)
			color &= ~0x01;
		dev_dbg(&led->udev->dev,
			"blue = %d, red = %d, green = %d, color = %.2x\n",
			led->blue, led->red, led->green, color);

		retval = usb_control_msg(led->udev,
					usb_sndctrlpipe(led->udev, 0),
					0x12,
					0xc8,
					(0x02 * 0x100) + 0x0a,
					(0x00 * 0x100) + color,
					buffer,
					8,
					2000);
		break;
	}

	case DREAM_CHEEKY_WEBMAIL_NOTIFIER:
		dev_dbg(&led->udev->dev,
			"red = %d, green = %d, blue = %d\n",
			led->red, led->green, led->blue);

		buffer[0] = led->red;
		buffer[1] = led->green;
		buffer[2] = led->blue;
		buffer[3] = buffer[4] = buffer[5] = 0;
		buffer[6] = 0x1a;
		buffer[7] = 0x05;

		retval = usb_control_msg(led->udev,
					usb_sndctrlpipe(led->udev, 0),
					0x09,
					0x21,
					0x200,
					0,
					buffer,
					8,
					2000);
		break;

	default:
		dev_err(&led->udev->dev, "unknown device type %d\n", led->type);
	}

	if (retval)
		dev_dbg(&led->udev->dev, "retval = %d\n", retval);
	kfree(buffer);
}

#define show_set(value)	\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr,\
			    char *buf)					\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_led *led = usb_get_intfdata(intf);			\
									\
	return sprintf(buf, "%d\n", led->value);			\
}									\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr,\
			   const char *buf, size_t count)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_led *led = usb_get_intfdata(intf);			\
	int temp = simple_strtoul(buf, NULL, 10);			\
									\
	led->value = temp;						\
	change_color(led);						\
	return count;							\
}									\
static DEVICE_ATTR(value, S_IRUGO | S_IWUSR, show_##value, set_##value);
show_set(blue);
show_set(red);
show_set(green);

static int led_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_led *dev = NULL;
	int retval = -ENOMEM;

	dev = kzalloc(sizeof(struct usb_led), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&interface->dev, "out of memory\n");
		goto error_mem;
	}

	dev->udev = usb_get_dev(udev);
	dev->type = id->driver_info;

	usb_set_intfdata(interface, dev);

	retval = device_create_file(&interface->dev, &dev_attr_blue);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_red);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_green);
	if (retval)
		goto error;

	if (dev->type == DREAM_CHEEKY_WEBMAIL_NOTIFIER) {
		unsigned char *enable;

		enable = kmemdup("\x1f\x02\0\x5f\0\0\x1a\x03", 8, GFP_KERNEL);
		if (!enable) {
			dev_err(&interface->dev, "out of memory\n");
			retval = -ENOMEM;
			goto error;
		}

		retval = usb_control_msg(udev,
					usb_sndctrlpipe(udev, 0),
					0x09,
					0x21,
					0x200,
					0,
					enable,
					8,
					2000);

		kfree(enable);
		if (retval != 8)
			goto error;
	}

	dev_info(&interface->dev, "USB LED device now attached\n");
	return 0;

error:
	device_remove_file(&interface->dev, &dev_attr_blue);
	device_remove_file(&interface->dev, &dev_attr_red);
	device_remove_file(&interface->dev, &dev_attr_green);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
error_mem:
	return retval;
}

static void led_disconnect(struct usb_interface *interface)
{
	struct usb_led *dev;

	dev = usb_get_intfdata(interface);

	device_remove_file(&interface->dev, &dev_attr_blue);
	device_remove_file(&interface->dev, &dev_attr_red);
	device_remove_file(&interface->dev, &dev_attr_green);

	/* first remove the files, then set the pointer to NULL */
	usb_set_intfdata(interface, NULL);

	usb_put_dev(dev->udev);

	kfree(dev);

	dev_info(&interface->dev, "USB LED now disconnected\n");
}

static struct usb_driver led_driver = {
	.name =		"usbled",
	.probe =	led_probe,
	.disconnect =	led_disconnect,
	.id_table =	id_table,
};

module_usb_driver(led_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
