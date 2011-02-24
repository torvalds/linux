/*
 * Force feedback support for ACRUX game controllers
 *
 * From what I have gathered, these devices are mass produced in China
 * by several vendors. They often share the same design as the original
 * Xbox 360 controller.
 *
 * 1a34:0802 "ACRUX USB GAMEPAD 8116"
 *  - tested with a EXEQ EQ-PCU-02090 game controller.
 *
 * Copyright (c) 2010 Sergei Kolzun <x0r@dv-life.ru>
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

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/hid.h>

#include "hid-ids.h"
#include "usbhid/usbhid.h"

struct axff_device {
	struct hid_report *report;
};

static int axff_play(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct axff_device *axff = data;
	int left, right;

	left = effect->u.rumble.strong_magnitude;
	right = effect->u.rumble.weak_magnitude;

	dbg_hid("called with 0x%04x 0x%04x", left, right);

	left = left * 0xff / 0xffff;
	right = right * 0xff / 0xffff;

	axff->report->field[0]->value[0] = left;
	axff->report->field[1]->value[0] = right;
	axff->report->field[2]->value[0] = left;
	axff->report->field[3]->value[0] = right;
	dbg_hid("running with 0x%02x 0x%02x", left, right);
	usbhid_submit_report(hid, axff->report, USB_DIR_OUT);

	return 0;
}

static int axff_init(struct hid_device *hid)
{
	struct axff_device *axff;
	struct hid_report *report;
	struct hid_input *hidinput = list_first_entry(&hid->inputs, struct hid_input, list);
	struct list_head *report_list =&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	int error;

	if (list_empty(report_list)) {
		hid_err(hid, "no output reports found\n");
		return -ENODEV;
	}

	report = list_first_entry(report_list, struct hid_report, list);

	if (report->maxfield < 4) {
		hid_err(hid, "no fields in the report: %d\n", report->maxfield);
		return -ENODEV;
	}

	axff = kzalloc(sizeof(struct axff_device), GFP_KERNEL);
	if (!axff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, axff, axff_play);
	if (error)
		goto err_free_mem;

	axff->report = report;
	axff->report->field[0]->value[0] = 0x00;
	axff->report->field[1]->value[0] = 0x00;
	axff->report->field[2]->value[0] = 0x00;
	axff->report->field[3]->value[0] = 0x00;
	usbhid_submit_report(hid, axff->report, USB_DIR_OUT);

	hid_info(hid, "Force Feedback for ACRUX game controllers by Sergei Kolzun<x0r@dv-life.ru>\n");

	return 0;

err_free_mem:
	kfree(axff);
	return error;
}

static int ax_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int error;

	dev_dbg(&hdev->dev, "ACRUX HID hardware probe...\n");

	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "parse failed\n");
		return error;
	}

	error = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (error) {
		hid_err(hdev, "hw start failed\n");
		return error;
	}

	error = axff_init(hdev);
	if (error) {
		/*
		 * Do not fail device initialization completely as device
		 * may still be partially operable, just warn.
		 */
		hid_warn(hdev,
			 "Failed to enable force feedback support, error: %d\n",
			 error);
	}

	return 0;
}

static const struct hid_device_id ax_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ACRUX, 0x0802), },
	{ }
};
MODULE_DEVICE_TABLE(hid, ax_devices);

static struct hid_driver ax_driver = {
	.name = "acrux",
	.id_table = ax_devices,
	.probe = ax_probe,
};

static int __init ax_init(void)
{
	return hid_register_driver(&ax_driver);
}

static void __exit ax_exit(void)
{
	hid_unregister_driver(&ax_driver);
}

module_init(ax_init);
module_exit(ax_exit);

MODULE_AUTHOR("Sergei Kolzun");
MODULE_DESCRIPTION("Force feedback support for ACRUX game controllers");
MODULE_LICENSE("GPL");
