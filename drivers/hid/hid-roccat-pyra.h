/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HID_ROCCAT_PYRA_H
#define __HID_ROCCAT_PYRA_H

/*
 * Copyright (c) 2010 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 */

#include <linux/types.h>

enum {
	PYRA_SIZE_CONTROL = 0x03,
	PYRA_SIZE_INFO = 0x06,
	PYRA_SIZE_PROFILE_SETTINGS = 0x0d,
	PYRA_SIZE_PROFILE_BUTTONS = 0x13,
	PYRA_SIZE_SETTINGS = 0x03,
};

enum pyra_control_requests {
	PYRA_CONTROL_REQUEST_PROFILE_SETTINGS = 0x10,
	PYRA_CONTROL_REQUEST_PROFILE_BUTTONS = 0x20
};

struct pyra_settings {
	uint8_t command; /* PYRA_COMMAND_SETTINGS */
	uint8_t size; /* always 3 */
	uint8_t startup_profile; /* Range 0-4! */
} __attribute__ ((__packed__));

struct pyra_profile_settings {
	uint8_t command; /* PYRA_COMMAND_PROFILE_SETTINGS */
	uint8_t size; /* always 0xd */
	uint8_t number; /* Range 0-4 */
	uint8_t xysync;
	uint8_t x_sensitivity; /* 0x1-0xa */
	uint8_t y_sensitivity;
	uint8_t x_cpi; /* unused */
	uint8_t y_cpi; /* this value is for x and y */
	uint8_t lightswitch; /* 0 = off, 1 = on */
	uint8_t light_effect;
	uint8_t handedness;
	uint16_t checksum; /* byte sum */
} __attribute__ ((__packed__));

struct pyra_info {
	uint8_t command; /* PYRA_COMMAND_INFO */
	uint8_t size; /* always 6 */
	uint8_t firmware_version;
	uint8_t unknown1; /* always 0 */
	uint8_t unknown2; /* always 1 */
	uint8_t unknown3; /* always 0 */
} __attribute__ ((__packed__));

enum pyra_commands {
	PYRA_COMMAND_CONTROL = 0x4,
	PYRA_COMMAND_SETTINGS = 0x5,
	PYRA_COMMAND_PROFILE_SETTINGS = 0x6,
	PYRA_COMMAND_PROFILE_BUTTONS = 0x7,
	PYRA_COMMAND_INFO = 0x9,
	PYRA_COMMAND_B = 0xb
};

enum pyra_mouse_report_numbers {
	PYRA_MOUSE_REPORT_NUMBER_HID = 1,
	PYRA_MOUSE_REPORT_NUMBER_AUDIO = 2,
	PYRA_MOUSE_REPORT_NUMBER_BUTTON = 3,
};

struct pyra_mouse_event_button {
	uint8_t report_number; /* always 3 */
	uint8_t unknown; /* always 0 */
	uint8_t type;
	uint8_t data1;
	uint8_t data2;
} __attribute__ ((__packed__));

struct pyra_mouse_event_audio {
	uint8_t report_number; /* always 2 */
	uint8_t type;
	uint8_t unused; /* always 0 */
} __attribute__ ((__packed__));

/* hid audio controls */
enum pyra_mouse_event_audio_types {
	PYRA_MOUSE_EVENT_AUDIO_TYPE_MUTE = 0xe2,
	PYRA_MOUSE_EVENT_AUDIO_TYPE_VOLUME_UP = 0xe9,
	PYRA_MOUSE_EVENT_AUDIO_TYPE_VOLUME_DOWN = 0xea,
};

enum pyra_mouse_event_button_types {
	/*
	 * Mouse sends tilt events on report_number 1 and 3
	 * Tilt events are sent repeatedly with 0.94s between first and second
	 * event and 0.22s on subsequent
	 */
	PYRA_MOUSE_EVENT_BUTTON_TYPE_TILT = 0x10,

	/*
	 * These are sent sequentially
	 * data1 contains new profile number in range 1-5
	 */
	PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_1 = 0x20,
	PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2 = 0x30,

	/*
	 * data1 = button_number (rmp index)
	 * data2 = pressed/released
	 */
	PYRA_MOUSE_EVENT_BUTTON_TYPE_MACRO = 0x40,
	PYRA_MOUSE_EVENT_BUTTON_TYPE_SHORTCUT = 0x50,

	/*
	 * data1 = button_number (rmp index)
	 */
	PYRA_MOUSE_EVENT_BUTTON_TYPE_QUICKLAUNCH = 0x60,

	/* data1 = new cpi */
	PYRA_MOUSE_EVENT_BUTTON_TYPE_CPI = 0xb0,

	/* data1 and data2 = new sensitivity */
	PYRA_MOUSE_EVENT_BUTTON_TYPE_SENSITIVITY = 0xc0,

	PYRA_MOUSE_EVENT_BUTTON_TYPE_MULTIMEDIA = 0xf0,
};

enum {
	PYRA_MOUSE_EVENT_BUTTON_PRESS = 0,
	PYRA_MOUSE_EVENT_BUTTON_RELEASE = 1,
};

struct pyra_roccat_report {
	uint8_t type;
	uint8_t value;
	uint8_t key;
} __attribute__ ((__packed__));

struct pyra_device {
	int actual_profile;
	int actual_cpi;
	int roccat_claimed;
	int chrdev_minor;
	struct mutex pyra_lock;
	struct pyra_profile_settings profile_settings[5];
};

#endif
