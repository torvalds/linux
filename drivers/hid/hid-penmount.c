/*
 *  HID driver for PenMount touchscreens
 *
 *  Copyright (c) 2014 Christian Gmeiner <christian.gmeiner <at> gmail.com>
 *
 *  based on hid-penmount copyrighted by
 *    PenMount Touch Solutions <penmount <at> seed.net.tw>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/hid.h>
#include "hid-ids.h"

static int penmount_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON) {
		if (((usage->hid - 1) & HID_USAGE) == 0) {
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			return 1;
		} else {
			return -1;
		}
	}

	return 0;
}

static const struct hid_device_id penmount_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_PENMOUNT, USB_DEVICE_ID_PENMOUNT_6000) },
	{ }
};
MODULE_DEVICE_TABLE(hid, penmount_devices);

static struct hid_driver penmount_driver = {
	.name = "hid-penmount",
	.id_table = penmount_devices,
	.input_mapping = penmount_input_mapping,
};

module_hid_driver(penmount_driver);

MODULE_AUTHOR("Christian Gmeiner <christian.gmeiner@gmail.com>");
MODULE_DESCRIPTION("PenMount HID TouchScreen driver");
MODULE_LICENSE("GPL");
