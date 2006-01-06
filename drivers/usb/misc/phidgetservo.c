/*
 * USB PhidgetServo driver 1.0
 *
 * Copyright (C) 2004 Sean Young <sean@mess.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is a driver for the USB PhidgetServo version 2.0 and 3.0 servo 
 * controllers available at: http://www.phidgets.com/ 
 *
 * Note that the driver takes input as: degrees.minutes
 *
 * CAUTION: Generally you should use 0 < degrees < 180 as anything else
 * is probably beyond the range of your servo and may damage it.
 *
 * Jun 16, 2004: Sean Young <sean@mess.org>
 *  - cleanups
 *  - was using memory after kfree()
 * Aug 8, 2004: Sean Young <sean@mess.org>
 *  - set the highest angle as high as the hardware allows, there are 
 *    some odd servos out there
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DRIVER_AUTHOR "Sean Young <sean@mess.org>"
#define DRIVER_DESC "USB PhidgetServo Driver"

#define VENDOR_ID_GLAB				0x06c2
#define DEVICE_ID_GLAB_PHIDGETSERVO_QUAD	0x0038
#define DEVICE_ID_GLAB_PHIDGETSERVO_UNI		0x0039

#define VENDOR_ID_WISEGROUP			0x0925
#define VENDOR_ID_WISEGROUP_PHIDGETSERVO_QUAD	0x8101
#define VENDOR_ID_WISEGROUP_PHIDGETSERVO_UNI	0x8104

#define SERVO_VERSION_30			0x01
#define SERVO_COUNT_QUAD			0x02

static struct usb_device_id id_table[] = {
	{
		USB_DEVICE(VENDOR_ID_GLAB, DEVICE_ID_GLAB_PHIDGETSERVO_QUAD), 
		.driver_info = SERVO_VERSION_30 | SERVO_COUNT_QUAD 
	},
	{
		USB_DEVICE(VENDOR_ID_GLAB, DEVICE_ID_GLAB_PHIDGETSERVO_UNI),
		.driver_info = SERVO_VERSION_30 
	},
	{
		USB_DEVICE(VENDOR_ID_WISEGROUP, 
				VENDOR_ID_WISEGROUP_PHIDGETSERVO_QUAD),
		.driver_info = SERVO_COUNT_QUAD 
	},
	{
		USB_DEVICE(VENDOR_ID_WISEGROUP, 
				VENDOR_ID_WISEGROUP_PHIDGETSERVO_UNI),
		.driver_info = 0
	},
	{}
};

MODULE_DEVICE_TABLE(usb, id_table);

struct phidget_servo {
	struct usb_device *udev;
	ulong type;
	int pulse[4];
	int degrees[4];
	int minutes[4];
};

static int
change_position_v30(struct phidget_servo *servo, int servo_no, int degrees, 
								int minutes)
{
	int retval;
	unsigned char *buffer;

	if (degrees < -23 || degrees > 362)
		return -EINVAL;

	buffer = kmalloc(6, GFP_KERNEL);
	if (!buffer) {
		dev_err(&servo->udev->dev, "%s - out of memory\n",
			__FUNCTION__);
		return -ENOMEM;
	}

	/*
	 * pulse = 0 - 4095
	 * angle = 0 - 180 degrees
	 *
	 * pulse = angle * 10.6 + 243.8	
	 */
	servo->pulse[servo_no] = ((degrees*60 + minutes)*106 + 2438*60)/600;	
	servo->degrees[servo_no]= degrees;
	servo->minutes[servo_no]= minutes;	

	/* 
	 * The PhidgetServo v3.0 is controlled by sending 6 bytes,
	 * 4 * 12 bits for each servo.
	 *
	 * low = lower 8 bits pulse
	 * high = higher 4 bits pulse
	 *
	 * offset     bits
	 * +---+-----------------+
	 * | 0 |      low 0      |
	 * +---+--------+--------+
	 * | 1 | high 1 | high 0 |
	 * +---+--------+--------+
	 * | 2 |      low 1      |
	 * +---+-----------------+
	 * | 3 |      low 2      |
	 * +---+--------+--------+
	 * | 4 | high 3 | high 2 |
	 * +---+--------+--------+
	 * | 5 |      low 3      |
	 * +---+-----------------+
	 */

	buffer[0] = servo->pulse[0] & 0xff;
	buffer[1] = (servo->pulse[0] >> 8 & 0x0f)
	    | (servo->pulse[1] >> 4 & 0xf0);
	buffer[2] = servo->pulse[1] & 0xff;
	buffer[3] = servo->pulse[2] & 0xff;
	buffer[4] = (servo->pulse[2] >> 8 & 0x0f)
	    | (servo->pulse[3] >> 4 & 0xf0);
	buffer[5] = servo->pulse[3] & 0xff;

	dev_dbg(&servo->udev->dev,
		"data: %02x %02x %02x %02x %02x %02x\n",
		buffer[0], buffer[1], buffer[2],
		buffer[3], buffer[4], buffer[5]);

	retval = usb_control_msg(servo->udev,
				 usb_sndctrlpipe(servo->udev, 0),
				 0x09, 0x21, 0x0200, 0x0000, buffer, 6, 2000);

	kfree(buffer);

	return retval;
}

static int
change_position_v20(struct phidget_servo *servo, int servo_no, int degrees,
								int minutes)
{
	int retval;
	unsigned char *buffer;

	if (degrees < -23 || degrees > 278)
		return -EINVAL;

	buffer = kmalloc(2, GFP_KERNEL);
	if (!buffer) {
		dev_err(&servo->udev->dev, "%s - out of memory\n",
			__FUNCTION__);
		return -ENOMEM;
	}

	/*
	 * angle = 0 - 180 degrees
	 * pulse = angle + 23
	 */
	servo->pulse[servo_no]= degrees + 23;
	servo->degrees[servo_no]= degrees;
	servo->minutes[servo_no]= 0;

	/*
	 * The PhidgetServo v2.0 is controlled by sending two bytes. The
	 * first byte is the servo number xor'ed with 2:
	 *
	 * servo 0 = 2
	 * servo 1 = 3
	 * servo 2 = 0
	 * servo 3 = 1
	 *
	 * The second byte is the position.
	 */

	buffer[0] = servo_no ^ 2;
	buffer[1] = servo->pulse[servo_no];

	dev_dbg(&servo->udev->dev, "data: %02x %02x\n", buffer[0], buffer[1]);

	retval = usb_control_msg(servo->udev,
				 usb_sndctrlpipe(servo->udev, 0),
				 0x09, 0x21, 0x0200, 0x0000, buffer, 2, 2000);

	kfree(buffer);

	return retval;
}

#define show_set(value)	\
static ssize_t set_servo##value (struct device *dev, struct device_attribute *attr,			\
					const char *buf, size_t count)	\
{									\
	int degrees, minutes, retval;					\
	struct usb_interface *intf = to_usb_interface (dev);		\
	struct phidget_servo *servo = usb_get_intfdata (intf);		\
									\
	minutes = 0;							\
	/* must at least convert degrees */				\
	if (sscanf (buf, "%d.%d", &degrees, &minutes) < 1) {		\
		return -EINVAL;						\
	}								\
									\
	if (minutes < 0 || minutes > 59) 				\
		return -EINVAL;						\
									\
	if (servo->type & SERVO_VERSION_30)				\
		retval = change_position_v30 (servo, value, degrees, 	\
							minutes);	\
	else 								\
		retval = change_position_v20 (servo, value, degrees, 	\
							minutes);	\
									\
	return retval < 0 ? retval : count;				\
}									\
									\
static ssize_t show_servo##value (struct device *dev, struct device_attribute *attr, char *buf) 	\
{									\
	struct usb_interface *intf = to_usb_interface (dev);		\
	struct phidget_servo *servo = usb_get_intfdata (intf);		\
									\
	return sprintf (buf, "%d.%02d\n", servo->degrees[value],	\
				servo->minutes[value]);			\
}									\
static DEVICE_ATTR(servo##value, S_IWUGO | S_IRUGO,			\
	  show_servo##value, set_servo##value);

show_set(0);
show_set(1);
show_set(2);
show_set(3);

static int
servo_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct phidget_servo *dev;

	dev = kzalloc(sizeof (struct phidget_servo), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&interface->dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}

	dev->udev = usb_get_dev(udev);
	dev->type = id->driver_info;
	usb_set_intfdata(interface, dev);

	device_create_file(&interface->dev, &dev_attr_servo0);
	if (dev->type & SERVO_COUNT_QUAD) {
		device_create_file(&interface->dev, &dev_attr_servo1);
		device_create_file(&interface->dev, &dev_attr_servo2);
		device_create_file(&interface->dev, &dev_attr_servo3);
	}

	dev_info(&interface->dev, "USB %d-Motor PhidgetServo v%d.0 attached\n",
		dev->type & SERVO_COUNT_QUAD ? 4 : 1,
		dev->type & SERVO_VERSION_30 ? 3 : 2);

	if(!(dev->type & SERVO_VERSION_30))
		dev_info(&interface->dev,
			 "WARNING: v2.0 not tested! Please report if it works.\n");

	return 0;
}

static void
servo_disconnect(struct usb_interface *interface)
{
	struct phidget_servo *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	device_remove_file(&interface->dev, &dev_attr_servo0);
	if (dev->type & SERVO_COUNT_QUAD) {
		device_remove_file(&interface->dev, &dev_attr_servo1);
		device_remove_file(&interface->dev, &dev_attr_servo2);
		device_remove_file(&interface->dev, &dev_attr_servo3);
	}

	usb_put_dev(dev->udev);

	dev_info(&interface->dev, "USB %d-Motor PhidgetServo v%d.0 detached\n",
		dev->type & SERVO_COUNT_QUAD ? 4 : 1,
		dev->type & SERVO_VERSION_30 ? 3 : 2);

	kfree(dev);
}

static struct usb_driver servo_driver = {
	.name = "phidgetservo",
	.probe = servo_probe,
	.disconnect = servo_disconnect,
	.id_table = id_table
};

static int __init
phidget_servo_init(void)
{
	int retval;

	retval = usb_register(&servo_driver);
	if (retval)
		err("usb_register failed. Error number %d", retval);

	return retval;
}

static void __exit
phidget_servo_exit(void)
{
	usb_deregister(&servo_driver);
}

module_init(phidget_servo_init);
module_exit(phidget_servo_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
