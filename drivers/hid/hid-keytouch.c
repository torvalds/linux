// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Keytouch devices not fully compliant with HID standard
 *
 *  Copyright (c) 2011 Jiri Kosina
 */

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

/* Replace the broken report descriptor of this device with rather
 * a default one */
static __u8 keytouch_fixed_rdesc[] = {
0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15,
0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
0x81, 0x01, 0x95, 0x03, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91,
0x02, 0x95, 0x05, 0x75, 0x01, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
0x26, 0xff, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2a, 0xff, 0x00, 0x81, 0x00, 0xc0
};

static __u8 *keytouch_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	hid_info(hdev, "fixing up Keytouch IEC report descriptor\n");

	rdesc = keytouch_fixed_rdesc;
	*rsize = sizeof(keytouch_fixed_rdesc);

	return rdesc;
}

static const struct hid_device_id keytouch_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_KEYTOUCH, USB_DEVICE_ID_KEYTOUCH_IEC) },
	{ }
};
MODULE_DEVICE_TABLE(hid, keytouch_devices);

static struct hid_driver keytouch_driver = {
	.name = "keytouch",
	.id_table = keytouch_devices,
	.report_fixup = keytouch_report_fixup,
};
module_hid_driver(keytouch_driver);

MODULE_DESCRIPTION("HID driver for Keytouch devices not fully compliant with HID standard");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jiri Kosina");
