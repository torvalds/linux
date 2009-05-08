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

#ifndef VARIAX_H
#define VARIAX_H


#include "driver.h"

#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/wait.h>

#include <sound/core.h>

#include "dumprequest.h"


#define VARIAX_ACTIVATE_DELAY 10
#define VARIAX_STARTUP_DELAY 3


enum {
	VARIAX_DUMP_PASS1 = LINE6_DUMP_CURRENT,
	VARIAX_DUMP_PASS2,
	VARIAX_DUMP_PASS3
};


/**
	 Binary Variax model dump
*/
struct variax_model {
	/**
		 Header information (including program name).
	*/
	unsigned char name[18];

	/**
		 Model parameters.
	*/
	unsigned char control[78 * 2];
};

struct usb_line6_variax {
	/**
		 Generic Line6 USB data.
	*/
	struct usb_line6 line6;

	/**
		 Dump request structure.
		 Append two extra buffers for 3-pass data query.
	*/
	struct line6_dump_request dumpreq; struct line6_dump_reqbuf extrabuf[2];

	/**
		 Buffer for activation code.
	*/
	unsigned char *buffer_activate;

	/**
		 Model number.
	*/
	int model;

	/**
		 Current model settings.
	*/
	struct variax_model model_data;

	/**
		 Name of current model bank.
	*/
	unsigned char bank[18];

	/**
		 Position of volume dial.
	*/
	int volume;

	/**
		 Position of tone control dial.
	*/
	int tone;

	/**
		 Timer for delayed activation request.
	*/
	struct timer_list activate_timer;
};


extern void variax_disconnect(struct usb_interface *interface);
extern int variax_init(struct usb_interface *interface,
		       struct usb_line6_variax *variax);
extern void variax_process_message(struct usb_line6_variax *variax);


#endif
