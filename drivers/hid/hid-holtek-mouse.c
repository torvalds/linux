// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for Holtek gaming mice
 * Copyright (c) 2013 Christian Ohm
 * Heavily inspired by various other HID drivers that adjust the report
 * descriptor.
*/

/*
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "hid-ids.h"

/*
 * The report descriptor of some Holtek based gaming mice specifies an
 * excessively large number of consumer usages (2^15), which is more than
 * HID_MAX_USAGES. This prevents proper parsing of the report descriptor.
 *
 * This driver fixes the report descriptor for:
 * - USB ID 04d9:a067, sold as Sharkoon Drakonia and Perixx MX-2000
 * - USB ID 04d9:a04a, sold as Tracer Sniper TRM-503, NOVA Gaming Slider X200
 *   and Zalman ZM-GM1
 * - USB ID 04d9:a081, sold as SHARKOON DarkGlider Gaming mouse
 * - USB ID 04d9:a072, sold as LEETGION Hellion Gaming Mouse
 * - USB ID 04d9:a0c2, sold as ETEKCITY Scroll T-140 Gaming Mouse
 */

static const __u8 *holtek_mouse_report_fixup(struct hid_device *hdev,
		__u8 *rdesc, unsigned int *rsize)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		/* Change usage maximum and logical maximum from 0x7fff to
		 * 0x2fff, so they don't exceed HID_MAX_USAGES */
		switch (hdev->product) {
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A067:
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A072:
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A0C2:
			if (*rsize >= 122 && rdesc[115] == 0xff && rdesc[116] == 0x7f
					&& rdesc[120] == 0xff && rdesc[121] == 0x7f) {
				hid_info(hdev, "Fixing up report descriptor\n");
				rdesc[116] = rdesc[121] = 0x2f;
			}
			break;
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A04A:
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A070:
		case USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A081:
			if (*rsize >= 113 && rdesc[106] == 0xff && rdesc[107] == 0x7f
					&& rdesc[111] == 0xff && rdesc[112] == 0x7f) {
				hid_info(hdev, "Fixing up report descriptor\n");
				rdesc[107] = rdesc[112] = 0x2f;
			}
			break;
		}

	}
	return rdesc;
}

static int holtek_mouse_probe(struct hid_device *hdev,
			      const struct hid_device_id *id)
{
	int ret;

	if (!hid_is_usb(hdev))
		return -EINVAL;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct hid_device_id holtek_mouse_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A067) },
        { HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A070) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A04A) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A072) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A081) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_MOUSE_A0C2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, holtek_mouse_devices);

static struct hid_driver holtek_mouse_driver = {
	.name = "holtek_mouse",
	.id_table = holtek_mouse_devices,
	.report_fixup = holtek_mouse_report_fixup,
	.probe = holtek_mouse_probe,
};

module_hid_driver(holtek_mouse_driver);
MODULE_DESCRIPTION("HID driver for Holtek gaming mice");
MODULE_LICENSE("GPL");
