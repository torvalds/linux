/*
 * comedi_usb.h
 * header file for USB Comedi drivers
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

#ifndef _COMEDI_USB_H
#define _COMEDI_USB_H

#include <linux/usb.h>

#include "comedidev.h"

struct usb_interface *comedi_to_usb_interface(struct comedi_device *dev);
struct usb_device *comedi_to_usb_dev(struct comedi_device *dev);

int comedi_usb_auto_config(struct usb_interface *intf,
			   struct comedi_driver *driver, unsigned long context);
void comedi_usb_auto_unconfig(struct usb_interface *intf);

int comedi_usb_driver_register(struct comedi_driver *comedi_driver,
			       struct usb_driver *usb_driver);
void comedi_usb_driver_unregister(struct comedi_driver *comedi_driver,
				  struct usb_driver *usb_driver);

/**
 * module_comedi_usb_driver() - Helper macro for registering a comedi USB driver
 * @__comedi_driver: comedi_driver struct
 * @__usb_driver: usb_driver struct
 *
 * Helper macro for comedi USB drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 */
#define module_comedi_usb_driver(__comedi_driver, __usb_driver) \
	module_driver(__comedi_driver, comedi_usb_driver_register, \
			comedi_usb_driver_unregister, &(__usb_driver))

#endif /* _COMEDI_USB_H */
