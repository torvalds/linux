// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi_usb.c
 * Comedi USB driver specific functions.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#include "comedi_usb.h"

/**
 * comedi_to_usb_interface() - Return USB interface attached to COMEDI device
 * @dev: COMEDI device.
 *
 * Assuming @dev->hw_dev is non-%NULL, it is assumed to be pointing to a
 * a &struct device embedded in a &struct usb_interface.
 *
 * Return: Attached USB interface if @dev->hw_dev is non-%NULL.
 * Return %NULL if @dev->hw_dev is %NULL.
 */
struct usb_interface *comedi_to_usb_interface(struct comedi_device *dev)
{
	return dev->hw_dev ? to_usb_interface(dev->hw_dev) : NULL;
}
EXPORT_SYMBOL_GPL(comedi_to_usb_interface);

/**
 * comedi_to_usb_dev() - Return USB device attached to COMEDI device
 * @dev: COMEDI device.
 *
 * Assuming @dev->hw_dev is non-%NULL, it is assumed to be pointing to a
 * a &struct device embedded in a &struct usb_interface.
 *
 * Return: USB device to which the USB interface belongs if @dev->hw_dev is
 * non-%NULL.  Return %NULL if @dev->hw_dev is %NULL.
 */
struct usb_device *comedi_to_usb_dev(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);

	return intf ? interface_to_usbdev(intf) : NULL;
}
EXPORT_SYMBOL_GPL(comedi_to_usb_dev);

/**
 * comedi_usb_auto_config() - Configure/probe a USB COMEDI driver
 * @intf: USB interface.
 * @driver: Registered COMEDI driver.
 * @context: Driver specific data, passed to comedi_auto_config().
 *
 * Typically called from the usb_driver (*probe) function.  Auto-configure a
 * COMEDI device, using a pointer to the &struct device embedded in *@intf as
 * the hardware device.  The @context value gets passed through to @driver's
 * "auto_attach" handler.  The "auto_attach" handler may call
 * comedi_to_usb_interface() on the passed in COMEDI device to recover @intf.
 *
 * Return: The result of calling comedi_auto_config() (%0 on success, or
 * a negative error number on failure).
 */
int comedi_usb_auto_config(struct usb_interface *intf,
			   struct comedi_driver *driver,
			   unsigned long context)
{
	return comedi_auto_config(&intf->dev, driver, context);
}
EXPORT_SYMBOL_GPL(comedi_usb_auto_config);

/**
 * comedi_usb_auto_unconfig() - Unconfigure/disconnect a USB COMEDI device
 * @intf: USB interface.
 *
 * Typically called from the usb_driver (*disconnect) function.
 * Auto-unconfigure a COMEDI device attached to this USB interface, using a
 * pointer to the &struct device embedded in *@intf as the hardware device.
 * The COMEDI driver's "detach" handler will be called during unconfiguration
 * of the COMEDI device.
 *
 * Note that the COMEDI device may have already been unconfigured using the
 * %COMEDI_DEVCONFIG ioctl, in which case this attempt to unconfigure it
 * again should be ignored.
 */
void comedi_usb_auto_unconfig(struct usb_interface *intf)
{
	comedi_auto_unconfig(&intf->dev);
}
EXPORT_SYMBOL_GPL(comedi_usb_auto_unconfig);

/**
 * comedi_usb_driver_register() - Register a USB COMEDI driver
 * @comedi_driver: COMEDI driver to be registered.
 * @usb_driver: USB driver to be registered.
 *
 * This function is called from the module_init() of USB COMEDI driver modules
 * to register the COMEDI driver and the USB driver.  Do not call it directly,
 * use the module_comedi_usb_driver() helper macro instead.
 *
 * Return: %0 on success, or a negative error number on failure.
 */
int comedi_usb_driver_register(struct comedi_driver *comedi_driver,
			       struct usb_driver *usb_driver)
{
	int ret;

	ret = comedi_driver_register(comedi_driver);
	if (ret < 0)
		return ret;

	ret = usb_register(usb_driver);
	if (ret < 0) {
		comedi_driver_unregister(comedi_driver);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_usb_driver_register);

/**
 * comedi_usb_driver_unregister() - Unregister a USB COMEDI driver
 * @comedi_driver: COMEDI driver to be registered.
 * @usb_driver: USB driver to be registered.
 *
 * This function is called from the module_exit() of USB COMEDI driver modules
 * to unregister the USB driver and the COMEDI driver.  Do not call it
 * directly, use the module_comedi_usb_driver() helper macro instead.
 */
void comedi_usb_driver_unregister(struct comedi_driver *comedi_driver,
				  struct usb_driver *usb_driver)
{
	usb_deregister(usb_driver);
	comedi_driver_unregister(comedi_driver);
}
EXPORT_SYMBOL_GPL(comedi_usb_driver_unregister);

static int __init comedi_usb_init(void)
{
	return 0;
}
module_init(comedi_usb_init);

static void __exit comedi_usb_exit(void)
{
}
module_exit(comedi_usb_exit);

MODULE_AUTHOR("http://www.comedi.org");
MODULE_DESCRIPTION("Comedi USB interface module");
MODULE_LICENSE("GPL");
