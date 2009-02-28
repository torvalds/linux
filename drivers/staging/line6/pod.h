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

#ifndef POD_H
#define POD_H


#include "driver.h"

#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <sound/core.h>

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
#define POD_STARTUP_DELAY 3


/**
	 Data structure for values that need to be requested explicitly.
	 This is the case for system and tuner settings.
*/
struct ValueWait {
	unsigned short value;
	wait_queue_head_t wait;
};

/**
	 Binary PodXT Pro program dump
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
		 Current program number.
	*/
	unsigned char channel_num;

	/**
		 Current program settings.
	*/
	struct pod_program prog_data;

	/**
		 Buffer for data retrieved from or to be stored on PODxt Pro.
	*/
	struct pod_program prog_data_buf;

	/**
		 Buffer for requesting version number.
	*/
	unsigned char *buffer_versionreq;

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
	struct ValueWait monitor_level;

	/**
		 Audio routing mode.
		 0: send processed guitar
		 1: send clean guitar
		 2: send clean guitar re-amp playback
		 3: send re-amp playback
	*/
	struct ValueWait routing;

	/**
		 Wait for audio clipping event.
	*/
	struct ValueWait clipping;

	/**
		 Bottom-half for creation of sysfs special files.
	*/
	struct work_struct create_files_work;

	/**
		 Dirty flags for access to parameter data.
	*/
	unsigned long param_dirty[POD_CONTROL_SIZE / sizeof(unsigned long)];

	/**
		 Some atomic flags.
	*/
	unsigned long atomic_flags;

	/**
		 Counter for startup process.
	*/
	int startup_count;

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
		 Flag to indicate modification of current program settings.
	*/
	char dirty;

	/**
		 Flag if initial firmware version request has been successful.
	*/
	char versionreq_ok;

	/**
		 Flag to enable MIDI postprocessing.
	*/
	char midi_postprocess;
};


extern void pod_disconnect(struct usb_interface *interface);
extern int pod_init(struct usb_interface *interface, struct usb_line6_pod *pod);
extern void pod_midi_postprocess(struct usb_line6_pod *pod,
				 unsigned char *data, int length);
extern void pod_process_message(struct usb_line6_pod *pod);
extern void pod_receive_parameter(struct usb_line6_pod *pod, int param);
extern void pod_transmit_parameter(struct usb_line6_pod *pod, int param,
				   int value);


#endif
