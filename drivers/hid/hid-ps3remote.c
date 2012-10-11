/*
 * HID driver for Sony PS3 BD Remote Control
 *
 * Copyright (c) 2012 David Dillow <dave@thedillows.org>
 * Based on a blend of the bluez fakehid user-space code by Marcel Holtmann
 * and other kernel HID drivers.
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* NOTE: in order for the Sony PS3 BD Remote Control to be found by
 * a Bluetooth host, the key combination Start+Enter has to be kept pressed
 * for about 7 seconds with the Bluetooth Host Controller in discovering mode.
 *
 * There will be no PIN request from the device.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

static __u8 ps3remote_rdesc[] = {
	0x05, 0x01,          /* GUsagePage Generic Desktop */
	0x09, 0x05,          /* LUsage 0x05 [Game Pad] */
	0xA1, 0x01,          /* MCollection Application (mouse, keyboard) */

	 /* Use collection 1 for joypad buttons */
	 0xA1, 0x02,         /* MCollection Logical (interrelated data) */

	  /* Ignore the 1st byte, maybe it is used for a controller
	   * number but it's not needed for correct operation */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x01,        /* GReportCount 0x01 [1] */
	  0x81, 0x01,        /* MInput 0x01 (Const[0] Arr[1] Abs[2]) */

	  /* Bytes from 2nd to 4th are a bitmap for joypad buttons, for these
	   * buttons multiple keypresses are allowed */
	  0x05, 0x09,        /* GUsagePage Button */
	  0x19, 0x01,        /* LUsageMinimum 0x01 [Button 1 (primary/trigger)] */
	  0x29, 0x18,        /* LUsageMaximum 0x18 [Button 24] */
	  0x14,              /* GLogicalMinimum [0] */
	  0x25, 0x01,        /* GLogicalMaximum 0x01 [1] */
	  0x75, 0x01,        /* GReportSize 0x01 [1] */
	  0x95, 0x18,        /* GReportCount 0x18 [24] */
	  0x81, 0x02,        /* MInput 0x02 (Data[0] Var[1] Abs[2]) */

	  0xC0,              /* MEndCollection */

	 /* Use collection 2 for remote control buttons */
	 0xA1, 0x02,         /* MCollection Logical (interrelated data) */

	  /* 5th byte is used for remote control buttons */
	  0x05, 0x09,        /* GUsagePage Button */
	  0x18,              /* LUsageMinimum [No button pressed] */
	  0x29, 0xFE,        /* LUsageMaximum 0xFE [Button 254] */
	  0x14,              /* GLogicalMinimum [0] */
	  0x26, 0xFE, 0x00,  /* GLogicalMaximum 0x00FE [254] */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x01,        /* GReportCount 0x01 [1] */
	  0x80,              /* MInput  */

	  /* Ignore bytes from 6th to 11th, 6th to 10th are always constant at
	   * 0xff and 11th is for press indication */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x06,        /* GReportCount 0x06 [6] */
	  0x81, 0x01,        /* MInput 0x01 (Const[0] Arr[1] Abs[2]) */

	  /* 12th byte is for battery strength */
	  0x05, 0x06,        /* GUsagePage Generic Device Controls */
	  0x09, 0x20,        /* LUsage 0x20 [Battery Strength] */
	  0x14,              /* GLogicalMinimum [0] */
	  0x25, 0x05,        /* GLogicalMaximum 0x05 [5] */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x01,        /* GReportCount 0x01 [1] */
	  0x81, 0x02,        /* MInput 0x02 (Data[0] Var[1] Abs[2]) */

	  0xC0,              /* MEndCollection */

	 0xC0                /* MEndCollection [Game Pad] */
};

static const unsigned int ps3remote_keymap_joypad_buttons[] = {
	[0x01] = KEY_SELECT,
	[0x02] = BTN_THUMBL,		/* L3 */
	[0x03] = BTN_THUMBR,		/* R3 */
	[0x04] = BTN_START,
	[0x05] = KEY_UP,
	[0x06] = KEY_RIGHT,
	[0x07] = KEY_DOWN,
	[0x08] = KEY_LEFT,
	[0x09] = BTN_TL2,		/* L2 */
	[0x0a] = BTN_TR2,		/* R2 */
	[0x0b] = BTN_TL,		/* L1 */
	[0x0c] = BTN_TR,		/* R1 */
	[0x0d] = KEY_OPTION,		/* options/triangle */
	[0x0e] = KEY_BACK,		/* back/circle */
	[0x0f] = BTN_0,			/* cross */
	[0x10] = KEY_SCREEN,		/* view/square */
	[0x11] = KEY_HOMEPAGE,		/* PS button */
	[0x14] = KEY_ENTER,
};
static const unsigned int ps3remote_keymap_remote_buttons[] = {
	[0x00] = KEY_1,
	[0x01] = KEY_2,
	[0x02] = KEY_3,
	[0x03] = KEY_4,
	[0x04] = KEY_5,
	[0x05] = KEY_6,
	[0x06] = KEY_7,
	[0x07] = KEY_8,
	[0x08] = KEY_9,
	[0x09] = KEY_0,
	[0x0e] = KEY_ESC,		/* return */
	[0x0f] = KEY_CLEAR,
	[0x16] = KEY_EJECTCD,
	[0x1a] = KEY_MENU,		/* top menu */
	[0x28] = KEY_TIME,
	[0x30] = KEY_PREVIOUS,
	[0x31] = KEY_NEXT,
	[0x32] = KEY_PLAY,
	[0x33] = KEY_REWIND,		/* scan back */
	[0x34] = KEY_FORWARD,		/* scan forward */
	[0x38] = KEY_STOP,
	[0x39] = KEY_PAUSE,
	[0x40] = KEY_CONTEXT_MENU,	/* pop up/menu */
	[0x60] = KEY_FRAMEBACK,		/* slow/step back */
	[0x61] = KEY_FRAMEFORWARD,	/* slow/step forward */
	[0x63] = KEY_SUBTITLE,
	[0x64] = KEY_AUDIO,
	[0x65] = KEY_ANGLE,
	[0x70] = KEY_INFO,		/* display */
	[0x80] = KEY_BLUE,
	[0x81] = KEY_RED,
	[0x82] = KEY_GREEN,
	[0x83] = KEY_YELLOW,
};

static __u8 *ps3remote_fixup(struct hid_device *hdev, __u8 *rdesc,
			     unsigned int *rsize)
{
	*rsize = sizeof(ps3remote_rdesc);
	return ps3remote_rdesc;
}

static int ps3remote_mapping(struct hid_device *hdev, struct hid_input *hi,
			     struct hid_field *field, struct hid_usage *usage,
			     unsigned long **bit, int *max)
{
	unsigned int key = usage->hid & HID_USAGE;

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
		return -1;

	switch (usage->collection_index) {
	case 1:
		if (key >= ARRAY_SIZE(ps3remote_keymap_joypad_buttons))
			return -1;

		key = ps3remote_keymap_joypad_buttons[key];
		if (!key)
			return -1;
		break;
	case 2:
		if (key >= ARRAY_SIZE(ps3remote_keymap_remote_buttons))
			return -1;

		key = ps3remote_keymap_remote_buttons[key];
		if (!key)
			return -1;
		break;
	default:
		return -1;
	}

	hid_map_usage_clear(hi, usage, bit, max, EV_KEY, key);
	return 1;
}

static const struct hid_device_id ps3remote_devices[] = {
	/* PS3 BD Remote Control */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_BDREMOTE) },
	/* Logitech Harmony Adapter for PS3 */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_HARMONY_PS3) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ps3remote_devices);

static struct hid_driver ps3remote_driver = {
	.name          = "ps3_remote",
	.id_table      = ps3remote_devices,
	.report_fixup  = ps3remote_fixup,
	.input_mapping = ps3remote_mapping,
};

static int __init ps3remote_init(void)
{
	return hid_register_driver(&ps3remote_driver);
}

static void __exit ps3remote_exit(void)
{
	hid_unregister_driver(&ps3remote_driver);
}

module_init(ps3remote_init);
module_exit(ps3remote_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Dillow <dave@thedillows.org>, Antonio Ospite <ospite@studenti.unina.it>");
