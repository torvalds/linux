// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for some a4tech "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby
 */

/*
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "hid-ids.h"

#define A4_2WHEEL_MOUSE_HACK_7	0x01
#define A4_2WHEEL_MOUSE_HACK_B8	0x02

#define A4_WHEEL_ORIENTATION	(HID_UP_GENDESK | 0x000000b8)

struct a4tech_sc {
	unsigned long quirks;
	unsigned int hw_wheel;
	__s32 delayed_value;
};

static int a4_input_mapping(struct hid_device *hdev, struct hid_input *hi,
			    struct hid_field *field, struct hid_usage *usage,
			    unsigned long **bit, int *max)
{
	struct a4tech_sc *a4 = hid_get_drvdata(hdev);

	if (a4->quirks & A4_2WHEEL_MOUSE_HACK_B8 &&
	    usage->hid == A4_WHEEL_ORIENTATION) {
		/*
		 * We do not want to have this usage mapped to anything as it's
		 * nonstandard and doesn't really behave like an HID report.
		 * It's only selecting the orientation (vertical/horizontal) of
		 * the previous mouse wheel report. The input_events will be
		 * generated once both reports are recorded in a4_event().
		 */
		return -1;
	}

	return 0;

}

static int a4_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct a4tech_sc *a4 = hid_get_drvdata(hdev);

	if (usage->type == EV_REL && usage->code == REL_WHEEL_HI_RES) {
		set_bit(REL_HWHEEL, *bit);
		set_bit(REL_HWHEEL_HI_RES, *bit);
	}

	if ((a4->quirks & A4_2WHEEL_MOUSE_HACK_7) && usage->hid == 0x00090007)
		return -1;

	return 0;
}

static int a4_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct a4tech_sc *a4 = hid_get_drvdata(hdev);
	struct input_dev *input;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput)
		return 0;

	input = field->hidinput->input;

	if (a4->quirks & A4_2WHEEL_MOUSE_HACK_B8) {
		if (usage->type == EV_REL && usage->code == REL_WHEEL_HI_RES) {
			a4->delayed_value = value;
			return 1;
		}

		if (usage->hid == A4_WHEEL_ORIENTATION) {
			input_event(input, EV_REL, value ? REL_HWHEEL :
					REL_WHEEL, a4->delayed_value);
			input_event(input, EV_REL, value ? REL_HWHEEL_HI_RES :
					REL_WHEEL_HI_RES, a4->delayed_value * 120);
			return 1;
		}
	}

	if ((a4->quirks & A4_2WHEEL_MOUSE_HACK_7) && usage->hid == 0x00090007) {
		a4->hw_wheel = !!value;
		return 1;
	}

	if (usage->code == REL_WHEEL_HI_RES && a4->hw_wheel) {
		input_event(input, usage->type, REL_HWHEEL, value);
		input_event(input, usage->type, REL_HWHEEL_HI_RES, value * 120);
		return 1;
	}

	return 0;
}

static int a4_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct a4tech_sc *a4;
	int ret;

	a4 = devm_kzalloc(&hdev->dev, sizeof(*a4), GFP_KERNEL);
	if (a4 == NULL) {
		hid_err(hdev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}

	a4->quirks = id->driver_data;

	hid_set_drvdata(hdev, a4);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static const struct hid_device_id a4_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_WCP32PU),
		.driver_data = A4_2WHEEL_MOUSE_HACK_7 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_X5_005D),
		.driver_data = A4_2WHEEL_MOUSE_HACK_B8 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_RP_649),
		.driver_data = A4_2WHEEL_MOUSE_HACK_B8 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_NB_95),
		.driver_data = A4_2WHEEL_MOUSE_HACK_B8 },
	{ }
};
MODULE_DEVICE_TABLE(hid, a4_devices);

static struct hid_driver a4_driver = {
	.name = "a4tech",
	.id_table = a4_devices,
	.input_mapping = a4_input_mapping,
	.input_mapped = a4_input_mapped,
	.event = a4_event,
	.probe = a4_probe,
};
module_hid_driver(a4_driver);

MODULE_LICENSE("GPL");
