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

#ifndef POD_H
#define POD_H

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#include <sound/core.h>

#include "driver.h"

/*
	PODxt Live interfaces
*/
#define PODXTLIVE_INTERFACE_POD    0
#define PODXTLIVE_INTERFACE_VARIAX 1

/*
	Locate name in binary program dump
*/
#define	POD_NAME_OFFSET 0
#define	POD_NAME_LENGTH 16

/*
	Other constants
*/
#define POD_CONTROL_SIZE 0x80
#define POD_BUFSIZE_DUMPREQ 7
#define POD_STARTUP_DELAY 1000

/*
	Stages of POD startup procedure
*/
enum {
	POD_STARTUP_INIT = 1,
	POD_STARTUP_VERSIONREQ,
	POD_STARTUP_WORKQUEUE,
	POD_STARTUP_SETUP,
	POD_STARTUP_LAST = POD_STARTUP_SETUP - 1
};

struct usb_line6_pod {
	/**
		Generic Line6 USB data.
	*/
	struct usb_line6 line6;

	/**
		Instrument monitor level.
	*/
	int monitor_level;

	/**
		Timer for device initializaton.
	*/
	struct timer_list startup_timer;

	/**
		Work handler for device initializaton.
	*/
	struct work_struct startup_work;

	/**
		Current progress in startup procedure.
	*/
	int startup_progress;

	/**
		Serial number of device.
	*/
	int serial_number;

	/**
		Firmware version (x 100).
	*/
	int firmware_version;

	/**
		Device ID.
	*/
	int device_id;
};

extern void line6_pod_disconnect(struct usb_interface *interface);
extern int line6_pod_init(struct usb_interface *interface,
			  struct usb_line6_pod *pod);
extern void line6_pod_process_message(struct usb_line6_pod *pod);
extern void line6_pod_transmit_parameter(struct usb_line6_pod *pod, int param,
					 u8 value);

#endif
