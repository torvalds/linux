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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* #define DEBUG */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

#ifdef CONFIG_SMARTJOYPLUS_FF
#include "usbhid/usbhid.h"

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
	usbhid_submit_report(hid, sjoyff->report, USB_DIR_OUT);

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
		usbhid_submit_report(hid, sjoyff->report, USB_DIR_OUT);
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
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SUPER_JOY_BOX_3_PRO) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SUPER_DUAL_BOX_PRO),
		.driver_data = HID_QUIRK_MULTI_INPUT | HID_QUIRK_NOGET |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SUPER_JOY_BOX_5_PRO),
		.driver_data = HID_QUIRK_MULTI_INPUT | HID_QUIRK_NOGET |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_SMARTJOY_PLUS) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_DUAL_USB_JOYPAD),
		.driver_data = HID_QUIRK_MULTI_INPUT | HID_QUIRK_NOGET |
			       HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ }
};
MODULE_DEVICE_TABLE(hid, sjoy_devices);

static struct hid_driver sjoy_driver = {
	.name = "smartjoyplus",
	.id_table = sjoy_devices,
	.probe = sjoy_probe,
};

static int __init sjoy_init(void)
{
	return hid_register_driver(&sjoy_driver);
}

static void __exit sjoy_exit(void)
{
	hid_unregister_driver(&sjoy_driver);
}

module_init(sjoy_init);
module_exit(sjoy_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jussi Kivilinna");

