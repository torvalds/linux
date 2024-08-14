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
	if (*rsize == 184 && !memcmp(&rdesc[175], "\x25\x40\x75\x30\x95\x01", 6) &&
			rdesc[182] == 0x40) {
		samsung_irda_dev_trace(hdev, 184);
		rdesc[176] = 0xff;
		rdesc[178] = 0x08;
		rdesc[180] = 0x06;
		rdesc[182] = 0x42;
	} else if (*rsize == 203 && !memcmp(&rdesc[192], "\x15\x00\x25\x12", 4)) {
		samsung_irda_dev_trace(hdev, 203);
		rdesc[193] = 0x01;
		rdesc[195] = 0x0f;
	} else if (*rsize == 135 && !memcmp(&rdesc[124], "\x15\x00\x25\x11", 4)) {
		samsung_irda_dev_trace(hdev, 135);
		rdesc[125] = 0x01;
		rdesc[127] = 0x0e;
	} else if (*rsize == 171 && !memcmp(&rdesc[160], "\x15\x00\x25\x01", 4)) {
		samsung_irda_dev_trace(hdev, 171);
		rdesc[161] = 0x01;
		rdesc[163] = 0x03;
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

	if (ifnum != 1 || HID_UP_CONSUMER != (usage->hid & HID_USAGE_PAGE))
		return 0;

	dbg_hid("samsung wireless keyboard/mouse input mapping event [0x%x]\n",
		usage->hid & HID_USAGE);

	switch (usage->hid & HID_USAGE) {
	/* report 2 */
	case 0x183:
		samsung_kbd_mouse_map_key_clear(KEY_MEDIA);
		break;
	case 0x195:
		samsung_kbd_mouse_map_key_clear(KEY_EMAIL);
		break;
	case 0x196:
		samsung_kbd_mouse_map_key_clear(KEY_CALC);
		break;
	case 0x197:
		samsung_kbd_mouse_map_key_clear(KEY_COMPUTER);
		break;
	case 0x22b:
		samsung_kbd_mouse_map_key_clear(KEY_SEARCH);
		break;
	case 0x22c:
		samsung_kbd_mouse_map_key_clear(KEY_WWW);
		break;
	case 0x22d:
		samsung_kbd_mouse_map_key_clear(KEY_BACK);
		break;
	case 0x22e:
		samsung_kbd_mouse_map_key_clear(KEY_FORWARD);
		break;
	case 0x22f:
		samsung_kbd_mouse_map_key_clear(KEY_FAVORITES);
		break;
	case 0x230:
		samsung_kbd_mouse_map_key_clear(KEY_REFRESH);
		break;
	case 0x231:
		samsung_kbd_mouse_map_key_clear(KEY_STOP);
		break;
	default:
		return 0;
	}

	return 1;
}

static int samsung_kbd_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	if (!(HID_UP_CONSUMER == (usage->hid & HID_USAGE_PAGE) ||
			HID_UP_KEYBOARD == (usage->hid & HID_USAGE_PAGE)))
		return 0;

	dbg_hid("samsung wireless keyboard input mapping event [0x%x]\n",
		usage->hid & HID_USAGE);

	if (HID_UP_KEYBOARD == (usage->hid & HID_USAGE_PAGE)) {
		set_bit(EV_REP, hi->input->evbit);
		switch (usage->hid & HID_USAGE) {
		case 0x32:
			samsung_kbd_mouse_map_key_clear(KEY_BACKSLASH);
			break;
		case 0x64:
			samsung_kbd_mouse_map_key_clear(KEY_102ND);
			break;
		/* Only for BR keyboard */
		case 0x87:
			samsung_kbd_mouse_map_key_clear(KEY_RO);
			break;
		default:
			return 0;
		}
	}

	if (HID_UP_CONSUMER == (usage->hid & HID_USAGE_PAGE)) {
		switch (usage->hid & HID_USAGE) {
		/* report 2 */
		/* MENU */
		case 0x040:
			samsung_kbd_mouse_map_key_clear(KEY_MENU);
			break;
		case 0x18a:
			samsung_kbd_mouse_map_key_clear(KEY_MAIL);
			break;
		case 0x196:
			samsung_kbd_mouse_map_key_clear(KEY_WWW);
			break;
		case 0x19e:
			samsung_kbd_mouse_map_key_clear(KEY_SCREENLOCK);
			break;
		case 0x221:
			samsung_kbd_mouse_map_key_clear(KEY_SEARCH);
			break;
		case 0x223:
			samsung_kbd_mouse_map_key_clear(KEY_HOMEPAGE);
			break;
		/* Smtart Voice Key */
		case 0x300:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY13);
			break;
		/* RECENTAPPS */
		case 0x301:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY1);
			break;
		/* APPLICATION */
		case 0x302:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY2);
			break;
		/* Voice search */
		case 0x305:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY4);
			break;
		/* QPANEL on/off */
		case 0x306:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY5);
			break;
		/* SIP on/off */
		case 0x307:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY3);
			break;
		/* LANG */
		case 0x308:
			samsung_kbd_mouse_map_key_clear(KEY_LANGUAGE);
			break;
		case 0x30a:
			samsung_kbd_mouse_map_key_clear(KEY_BRIGHTNESSDOWN);
			break;
		case 0x30b:
			samsung_kbd_mouse_map_key_clear(KEY_BRIGHTNESSUP);
			break;
		default:
			return 0;
		}
	}

	return 1;
}

static int samsung_gamepad_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	if (!(HID_UP_BUTTON == (usage->hid & HID_USAGE_PAGE) ||
			HID_UP_CONSUMER == (usage->hid & HID_USAGE_PAGE)))
		return 0;

	dbg_hid("samsung wireless gamepad input mapping event [0x%x], %ld, %ld, [0x%x]\n",
		usage->hid & HID_USAGE, hi->input->evbit[0], hi->input->absbit[0], usage->hid & HID_USAGE_PAGE);

	if (HID_UP_BUTTON == (usage->hid & HID_USAGE_PAGE)) {
		switch (usage->hid & HID_USAGE) {
		case 0x01:
			samsung_kbd_mouse_map_key_clear(BTN_A);
			break;
		case 0x02:
			samsung_kbd_mouse_map_key_clear(BTN_B);
			break;
		case 0x03:
			samsung_kbd_mouse_map_key_clear(BTN_C);
			break;
		case 0x04:
			samsung_kbd_mouse_map_key_clear(BTN_X);
			break;
		case 0x05:
			samsung_kbd_mouse_map_key_clear(BTN_Y);
			break;
		case 0x06:
			samsung_kbd_mouse_map_key_clear(BTN_Z);
			break;
		case 0x07:
			samsung_kbd_mouse_map_key_clear(BTN_TL);
			break;
		case 0x08:
			samsung_kbd_mouse_map_key_clear(BTN_TR);
			break;
		case 0x09:
			samsung_kbd_mouse_map_key_clear(BTN_TL2);
			break;
		case 0x0a:
			samsung_kbd_mouse_map_key_clear(BTN_TR2);
			break;
		case 0x0b:
			samsung_kbd_mouse_map_key_clear(BTN_SELECT);
			break;
		case 0x0c:
			samsung_kbd_mouse_map_key_clear(BTN_START);
			break;
		case 0x0d:
			samsung_kbd_mouse_map_key_clear(BTN_MODE);
			break;
		case 0x0e:
			samsung_kbd_mouse_map_key_clear(BTN_THUMBL);
			break;
		case 0x0f:
			samsung_kbd_mouse_map_key_clear(BTN_THUMBR);
			break;
		case 0x10:
			samsung_kbd_mouse_map_key_clear(0x13f);
			break;
		default:
			return 0;
		}
	}

	if (HID_UP_CONSUMER == (usage->hid & HID_USAGE_PAGE)) {
		switch (usage->hid & HID_USAGE) {
		case 0x040:
			samsung_kbd_mouse_map_key_clear(KEY_MENU);
			break;
		case 0x223:
			samsung_kbd_mouse_map_key_clear(KEY_HOMEPAGE);
			break;
		case 0x224:
			samsung_kbd_mouse_map_key_clear(KEY_BACK);
			break;

		/* Screen Capture */
		case 0x303:
			samsung_kbd_mouse_map_key_clear(KEY_SYSRQ);
			break;

		default:
			return 0;
		}
	}

	return 1;
}

static int samsung_actionmouse_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{

	dbg_hid("samsung wireless actionmouse input mapping event [0x%x], [0x%x], %ld, %ld, [0x%x]\n",
			usage->hid, usage->hid & HID_USAGE, hi->input->evbit[0], hi->input->absbit[0],
			usage->hid & HID_USAGE_PAGE);

	if (((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER) && ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON))
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x301:
		samsung_kbd_mouse_map_key_clear(254);
		break;
	default:
		return 0;
	}

	return 1;
}

static int samsung_universal_kbd_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	if (!(HID_UP_CONSUMER == (usage->hid & HID_USAGE_PAGE) ||
			HID_UP_KEYBOARD == (usage->hid & HID_USAGE_PAGE)))
		return 0;

	dbg_hid("samsung wireless keyboard input mapping event [0x%x]\n",
		usage->hid & HID_USAGE);

	if (HID_UP_KEYBOARD == (usage->hid & HID_USAGE_PAGE)) {
		set_bit(EV_REP, hi->input->evbit);
		switch (usage->hid & HID_USAGE) {
		case 0x32:
			samsung_kbd_mouse_map_key_clear(KEY_BACKSLASH);
			break;
		case 0x64:
			samsung_kbd_mouse_map_key_clear(KEY_102ND);
			break;
		/* Only for BR keyboard */
		case 0x87:
			samsung_kbd_mouse_map_key_clear(KEY_RO);
			break;
		default:
			return 0;
		}
	}

	if (HID_UP_CONSUMER == (usage->hid & HID_USAGE_PAGE)) {
		switch (usage->hid & HID_USAGE) {
		/* report 2 */
		/* MENU */
		case 0x040:
			samsung_kbd_mouse_map_key_clear(KEY_MENU);
			break;
		case 0x18a:
			samsung_kbd_mouse_map_key_clear(KEY_MAIL);
			break;
		case 0x196:
			samsung_kbd_mouse_map_key_clear(KEY_WWW);
			break;
		case 0x19e:
			samsung_kbd_mouse_map_key_clear(KEY_SCREENLOCK);
			break;
		case 0x221:
			samsung_kbd_mouse_map_key_clear(KEY_SEARCH);
			break;
		case 0x223:
			samsung_kbd_mouse_map_key_clear(KEY_HOMEPAGE);
			break;
		/* RECENTAPPS */
		case 0x301:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY1);
			break;
		/* APPLICATION */
		case 0x302:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY2);
			break;
		/* Voice search */
		case 0x305:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY4);
			break;
		/* QPANEL on/off */
		case 0x306:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY5);
			break;
		/* SIP on/off */
		case 0x307:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY3);
			break;
		/* LANG */
		case 0x308:
			samsung_kbd_mouse_map_key_clear(KEY_LANGUAGE);
			break;
		case 0x30a:
			samsung_kbd_mouse_map_key_clear(KEY_BRIGHTNESSDOWN);
			break;
		case 0x070:
			samsung_kbd_mouse_map_key_clear(KEY_BRIGHTNESSDOWN);
			break;
		case 0x30b:
			samsung_kbd_mouse_map_key_clear(KEY_BRIGHTNESSUP);
			break;
		case 0x06f:
			samsung_kbd_mouse_map_key_clear(KEY_BRIGHTNESSUP);
			break;
		/* S-Finder */
		case 0x304:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY7);
			break;
		/* Screen Capture */
		case 0x303:
			samsung_kbd_mouse_map_key_clear(KEY_SYSRQ);
			break;
		/* Multi Window */
		case 0x309:
			samsung_kbd_mouse_map_key_clear(BTN_TRIGGER_HAPPY9);
			break;
		/* HotKey App 1 */
		case 0x071:
			samsung_kbd_mouse_map_key_clear(0x2f5);
			break;
		/* HotKey App 2 */
		case 0x072:
			samsung_kbd_mouse_map_key_clear(0x2f6);
			break;
		/* HotKey App 3 */
		case 0x073:
			samsung_kbd_mouse_map_key_clear(0x2f7);
			break;
		/* Dex */
		case 0x06e:
			samsung_kbd_mouse_map_key_clear(0x2bd);
			break;
		default:
			return 0;
		}
	}

	return 1;
}

static __u8 *samsung_report_fixup(struct hid_device *hdev, __u8 *rdesc,
	unsigned int *rsize)
{
	if (hdev->product == USB_DEVICE_ID_SAMSUNG_IR_REMOTE && hid_is_usb(hdev))
		rdesc = samsung_irda_report_fixup(hdev, rdesc, rsize);
	return rdesc;
}

static int samsung_input_mapping(struct hid_device *hdev, struct hid_input *hi,
	struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	int ret = 0;

	if (hdev->product == USB_DEVICE_ID_SAMSUNG_WIRELESS_KBD_MOUSE && hid_is_usb(hdev))
		ret = samsung_kbd_mouse_input_mapping(hdev,
			hi, field, usage, bit, max);
	else if (hdev->product == USB_DEVICE_ID_SAMSUNG_WIRELESS_KBD)
		ret = samsung_kbd_input_mapping(hdev,
			hi, field, usage, bit, max);
	else if (hdev->product == USB_DEVICE_ID_SAMSUNG_WIRELESS_GAMEPAD)
		ret = samsung_gamepad_input_mapping(hdev,
			hi, field, usage, bit, max);
	else if (hdev->product == USB_DEVICE_ID_SAMSUNG_WIRELESS_ACTIONMOUSE)
		ret = samsung_actionmouse_input_mapping(hdev,
			hi, field, usage, bit, max);
	else if (hdev->product == USB_DEVICE_ID_SAMSUNG_WIRELESS_UNIVERSAL_KBD)
		ret = samsung_universal_kbd_input_mapping(hdev,
			hi, field, usage, bit, max);
	else if (hdev->product == USB_DEVICE_ID_SAMSUNG_WIRELESS_MULTI_HOGP_KBD)
		ret = samsung_universal_kbd_input_mapping(hdev,
			hi, field, usage, bit, max);

	return ret;
}

static int samsung_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;
	unsigned int cmask = HID_CONNECT_DEFAULT;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	if (hdev->product == USB_DEVICE_ID_SAMSUNG_IR_REMOTE) {
		if (!hid_is_usb(hdev)) {
			ret = -EINVAL;
			goto err_free;
		}
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
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SAMSUNG_ELECTRONICS, USB_DEVICE_ID_SAMSUNG_WIRELESS_KBD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SAMSUNG_ELECTRONICS, USB_DEVICE_ID_SAMSUNG_WIRELESS_GAMEPAD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SAMSUNG_ELECTRONICS, USB_DEVICE_ID_SAMSUNG_WIRELESS_ACTIONMOUSE) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SAMSUNG_ELECTRONICS, USB_DEVICE_ID_SAMSUNG_WIRELESS_UNIVERSAL_KBD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SAMSUNG_ELECTRONICS, USB_DEVICE_ID_SAMSUNG_WIRELESS_MULTI_HOGP_KBD) },
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

MODULE_DESCRIPTION("HID driver for some samsung \"special\" devices");
MODULE_LICENSE("GPL");
