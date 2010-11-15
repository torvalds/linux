/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef TONEPORT_H
#define TONEPORT_H

#include <linux/usb.h>
#include <sound/core.h>

#include "driver.h"

struct usb_line6_toneport {
	/**
		Generic Line6 USB data.
	*/
	struct usb_line6 line6;

	/**
		Source selector.
	*/
	int source;

	/**
		Serial number of device.
	*/
	int serial_number;

	/**
		Firmware version (x 100).
	*/
	int firmware_version;

	/**
		 Timer for delayed PCM startup.
	*/
	struct timer_list timer;
};

extern void line6_toneport_disconnect(struct usb_interface *interface);
extern int line6_toneport_init(struct usb_interface *interface,
			       struct usb_line6_toneport *toneport);
extern void line6_toneport_reset_resume(struct usb_line6_toneport *toneport);

#endif
