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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/usb.h>

#include "comedidev.h"

/**
 * comedi_to_usb_interface() - comedi_device pointer to usb_interface pointer.
 * @dev: comedi_device struct
 */
struct usb_interface *comedi_to_usb_interface(struct comedi_device *dev)
{
	return dev->hw_dev ? to_usb_interface(dev->hw_dev) : NULL;
}
EXPORT_SYMBOL_GPL(comedi_to_usb_interface);

/**
 * comedi_usb_auto_config() - Configure/probe a comedi USB driver.
 * @intf: usb_interface struct
 * @driver: comedi_driver struct
 * @context: driver specific data, passed to comedi_auto_config()
 *
 * Typically called from the usb_driver (*probe) function.
 */
int comedi_usb_auto_config(struct usb_interface *intf,
			   struct comedi_driver *driver,
			   unsigned long context)
{
	return comedi_auto_config(&intf->dev, driver, context);
}
EXPORT_SYMBOL_GPL(comedi_usb_auto_config);

/**
 * comedi_pci_auto_unconfig() - Unconfigure/disconnect a comedi USB driver.
 * @intf: usb_interface struct
 *
 * Typically called from the usb_driver (*disconnect) function.
 */
void comedi_usb_auto_unconfig(struct usb_interface *intf)
{
	comedi_auto_unconfig(&intf->dev);
}
EXPORT_SYMBOL_GPL(comedi_usb_auto_unconfig);

/**
 * comedi_usb_driver_register() - Register a comedi USB driver.
 * @comedi_driver: comedi_driver struct
 * @usb_driver: usb_driver struct
 *
 * This function is used for the module_init() of comedi USB drivers.
 * Do not call it directly, use the module_comedi_usb_driver() helper
 * macro instead.
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
 * comedi_usb_driver_unregister() - Unregister a comedi USB driver.
 * @comedi_driver: comedi_driver struct
 * @usb_driver: usb_driver struct
 *
 * This function is used for the module_exit() of comedi USB drivers.
 * Do not call it directly, use the module_comedi_usb_driver() helper
 * macro instead.
 */
void comedi_usb_driver_unregister(struct comedi_driver *comedi_driver,
				  struct usb_driver *usb_driver)
{
	usb_deregister(usb_driver);
	comedi_driver_unregister(comedi_driver);
}
EXPORT_SYMBOL_GPL(comedi_usb_driver_unregister);
