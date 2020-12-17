// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID driver for some ITE "special" devices
 * Copyright (c) 2017 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define QUIRK_TOUCHPAD_ON_OFF_REPORT		BIT(0)

static __u8 *ite_report_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if (quirks & QUIRK_TOUCHPAD_ON_OFF_REPORT) {
		/* For Acer Aspire Switch 10 SW5-012 keyboard-dock */
		if (*rsize == 188 && rdesc[162] == 0x81 && rdesc[163] == 0x02) {
			hid_info(hdev, "Fixing up Acer Sw5-012 ITE keyboard report descriptor\n");
			rdesc[163] = HID_MAIN_ITEM_RELATIVE;
		}
		/* For Acer One S1002 keyboard-dock */
		if (*rsize == 188 && rdesc[185] == 0x81 && rdesc[186] == 0x02) {
			hid_info(hdev, "Fixing up Acer S1002 ITE keyboard report descriptor\n");
			rdesc[186] = HID_MAIN_ITEM_RELATIVE;
		}
	}

	return rdesc;
}

static int ite_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit,
		int *max)
{

	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((quirks & QUIRK_TOUCHPAD_ON_OFF_REPORT) &&
	    (usage->hid & HID_USAGE_PAGE) == 0x00880000) {
		if (usage->hid == 0x00880078) {
			/* Touchpad on, userspace expects F22 for this */
			hid_map_usage_clear(hi, usage, bit, max, EV_KEY, KEY_F22);
			return 1;
		}
		if (usage->hid == 0x00880079) {
			/* Touchpad off, userspace expects F23 for this */
			hid_map_usage_clear(hi, usage, bit, max, EV_KEY, KEY_F23);
			return 1;
		}
		return -1;
	}

	return 0;
}

static int ite_event(struct hid_device *hdev, struct hid_field *field,
		     struct hid_usage *usage, __s32 value)
{
	struct input_dev *input;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput)
		return 0;

	input = field->hidinput->input;

	/*
	 * The ITE8595 always reports 0 as value for the rfkill button. Luckily
	 * it is the only button in its report, and it sends a report on
	 * release only, so receiving a report means the button was pressed.
	 */
	if (usage->hid == HID_GD_RFKILL_BTN) {
		input_event(input, EV_KEY, KEY_RFKILL, 1);
		input_sync(input);
		input_event(input, EV_KEY, KEY_RFKILL, 0);
		input_sync(input);
		return 1;
	}

	return 0;
}

static int ite_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	hid_set_drvdata(hdev, (void *)id->driver_data);

	ret = hid_open_report(hdev);
	if (ret)
		return ret;

	return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

static const struct hid_device_id ite_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ITE, USB_DEVICE_ID_ITE8595) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_258A, USB_DEVICE_ID_258A_6A88) },
	/* ITE8595 USB kbd ctlr, with Synaptics touchpad connected to it. */
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_SYNAPTICS,
		     USB_DEVICE_ID_SYNAPTICS_ACER_SWITCH5_012),
	  .driver_data = QUIRK_TOUCHPAD_ON_OFF_REPORT },
	/* ITE8910 USB kbd ctlr, with Synaptics touchpad connected to it. */
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_SYNAPTICS,
		     USB_DEVICE_ID_SYNAPTICS_ACER_ONE_S1002),
	  .driver_data = QUIRK_TOUCHPAD_ON_OFF_REPORT },
	/* ITE8910 USB kbd ctlr, with Synaptics touchpad connected to it. */
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_SYNAPTICS,
		     USB_DEVICE_ID_SYNAPTICS_ACER_ONE_S1003) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ite_devices);

static struct hid_driver ite_driver = {
	.name = "itetech",
	.id_table = ite_devices,
	.probe = ite_probe,
	.report_fixup = ite_report_fixup,
	.input_mapping = ite_input_mapping,
	.event = ite_event,
};
module_hid_driver(ite_driver);

MODULE_LICENSE("GPL");
