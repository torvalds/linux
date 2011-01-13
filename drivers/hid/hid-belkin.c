/*
 *  HID driver for some belkin "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define BELKIN_HIDDEV	0x01
#define BELKIN_WKBD	0x02

#define belkin_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int belkin_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER ||
			!(quirks & BELKIN_WKBD))
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x03a: belkin_map_key_clear(KEY_SOUND);		break;
	case 0x03b: belkin_map_key_clear(KEY_CAMERA);		break;
	case 0x03c: belkin_map_key_clear(KEY_DOCUMENTS);	break;
	default:
		return 0;
	}
	return 1;
}

static int belkin_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	int ret;

	hid_set_drvdata(hdev, (void *)quirks);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT |
		((quirks & BELKIN_HIDDEV) ? HID_CONNECT_HIDDEV_FORCE : 0));
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id belkin_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_BELKIN, USB_DEVICE_ID_FLIP_KVM),
		.driver_data = BELKIN_HIDDEV },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LABTEC, USB_DEVICE_ID_LABTEC_WIRELESS_KEYBOARD),
		.driver_data = BELKIN_WKBD },
	{ }
};
MODULE_DEVICE_TABLE(hid, belkin_devices);

static struct hid_driver belkin_driver = {
	.name = "belkin",
	.id_table = belkin_devices,
	.input_mapping = belkin_input_mapping,
	.probe = belkin_probe,
};

static int __init belkin_init(void)
{
	return hid_register_driver(&belkin_driver);
}

static void __exit belkin_exit(void)
{
	hid_unregister_driver(&belkin_driver);
}

module_init(belkin_init);
module_exit(belkin_exit);
MODULE_LICENSE("GPL");
