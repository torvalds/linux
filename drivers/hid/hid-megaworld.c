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

static int mwctrl_init(struct hid_device *hid)
{
	struct mwctrl_device *mwctrl;
	struct hid_report *report;
	struct hid_input *hidinput;
	struct input_dev *dev;
	int error;
	int i;

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}
	hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	dev = hidinput->input;

	for (i = 0; i < 4; i++) {
		report = hid_validate_values(hid, HID_OUTPUT_REPORT, 0, i, 1);
		if (!report)
			return -ENODEV;
	}

	mwctrl = kzalloc(sizeof(struct mwctrl_device), GFP_KERNEL);
	if (!mwctrl)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, mwctrl, mwctrl_play);
	if (error) {
		kfree(mwctrl);
		return error;
	}

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

	return 0;
}

static int mwctrl_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	ret = mwctrl_init(hdev);
	if (ret)
		hid_hw_stop(hdev);

	return ret;
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
	.probe = mwctrl_probe,
};
module_hid_driver(mwctrl_driver);

MODULE_LICENSE("GPL");
