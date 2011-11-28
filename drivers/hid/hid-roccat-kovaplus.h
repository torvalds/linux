#ifndef __HID_ROCCAT_KOVAPLUS_H
#define __HID_ROCCAT_KOVAPLUS_H

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

struct kovaplus_control {
	uint8_t command; /* KOVAPLUS_COMMAND_CONTROL */
	uint8_t value;
	uint8_t request;
} __packed;

enum kovaplus_control_requests {
	/* read after write; value = 1 */
	KOVAPLUS_CONTROL_REQUEST_STATUS = 0x0,
	/* write; value = profile number range 0-4 */
	KOVAPLUS_CONTROL_REQUEST_PROFILE_SETTINGS = 0x10,
	/* write; value = profile number range 0-4 */
	KOVAPLUS_CONTROL_REQUEST_PROFILE_BUTTONS = 0x20,
};

enum kovaplus_control_values {
	KOVAPLUS_CONTROL_REQUEST_STATUS_OVERLOAD = 0, /* supposed */
	KOVAPLUS_CONTROL_REQUEST_STATUS_OK = 1,
	KOVAPLUS_CONTROL_REQUEST_STATUS_WAIT = 3, /* supposed */
};

struct kovaplus_actual_profile {
	uint8_t command; /* KOVAPLUS_COMMAND_ACTUAL_PROFILE */
	uint8_t size; /* always 3 */
	uint8_t actual_profile; /* Range 0-4! */
} __packed;

struct kovaplus_profile_settings {
	uint8_t command; /* KOVAPLUS_COMMAND_PROFILE_SETTINGS */
	uint8_t size; /* 16 */
	uint8_t profile_index; /* range 0-4 */
	uint8_t unknown1;
	uint8_t sensitivity_x; /* range 1-10 */
	uint8_t sensitivity_y; /* range 1-10 */
	uint8_t cpi_levels_enabled;
	uint8_t cpi_startup_level; /* range 1-4 */
	uint8_t data[8];
} __packed;

struct kovaplus_profile_buttons {
	uint8_t command; /* KOVAPLUS_COMMAND_PROFILE_BUTTONS */
	uint8_t size; /* 23 */
	uint8_t profile_index; /* range 0-4 */
	uint8_t data[20];
} __packed;

struct kovaplus_info {
	uint8_t command; /* KOVAPLUS_COMMAND_INFO */
	uint8_t size; /* 6 */
	uint8_t firmware_version;
	uint8_t unknown[3];
} __packed;

/* writes 1 on plugin */
struct kovaplus_a {
	uint8_t command; /* KOVAPLUS_COMMAND_A */
	uint8_t size; /* 3 */
	uint8_t unknown;
} __packed;

enum kovaplus_commands {
	KOVAPLUS_COMMAND_CONTROL = 0x4,
	KOVAPLUS_COMMAND_ACTUAL_PROFILE = 0x5,
	KOVAPLUS_COMMAND_PROFILE_SETTINGS = 0x6,
	KOVAPLUS_COMMAND_PROFILE_BUTTONS = 0x7,
	KOVAPLUS_COMMAND_INFO = 0x9,
	KOVAPLUS_COMMAND_A = 0xa,
};

enum kovaplus_mouse_report_numbers {
	KOVAPLUS_MOUSE_REPORT_NUMBER_MOUSE = 1,
	KOVAPLUS_MOUSE_REPORT_NUMBER_AUDIO = 2,
	KOVAPLUS_MOUSE_REPORT_NUMBER_BUTTON = 3,
	KOVAPLUS_MOUSE_REPORT_NUMBER_KBD = 4,
};

struct kovaplus_mouse_report_button {
	uint8_t report_number; /* KOVAPLUS_MOUSE_REPORT_NUMBER_BUTTON */
	uint8_t unknown1;
	uint8_t type;
	uint8_t data1;
	uint8_t data2;
} __packed;

enum kovaplus_mouse_report_button_types {
	/* data1 = profile_number range 1-5; no release event */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE_1 = 0x20,
	/* data1 = profile_number range 1-5; no release event */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE_2 = 0x30,
	/* data1 = button_number range 1-18; data2 = action */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_MACRO = 0x40,
	/* data1 = button_number range 1-18; data2 = action */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_SHORTCUT = 0x50,
	/* data1 = button_number range 1-18; data2 = action */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_QUICKLAUNCH = 0x60,
	/* data1 = button_number range 1-18; data2 = action */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_TIMER = 0x80,
	/* data1 = 1 = 400, 2 = 800, 4 = 1600, 7 = 3200; no release event */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_CPI = 0xb0,
	/* data1 + data2 = sense range 1-10; no release event */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_SENSITIVITY = 0xc0,
	/* data1 = type as in profile_buttons; data2 = action */
	KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_MULTIMEDIA = 0xf0,
};

enum kovaplus_mouse_report_button_actions {
	KOVAPLUS_MOUSE_REPORT_BUTTON_ACTION_PRESS = 0,
	KOVAPLUS_MOUSE_REPORT_BUTTON_ACTION_RELEASE = 1,
};

struct kovaplus_roccat_report {
	uint8_t type;
	uint8_t profile;
	uint8_t button;
	uint8_t data1;
	uint8_t data2;
} __packed;

struct kovaplus_device {
	int actual_profile;
	int actual_cpi;
	int actual_x_sensitivity;
	int actual_y_sensitivity;
	int roccat_claimed;
	int chrdev_minor;
	struct mutex kovaplus_lock;
	struct kovaplus_info info;
	struct kovaplus_profile_settings profile_settings[5];
	struct kovaplus_profile_buttons profile_buttons[5];
};

#endif
