// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Force feedback support for SmartJoy PLUS PS2->USB adapter
 *
 *  Copyright (c) 2009 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 *  Based of hid-pl.c and hid-gaff.c
 *   Copyright (c) 2007, 2009 Anssi Hannula <anssi.hannula@gmail.com>
 *   Copyright (c) 2008 Lukasz Lubojanski <lukasz@lubojanski.info>
 */

/*
 */

/* #define DEBUG */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

#ifdef CONFIG_SMARTJOYPLUS_FF

struct sjoyff_device {
	struct hid_report *report;
};

static int hid_sjoyff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct sjoyff_device *sjoyff = data;
	u32 left, right;

	left = effect->u.rumble.strong_magnitude;
	right = effect->u.rumble.weak_magnitude;
	dev_dbg(&dev->dev, "called with 0x%08x 0x%08x\n", left, right);

	left = left * 0xff / 0xffff;
	right = (right != 0); /* on/off only */

	sjoyff->report->field[0]->value[1] = right;
	sjoyff->report->field[0]->value[2] = left;
	dev_dbg(&dev->dev, "running with 0x%02x 0x%02x\n", left, right);
	hid_hw_request(hid, sjoyff->report, HID_REQ_SET_REPORT);

	return 0;
}

static int sjoyff_init(struct hid_device *hid)
{
	struct sjoyff_device *sjoyff;
	struct hid_report *report;
	struct hid_input *hidinput;
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct list_head *report_ptr = report_list;
	struct input_dev *dev;
	int error;

	if (list_empty(report_list)) {
		hid_err(hid, "no output reports found\n");
		return -ENODEV;
	}

	list_for_each_entry(hidinput, &hid->inputs, list) {
		report_ptr = report_ptr->next;

		if (report_ptr == report_list) {
			hid_err(hid, "required output report is missing\n");
			return -ENODEV;
		}

		report = list_entry(report_ptr, struct hid_report, list);
		if (report->maxfield < 1) {
			hid_err(hid, "no fields in the report\n");
			return -ENODEV;
		}

		if (report->field[0]->report_count < 3) {
			hid_err(hid, "not enough values in the field\n");
			return -ENODEV;
		}

		sjoyff = kzalloc(sizeof(struct sjoyff_device), GFP_KERNEL);
		if (!sjoyff)
			return -ENOMEM;

		dev = hidinput->input;

		set_bit(FF_RUMBLE, dev->ffbit);

		error = input_ff_create_memless(dev, sjoyff, hid_sjoyff_play);
		if (error) {
			kfree(sjoyff);
			return error;
		}

		sjoyff->report = report;
		sjoyff->report->field[0]->value[0] = 0x01;
		sjoyff->report->field[0]->value[1] = 0x00;
		sjoyff->report->field[0]->value[2] = 0x00;
		hid_hw_request(hid, sjoyff->report, HID_REQ_SET_REPORT);
	}

	hid_info(hid, "Force feedback for SmartJoy PLUS PS2/USB adapter\n");

	return 0;
}
#else
static inline int sjoyff_init(struct hid_device *hid)
{
	return 0;
}
#endif

static int sjoy_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	hdev->quirks |= id->driver_data;

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

	sjoyff_init(hdev);

	return 0;
err:
	return ret;
}

static const struct hid_device_id sjoy_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SUPER_JOY_BOX_3_PRO),
		.driver_data = HID_QUIRK_NOGET },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SUPER_DUAL_BOX_PRO),
		.driver_data = HID_QUIRK_MULTI_INPUT | HID_QUIRK_NOGET |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SUPER_JOY_BOX_5_PRO),
		.driver_data = HID_QUIRK_MULTI_INPUT | HID_QUIRK_NOGET |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_SMARTJOY_PLUS) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_SUPER_JOY_BOX_3) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_DUAL_USB_JOYPAD),
		.driver_data = HID_QUIRK_MULTI_INPUT |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLAYDOTCOM, USB_DEVICE_ID_PLAYDOTCOM_EMS_USBII),
		.driver_data = HID_QUIRK_MULTI_INPUT |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ }
};
MODULE_DEVICE_TABLE(hid, sjoy_devices);

static struct hid_driver sjoy_driver = {
	.name = "smartjoyplus",
	.id_table = sjoy_devices,
	.probe = sjoy_probe,
};
module_hid_driver(sjoy_driver);

MODULE_DESCRIPTION("Force feedback support for SmartJoy PLUS PS2->USB adapter");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jussi Kivilinna");

