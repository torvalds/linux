// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Force feedback support for Betop based devices
 *
 *  The devices are distributed under various names and the same USB device ID
 *  can be used in both adapters and actual game controllers.
 *
 *  0x11c2:0x2208 "BTP2185 BFM mode Joystick"
 *   - tested with BTP2185 BFM Mode.
 *
 *  0x11C0:0x5506 "BTP2185 PC mode Joystick"
 *   - tested with BTP2185 PC Mode.
 *
 *  0x8380:0x1850 "BTP2185 V2 PC mode USB Gamepad"
 *   - tested with BTP2185 PC Mode with another version.
 *
 *  0x20bc:0x5500 "BTP2185 V2 BFM mode Joystick"
 *   - tested with BTP2171s.
 *  Copyright (c) 2014 Huang Bo <huangbobupt@163.com>
 */

/*
 */


#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/hid.h>

#include "hid-ids.h"

struct betopff_device {
	struct hid_report *report;
};

static int hid_betopff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct betopff_device *betopff = data;
	__u16 left, right;

	left = effect->u.rumble.strong_magnitude;
	right = effect->u.rumble.weak_magnitude;

	betopff->report->field[2]->value[0] = left / 256;
	betopff->report->field[3]->value[0] = right / 256;

	hid_hw_request(hid, betopff->report, HID_REQ_SET_REPORT);

	return 0;
}

static int betopff_init(struct hid_device *hid)
{
	struct betopff_device *betopff;
	struct hid_report *report;
	struct hid_input *hidinput;
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev;
	int field_count = 0;
	int error;
	int i, j;

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}

	hidinput = list_first_entry(&hid->inputs, struct hid_input, list);
	dev = hidinput->input;

	if (list_empty(report_list)) {
		hid_err(hid, "no output reports found\n");
		return -ENODEV;
	}

	report = list_first_entry(report_list, struct hid_report, list);
	/*
	 * Actually there are 4 fields for 4 Bytes as below:
	 * -----------------------------------------
	 * Byte0  Byte1  Byte2	  Byte3
	 * 0x00   0x00   left_motor right_motor
	 * -----------------------------------------
	 * Do init them with default value.
	 */
	for (i = 0; i < report->maxfield; i++) {
		for (j = 0; j < report->field[i]->report_count; j++) {
			report->field[i]->value[j] = 0x00;
			field_count++;
		}
	}

	if (field_count < 4) {
		hid_err(hid, "not enough fields in the report: %d\n",
				field_count);
		return -ENODEV;
	}

	betopff = kzalloc(sizeof(*betopff), GFP_KERNEL);
	if (!betopff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, betopff, hid_betopff_play);
	if (error) {
		kfree(betopff);
		return error;
	}

	betopff->report = report;
	hid_hw_request(hid, betopff->report, HID_REQ_SET_REPORT);

	hid_info(hid, "Force feedback for betop devices by huangbo <huangbobupt@163.com>\n");

	return 0;
}

static int betop_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	if (id->driver_data)
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	betopff_init(hdev);

	return 0;
err:
	return ret;
}

static const struct hid_device_id betop_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_BETOP_2185BFM, 0x2208) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_BETOP_2185PC, 0x5506) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_BETOP_2185V2PC, 0x1850) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_BETOP_2185V2BFM, 0x5500) },
	{ }
};
MODULE_DEVICE_TABLE(hid, betop_devices);

static struct hid_driver betop_driver = {
	.name = "betop",
	.id_table = betop_devices,
	.probe = betop_probe,
};
module_hid_driver(betop_driver);

MODULE_LICENSE("GPL");
