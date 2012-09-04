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
	ISKU_PROFILE_NUM = 5,
	ISKU_USB_INTERFACE_PROTOCOL = 0,
};

struct isku_control {
	uint8_t command; /* ISKU_COMMAND_CONTROL */
	uint8_t value;
	uint8_t request;
} __packed;

struct isku_actual_profile {
	uint8_t command; /* ISKU_COMMAND_ACTUAL_PROFILE */
	uint8_t size; /* always 3 */
	uint8_t actual_profile;
} __packed;

struct isku_key_mask {
	uint8_t command; /* ISKU_COMMAND_KEY_MASK */
	uint8_t size; /* 6 */
	uint8_t profile_number; /* 0-4 */
	uint8_t mask;
	uint16_t checksum;
} __packed;

struct isku_keys_function {
	uint8_t data[0x29];
} __packed;

struct isku_keys_easyzone {
	uint8_t data[0x41];
} __packed;

struct isku_keys_media {
	uint8_t data[0x1d];
} __packed;

struct isku_keys_thumbster {
	uint8_t data[0x17];
} __packed;

struct isku_keys_macro {
	uint8_t data[0x23];
} __packed;

struct isku_keys_capslock {
	uint8_t data[0x6];
} __packed;

struct isku_macro {
	uint8_t data[0x823];
} __packed;

struct isku_light {
	uint8_t data[0xa];
} __packed;

struct isku_info {
	uint8_t data[2];
	uint8_t firmware_version;
	uint8_t unknown[3];
} __packed;

struct isku_talk {
	uint8_t data[0x10];
} __packed;

struct isku_last_set {
	uint8_t data[0x14];
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
	ISKU_COMMAND_KEYS_CAPSLOCK = 0x13,
	ISKU_COMMAND_LAST_SET = 0x14,
	ISKU_COMMAND_15 = 0x15,
	ISKU_COMMAND_TALK = 0x16,
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
