/*
 * USB BlackBerry charging module
 *
 * Copyright (C) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Information on how to switch configs was taken by the bcharge.cc file
 * created by the barry.sf.net project.
 *
 * bcharge.cc has the following copyright:
 * 	Copyright (C) 2006, Net Direct Inc. (http://www.netdirect.ca/)
 * and is released under the GPLv2.
 *
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>

#define RIM_VENDOR		0x0fca
#define BLACKBERRY		0x0001

static int debug;

#ifdef dbg
#undef dbg
#endif
#define dbg(dev, format, arg...)				\
	if (debug)						\
		dev_printk(KERN_DEBUG , dev , format , ## arg)

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(RIM_VENDOR, BLACKBERRY) },
	{ },					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static int magic_charge(struct usb_device *udev)
{
	char *dummy_buffer = kzalloc(2, GFP_KERNEL);
	int retval;

	if (!dummy_buffer)
		return -ENOMEM;

	/* send two magic commands and then set the configuration.  The device
	 * will then reset itself with the new power usage and should start
	 * charging. */

	/* Note, with testing, it only seems that the first message is really
	 * needed (at least for the 8700c), but to be safe, we emulate what
	 * other operating systems seem to be sending to their device.  We
	 * really need to get some specs for this device to be sure about what
	 * is going on here.
	 */
	dbg(&udev->dev, "Sending first magic command\n");
	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 0xa5, 0xc0, 0, 1, dummy_buffer, 2, 100);
	if (retval != 2) {
		dev_err(&udev->dev, "First magic command failed: %d.\n",
			retval);
		return retval;
	}

	dbg(&udev->dev, "Sending second magic command\n");
	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 0xa2, 0x40, 0, 1, dummy_buffer, 0, 100);
	if (retval != 0) {
		dev_err(&udev->dev, "Second magic command failed: %d.\n",
			retval);
		return retval;
	}

	dbg(&udev->dev, "Calling set_configuration\n");
	retval = usb_driver_set_configuration(udev, 1);
	if (retval)
		dev_err(&udev->dev, "Set Configuration failed :%d.\n", retval);

	return retval;
}

static int berry_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);

	dbg(&udev->dev, "Power is set to %dmA\n",
	    udev->actconfig->desc.bMaxPower * 2);

	/* check the power usage so we don't try to enable something that is
	 * already enabled */
	if ((udev->actconfig->desc.bMaxPower * 2) == 500) {
		dbg(&udev->dev, "device is already charging, power is "
		    "set to %dmA\n", udev->actconfig->desc.bMaxPower * 2);
		return -ENODEV;
	}

	/* turn the power on */
	magic_charge(udev);

	/* we don't really want to bind to the device, userspace programs can
	 * handle the syncing just fine, so get outta here. */
	return -ENODEV;
}

static void berry_disconnect(struct usb_interface *intf)
{
}

static struct usb_driver berry_driver = {
	.name =		"berry_charge",
	.probe =	berry_probe,
	.disconnect =	berry_disconnect,
	.id_table =	id_table,
};

static int __init berry_init(void)
{
	return usb_register(&berry_driver);
}

static void __exit berry_exit(void)
{
	usb_deregister(&berry_driver);
}

module_init(berry_init);
module_exit(berry_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@suse.de>");
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
