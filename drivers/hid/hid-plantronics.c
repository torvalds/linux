/*
 *  Plantronics USB HID Driver
 *
 *  Copyright (c) 2014 JD Cole <jd.cole@plantronics.com>
 *  Copyright (c) 2014 Terry Junge <terry.junge@plantronics.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "hid-ids.h"

#include <linux/hid.h>
#include <linux/module.h>

static int plantronics_input_mapping(struct hid_device *hdev,
				     struct hid_input *hi,
				     struct hid_field *field,
				     struct hid_usage *usage,
				     unsigned long **bit, int *max)
{
	if (field->application == HID_CP_CONSUMERCONTROL
	    && (usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER) {
		hid_dbg(hdev, "usage: %08x (appl: %08x) - defaulted\n",
			 usage->hid, field->application);
		return 0;
	}

	hid_dbg(hdev, "usage: %08x (appl: %08x) - ignored\n",
		usage->hid, field->application);

	return -1;
}

static const struct hid_device_id plantronics_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, plantronics_devices);

static struct hid_driver plantronics_driver = {
	.name = "plantronics",
	.id_table = plantronics_devices,
	.input_mapping = plantronics_input_mapping,
};
module_hid_driver(plantronics_driver);

MODULE_AUTHOR("JD Cole <jd.cole@plantronics.com>");
MODULE_AUTHOR("Terry Junge <terry.junge@plantronics.com>");
MODULE_DESCRIPTION("Plantronics USB HID Driver");
MODULE_LICENSE("GPL");
