#ifndef __HID_ROCCAT_ISKU_H
#define __HID_ROCCAT_ISKU_H

/*
 * Copyright (c) 2011 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/types.h>

enum {
	ISKU_SIZE_CONTROL = 0x03,
	ISKU_SIZE_INFO = 0x06,
	ISKU_SIZE_KEY_MASK = 0x06,
	ISKU_SIZE_KEYS_FUNCTION = 0x29,
	ISKU_SIZE_KEYS_EASYZONE = 0x41,
	ISKU_SIZE_KEYS_MEDIA = 0x1d,
	ISKU_SIZE_KEYS_THUMBSTER = 0x17,
	ISKU_SIZE_KEYS_MACRO = 0x23,
	ISKU_SIZE_KEYS_CAPSLOCK = 0x06,
	ISKU_SIZE_LAST_SET = 0x14,
	ISKU_SIZE_LIGHT = 0x10,
	ISKU_SIZE_MACRO = 0x823,
	ISKU_SIZE_RESET = 0x03,
	ISKU_SIZE_TALK = 0x10,
	ISKU_SIZE_TALKFX = 0x10,
};

enum {
	ISKU_PROFILE_NUM = 5,
	ISKU_USB_INTERFACE_PROTOCOL = 0,
};

struct isku_actual_profile {
	uint8_t command; /* ISKU_COMMAND_ACTUAL_PROFILE */
	uint8_t size; /* always 3 */
	uint8_t actual_profile;
} __packed;

enum isku_commands {
	ISKU_COMMAND_CONTROL = 0x4,
	ISKU_COMMAND_ACTUAL_PROFILE = 0x5,
	ISKU_COMMAND_KEY_MASK = 0x7,
	ISKU_COMMAND_KEYS_FUNCTION = 0x8,
	ISKU_COMMAND_KEYS_EASYZONE = 0x9,
	ISKU_COMMAND_KEYS_MEDIA = 0xa,
	ISKU_COMMAND_KEYS_THUMBSTER = 0xb,
	ISKU_COMMAND_KEYS_MACRO = 0xd,
	ISKU_COMMAND_MACRO = 0xe,
	ISKU_COMMAND_INFO = 0xf,
	ISKU_COMMAND_LIGHT = 0x10,
	ISKU_COMMAND_RESET = 0x11,
	ISKU_COMMAND_KEYS_CAPSLOCK = 0x13,
	ISKU_COMMAND_LAST_SET = 0x14,
	ISKU_COMMAND_15 = 0x15,
	ISKU_COMMAND_TALK = 0x16,
	ISKU_COMMAND_TALKFX = 0x17,
	ISKU_COMMAND_FIRMWARE_WRITE = 0x1b,
	ISKU_COMMAND_FIRMWARE_WRITE_CONTROL = 0x1c,
};

struct isku_report_button {
	uint8_t number; /* ISKU_REPORT_NUMBER_BUTTON */
	uint8_t zero;
	uint8_t event;
	uint8_t data1;
	uint8_t data2;
};

enum isku_report_numbers {
	ISKU_REPORT_NUMBER_BUTTON = 3,
};

enum isku_report_button_events {
	ISKU_REPORT_BUTTON_EVENT_PROFILE = 0x2,
};

struct isku_roccat_report {
	uint8_t event;
	uint8_t data1;
	uint8_t data2;
	uint8_t profile;
} __packed;

struct isku_device {
	int roccat_claimed;
	int chrdev_minor;

	struct mutex isku_lock;

	int actual_profile;
};

#endif
