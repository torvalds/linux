/*
 *  Jabra USB HID Driver
 *
 *  Copyright (c) 2017 Niels Skou Olsen <nolsen@jabra.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define HID_UP_VENDOR_DEFINED_MIN	0xff000000
#define HID_UP_VENDOR_DEFINED_MAX	0xffff0000

static int jabra_input_mapping(struct hid_device *hdev,
			       struct hid_input *hi,
			       struct hid_field *field,
			       struct hid_usage *usage,
			       unsigned long **bit, int *max)
{
	int is_vendor_defined =
		((usage->hid & HID_USAGE_PAGE) >= HID_UP_VENDOR_DEFINED_MIN &&
		 (usage->hid & HID_USAGE_PAGE) <= HID_UP_VENDOR_DEFINED_MAX);

	dbg_hid("hid=0x%08x appl=0x%08x coll_idx=0x%02x usage_idx=0x%02x: %s\n",
		usage->hid,
		field->application,
		usage->collection_index,
		usage->usage_index,
		is_vendor_defined ? "ignored" : "defaulted");

	/* Ignore vendor defined usages, default map standard usages */
	return is_vendor_defined ? -1 : 0;
}

static const struct hid_device_id jabra_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_JABRA, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, jabra_devices);

static struct hid_driver jabra_driver = {
	.name = "jabra",
	.id_table = jabra_devices,
	.input_mapping = jabra_input_mapping,
};
module_hid_driver(jabra_driver);

MODULE_AUTHOR("Niels Skou Olsen <nolsen@jabra.com>");
MODULE_DESCRIPTION("Jabra USB HID Driver");
MODULE_LICENSE("GPL");
