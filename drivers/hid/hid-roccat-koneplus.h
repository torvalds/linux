#ifndef __HID_ROCCAT_KONEPLUS_H
#define __HID_ROCCAT_KONEPLUS_H

/*
 * Copyright (c) 2010 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/types.h>

enum {
	KONEPLUS_SIZE_ACTUAL_PROFILE = 0x03,
	KONEPLUS_SIZE_CONTROL = 0x03,
	KONEPLUS_SIZE_FIRMWARE_WRITE = 0x0402,
	KONEPLUS_SIZE_INFO = 0x06,
	KONEPLUS_SIZE_MACRO = 0x0822,
	KONEPLUS_SIZE_PROFILE_SETTINGS = 0x2b,
	KONEPLUS_SIZE_PROFILE_BUTTONS = 0x4d,
	KONEPLUS_SIZE_SENSOR = 0x06,
	KONEPLUS_SIZE_TALK = 0x10,
	KONEPLUS_SIZE_TCU = 0x04,
	KONEPLUS_SIZE_TCU_IMAGE = 0x0404,
};

enum koneplus_control_requests {
	KONEPLUS_CONTROL_REQUEST_PROFILE_SETTINGS = 0x80,
	KONEPLUS_CONTROL_REQUEST_PROFILE_BUTTONS = 0x90,
};

struct koneplus_actual_profile {
	uint8_t command; /* KONEPLUS_COMMAND_ACTUAL_PROFILE */
	uint8_t size; /* always 3 */
	uint8_t actual_profile; /* Range 0-4! */
} __attribute__ ((__packed__));

struct koneplus_info {
	uint8_t command; /* KONEPLUS_COMMAND_INFO */
	uint8_t size; /* always 6 */
	uint8_t firmware_version;
	uint8_t unknown[3];
} __attribute__ ((__packed__));

enum koneplus_commands {
	KONEPLUS_COMMAND_ACTUAL_PROFILE = 0x5,
	KONEPLUS_COMMAND_CONTROL = 0x4,
	KONEPLUS_COMMAND_PROFILE_SETTINGS = 0x6,
	KONEPLUS_COMMAND_PROFILE_BUTTONS = 0x7,
	KONEPLUS_COMMAND_MACRO = 0x8,
	KONEPLUS_COMMAND_INFO = 0x9,
	KONEPLUS_COMMAND_TCU = 0xc,
	KONEPLUS_COMMAND_TCU_IMAGE = 0xc,
	KONEPLUS_COMMAND_E = 0xe,
	KONEPLUS_COMMAND_SENSOR = 0xf,
	KONEPLUS_COMMAND_TALK = 0x10,
	KONEPLUS_COMMAND_FIRMWARE_WRITE = 0x1b,
	KONEPLUS_COMMAND_FIRMWARE_WRITE_CONTROL = 0x1c,
};

enum koneplus_mouse_report_numbers {
	KONEPLUS_MOUSE_REPORT_NUMBER_HID = 1,
	KONEPLUS_MOUSE_REPORT_NUMBER_AUDIO = 2,
	KONEPLUS_MOUSE_REPORT_NUMBER_BUTTON = 3,
};

struct koneplus_mouse_report_button {
	uint8_t report_number; /* always KONEPLUS_MOUSE_REPORT_NUMBER_BUTTON */
	uint8_t zero1;
	uint8_t type;
	uint8_t data1;
	uint8_t data2;
	uint8_t zero2;
	uint8_t unknown[2];
} __attribute__ ((__packed__));

enum koneplus_mouse_report_button_types {
	/* data1 = new profile range 1-5 */
	KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE = 0x20,

	/* data1 = button number range 1-24; data2 = action */
	KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_QUICKLAUNCH = 0x60,

	/* data1 = button number range 1-24; data2 = action */
	KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_TIMER = 0x80,

	/* data1 = setting number range 1-5 */
	KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_CPI = 0xb0,

	/* data1 and data2 = range 0x1-0xb */
	KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_SENSITIVITY = 0xc0,

	/* data1 = 22 = next track...
	 * data2 = action
	 */
	KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_MULTIMEDIA = 0xf0,
	KONEPLUS_MOUSE_REPORT_TALK = 0xff,
};

enum koneplus_mouse_report_button_action {
	KONEPLUS_MOUSE_REPORT_BUTTON_ACTION_PRESS = 0,
	KONEPLUS_MOUSE_REPORT_BUTTON_ACTION_RELEASE = 1,
};

struct koneplus_roccat_report {
	uint8_t type;
	uint8_t data1;
	uint8_t data2;
	uint8_t profile;
} __attribute__ ((__packed__));

struct koneplus_device {
	int actual_profile;

	int roccat_claimed;
	int chrdev_minor;

	struct mutex koneplus_lock;
};

#endif
