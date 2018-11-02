/*
 *  HID driver for Saitek devices.
 *
 *  PS1000 (USB gamepad):
 *  Fixes the HID report descriptor by removing a non-existent axis and
 *  clearing the constant bit on the input reports for buttons and d-pad.
 *  (This module is based on "hid-ortek".)
 *  Copyright (c) 2012 Andreas Hübner
 *
 *  R.A.T.7, R.A.T.9, M.M.O.7 (USB gaming mice):
 *  Fixes the mode button which cycles through three constantly pressed
 *  buttons. All three press events are mapped to one button and the
 *  missing release event is generated immediately.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "hid-ids.h"

#define SAITEK_FIX_PS1000	0x0001
#define SAITEK_RELEASE_MODE_RAT7	0x0002
#define SAITEK_RELEASE_MODE_MMO7	0x0004

struct saitek_sc {
	unsigned long quirks;
	int mode;
};

static int saitek_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	struct saitek_sc *ssc;
	int ret;

	ssc = devm_kzalloc(&hdev->dev, sizeof(*ssc), GFP_KERNEL);
	if (ssc == NULL) {
		hid_err(hdev, "can't alloc saitek descriptor\n");
		return -ENOMEM;
	}

	ssc->quirks = quirks;
	ssc->mode = -1;

	hid_set_drvdata(hdev, ssc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static __u8 *saitek_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct saitek_sc *ssc = hid_get_drvdata(hdev);

	if ((ssc->quirks & SAITEK_FIX_PS1000) && *rsize == 137 &&
			rdesc[20] == 0x09 && rdesc[21] == 0x33 &&
			rdesc[94] == 0x81 && rdesc[95] == 0x03 &&
			rdesc[110] == 0x81 && rdesc[111] == 0x03) {

		hid_info(hdev, "Fixing up Saitek PS1000 report descriptor\n");

		/* convert spurious axis to a "noop" Logical Minimum (0) */
		rdesc[20] = 0x15;
		rdesc[21] = 0x00;

		/* clear constant bit on buttons and d-pad */
		rdesc[95] = 0x02;
		rdesc[111] = 0x02;

	}
	return rdesc;
}

static int saitek_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	struct saitek_sc *ssc = hid_get_drvdata(hdev);

	if (ssc->quirks & SAITEK_RELEASE_MODE_RAT7 && size == 7) {
		/* R.A.T.7 uses bits 13, 14, 15 for the mode */
		int mode = -1;
		if (raw_data[1] & 0x01)
			mode = 0;
		else if (raw_data[1] & 0x02)
			mode = 1;
		else if (raw_data[1] & 0x04)
			mode = 2;

		/* clear mode bits */
		raw_data[1] &= ~0x07;

		if (mode != ssc->mode) {
			hid_dbg(hdev, "entered mode %d\n", mode);
			if (ssc->mode != -1) {
				/* use bit 13 as the mode button */
				raw_data[1] |= 0x04;
			}
			ssc->mode = mode;
		}
	} else if (ssc->quirks & SAITEK_RELEASE_MODE_MMO7 && size == 8) {

		/* M.M.O.7 uses bits 8, 22, 23 for the mode */
		int mode = -1;
		if (raw_data[1] & 0x80)
			mode = 0;
		else if (raw_data[2] & 0x01)
			mode = 1;
		else if (raw_data[2] & 0x02)
			mode = 2;

		/* clear mode bits */
		raw_data[1] &= ~0x80;
		raw_data[2] &= ~0x03;

		if (mode != ssc->mode) {
			hid_dbg(hdev, "entered mode %d\n", mode);
			if (ssc->mode != -1) {
				/* use bit 8 as the mode button, bits 22
				 * and 23 do not represent buttons
				 * according to the HID report descriptor
				 */
				raw_data[1] |= 0x80;
			}
			ssc->mode = mode;
		}
	}

	return 0;
}

static int saitek_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct saitek_sc *ssc = hid_get_drvdata(hdev);
	struct input_dev *input = field->hidinput->input;

	if (usage->type == EV_KEY && value &&
			(((ssc->quirks & SAITEK_RELEASE_MODE_RAT7) &&
			  usage->code - BTN_MOUSE == 10) ||
			((ssc->quirks & SAITEK_RELEASE_MODE_MMO7) &&
			 usage->code - BTN_MOUSE == 15))) {

		input_report_key(input, usage->code, 1);

		/* report missing release event */
		input_report_key(input, usage->code, 0);

		return 1;
	}

	return 0;
}

static const struct hid_device_id saitek_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_PS1000),
		.driver_data = SAITEK_FIX_PS1000 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MADCATZ, USB_DEVICE_ID_MADCATZ_RAT5),
		.driver_data = SAITEK_RELEASE_MODE_RAT7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RAT7_OLD),
		.driver_data = SAITEK_RELEASE_MODE_RAT7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RAT7),
		.driver_data = SAITEK_RELEASE_MODE_RAT7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RAT7_CONTAGION),
		.driver_data = SAITEK_RELEASE_MODE_RAT7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RAT9),
		.driver_data = SAITEK_RELEASE_MODE_RAT7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MADCATZ, USB_DEVICE_ID_MADCATZ_RAT9),
		.driver_data = SAITEK_RELEASE_MODE_RAT7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_MMO7),
		.driver_data = SAITEK_RELEASE_MODE_MMO7 },
	{ }
};

MODULE_DEVICE_TABLE(hid, saitek_devices);

static struct hid_driver saitek_driver = {
	.name = "saitek",
	.id_table = saitek_devices,
	.probe = saitek_probe,
	.report_fixup = saitek_report_fixup,
	.raw_event = saitek_raw_event,
	.event = saitek_event,
};
module_hid_driver(saitek_driver);

MODULE_LICENSE("GPL");
