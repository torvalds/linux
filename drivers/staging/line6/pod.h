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
#include <linux/wait.h>

#include <sound/core.h>

#include "driver.h"
#include "dumprequest.h"

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
	POD_STARTUP_DUMPREQ,
	POD_STARTUP_VERSIONREQ,
	POD_STARTUP_WORKQUEUE,
	POD_STARTUP_SETUP,
	POD_STARTUP_LAST = POD_STARTUP_SETUP - 1
};

/**
	Data structure for values that need to be requested explicitly.
	This is the case for system and tuner settings.
*/
struct ValueWait {
	int value;
	wait_queue_head_t wait;
};

/**
	Binary PODxt Pro program dump
*/
struct pod_program {
	/**
		Header information (including program name).
	*/
	unsigned char header[0x20];

	/**
		Program parameters.
	*/
	unsigned char control[POD_CONTROL_SIZE];
};

struct usb_line6_pod {
	/**
		Generic Line6 USB data.
	*/
	struct usb_line6 line6;

	/**
		Dump request structure.
	*/
	struct line6_dump_request dumpreq;

	/**
		Current program settings.
	*/
	struct pod_program prog_data;

	/**
		Buffer for data retrieved from or to be stored on PODxt Pro.
	*/
	struct pod_program prog_data_buf;

	/**
		Tuner mute mode.
	*/
	struct ValueWait tuner_mute;

	/**
		Tuner base frequency (typically 440Hz).
	*/
	struct ValueWait tuner_freq;

	/**
		Note received from tuner.
	*/
	struct ValueWait tuner_note;

	/**
		Pitch value received from tuner.
	*/
	struct ValueWait tuner_pitch;

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
		Some atomic flags.
	*/
	unsigned long atomic_flags;

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

	/**
		Flag to enable MIDI postprocessing.
	*/
	char midi_postprocess;
};

extern void line6_pod_disconnect(struct usb_interface *interface);
extern int line6_pod_init(struct usb_interface *interface,
			  struct usb_line6_pod *pod);
extern void line6_pod_midi_postprocess(struct usb_line6_pod *pod,
				       unsigned char *data, int length);
extern void line6_pod_process_message(struct usb_line6_pod *pod);
extern void line6_pod_transmit_parameter(struct usb_line6_pod *pod, int param,
					 u8 value);

#endif
