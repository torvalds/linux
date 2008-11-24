/*
 *  HID driver for some petalynx "special" devices
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

/* Petalynx Maxter Remote has maximum for consumer page set too low */
static void pl_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int rsize)
{
	if (rsize >= 60 && rdesc[39] == 0x2a && rdesc[40] == 0xf5 &&
			rdesc[41] == 0x00 && rdesc[59] == 0x26 &&
			rdesc[60] == 0xf9 && rdesc[61] == 0x00) {
		dev_info(&hdev->dev, "fixing up Petalynx Maxter Remote report "
				"descriptor\n");
		rdesc[60] = 0xfa;
		rdesc[40] = 0xfa;
	}
}

#define pl_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int pl_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_LOGIVENDOR) {
		switch (usage->hid & HID_USAGE) {
		case 0x05a: pl_map_key_clear(KEY_TEXT);		break;
		case 0x05b: pl_map_key_clear(KEY_RED);		break;
		case 0x05c: pl_map_key_clear(KEY_GREEN);	break;
		case 0x05d: pl_map_key_clear(KEY_YELLOW);	break;
		case 0x05e: pl_map_key_clear(KEY_BLUE);		break;
		default:
			return 0;
		}
		return 1;
	}

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER) {
		switch (usage->hid & HID_USAGE) {
		case 0x0f6: pl_map_key_clear(KEY_NEXT);		break;
		case 0x0fa: pl_map_key_clear(KEY_BACK);		break;
		default:
			return 0;
		}
		return 1;
	}

	return 0;
}

static int pl_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	hdev->quirks |= HID_QUIRK_NOGET;

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id pl_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_PETALYNX, USB_DEVICE_ID_PETALYNX_MAXTER_REMOTE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, pl_devices);

static struct hid_driver pl_driver = {
	.name = "petalynx",
	.id_table = pl_devices,
	.report_fixup = pl_report_fixup,
	.input_mapping = pl_input_mapping,
	.probe = pl_probe,
};

static int pl_init(void)
{
	return hid_register_driver(&pl_driver);
}

static void pl_exit(void)
{
	hid_unregister_driver(&pl_driver);
}

module_init(pl_init);
module_exit(pl_exit);
MODULE_LICENSE("GPL");

HID_COMPAT_LOAD_DRIVER(petalynx);
