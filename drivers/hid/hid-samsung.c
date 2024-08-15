// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for some samsung "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2010 Don Prince <dhprince.devel@yahoo.co.uk>
 *
 *  This driver supports several HID devices:
 *
 *  [0419:0001] Samsung IrDA remote controller (reports as Cypress USB Mouse).
 *	various hid report fixups for different variants.
 *
 *  [0419:0600] Creative Desktop Wireless 6000 keyboard/mouse combo
 *	several key mappings used from the consumer usage page
 *	deviate from the USB HUT 1.12 standard.
 */

/*
 */

#include <linux/device.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

/*
 * There are several variants for 0419:0001:
 *
 * 1. 184 byte report descriptor
 * Vendor specific report #4 has a size of 48 bit,
 * and therefore is not accepted when inspecting the descriptors.
 * As a workaround we reinterpret the report as:
 *   Variable type, count 6, size 8 bit, log. maximum 255
 * The burden to reconstruct the data is moved into user space.
 *
 * 2. 203 byte report descriptor
 * Report #4 has an array field with logical range 0..18 instead of 1..15.
 *
 * 3. 135 byte report descriptor
 * Report #4 has an array field with logical range 0..17 instead of 1..14.
 *
 * 4. 171 byte report descriptor
 * Report #3 has an array field with logical range 0..1 instead of 1..3.
 */
static inline void samsung_irda_dev_trace(struct hid_device *hdev,
		unsigned int rsize)
{
	hid_info(hdev, "fixing up Samsung IrDA %d byte report descriptor\n",
		 rsize);
}

static __u8 *samsung_irda_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize == 184 && rdesc[175] == 0x25 && rdesc[176] == 0x40 &&
			rdesc[177] == 0x75 && rdesc[178] == 0x30 &&
			rdesc[179] == 0x95 && rdesc[180] == 0x01 &&
			rdesc[182] == 0x40) {
		samsung_irda_dev_trace(hdev, 184);
		rdesc[176] = 0xff;
		rdesc[178] = 0x08;
		rdesc[180] = 0x06;
		rdesc[182] = 0x42;
	} else
	if (*rsize == 203 && rdesc[192] == 0x15 && rdesc[193] == 0x0 &&
			rdesc[194] == 0x25 && rdesc[195] == 0x12) {
		samsung_irda_dev_trace(hdev, 203);
		rdesc[193] = 0x1;
		rdesc[195] = 0xf;
	} else
	if (*rsize == 135 && rdesc[124] == 0x15 && rdesc[125] == 0x0 &&
			rdesc[126] == 0x25 && rdesc[127] == 0x11) {
		samsung_irda_dev_trace(hdev, 135);
		rdesc[125] = 0x1;
		rdesc[127] = 0xe;
	} else
	if (*rsize == 171 && rdesc[160] == 0x15 && rdesc[161] == 0x0 &&
			rdesc[162] == 0x25 && rdesc[163] == 0x01) {
		samsung_irda_dev_trace(hdev, 171);
		rdesc[161] = 0x1;
		rdesc[163] = 0x3;
	}
	return rdesc;
}

#define samsung_kbd_mouse_map_key_clear(c) \
	hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

static int samsung_kbd_mouse_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	unsigned short ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	if (1 != ifnum || HID_UP_CONSUMER != (usage->hid & HID_USAGE_PAGE))
		return 0;

	dbg_hid("samsung wireless keyboard/mouse input mapping event [0x%x]\n",
		usage->hid & HID_USAGE);

	switch (usage->hid & HID_USAGE) {
	/* report 2 */
	case 0x183: samsung_kbd_mouse_map_key_clear(KEY_MEDIA); break;
	case 0x195: samsung_kbd_mouse_map_key_clear(KEY_EMAIL);	break;
	case 0x196: samsung_kbd_mouse_map_key_clear(KEY_CALC); break;
	case 0x197: samsung_kbd_mouse_map_key_clear(KEY_COMPUTER); break;
	case 0x22b: samsung_kbd_mouse_map_key_clear(KEY_SEARCH); break;
	case 0x22c: samsung_kbd_mouse_map_key_clear(KEY_WWW); break;
	case 0x22d: samsung_kbd_mouse_map_key_clear(KEY_BACK); break;
	case 0x22e: samsung_kbd_mouse_map_key_clear(KEY_FORWARD); break;
	case 0x22f: samsung_kbd_mouse_map_key_clear(KEY_FAVORITES); break;
	case 0x230: samsung_kbd_mouse_map_key_clear(KEY_REFRESH); break;
	case 0x231: samsung_kbd_mouse_map_key_clear(KEY_STOP); break;
	default:
		return 0;
	}

	return 1;
}

static __u8 *samsung_report_fixup(struct hid_device *hdev, __u8 *rdesc,
	unsigned int *rsize)
{
	if (USB_DEVICE_ID_SAMSUNG_IR_REMOTE == hdev->product)
		rdesc = samsung_irda_report_fixup(hdev, rdesc, rsize);
	return rdesc;
}

static int samsung_input_mapping(struct hid_device *hdev, struct hid_input *hi,
	struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	int ret = 0;

	if (USB_DEVICE_ID_SAMSUNG_WIRELESS_KBD_MOUSE == hdev->product)
		ret = samsung_kbd_mouse_input_mapping(hdev,
			hi, field, usage, bit, max);

	return ret;
}

static int samsung_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;
	unsigned int cmask = HID_CONNECT_DEFAULT;

	if (!hid_is_usb(hdev))
		return -EINVAL;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	if (USB_DEVICE_ID_SAMSUNG_IR_REMOTE == hdev->product) {
		if (hdev->rsize == 184) {
			/* disable hidinput, force hiddev */
			cmask = (cmask & ~HID_CONNECT_HIDINPUT) |
				HID_CONNECT_HIDDEV_FORCE;
		}
	}

	ret = hid_hw_start(hdev, cmask);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id samsung_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAMSUNG, USB_DEVICE_ID_SAMSUNG_IR_REMOTE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SAMSUNG, USB_DEVICE_ID_SAMSUNG_WIRELESS_KBD_MOUSE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, samsung_devices);

static struct hid_driver samsung_driver = {
	.name = "samsung",
	.id_table = samsung_devices,
	.report_fixup = samsung_report_fixup,
	.input_mapping = samsung_input_mapping,
	.probe = samsung_probe,
};
module_hid_driver(samsung_driver);

MODULE_LICENSE("GPL");
