// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Vibration support for Mega World controllers
 *
 * Copyright 2022 Frank Zago
 *
 * Derived from hid-zpff.c:
 *   Copyright (c) 2005, 2006 Anssi Hannula <anssi.hannula@gmail.com>
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "hid-ids.h"

struct mwctrl_device {
	struct hid_report *report;
	s32 *weak;
	s32 *strong;
};

static int mwctrl_play(struct input_dev *dev, void *data,
		       struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct mwctrl_device *mwctrl = data;

	*mwctrl->strong = effect->u.rumble.strong_magnitude >> 8;
	*mwctrl->weak = effect->u.rumble.weak_magnitude >> 8;

	hid_hw_request(hid, mwctrl->report, HID_REQ_SET_REPORT);

	return 0;
}

static int mwctrl_input_configured(struct hid_device *hid, struct hid_input *hidinput)
{
	struct mwctrl_device *mwctrl;
	struct hid_report *report;
	struct input_dev *dev = hidinput->input;
	int error;
	int i;

	if (!list_is_first(&hidinput->list, &hid->inputs))
		return 0;

	for (i = 0; i < 4; i++) {
		report = hid_validate_values(hid, HID_OUTPUT_REPORT, 0, i, 1);
		if (!report)
			return -ENODEV;
	}

	mwctrl = kzalloc_obj(struct mwctrl_device);
	if (!mwctrl)
		return -ENOMEM;

	mwctrl->report = report;
	/* Field 0 is always 2, and field 1 is always 0. The original
	 * windows driver has a 5 bytes command, where the 5th byte is
	 * a repeat of the 3rd byte, however the device has only 4
	 * fields. It could be a bug in the driver, or there is a
	 * different device that needs it.
	 */
	report->field[0]->value[0] = 0x02;

	mwctrl->strong = &report->field[2]->value[0];
	mwctrl->weak = &report->field[3]->value[0];

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, mwctrl, mwctrl_play);
	if (error) {
		kfree(mwctrl);
		return error;
	}

	return 0;
}

static const struct hid_device_id mwctrl_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_MEGAWORLD,
			 USB_DEVICE_ID_MEGAWORLD_GAMEPAD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, mwctrl_devices);

static struct hid_driver mwctrl_driver = {
	.name = "megaworld",
	.id_table = mwctrl_devices,
	.input_configured = mwctrl_input_configured,
};
module_hid_driver(mwctrl_driver);

MODULE_DESCRIPTION("Vibration support for Mega World controllers");
MODULE_LICENSE("GPL");
