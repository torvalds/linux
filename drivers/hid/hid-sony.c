/*
 *  HID driver for Sony / PS2 / PS3 BD devices.
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2012 David Dillow <dave@thedillows.org>
 *  Copyright (c) 2006-2013 Jiri Kosina
 *  Copyright (c) 2013 Colin Leitner <colin.leitner@gmail.com>
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
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/leds.h>

#include "hid-ids.h"

#define VAIO_RDESC_CONSTANT       BIT(0)
#define SIXAXIS_CONTROLLER_USB    BIT(1)
#define SIXAXIS_CONTROLLER_BT     BIT(2)
#define BUZZ_CONTROLLER           BIT(3)
#define PS3REMOTE                 BIT(4)
#define DUALSHOCK4_CONTROLLER_USB BIT(5)
#define DUALSHOCK4_CONTROLLER_BT  BIT(6)

#define SONY_LED_SUPPORT (SIXAXIS_CONTROLLER_USB | BUZZ_CONTROLLER | DUALSHOCK4_CONTROLLER_USB)

#define MAX_LEDS 4

static const u8 sixaxis_rdesc_fixup[] = {
	0x95, 0x13, 0x09, 0x01, 0x81, 0x02, 0x95, 0x0C,
	0x81, 0x01, 0x75, 0x10, 0x95, 0x04, 0x26, 0xFF,
	0x03, 0x46, 0xFF, 0x03, 0x09, 0x01, 0x81, 0x02
};

static const u8 sixaxis_rdesc_fixup2[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02,
	0x85, 0x01, 0x75, 0x08, 0x95, 0x01, 0x15, 0x00,
	0x26, 0xff, 0x00, 0x81, 0x03, 0x75, 0x01, 0x95,
	0x13, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45,
	0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x13, 0x81,
	0x02, 0x75, 0x01, 0x95, 0x0d, 0x06, 0x00, 0xff,
	0x81, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05,
	0x01, 0x09, 0x01, 0xa1, 0x00, 0x75, 0x08, 0x95,
	0x04, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30,
	0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
	0xc0, 0x05, 0x01, 0x95, 0x13, 0x09, 0x01, 0x81,
	0x02, 0x95, 0x0c, 0x81, 0x01, 0x75, 0x10, 0x95,
	0x04, 0x26, 0xff, 0x03, 0x46, 0xff, 0x03, 0x09,
	0x01, 0x81, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0x02,
	0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xb1, 0x02,
	0xc0, 0xa1, 0x02, 0x85, 0xee, 0x75, 0x08, 0x95,
	0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02,
	0x85, 0xef, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
	0xb1, 0x02, 0xc0, 0xc0,
};

static u8 dualshock4_usb_rdesc[] = {
	0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01,
	0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
	0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
	0x04, 0x81, 0x02, 0x09, 0x39, 0x15, 0x00, 0x25,
	0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
	0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x0e, 0x15, 0x00,
	0x25, 0x01, 0x75, 0x01, 0x95, 0x0e, 0x81, 0x02,
	0x06, 0x00, 0xff, 0x09, 0x20, 0x75, 0x06, 0x95,
	0x01, 0x15, 0x00, 0x25, 0x7f, 0x81, 0x02, 0x05,
	0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26,
	0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0x06, 0x00, 0xff, 0x09, 0x21, 0x95, 0x03, 0x81,
	0x02, 0x05, 0x01, 0x19, 0x40, 0x29, 0x42, 0x16,
	0x00, 0x80, 0x26, 0x00, 0x7f, 0x75, 0x10, 0x95,
	0x03, 0x81, 0x02, 0x05, 0x01, 0x19, 0x43, 0x29,
	0x45, 0x16, 0xff, 0xbf, 0x26, 0x00, 0x40, 0x95,
	0x03, 0x81, 0x02, 0x06, 0x00, 0xff, 0x09, 0x21,
	0x75, 0x08, 0x95, 0x27, 0x81, 0x02, 0x85, 0x05,
	0x09, 0x22, 0x95, 0x1f, 0x91, 0x02, 0x85, 0x04,
	0x09, 0x23, 0x95, 0x24, 0xb1, 0x02, 0x85, 0x02,
	0x09, 0x24, 0x95, 0x24, 0xb1, 0x02, 0x85, 0x08,
	0x09, 0x25, 0x95, 0x03, 0xb1, 0x02, 0x85, 0x10,
	0x09, 0x26, 0x95, 0x04, 0xb1, 0x02, 0x85, 0x11,
	0x09, 0x27, 0x95, 0x02, 0xb1, 0x02, 0x85, 0x12,
	0x06, 0x02, 0xff, 0x09, 0x21, 0x95, 0x0f, 0xb1,
	0x02, 0x85, 0x13, 0x09, 0x22, 0x95, 0x16, 0xb1,
	0x02, 0x85, 0x14, 0x06, 0x05, 0xff, 0x09, 0x20,
	0x95, 0x10, 0xb1, 0x02, 0x85, 0x15, 0x09, 0x21,
	0x95, 0x2c, 0xb1, 0x02, 0x06, 0x80, 0xff, 0x85,
	0x80, 0x09, 0x20, 0x95, 0x06, 0xb1, 0x02, 0x85,
	0x81, 0x09, 0x21, 0x95, 0x06, 0xb1, 0x02, 0x85,
	0x82, 0x09, 0x22, 0x95, 0x05, 0xb1, 0x02, 0x85,
	0x83, 0x09, 0x23, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0x84, 0x09, 0x24, 0x95, 0x04, 0xb1, 0x02, 0x85,
	0x85, 0x09, 0x25, 0x95, 0x06, 0xb1, 0x02, 0x85,
	0x86, 0x09, 0x26, 0x95, 0x06, 0xb1, 0x02, 0x85,
	0x87, 0x09, 0x27, 0x95, 0x23, 0xb1, 0x02, 0x85,
	0x88, 0x09, 0x28, 0x95, 0x22, 0xb1, 0x02, 0x85,
	0x89, 0x09, 0x29, 0x95, 0x02, 0xb1, 0x02, 0x85,
	0x90, 0x09, 0x30, 0x95, 0x05, 0xb1, 0x02, 0x85,
	0x91, 0x09, 0x31, 0x95, 0x03, 0xb1, 0x02, 0x85,
	0x92, 0x09, 0x32, 0x95, 0x03, 0xb1, 0x02, 0x85,
	0x93, 0x09, 0x33, 0x95, 0x0c, 0xb1, 0x02, 0x85,
	0xa0, 0x09, 0x40, 0x95, 0x06, 0xb1, 0x02, 0x85,
	0xa1, 0x09, 0x41, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0xa2, 0x09, 0x42, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0xa3, 0x09, 0x43, 0x95, 0x30, 0xb1, 0x02, 0x85,
	0xa4, 0x09, 0x44, 0x95, 0x0d, 0xb1, 0x02, 0x85,
	0xa5, 0x09, 0x45, 0x95, 0x15, 0xb1, 0x02, 0x85,
	0xa6, 0x09, 0x46, 0x95, 0x15, 0xb1, 0x02, 0x85,
	0xf0, 0x09, 0x47, 0x95, 0x3f, 0xb1, 0x02, 0x85,
	0xf1, 0x09, 0x48, 0x95, 0x3f, 0xb1, 0x02, 0x85,
	0xf2, 0x09, 0x49, 0x95, 0x0f, 0xb1, 0x02, 0x85,
	0xa7, 0x09, 0x4a, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0xa8, 0x09, 0x4b, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0xa9, 0x09, 0x4c, 0x95, 0x08, 0xb1, 0x02, 0x85,
	0xaa, 0x09, 0x4e, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0xab, 0x09, 0x4f, 0x95, 0x39, 0xb1, 0x02, 0x85,
	0xac, 0x09, 0x50, 0x95, 0x39, 0xb1, 0x02, 0x85,
	0xad, 0x09, 0x51, 0x95, 0x0b, 0xb1, 0x02, 0x85,
	0xae, 0x09, 0x52, 0x95, 0x01, 0xb1, 0x02, 0x85,
	0xaf, 0x09, 0x53, 0x95, 0x02, 0xb1, 0x02, 0x85,
	0xb0, 0x09, 0x54, 0x95, 0x3f, 0xb1, 0x02, 0xc0,
};

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

static const unsigned int buzz_keymap[] = {
	/* The controller has 4 remote buzzers, each with one LED and 5
	 * buttons.
	 * 
	 * We use the mapping chosen by the controller, which is:
	 *
	 * Key          Offset
	 * -------------------
	 * Buzz              1
	 * Blue              5
	 * Orange            4
	 * Green             3
	 * Yellow            2
	 *
	 * So, for example, the orange button on the third buzzer is mapped to
	 * BTN_TRIGGER_HAPPY14
	 */
	[ 1] = BTN_TRIGGER_HAPPY1,
	[ 2] = BTN_TRIGGER_HAPPY2,
	[ 3] = BTN_TRIGGER_HAPPY3,
	[ 4] = BTN_TRIGGER_HAPPY4,
	[ 5] = BTN_TRIGGER_HAPPY5,
	[ 6] = BTN_TRIGGER_HAPPY6,
	[ 7] = BTN_TRIGGER_HAPPY7,
	[ 8] = BTN_TRIGGER_HAPPY8,
	[ 9] = BTN_TRIGGER_HAPPY9,
	[10] = BTN_TRIGGER_HAPPY10,
	[11] = BTN_TRIGGER_HAPPY11,
	[12] = BTN_TRIGGER_HAPPY12,
	[13] = BTN_TRIGGER_HAPPY13,
	[14] = BTN_TRIGGER_HAPPY14,
	[15] = BTN_TRIGGER_HAPPY15,
	[16] = BTN_TRIGGER_HAPPY16,
	[17] = BTN_TRIGGER_HAPPY17,
	[18] = BTN_TRIGGER_HAPPY18,
	[19] = BTN_TRIGGER_HAPPY19,
	[20] = BTN_TRIGGER_HAPPY20,
};

struct sony_sc {
	struct hid_device *hdev;
	struct led_classdev *leds[MAX_LEDS];
	unsigned long quirks;
	struct work_struct state_worker;

#ifdef CONFIG_SONY_FF
	__u8 left;
	__u8 right;
#endif

	__u8 led_state[MAX_LEDS];
	__u8 led_count;
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


/* Sony Vaio VGX has wrongly mouse pointer declared as constant */
static __u8 *sony_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/*
	 * Some Sony RF receivers wrongly declare the mouse pointer as a
	 * a constant non-data variable.
	 */
	if ((sc->quirks & VAIO_RDESC_CONSTANT) && *rsize >= 56 &&
	    /* usage page: generic desktop controls */
	    /* rdesc[0] == 0x05 && rdesc[1] == 0x01 && */
	    /* usage: mouse */
	    rdesc[2] == 0x09 && rdesc[3] == 0x02 &&
	    /* input (usage page for x,y axes): constant, variable, relative */
	    rdesc[54] == 0x81 && rdesc[55] == 0x07) {
		hid_info(hdev, "Fixing up Sony RF Receiver report descriptor\n");
		/* input: data, variable, relative */
		rdesc[55] = 0x06;
	}

	/*
	 * The default Dualshock 4 USB descriptor doesn't assign
	 * the gyroscope values to corresponding axes so we need a
	 * modified one.
	 */
	if ((sc->quirks & DUALSHOCK4_CONTROLLER_USB) && *rsize == 467) {
		hid_info(hdev, "Using modified Dualshock 4 report descriptor with gyroscope axes\n");
		rdesc = dualshock4_usb_rdesc;
		*rsize = sizeof(dualshock4_usb_rdesc);
	}

	/* The HID descriptor exposed over BT has a trailing zero byte */
	if ((((sc->quirks & SIXAXIS_CONTROLLER_USB) && *rsize == 148) ||
			((sc->quirks & SIXAXIS_CONTROLLER_BT) && *rsize == 149)) &&
			rdesc[83] == 0x75) {
		hid_info(hdev, "Fixing up Sony Sixaxis report descriptor\n");
		memcpy((void *)&rdesc[83], (void *)&sixaxis_rdesc_fixup,
			sizeof(sixaxis_rdesc_fixup));
	} else if (sc->quirks & SIXAXIS_CONTROLLER_USB &&
		   *rsize > sizeof(sixaxis_rdesc_fixup2)) {
		hid_info(hdev, "Sony Sixaxis clone detected. Using original report descriptor (size: %d clone; %d new)\n",
			 *rsize, (int)sizeof(sixaxis_rdesc_fixup2));
		*rsize = sizeof(sixaxis_rdesc_fixup2);
		memcpy(rdesc, &sixaxis_rdesc_fixup2, *rsize);
	}

	if (sc->quirks & PS3REMOTE)
		return ps3remote_fixup(hdev, rdesc, rsize);

	return rdesc;
}

static int sony_raw_event(struct hid_device *hdev, struct hid_report *report,
		__u8 *rd, int size)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/* Sixaxis HID report has acclerometers/gyro with MSByte first, this
	 * has to be BYTE_SWAPPED before passing up to joystick interface
	 */
	if ((sc->quirks & (SIXAXIS_CONTROLLER_USB | SIXAXIS_CONTROLLER_BT)) &&
			rd[0] == 0x01 && size == 49) {
		swap(rd[41], rd[42]);
		swap(rd[43], rd[44]);
		swap(rd[45], rd[46]);
		swap(rd[47], rd[48]);
	}

	return 0;
}

static int sony_mapping(struct hid_device *hdev, struct hid_input *hi,
			struct hid_field *field, struct hid_usage *usage,
			unsigned long **bit, int *max)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & BUZZ_CONTROLLER) {
		unsigned int key = usage->hid & HID_USAGE;

		if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
			return -1;

		switch (usage->collection_index) {
		case 1:
			if (key >= ARRAY_SIZE(buzz_keymap))
				return -1;

			key = buzz_keymap[key];
			if (!key)
				return -1;
			break;
		default:
			return -1;
		}

		hid_map_usage_clear(hi, usage, bit, max, EV_KEY, key);
		return 1;
	}

	if (sc->quirks & PS3REMOTE)
		return ps3remote_mapping(hdev, hi, field, usage, bit, max);

	/* Let hid-core decide for the others */
	return 0;
}

/*
 * The Sony Sixaxis does not handle HID Output Reports on the Interrupt EP
 * like it should according to usbhid/hid-core.c::usbhid_output_raw_report()
 * so we need to override that forcing HID Output Reports on the Control EP.
 *
 * There is also another issue about HID Output Reports via USB, the Sixaxis
 * does not want the report_id as part of the data packet, so we have to
 * discard buf[0] when sending the actual control message, even for numbered
 * reports, humpf!
 */
static int sixaxis_usb_output_raw_report(struct hid_device *hid, __u8 *buf,
		size_t count, unsigned char report_type)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface = intf->cur_altsetting;
	int report_id = buf[0];
	int ret;

	if (report_type == HID_OUTPUT_REPORT) {
		/* Don't send the Report ID */
		buf++;
		count--;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_REPORT,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		((report_type + 1) << 8) | report_id,
		interface->desc.bInterfaceNumber, buf, count,
		USB_CTRL_SET_TIMEOUT);

	/* Count also the Report ID, in case of an Output report. */
	if (ret > 0 && report_type == HID_OUTPUT_REPORT)
		ret++;

	return ret;
}

/*
 * Sending HID_REQ_GET_REPORT changes the operation mode of the ps3 controller
 * to "operational".  Without this, the ps3 controller will not report any
 * events.
 */
static int sixaxis_set_operational_usb(struct hid_device *hdev)
{
	int ret;
	char *buf = kmalloc(18, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	ret = hdev->hid_get_raw_report(hdev, 0xf2, buf, 17, HID_FEATURE_REPORT);

	if (ret < 0)
		hid_err(hdev, "can't set operational mode\n");

	kfree(buf);

	return ret;
}

static int sixaxis_set_operational_bt(struct hid_device *hdev)
{
	unsigned char buf[] = { 0xf4,  0x42, 0x03, 0x00, 0x00 };
	return hdev->hid_output_raw_report(hdev, buf, sizeof(buf), HID_FEATURE_REPORT);
}

static void buzz_set_leds(struct hid_device *hdev, const __u8 *leds)
{
	struct list_head *report_list =
		&hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next,
		struct hid_report, list);
	__s32 *value = report->field[0]->value;

	value[0] = 0x00;
	value[1] = leds[0] ? 0xff : 0x00;
	value[2] = leds[1] ? 0xff : 0x00;
	value[3] = leds[2] ? 0xff : 0x00;
	value[4] = leds[3] ? 0xff : 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static void sony_set_leds(struct hid_device *hdev, const __u8 *leds, int count)
{
	struct sony_sc *drv_data = hid_get_drvdata(hdev);
	int n;

	BUG_ON(count > MAX_LEDS);

	if (drv_data->quirks & BUZZ_CONTROLLER && count == 4) {
		buzz_set_leds(hdev, leds);
	} else if ((drv_data->quirks & SIXAXIS_CONTROLLER_USB) ||
		   (drv_data->quirks & DUALSHOCK4_CONTROLLER_USB)) {
		for (n = 0; n < count; n++)
			drv_data->led_state[n] = leds[n];
		schedule_work(&drv_data->state_worker);
	}
}

static void sony_led_set_brightness(struct led_classdev *led,
				    enum led_brightness value)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct sony_sc *drv_data;

	int n;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "No device data\n");
		return;
	}

	for (n = 0; n < drv_data->led_count; n++) {
		if (led == drv_data->leds[n]) {
			if (value != drv_data->led_state[n]) {
				drv_data->led_state[n] = value;
				sony_set_leds(hdev, drv_data->led_state, drv_data->led_count);
			}
			break;
		}
	}
}

static enum led_brightness sony_led_get_brightness(struct led_classdev *led)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct sony_sc *drv_data;

	int n;
	int on = 0;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "No device data\n");
		return LED_OFF;
	}

	for (n = 0; n < drv_data->led_count; n++) {
		if (led == drv_data->leds[n]) {
			on = !!(drv_data->led_state[n]);
			break;
		}
	}

	return on ? LED_FULL : LED_OFF;
}

static void sony_leds_remove(struct hid_device *hdev)
{
	struct sony_sc *drv_data;
	struct led_classdev *led;
	int n;

	drv_data = hid_get_drvdata(hdev);
	BUG_ON(!(drv_data->quirks & SONY_LED_SUPPORT));

	for (n = 0; n < drv_data->led_count; n++) {
		led = drv_data->leds[n];
		drv_data->leds[n] = NULL;
		if (!led)
			continue;
		led_classdev_unregister(led);
		kfree(led);
	}

	drv_data->led_count = 0;
}

static int sony_leds_init(struct hid_device *hdev)
{
	struct sony_sc *drv_data;
	int n, ret = 0;
	int max_brightness;
	struct led_classdev *led;
	size_t name_sz;
	char *name;
	size_t name_len;
	const char *name_fmt;
	static const __u8 initial_values[MAX_LEDS] = { 0x00, 0x00, 0x00, 0x00 };

	drv_data = hid_get_drvdata(hdev);
	BUG_ON(!(drv_data->quirks & SONY_LED_SUPPORT));

	if (drv_data->quirks & BUZZ_CONTROLLER) {
		name_len = strlen("::buzz#");
		name_fmt = "%s::buzz%d";
		/* Validate expected report characteristics. */
		if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 0, 0, 7))
			return -ENODEV;
	} else {
		name_len = strlen("::sony#");
		name_fmt = "%s::sony%d";
	}

	if (drv_data->quirks & DUALSHOCK4_CONTROLLER_USB) {
		drv_data->led_count = 3;
		max_brightness = 255;
	} else {
		drv_data->led_count = 4;
		max_brightness = 1;
	}

	/* Clear LEDs as we have no way of reading their initial state. This is
	 * only relevant if the driver is loaded after somebody actively set the
	 * LEDs to on */
	sony_set_leds(hdev, initial_values, drv_data->led_count);

	name_sz = strlen(dev_name(&hdev->dev)) + name_len + 1;

	for (n = 0; n < drv_data->led_count; n++) {
		led = kzalloc(sizeof(struct led_classdev) + name_sz, GFP_KERNEL);
		if (!led) {
			hid_err(hdev, "Couldn't allocate memory for LED %d\n", n);
			ret = -ENOMEM;
			goto error_leds;
		}

		name = (void *)(&led[1]);
		snprintf(name, name_sz, name_fmt, dev_name(&hdev->dev), n + 1);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = max_brightness;
		led->brightness_get = sony_led_get_brightness;
		led->brightness_set = sony_led_set_brightness;

		ret = led_classdev_register(&hdev->dev, led);
		if (ret) {
			hid_err(hdev, "Failed to register LED %d\n", n);
			kfree(led);
			goto error_leds;
		}

		drv_data->leds[n] = led;
	}

	return ret;

error_leds:
	sony_leds_remove(hdev);

	return ret;
}

static void sixaxis_state_worker(struct work_struct *work)
{
	struct sony_sc *sc = container_of(work, struct sony_sc, state_worker);
	unsigned char buf[] = {
		0x01,
		0x00, 0xff, 0x00, 0xff, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0x00, 0x00, 0x00, 0x00, 0x00
	};

#ifdef CONFIG_SONY_FF
	buf[3] = sc->right ? 1 : 0;
	buf[5] = sc->left;
#endif

	buf[10] |= sc->led_state[0] << 1;
	buf[10] |= sc->led_state[1] << 2;
	buf[10] |= sc->led_state[2] << 3;
	buf[10] |= sc->led_state[3] << 4;

	sc->hdev->hid_output_raw_report(sc->hdev, buf, sizeof(buf),
					HID_OUTPUT_REPORT);
}

static void dualshock4_state_worker(struct work_struct *work)
{
	struct sony_sc *sc = container_of(work, struct sony_sc, state_worker);
	struct hid_device *hdev = sc->hdev;
	struct list_head *head, *list;
	struct hid_report *report;
	__s32 *value;

	list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;

	list_for_each(head, list) {
		report = list_entry(head, struct hid_report, list);

		/* Report 5 is used to send data to the controller via USB */
		if ((sc->quirks & DUALSHOCK4_CONTROLLER_USB) && report->id == 5)
			break;
	}

	if (head == list) {
		hid_err(hdev, "Dualshock 4 output report not found\n");
		return;
	}

	value = report->field[0]->value;
	value[0] = 0x03;

#ifdef CONFIG_SONY_FF
	value[3] = sc->right;
	value[4] = sc->left;
#endif

	value[5] = sc->led_state[0];
	value[6] = sc->led_state[1];
	value[7] = sc->led_state[2];

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

#ifdef CONFIG_SONY_FF
static int sony_play_effect(struct input_dev *dev, void *data,
			    struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct sony_sc *sc = hid_get_drvdata(hid);

	if (effect->type != FF_RUMBLE)
		return 0;

	sc->left = effect->u.rumble.strong_magnitude / 256;
	sc->right = effect->u.rumble.weak_magnitude / 256;

	schedule_work(&sc->state_worker);
	return 0;
}

static int sony_init_ff(struct hid_device *hdev)
{
	struct hid_input *hidinput = list_entry(hdev->inputs.next,
						struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;

	input_set_capability(input_dev, EV_FF, FF_RUMBLE);
	return input_ff_create_memless(input_dev, NULL, sony_play_effect);
}

static void sony_destroy_ff(struct hid_device *hdev)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	cancel_work_sync(&sc->state_worker);
}

#else
static int sony_init_ff(struct hid_device *hdev)
{
	return 0;
}

static void sony_destroy_ff(struct hid_device *hdev)
{
}
#endif

static int sony_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	unsigned long quirks = id->driver_data;
	struct sony_sc *sc;
	unsigned int connect_mask = HID_CONNECT_DEFAULT;

	sc = devm_kzalloc(&hdev->dev, sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		hid_err(hdev, "can't alloc sony descriptor\n");
		return -ENOMEM;
	}

	sc->quirks = quirks;
	hid_set_drvdata(hdev, sc);
	sc->hdev = hdev;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	if (sc->quirks & VAIO_RDESC_CONSTANT)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	else if (sc->quirks & SIXAXIS_CONTROLLER_USB)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	else if (sc->quirks & SIXAXIS_CONTROLLER_BT)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	if (sc->quirks & SIXAXIS_CONTROLLER_USB) {
		hdev->hid_output_raw_report = sixaxis_usb_output_raw_report;
		ret = sixaxis_set_operational_usb(hdev);
		INIT_WORK(&sc->state_worker, sixaxis_state_worker);
	}
	else if (sc->quirks & SIXAXIS_CONTROLLER_BT)
		ret = sixaxis_set_operational_bt(hdev);
	else if (sc->quirks & DUALSHOCK4_CONTROLLER_USB) {
		ret = 0;
		INIT_WORK(&sc->state_worker, dualshock4_state_worker);
	} else {
		ret = 0;
	}

	if (ret < 0)
		goto err_stop;

	if (sc->quirks & SONY_LED_SUPPORT) {
		ret = sony_leds_init(hdev);
		if (ret < 0)
			goto err_stop;
	}

	ret = sony_init_ff(hdev);
	if (ret < 0)
		goto err_stop;

	return 0;
err_stop:
	if (sc->quirks & SONY_LED_SUPPORT)
		sony_leds_remove(hdev);
	hid_hw_stop(hdev);
	return ret;
}

static void sony_remove(struct hid_device *hdev)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & SONY_LED_SUPPORT)
		sony_leds_remove(hdev);

	sony_destroy_ff(hdev);

	hid_hw_stop(hdev);
}

static const struct hid_device_id sony_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_NAVIGATION_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_BT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGX_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGP_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	/* Wired Buzz Controller. Reported as Sony Hub from its USB ID and as
	 * Logitech joystick from the device descriptor. */
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_BUZZ_CONTROLLER),
		.driver_data = BUZZ_CONTROLLER },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_WIRELESS_BUZZ_CONTROLLER),
		.driver_data = BUZZ_CONTROLLER },
	/* PS3 BD Remote Control */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_BDREMOTE),
		.driver_data = PS3REMOTE },
	/* Logitech Harmony Adapter for PS3 */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_HARMONY_PS3),
		.driver_data = PS3REMOTE },
	/* Sony Dualshock 4 controllers for PS4 */
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER),
		.driver_data = DUALSHOCK4_CONTROLLER_USB },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER),
		.driver_data = DUALSHOCK4_CONTROLLER_BT },
	{ }
};
MODULE_DEVICE_TABLE(hid, sony_devices);

static struct hid_driver sony_driver = {
	.name          = "sony",
	.id_table      = sony_devices,
	.input_mapping = sony_mapping,
	.probe         = sony_probe,
	.remove        = sony_remove,
	.report_fixup  = sony_report_fixup,
	.raw_event     = sony_raw_event
};
module_hid_driver(sony_driver);

MODULE_LICENSE("GPL");
