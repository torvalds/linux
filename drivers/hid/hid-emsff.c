/*
 *  Force feedback support for EMS Trio Linker Plus II
 *
 *  Copyright (c) 2010 Ignaz Forster <ignaz.forster@gmx.de>
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


#include <linux/hid.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/module.h>

#include "hid-ids.h"
#include "usbhid/usbhid.h"

struct emsff_device {
	struct hid_report *report;
};

static int emsff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct emsff_device *emsff = data;
	int weak, strong;

	weak = effect->u.rumble.weak_magnitude;
	strong = effect->u.rumble.strong_magnitude;

	dbg_hid("called with 0x%04x 0x%04x\n", strong, weak);

	weak = weak * 0xff / 0xffff;
	strong = strong * 0xff / 0xffff;

	emsff->report->field[0]->value[1] = weak;
	emsff->report->field[0]->value[2] = strong;

	dbg_hid("running with 0x%02x 0x%02x\n", strong, weak);
	usbhid_submit_report(hid, emsff->report, USB_DIR_OUT);

	return 0;
}

static int emsff_init(struct hid_device *hid)
{
	struct emsff_device *emsff;
	struct hid_report *report;
	struct hid_input *hidinput = list_first_entry(&hid->inputs,
						struct hid_input, list);
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	int error;

	if (list_empty(report_list)) {
		hid_err(hid, "no output reports found\n");
		return -ENODEV;
	}

	report = list_first_entry(report_list, struct hid_report, list);
	if (report->maxfield < 1) {
		hid_err(hid, "no fields in the report\n");
		return -ENODEV;
	}

	if (report->field[0]->report_count < 7) {
		hid_err(hid, "not enough values in the field\n");
		return -ENODEV;
	}

	emsff = kzalloc(sizeof(struct emsff_device), GFP_KERNEL);
	if (!emsff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, emsff, emsff_play);
	if (error) {
		kfree(emsff);
		return error;
	}

	emsff->report = report;
	emsff->report->field[0]->value[0] = 0x01;
	emsff->report->field[0]->value[1] = 0x00;
	emsff->report->field[0]->value[2] = 0x00;
	emsff->report->field[0]->value[3] = 0x00;
	emsff->report->field[0]->value[4] = 0x00;
	emsff->report->field[0]->value[5] = 0x00;
	emsff->report->field[0]->value[6] = 0x00;
	usbhid_submit_report(hid, emsff->report, USB_DIR_OUT);

	hid_info(hid, "force feedback for EMS based devices by Ignaz Forster <ignaz.forster@gmx.de>\n");

	return 0;
}

static int ems_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

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

	ret = emsff_init(hdev);
	if (ret) {
		dev_err(&hdev->dev, "force feedback init failed\n");
		hid_hw_stop(hdev);
		goto err;
	}

	return 0;
err:
	return ret;
}

static const struct hid_device_id ems_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_EMS, 0x118) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ems_devices);

static struct hid_driver ems_driver = {
	.name = "hkems",
	.id_table = ems_devices,
	.probe = ems_probe,
};

static int ems_init(void)
{
	return hid_register_driver(&ems_driver);
}

static void ems_exit(void)
{
	hid_unregister_driver(&ems_driver);
}

module_init(ems_init);
module_exit(ems_exit);
MODULE_LICENSE("GPL");

