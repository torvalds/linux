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

/*
 * case 1: writes request 80 and reads value 1
 *
 */
struct koneplus_control {
	uint8_t command; /* KONEPLUS_COMMAND_CONTROL */
	/*
	 * value is profile number in range 0-4 for requesting settings and buttons
	 * 1 if status ok for requesting status
	 */
	uint8_t value;
	uint8_t request;
} __attribute__ ((__packed__));

enum koneplus_control_requests {
	KONEPLUS_CONTROL_REQUEST_STATUS = 0x00,
	KONEPLUS_CONTROL_REQUEST_PROFILE_SETTINGS = 0x80,
	KONEPLUS_CONTROL_REQUEST_PROFILE_BUTTONS = 0x90,
};

enum koneplus_control_values {
	KONEPLUS_CONTROL_REQUEST_STATUS_OVERLOAD = 0,
	KONEPLUS_CONTROL_REQUEST_STATUS_OK = 1,
	KONEPLUS_CONTROL_REQUEST_STATUS_WAIT = 3,
};

struct koneplus_startup_profile {
	uint8_t command; /* KONEPLUS_COMMAND_STARTUP_PROFILE */
	uint8_t size; /* always 3 */
	uint8_t startup_profile; /* Range 0-4! */
} __attribute__ ((__packed__));

struct koneplus_profile_settings {
	uint8_t command; /* KONEPLUS_COMMAND_PROFILE_SETTINGS */
	uint8_t size; /* always 43 */
	uint8_t number; /* range 0-4 */
	uint8_t advanced_sensitivity;
	uint8_t sensitivity_x;
	uint8_t sensitivity_y;
	uint8_t cpi_levels_enabled;
	uint8_t cpi_levels_x[5];
	uint8_t cpi_startup_level; /* range 0-4 */
	uint8_t cpi_levels_y[5]; /* range 1-60 means 100-6000 cpi */
	uint8_t unknown1;
	uint8_t polling_rate;
	uint8_t lights_enabled;
	uint8_t light_effect_mode;
	uint8_t color_flow_effect;
	uint8_t light_effect_type;
	uint8_t light_effect_speed;
	uint8_t lights[16];
	uint16_t checksum;
} __attribute__ ((__packed__));

struct koneplus_profile_buttons {
	uint8_t command; /* KONEPLUS_COMMAND_PROFILE_BUTTONS */
	uint8_t size; /* always 77 */
	uint8_t number; /* range 0-4 */
	uint8_t data[72];
	uint16_t checksum;
} __attribute__ ((__packed__));

struct koneplus_macro {
	uint8_t command; /* KONEPLUS_COMMAND_MACRO */
	uint16_t size; /* always 0x822 little endian */
	uint8_t profile; /* range 0-4 */
	uint8_t button; /* range 0-23 */
	uint8_t data[2075];
	uint16_t checksum;
} __attribute__ ((__packed__));

struct koneplus_info {
	uint8_t command; /* KONEPLUS_COMMAND_INFO */
	uint8_t size; /* always 6 */
	uint8_t firmware_version;
	uint8_t unknown[3];
} __attribute__ ((__packed__));

struct koneplus_e {
	uint8_t command; /* KONEPLUS_COMMAND_E */
	uint8_t size; /* always 3 */
	uint8_t unknown; /* TODO 1; 0 before firmware update */
} __attribute__ ((__packed__));

struct koneplus_sensor {
	uint8_t command;  /* KONEPLUS_COMMAND_SENSOR */
	uint8_t size; /* always 6 */
	uint8_t data[4];
} __attribute__ ((__packed__));

struct koneplus_firmware_write {
	uint8_t command; /* KONEPLUS_COMMAND_FIRMWARE_WRITE */
	uint8_t unknown[1025];
} __attribute__ ((__packed__));

struct koneplus_firmware_write_control {
	uint8_t command; /* KONEPLUS_COMMAND_FIRMWARE_WRITE_CONTROL */
	/*
	 * value is 1 on success
	 * 3 means "not finished yet"
	 */
	uint8_t value;
	uint8_t unknown; /* always 0x75 */
} __attribute__ ((__packed__));

struct koneplus_tcu {
	uint16_t usb_command; /* KONEPLUS_USB_COMMAND_TCU */
	uint8_t data[2];
} __attribute__ ((__packed__));

struct koneplus_tcu_image {
	uint16_t usb_command; /* KONEPLUS_USB_COMMAND_TCU */
	uint8_t data[1024];
	uint16_t checksum;
} __attribute__ ((__packed__));

enum koneplus_commands {
	KONEPLUS_COMMAND_CONTROL = 0x4,
	KONEPLUS_COMMAND_STARTUP_PROFILE = 0x5,
	KONEPLUS_COMMAND_PROFILE_SETTINGS = 0x6,
	KONEPLUS_COMMAND_PROFILE_BUTTONS = 0x7,
	KONEPLUS_COMMAND_MACRO = 0x8,
	KONEPLUS_COMMAND_INFO = 0x9,
	KONEPLUS_COMMAND_E = 0xe,
	KONEPLUS_COMMAND_SENSOR = 0xf,
	KONEPLUS_COMMAND_FIRMWARE_WRITE = 0x1b,
	KONEPLUS_COMMAND_FIRMWARE_WRITE_CONTROL = 0x1c,
};

enum koneplus_usb_commands {
	KONEPLUS_USB_COMMAND_CONTROL = 0x304,
	KONEPLUS_USB_COMMAND_STARTUP_PROFILE = 0x305,
	KONEPLUS_USB_COMMAND_PROFILE_SETTINGS = 0x306,
	KONEPLUS_USB_COMMAND_PROFILE_BUTTONS = 0x307,
	KONEPLUS_USB_COMMAND_MACRO = 0x308,
	KONEPLUS_USB_COMMAND_INFO = 0x309,
	KONEPLUS_USB_COMMAND_TCU = 0x30c,
	KONEPLUS_USB_COMMAND_E = 0x30e,
	KONEPLUS_USB_COMMAND_SENSOR = 0x30f,
	KONEPLUS_USB_COMMAND_FIRMWARE_WRITE = 0x31b,
	KONEPLUS_USB_COMMAND_FIRMWARE_WRITE_CONTROL = 0x31c,
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

	int startup_profile;
	struct koneplus_info info;
	struct koneplus_profile_settings profile_settings[5];
	struct koneplus_profile_buttons profile_buttons[5];
};

#endif
