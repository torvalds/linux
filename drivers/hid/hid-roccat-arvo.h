#ifndef __HID_ROCCAT_ARVO_H
#define __HID_ROCCAT_ARVO_H

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

struct arvo_mode_key { /* 2 bytes */
	uint8_t command; /* ARVO_COMMAND_MODE_KEY */
	uint8_t state;
} __packed;

struct arvo_button {
	uint8_t unknown[24];
} __packed;

struct arvo_info {
	uint8_t unknown[8];
} __packed;

struct arvo_key_mask { /* 2 bytes */
	uint8_t command; /* ARVO_COMMAND_KEY_MASK */
	uint8_t key_mask;
} __packed;

/* selected profile is persistent */
struct arvo_actual_profile { /* 2 bytes */
	uint8_t command; /* ARVO_COMMAND_ACTUAL_PROFILE */
	uint8_t actual_profile;
} __packed;

enum arvo_commands {
	ARVO_COMMAND_MODE_KEY = 0x3,
	ARVO_COMMAND_BUTTON = 0x4,
	ARVO_COMMAND_INFO = 0x5,
	ARVO_COMMAND_KEY_MASK = 0x6,
	ARVO_COMMAND_ACTUAL_PROFILE = 0x7,
};

struct arvo_special_report {
	uint8_t unknown1; /* always 0x01 */
	uint8_t event;
	uint8_t unknown2; /* always 0x70 */
} __packed;

enum arvo_special_report_events {
	ARVO_SPECIAL_REPORT_EVENT_ACTION_PRESS = 0x10,
	ARVO_SPECIAL_REPORT_EVENT_ACTION_RELEASE = 0x0,
};

enum arvo_special_report_event_masks {
	ARVO_SPECIAL_REPORT_EVENT_MASK_ACTION = 0xf0,
	ARVO_SPECIAL_REPORT_EVENT_MASK_BUTTON = 0x0f,
};

struct arvo_roccat_report {
	uint8_t profile;
	uint8_t button;
	uint8_t action;
} __packed;

enum arvo_roccat_report_action {
	ARVO_ROCCAT_REPORT_ACTION_RELEASE = 0,
	ARVO_ROCCAT_REPORT_ACTION_PRESS = 1,
};

struct arvo_device {
	int roccat_claimed;
	int chrdev_minor;

	struct mutex arvo_lock;

	int actual_profile;
};

#endif
