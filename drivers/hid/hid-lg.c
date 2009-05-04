/*
 *  HID driver for some logitech "special" devices
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
#include "hid-lg.h"

#define LG_RDESC		0x001
#define LG_BAD_RELATIVE_KEYS	0x002
#define LG_DUPLICATE_USAGES	0x004
#define LG_EXPANDED_KEYMAP	0x010
#define LG_IGNORE_DOUBLED_WHEEL	0x020
#define LG_WIRELESS		0x040
#define LG_INVERT_HWHEEL	0x080
#define LG_NOGET		0x100
#define LG_FF			0x200
#define LG_FF2			0x400

/*
 * Certain Logitech keyboards send in report #3 keys which are far
 * above the logical maximum described in descriptor. This extends
 * the original value of 0x28c of logical maximum to 0x104d
 */
static void lg_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int rsize)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((quirks & LG_RDESC) && rsize >= 90 && rdesc[83] == 0x26 &&
			rdesc[84] == 0x8c && rdesc[85] == 0x02) {
		dev_info(&hdev->dev, "fixing up Logitech keyboard report "
				"descriptor\n");
		rdesc[84] = rdesc[89] = 0x4d;
		rdesc[85] = rdesc[90] = 0x10;
	}
}

#define lg_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
		EV_KEY, (c))

static int lg_ultrax_remote_mapping(struct hid_input *hi,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	set_bit(EV_REP, hi->input->evbit);
	switch (usage->hid & HID_USAGE) {
	/* Reported on Logitech Ultra X Media Remote */
	case 0x004: lg_map_key_clear(KEY_AGAIN);	break;
	case 0x00d: lg_map_key_clear(KEY_HOME);		break;
	case 0x024: lg_map_key_clear(KEY_SHUFFLE);	break;
	case 0x025: lg_map_key_clear(KEY_TV);		break;
	case 0x026: lg_map_key_clear(KEY_MENU);		break;
	case 0x031: lg_map_key_clear(KEY_AUDIO);	break;
	case 0x032: lg_map_key_clear(KEY_TEXT);		break;
	case 0x033: lg_map_key_clear(KEY_LAST);		break;
	case 0x047: lg_map_key_clear(KEY_MP3);		break;
	case 0x048: lg_map_key_clear(KEY_DVD);		break;
	case 0x049: lg_map_key_clear(KEY_MEDIA);	break;
	case 0x04a: lg_map_key_clear(KEY_VIDEO);	break;
	case 0x04b: lg_map_key_clear(KEY_ANGLE);	break;
	case 0x04c: lg_map_key_clear(KEY_LANGUAGE);	break;
	case 0x04d: lg_map_key_clear(KEY_SUBTITLE);	break;
	case 0x051: lg_map_key_clear(KEY_RED);		break;
	case 0x052: lg_map_key_clear(KEY_CLOSE);	break;

	default:
		return 0;
	}
	return 1;
}

static int lg_wireless_mapping(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x1001: lg_map_key_clear(KEY_MESSENGER);		break;
	case 0x1003: lg_map_key_clear(KEY_SOUND);		break;
	case 0x1004: lg_map_key_clear(KEY_VIDEO);		break;
	case 0x1005: lg_map_key_clear(KEY_AUDIO);		break;
	case 0x100a: lg_map_key_clear(KEY_DOCUMENTS);		break;
	case 0x1011: lg_map_key_clear(KEY_PREVIOUSSONG);	break;
	case 0x1012: lg_map_key_clear(KEY_NEXTSONG);		break;
	case 0x1013: lg_map_key_clear(KEY_CAMERA);		break;
	case 0x1014: lg_map_key_clear(KEY_MESSENGER);		break;
	case 0x1015: lg_map_key_clear(KEY_RECORD);		break;
	case 0x1016: lg_map_key_clear(KEY_PLAYER);		break;
	case 0x1017: lg_map_key_clear(KEY_EJECTCD);		break;
	case 0x1018: lg_map_key_clear(KEY_MEDIA);		break;
	case 0x1019: lg_map_key_clear(KEY_PROG1);		break;
	case 0x101a: lg_map_key_clear(KEY_PROG2);		break;
	case 0x101b: lg_map_key_clear(KEY_PROG3);		break;
	case 0x101f: lg_map_key_clear(KEY_ZOOMIN);		break;
	case 0x1020: lg_map_key_clear(KEY_ZOOMOUT);		break;
	case 0x1021: lg_map_key_clear(KEY_ZOOMRESET);		break;
	case 0x1023: lg_map_key_clear(KEY_CLOSE);		break;
	case 0x1027: lg_map_key_clear(KEY_MENU);		break;
	/* this one is marked as 'Rotate' */
	case 0x1028: lg_map_key_clear(KEY_ANGLE);		break;
	case 0x1029: lg_map_key_clear(KEY_SHUFFLE);		break;
	case 0x102a: lg_map_key_clear(KEY_BACK);		break;
	case 0x102b: lg_map_key_clear(KEY_CYCLEWINDOWS);	break;
	case 0x1041: lg_map_key_clear(KEY_BATTERY);		break;
	case 0x1042: lg_map_key_clear(KEY_WORDPROCESSOR);	break;
	case 0x1043: lg_map_key_clear(KEY_SPREADSHEET);		break;
	case 0x1044: lg_map_key_clear(KEY_PRESENTATION);	break;
	case 0x1045: lg_map_key_clear(KEY_UNDO);		break;
	case 0x1046: lg_map_key_clear(KEY_REDO);		break;
	case 0x1047: lg_map_key_clear(KEY_PRINT);		break;
	case 0x1048: lg_map_key_clear(KEY_SAVE);		break;
	case 0x1049: lg_map_key_clear(KEY_PROG1);		break;
	case 0x104a: lg_map_key_clear(KEY_PROG2);		break;
	case 0x104b: lg_map_key_clear(KEY_PROG3);		break;
	case 0x104c: lg_map_key_clear(KEY_PROG4);		break;

	default:
		return 0;
	}
	return 1;
}

static int lg_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	/* extended mapping for certain Logitech hardware (Logitech cordless
	   desktop LX500) */
	static const u8 e_keymap[] = {
		  0,216,  0,213,175,156,  0,  0,  0,  0,
		144,  0,  0,  0,  0,  0,  0,  0,  0,212,
		174,167,152,161,112,  0,  0,  0,154,  0,
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  0,  0,  0,  0,183,184,185,186,187,
		188,189,190,191,192,193,194,  0,  0,  0
	};
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);
	unsigned int hid = usage->hid;

	if (hdev->product == USB_DEVICE_ID_LOGITECH_RECEIVER &&
			lg_ultrax_remote_mapping(hi, usage, bit, max))
		return 1;

	if ((quirks & LG_WIRELESS) && lg_wireless_mapping(hi, usage, bit, max))
		return 1;

	if ((hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
		return 0;

	hid &= HID_USAGE;

	/* Special handling for Logitech Cordless Desktop */
	if (field->application == HID_GD_MOUSE) {
		if ((quirks & LG_IGNORE_DOUBLED_WHEEL) &&
				(hid == 7 || hid == 8))
			return -1;
	} else {
		if ((quirks & LG_EXPANDED_KEYMAP) &&
				hid < ARRAY_SIZE(e_keymap) &&
				e_keymap[hid] != 0) {
			hid_map_usage(hi, usage, bit, max, EV_KEY,
					e_keymap[hid]);
			return 1;
		}
	}

	return 0;
}

static int lg_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((quirks & LG_BAD_RELATIVE_KEYS) && usage->type == EV_KEY &&
			(field->flags & HID_MAIN_ITEM_RELATIVE))
		field->flags &= ~HID_MAIN_ITEM_RELATIVE;

	if ((quirks & LG_DUPLICATE_USAGES) && (usage->type == EV_KEY ||
			 usage->type == EV_REL || usage->type == EV_ABS))
		clear_bit(usage->code, *bit);

	return 0;
}

static int lg_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((quirks & LG_INVERT_HWHEEL) && usage->code == REL_HWHEEL) {
		input_event(field->hidinput->input, usage->type, usage->code,
				-value);
		return 1;
	}

	return 0;
}

static int lg_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	unsigned int connect_mask = HID_CONNECT_DEFAULT;
	int ret;

	hid_set_drvdata(hdev, (void *)quirks);

	if (quirks & LG_NOGET)
		hdev->quirks |= HID_QUIRK_NOGET;

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "parse failed\n");
		goto err_free;
	}

	if (quirks & (LG_FF | LG_FF2))
		connect_mask &= ~HID_CONNECT_FF;

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto err_free;
	}

	if (quirks & LG_FF)
		lgff_init(hdev);
	if (quirks & LG_FF2)
		lg2ff_init(hdev);

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id lg_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_MX3000_RECEIVER),
		.driver_data = LG_RDESC | LG_WIRELESS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_S510_RECEIVER),
		.driver_data = LG_RDESC | LG_WIRELESS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_S510_RECEIVER_2),
		.driver_data = LG_RDESC | LG_WIRELESS },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RECEIVER),
		.driver_data = LG_BAD_RELATIVE_KEYS },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_DESKTOP),
		.driver_data = LG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_EDGE),
		.driver_data = LG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_MINI),
		.driver_data = LG_DUPLICATE_USAGES },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_ELITE_KBD),
		.driver_data = LG_IGNORE_DOUBLED_WHEEL | LG_EXPANDED_KEYMAP },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_CORDLESS_DESKTOP_LX500),
		.driver_data = LG_IGNORE_DOUBLED_WHEEL | LG_EXPANDED_KEYMAP },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_EXTREME_3D),
		.driver_data = LG_NOGET },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WHEEL),
		.driver_data = LG_NOGET | LG_FF },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD2_2),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WINGMAN_F3D),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_FORCE3D_PRO),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_MOMO_WHEEL),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD2),
		.driver_data = LG_FF2 },
	{ }
};
MODULE_DEVICE_TABLE(hid, lg_devices);

static struct hid_driver lg_driver = {
	.name = "logitech",
	.id_table = lg_devices,
	.report_fixup = lg_report_fixup,
	.input_mapping = lg_input_mapping,
	.input_mapped = lg_input_mapped,
	.event = lg_event,
	.probe = lg_probe,
};

static int lg_init(void)
{
	return hid_register_driver(&lg_driver);
}

static void lg_exit(void)
{
	hid_unregister_driver(&lg_driver);
}

module_init(lg_init);
module_exit(lg_exit);
MODULE_LICENSE("GPL");
