/*
 *  Force feedback support for GreenAsia (Product ID 0x12) based devices
 *
 *  The devices are distributed under various names and the same USB device ID
 *  can be used in many game controllers.
 *
 *
 *  0e8f:0012 "GreenAsia Inc.    USB Joystick     "
 *   - tested with MANTA Warior MM816 and SpeedLink Strike2 SL-6635.
 *
 *  Copyright (c) 2008 Lukasz Lubojanski <lukasz@lubojanski.info>
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
#include <linux/usb.h>
#include <linux/hid.h>
#include "hid-ids.h"

#ifdef CONFIG_GREENASIA_FF
#include "usbhid/usbhid.h"

struct gaff_device {
	struct hid_report *report;
};

static int hid_gaff_play(struct input_dev *dev, void *data,
			 struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct gaff_device *gaff = data;
	int left, right;

	left = effect->u.rumble.strong_magnitude;
	right = effect->u.rumble.weak_magnitude;

	dbg_hid("called with 0x%04x 0x%04x", left, right);

	left = left * 0xfe / 0xffff;
	right = right * 0xfe / 0xffff;

	gaff->report->field[0]->value[0] = 0x51;
	gaff->report->field[0]->value[1] = 0x0;
	gaff->report->field[0]->value[2] = right;
	gaff->report->field[0]->value[3] = 0;
	gaff->report->field[0]->value[4] = left;
	gaff->report->field[0]->value[5] = 0;
	dbg_hid("running with 0x%02x 0x%02x", left, right);
	usbhid_submit_report(hid, gaff->report, USB_DIR_OUT);

	gaff->report->field[0]->value[0] = 0xfa;
	gaff->report->field[0]->value[1] = 0xfe;
	gaff->report->field[0]->value[2] = 0x0;
	gaff->report->field[0]->value[4] = 0x0;

	usbhid_submit_report(hid, gaff->report, USB_DIR_OUT);

	return 0;
}

static int gaff_init(struct hid_device *hid)
{
	struct gaff_device *gaff;
	struct hid_report *report;
	struct hid_input *hidinput = list_entry(hid->inputs.next,
						struct hid_input, list);
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct list_head *report_ptr = report_list;
	struct input_dev *dev = hidinput->input;
	int error;

	if (list_empty(report_list)) {
		dev_err(&hid->dev, "no output reports found\n");
		return -ENODEV;
	}

	report_ptr = report_ptr->next;

	report = list_entry(report_ptr, struct hid_report, list);
	if (report->maxfield < 1) {
		dev_err(&hid->dev, "no fields in the report\n");
		return -ENODEV;
	}

	if (report->field[0]->report_count < 6) {
		dev_err(&hid->dev, "not enough values in the field\n");
		return -ENODEV;
	}

	gaff = kzalloc(sizeof(struct gaff_device), GFP_KERNEL);
	if (!gaff)
		return -ENOMEM;

	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, gaff, hid_gaff_play);
	if (error) {
		kfree(gaff);
		return error;
	}

	gaff->report = report;
	gaff->report->field[0]->value[0] = 0x51;
	gaff->report->field[0]->value[1] = 0x00;
	gaff->report->field[0]->value[2] = 0x00;
	gaff->report->field[0]->value[3] = 0x00;
	usbhid_submit_report(hid, gaff->report, USB_DIR_OUT);

	gaff->report->field[0]->value[0] = 0xfa;
	gaff->report->field[0]->value[1] = 0xfe;

	usbhid_submit_report(hid, gaff->report, USB_DIR_OUT);

	dev_info(&hid->dev, "Force Feedback for GreenAsia 0x12"
	       " devices by Lukasz Lubojanski <lukasz@lubojanski.info>\n");

	return 0;
}
#else
static inline int gaff_init(struct hid_device *hdev)
{
	return 0;
}
#endif

static int ga_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	dev_dbg(&hdev->dev, "Greenasia HID hardware probe...");

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto err;
	}

	gaff_init(hdev);

	return 0;
err:
	return ret;
}

static const struct hid_device_id ga_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GREENASIA, 0x0012),  },
	{ }
};
MODULE_DEVICE_TABLE(hid, ga_devices);

static struct hid_driver ga_driver = {
	.name = "greenasia",
	.id_table = ga_devices,
	.probe = ga_probe,
};

static int __init ga_init(void)
{
	return hid_register_driver(&ga_driver);
}

static void __exit ga_exit(void)
{
	hid_unregister_driver(&ga_driver);
}

module_init(ga_init);
module_exit(ga_exit);
MODULE_LICENSE("GPL");
