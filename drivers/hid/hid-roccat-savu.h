#ifndef __HID_ROCCAT_SAVU_H
#define __HID_ROCCAT_SAVU_H

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

struct savu_mouse_report_special {
	uint8_t report_number; /* always 3 */
	uint8_t zero;
	uint8_t type;
	uint8_t data[2];
} __packed;

enum {
	SAVU_MOUSE_REPORT_NUMBER_SPECIAL = 3,
};

enum savu_mouse_report_button_types {
	/* data1 = new profile range 1-5 */
	SAVU_MOUSE_REPORT_BUTTON_TYPE_PROFILE = 0x20,

	/* data1 = button number range 1-24; data2 = action */
	SAVU_MOUSE_REPORT_BUTTON_TYPE_QUICKLAUNCH = 0x60,

	/* data1 = button number range 1-24; data2 = action */
	SAVU_MOUSE_REPORT_BUTTON_TYPE_TIMER = 0x80,

	/* data1 = setting number range 1-5 */
	SAVU_MOUSE_REPORT_BUTTON_TYPE_CPI = 0xb0,

	/* data1 and data2 = range 0x1-0xb */
	SAVU_MOUSE_REPORT_BUTTON_TYPE_SENSITIVITY = 0xc0,

	/* data1 = 22 = next track...
	 * data2 = action
	 */
	SAVU_MOUSE_REPORT_BUTTON_TYPE_MULTIMEDIA = 0xf0,
};

struct savu_roccat_report {
	uint8_t type;
	uint8_t data[2];
} __packed;

#endif
