/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef TONEPORT_H
#define TONEPORT_H


#include "driver.h"

#include <linux/usb.h>
#include <sound/core.h>


struct usb_line6_toneport {
	/**
		 Generic Line6 USB data.
	*/
	struct usb_line6 line6;

	/**
		 Serial number of device.
	*/
	int serial_number;

	/**
		 Firmware version (x 100).
	*/
	int firmware_version;
};


extern void toneport_disconnect(struct usb_interface *interface);
extern int toneport_init(struct usb_interface *interface,
			 struct usb_line6_toneport *toneport);


#endif
