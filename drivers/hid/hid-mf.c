// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Mayflash game controller adapters.
 *
 * These devices are manufactured by Mayflash but identify themselves
 * using the vendor ID of DragonRise Inc.
 *
 * Tested with:
 * 0079:1801 "DragonRise Inc. Mayflash PS3 Game Controller Adapter"
 * 0079:1803 "DragonRise Inc. Mayflash Wireless Sensor DolphinBar"
 * 0079:1843 "DragonRise Inc. Mayflash GameCube Game Controller Adapter"
 * 0079:1844 "DragonRise Inc. Mayflash GameCube Game Controller Adapter (v04)"
 *
 * The following adapters probably work too, but need to be tested:
 * 0079:1800 "DragonRise Inc. Mayflash WIIU Game Controller Adapter"
 *
 * Copyright (c) 2016-2017 Marcel Hasler <mahasler@gmail.com>
 */

/*
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

struct mf_device {
	struct hid_report *report;
};

static int mf_play(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct mf_device *mf = data;
	int strong, weak;

	strong = effect->u.rumble.strong_magnitude;
	weak = effect->u.rumble.weak_magnitude;

	dbg_hid("Called with 0x%04x 0x%04x.\n", strong, weak);

	strong = strong * 0xff / 0xffff;
	weak = weak * 0xff / 0xffff;

	dbg_hid("Running with 0x%02x 0x%02x.\n", strong, weak);

	mf->report->field[0]->value[0] = weak;
	mf->report->field[0]->value[1] = strong;
	hid_hw_request(hid, mf->report, HID_REQ_SET_REPORT);

	return 0;
}

static int mf_init(struct hid_device *hid)
{
	struct mf_device *mf;

	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;

	struct list_head *report_ptr;
	struct hid_report *report;

	struct list_head *input_ptr = &hid->inputs;
	struct hid_input *input;

	struct input_dev *dev;

	int error;

	/* Setup each of the four inputs */
	list_for_each(report_ptr, report_list) {
		report = list_entry(report_ptr, struct hid_report, list);

		if (report->maxfield < 1 || report->field[0]->report_count < 2) {
			hid_err(hid, "Invalid report, this should never happen!\n");
			return -ENODEV;
		}

		if (list_is_last(input_ptr, &hid->inputs)) {
			hid_err(hid, "Missing input, this should never happen!\n");
			return -ENODEV;
		}

		input_ptr = input_ptr->next;
		input = list_entry(input_ptr, struct hid_input, list);

		mf = kzalloc(sizeof(struct mf_device), GFP_KERNEL);
		if (!mf)
			return -ENOMEM;

		dev = input->input;
		set_bit(FF_RUMBLE, dev->ffbit);

		error = input_ff_create_memless(dev, mf, mf_play);
		if (error) {
			kfree(mf);
			return error;
		}

		mf->report = report;
		mf->report->field[0]->value[0] = 0x00;
		mf->report->field[0]->value[1] = 0x00;
		hid_hw_request(hid, mf->report, HID_REQ_SET_REPORT);
	}

	hid_info(hid, "Force feedback for HJZ Mayflash game controller "
		      "adapters by Marcel Hasler <mahasler@gmail.com>\n");

	return 0;
}

static int mf_probe(struct hid_device *hid, const struct hid_device_id *id)
{
	int error;

	dev_dbg(&hid->dev, "Mayflash HID hardware probe...\n");

	/* Apply quirks as needed */
	hid->quirks |= id->driver_data;

	error = hid_parse(hid);
	if (error) {
		hid_err(hid, "HID parse failed.\n");
		return error;
	}

	error = hid_hw_start(hid, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (error) {
		hid_err(hid, "HID hw start failed\n");
		return error;
	}

	error = mf_init(hid);
	if (error) {
		hid_err(hid, "Force feedback init failed.\n");
		hid_hw_stop(hid);
		return error;
	}

	return 0;
}

static const struct hid_device_id mf_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_DRAGONRISE, USB_DEVICE_ID_DRAGONRISE_PS3),
		.driver_data = HID_QUIRK_MULTI_INPUT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DRAGONRISE, USB_DEVICE_ID_DRAGONRISE_DOLPHINBAR),
		.driver_data = HID_QUIRK_MULTI_INPUT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DRAGONRISE, USB_DEVICE_ID_DRAGONRISE_GAMECUBE1),
		.driver_data = HID_QUIRK_MULTI_INPUT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DRAGONRISE, USB_DEVICE_ID_DRAGONRISE_GAMECUBE2),
		.driver_data = 0 }, /* No quirk required */
	{ }
};
MODULE_DEVICE_TABLE(hid, mf_devices);

static struct hid_driver mf_driver = {
	.name = "hid_mf",
	.id_table = mf_devices,
	.probe = mf_probe,
};
module_hid_driver(mf_driver);

MODULE_LICENSE("GPL");
