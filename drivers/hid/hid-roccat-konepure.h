#ifndef __HID_ROCCAT_KONEPURE_H
#define __HID_ROCCAT_KONEPURE_H

/*
 * Copyright (c) 2012 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/types.h>

enum {
	KONEPURE_SIZE_ACTUAL_PROFILE = 0x03,
	KONEPURE_SIZE_CONTROL = 0x03,
	KONEPURE_SIZE_FIRMWARE_WRITE = 0x0402,
	KONEPURE_SIZE_INFO = 0x06,
	KONEPURE_SIZE_MACRO = 0x0822,
	KONEPURE_SIZE_PROFILE_SETTINGS = 0x1f,
	KONEPURE_SIZE_PROFILE_BUTTONS = 0x3b,
	KONEPURE_SIZE_SENSOR = 0x06,
	KONEPURE_SIZE_TALK = 0x10,
	KONEPURE_SIZE_TCU = 0x04,
	KONEPURE_SIZE_TCU_IMAGE = 0x0404,
};

enum konepure_control_requests {
	KONEPURE_CONTROL_REQUEST_GENERAL = 0x80,
	KONEPURE_CONTROL_REQUEST_BUTTONS = 0x90,
};

enum konepure_commands {
	KONEPURE_COMMAND_CONTROL = 0x04,
	KONEPURE_COMMAND_ACTUAL_PROFILE = 0x05,
	KONEPURE_COMMAND_PROFILE_SETTINGS = 0x06,
	KONEPURE_COMMAND_PROFILE_BUTTONS = 0x07,
	KONEPURE_COMMAND_MACRO = 0x08,
	KONEPURE_COMMAND_INFO = 0x09,
	KONEPURE_COMMAND_TCU = 0x0c,
	KONEPURE_COMMAND_TCU_IMAGE = 0x0c,
	KONEPURE_COMMAND_E = 0x0e,
	KONEPURE_COMMAND_SENSOR = 0x0f,
	KONEPURE_COMMAND_TALK = 0x10,
	KONEPURE_COMMAND_FIRMWARE_WRITE = 0x1b,
	KONEPURE_COMMAND_FIRMWARE_WRITE_CONTROL = 0x1c,
};

enum {
	KONEPURE_MOUSE_REPORT_NUMBER_BUTTON = 3,
};

struct konepure_mouse_report_button {
	uint8_t report_number; /* always KONEPURE_MOUSE_REPORT_NUMBER_BUTTON */
	uint8_t zero;
	uint8_t type;
	uint8_t data1;
	uint8_t data2;
	uint8_t zero2;
	uint8_t unknown[2];
} __packed;

struct konepure_device {
	int roccat_claimed;
	int chrdev_minor;
	struct mutex konepure_lock;
};

#endif
