/*
 *  HID driver for TiVo Slide Bluetooth remote
 *
 *  Copyright (c) 2011 Jarod Wilson <jarod@redhat.com>
 *  based on the hid-topseed driver, which is in turn, based on hid-cherry...
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

#define HID_UP_TIVOVENDOR	0xffff0000
#define tivo_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))

static int tivo_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	switch (usage->hid & HID_USAGE_PAGE) {
	case HID_UP_TIVOVENDOR:
		switch (usage->hid & HID_USAGE) {
		/* TiVo button */
		case 0x3d: tivo_map_key_clear(KEY_MEDIA);	break;
		/* Live TV */
		case 0x3e: tivo_map_key_clear(KEY_TV);		break;
		/* Red thumbs down */
		case 0x41: tivo_map_key_clear(KEY_KPMINUS);	break;
		/* Green thumbs up */
		case 0x42: tivo_map_key_clear(KEY_KPPLUS);	break;
		default:
			return 0;
		}
		break;
	case HID_UP_CONSUMER:
		switch (usage->hid & HID_USAGE) {
		/* Enter/Last (default mapping: KEY_LAST) */
		case 0x083: tivo_map_key_clear(KEY_ENTER);	break;
		/* Info (default mapping: KEY_PROPS) */
		case 0x209: tivo_map_key_clear(KEY_INFO);	break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	/* This means we found a matching mapping here, else, look in the
	 * standard hid mappings in hid-input.c */
	return 1;
}

static const struct hid_device_id tivo_devices[] = {
	/* TiVo Slide Bluetooth remote, pairs with a Broadcom dongle */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_TIVO, USB_DEVICE_ID_TIVO_SLIDE_BT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_TIVO, USB_DEVICE_ID_TIVO_SLIDE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, tivo_devices);

static struct hid_driver tivo_driver = {
	.name = "tivo_slide",
	.id_table = tivo_devices,
	.input_mapping = tivo_input_mapping,
};

static int __init tivo_init(void)
{
	return hid_register_driver(&tivo_driver);
}

static void __exit tivo_exit(void)
{
	hid_unregister_driver(&tivo_driver);
}

module_init(tivo_init);
module_exit(tivo_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
