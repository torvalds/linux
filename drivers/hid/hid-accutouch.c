// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for Elo Accutouch touchscreens
 *
 * Copyright (c) 2016, Collabora Ltd.
 * Copyright (c) 2016, General Electric Company
 *
 * based on hid-penmount.c
 *  Copyright (c) 2014 Christian Gmeiner <christian.gmeiner <at> gmail.com>
 */

/*
 */

#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

static int accutouch_input_mapping(struct hid_device *hdev,
				   struct hid_input *hi,
				   struct hid_field *field,
				   struct hid_usage *usage,
				   unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON) {
		hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
		return 1;
	}

	return 0;
}

static const struct hid_device_id accutouch_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELO, USB_DEVICE_ID_ELO_ACCUTOUCH_2216) },
	{ }
};
MODULE_DEVICE_TABLE(hid, accutouch_devices);

static struct hid_driver accutouch_driver = {
	.name = "hid-accutouch",
	.id_table = accutouch_devices,
	.input_mapping = accutouch_input_mapping,
};

module_hid_driver(accutouch_driver);

MODULE_AUTHOR("Martyn Welch <martyn.welch@collabora.co.uk");
MODULE_DESCRIPTION("Elo Accutouch HID TouchScreen driver");
MODULE_LICENSE("GPL");
