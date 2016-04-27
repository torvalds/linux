/*
 *  HID driver for Aureal Cy se W-01RN USB_V3.1 devices
 *
 *  Copyright (c) 2010 Franco Catrin <fcatrin@gmail.com>
 *  Copyright (c) 2010 Ben Cropley <bcropley@internode.on.net>
 *
 *  Based on HID sunplus driver by
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby
 */
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

static __u8 *aureal_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize >= 54 && rdesc[52] == 0x25 && rdesc[53] == 0x01) {
		dev_info(&hdev->dev, "fixing Aureal Cy se W-01RN USB_V3.1 report descriptor.\n");
		rdesc[53] = 0x65;
	}
	return rdesc;
}

static const struct hid_device_id aureal_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_AUREAL, USB_DEVICE_ID_AUREAL_W01RN) },
	{ }
};
MODULE_DEVICE_TABLE(hid, aureal_devices);

static struct hid_driver aureal_driver = {
	.name = "aureal",
	.id_table = aureal_devices,
	.report_fixup = aureal_report_fixup,
};
module_hid_driver(aureal_driver);

MODULE_LICENSE("GPL");
