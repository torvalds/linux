// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Force feedback support for GreenAsia (Product ID 0x12) based devices
 *
 *  The devices are distributed under various names and the same USB device ID
 *  can be used in many game controllers.
 *
 *  0e8f:0012 "GreenAsia Inc.    USB Joystick     "
 *   - tested with MANTA Warior MM816 and SpeedLink Strike2 SL-6635.
 *
 *  Copyright (c) 2008 Lukasz Lubojanski <lukasz@lubojanski.info>
 */

/*
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

#ifdef CONFIG_GREENASIA_FF

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
	hid_hw_request(hid, gaff->report, HID_REQ_SET_REPORT);

	gaff->report->field[0]->value[0] = 0xfa;
	gaff->report->field[0]->value[1] = 0xfe;
	gaff->report->field[0]->value[2] = 0x0;
	gaff->report->field[0]->value[4] = 0x0;

	hid_hw_request(hid, gaff->report, HID_REQ_SET_REPORT);

	return 0;
}

static int gaff_input_configured(struct hid_device *hid, struct hid_input *hidinput)
{
	struct gaff_device *gaff;
	struct hid_report *report;
	struct list_head *report_list =
			&hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct input_dev *dev = hidinput->input;
	int error;

	if (!list_is_first(&hidinput->list, &hid->inputs))
		return 0;

	report = list_first_entry_or_null(report_list, struct hid_report, list);
	if (!report) {
		hid_err(hid, "no output reports found\n");
		return -ENODEV;
	}
	if (report->maxfield < 1) {
		hid_err(hid, "no fields in the report\n");
		return -ENODEV;
	}

	if (report->field[0]->report_count < 6) {
		hid_err(hid, "not enough values in the field\n");
		return -ENODEV;
	}

	gaff = kzalloc_obj(struct gaff_device);
	if (!gaff)
		return -ENOMEM;

	gaff->report = report;
	set_bit(FF_RUMBLE, dev->ffbit);

	error = input_ff_create_memless(dev, gaff, hid_gaff_play);
	if (error) {
		kfree(gaff);
		return error;
	}

	gaff->report->field[0]->value[0] = 0x51;
	gaff->report->field[0]->value[1] = 0x00;
	gaff->report->field[0]->value[2] = 0x00;
	gaff->report->field[0]->value[3] = 0x00;
	hid_hw_request(hid, gaff->report, HID_REQ_SET_REPORT);

	gaff->report->field[0]->value[0] = 0xfa;
	gaff->report->field[0]->value[1] = 0xfe;

	hid_hw_request(hid, gaff->report, HID_REQ_SET_REPORT);

	hid_info(hid, "Force Feedback for GreenAsia 0x12 devices by Lukasz Lubojanski <lukasz@lubojanski.info>\n");

	return 0;
}
#else
static inline int gaff_input_configured(struct hid_device *hdev,
					struct hid_input *hidinput)
{
	return 0;
}
#endif

static const struct hid_device_id ga_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GREENASIA, 0x0012),  },
	{ }
};
MODULE_DEVICE_TABLE(hid, ga_devices);

static struct hid_driver ga_driver = {
	.name = "greenasia",
	.id_table = ga_devices,
	.input_configured = gaff_input_configured,
};
module_hid_driver(ga_driver);

MODULE_DESCRIPTION("Force feedback support for GreenAsia (Product ID 0x12) based devices");
MODULE_LICENSE("GPL");
