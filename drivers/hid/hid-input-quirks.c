/*
 *  HID-input usage mapping quirks
 *
 *  This is used to handle HID-input mappings for devices violating
 *  HUT 1.12 specification.
 *
 * Copyright (c) 2007-2008 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License
 */

#include <linux/input.h>
#include <linux/hid.h>

#define map_abs(c)      do { usage->code = c; usage->type = EV_ABS; *bit = input->absbit; *max = ABS_MAX; } while (0)
#define map_rel(c)      do { usage->code = c; usage->type = EV_REL; *bit = input->relbit; *max = REL_MAX; } while (0)
#define map_key(c)      do { usage->code = c; usage->type = EV_KEY; *bit = input->keybit; *max = KEY_MAX; } while (0)
#define map_led(c)      do { usage->code = c; usage->type = EV_LED; *bit = input->ledbit; *max = LED_MAX; } while (0)

#define map_abs_clear(c)        do { map_abs(c); clear_bit(c, *bit); } while (0)
#define map_key_clear(c)        do { map_key(c); clear_bit(c, *bit); } while (0)

static int quirk_belkin_wkbd(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
		case 0x03a: map_key_clear(KEY_SOUND);		break;
		case 0x03b: map_key_clear(KEY_CAMERA);		break;
		case 0x03c: map_key_clear(KEY_DOCUMENTS);	break;
		default:
			return 0;
	}
	return 1;
}

static int quirk_cherry_cymotion(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
		case 0x301: map_key_clear(KEY_PROG1);		break;
		case 0x302: map_key_clear(KEY_PROG2);		break;
		case 0x303: map_key_clear(KEY_PROG3);		break;
		default:
			return 0;
	}
	return 1;
}

static int quirk_logitech_ultrax_remote(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	set_bit(EV_REP, input->evbit);
	switch(usage->hid & HID_USAGE) {
		/* Reported on Logitech Ultra X Media Remote */
		case 0x004: map_key_clear(KEY_AGAIN);		break;
		case 0x00d: map_key_clear(KEY_HOME);		break;
		case 0x024: map_key_clear(KEY_SHUFFLE);		break;
		case 0x025: map_key_clear(KEY_TV);		break;
		case 0x026: map_key_clear(KEY_MENU);		break;
		case 0x031: map_key_clear(KEY_AUDIO);		break;
		case 0x032: map_key_clear(KEY_TEXT);		break;
		case 0x033: map_key_clear(KEY_LAST);		break;
		case 0x047: map_key_clear(KEY_MP3);		break;
		case 0x048: map_key_clear(KEY_DVD);		break;
		case 0x049: map_key_clear(KEY_MEDIA);		break;
		case 0x04a: map_key_clear(KEY_VIDEO);		break;
		case 0x04b: map_key_clear(KEY_ANGLE);		break;
		case 0x04c: map_key_clear(KEY_LANGUAGE);	break;
		case 0x04d: map_key_clear(KEY_SUBTITLE);	break;
		case 0x051: map_key_clear(KEY_RED);		break;
		case 0x052: map_key_clear(KEY_CLOSE);		break;

		default:
			return 0;
	}
	return 1;
}

static int quirk_gyration_remote(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	set_bit(EV_REP, input->evbit);
	switch(usage->hid & HID_USAGE) {
		/* Reported on Gyration MCE Remote */
		case 0x00d: map_key_clear(KEY_HOME);		break;
		case 0x024: map_key_clear(KEY_DVD);		break;
		case 0x025: map_key_clear(KEY_PVR);		break;
		case 0x046: map_key_clear(KEY_MEDIA);		break;
		case 0x047: map_key_clear(KEY_MP3);		break;
		case 0x049: map_key_clear(KEY_CAMERA);		break;
		case 0x04a: map_key_clear(KEY_VIDEO);		break;

		default:
			return 0;
	}
	return 1;
}

static int quirk_chicony_tactical_pad(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	set_bit(EV_REP, input->evbit);
	switch (usage->hid & HID_USAGE) {
		case 0xff01: map_key_clear(BTN_1);		break;
		case 0xff02: map_key_clear(BTN_2);		break;
		case 0xff03: map_key_clear(BTN_3);		break;
		case 0xff04: map_key_clear(BTN_4);		break;
		case 0xff05: map_key_clear(BTN_5);		break;
		case 0xff06: map_key_clear(BTN_6);		break;
		case 0xff07: map_key_clear(BTN_7);		break;
		case 0xff08: map_key_clear(BTN_8);		break;
		case 0xff09: map_key_clear(BTN_9);		break;
		case 0xff0a: map_key_clear(BTN_A);		break;
		case 0xff0b: map_key_clear(BTN_B);		break;
		default:
			return 0;
	}
	return 1;
}

static int quirk_microsoft_ergonomy_kb(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	switch(usage->hid & HID_USAGE) {
		case 0xfd06: map_key_clear(KEY_CHAT);		break;
		case 0xfd07: map_key_clear(KEY_PHONE);		break;
		case 0xff05:
			set_bit(EV_REP, input->evbit);
			map_key_clear(KEY_F13);
			set_bit(KEY_F14, input->keybit);
			set_bit(KEY_F15, input->keybit);
			set_bit(KEY_F16, input->keybit);
			set_bit(KEY_F17, input->keybit);
			set_bit(KEY_F18, input->keybit);
		default:
			return 0;
	}
	return 1;
}

static int quirk_microsoft_presenter_8k(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	set_bit(EV_REP, input->evbit);
	switch(usage->hid & HID_USAGE) {
		case 0xfd08: map_key_clear(KEY_FORWARD);	break;
		case 0xfd09: map_key_clear(KEY_BACK);		break;
		case 0xfd0b: map_key_clear(KEY_PLAYPAUSE);	break;
		case 0xfd0e: map_key_clear(KEY_CLOSE);		break;
		case 0xfd0f: map_key_clear(KEY_PLAY);		break;
		default:
			return 0;
	}
	return 1;
}

static int quirk_petalynx_remote(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if (((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR) &&
			((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER))
		return 0;

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_LOGIVENDOR)
		switch(usage->hid & HID_USAGE) {
			case 0x05a: map_key_clear(KEY_TEXT);		break;
			case 0x05b: map_key_clear(KEY_RED);		break;
			case 0x05c: map_key_clear(KEY_GREEN);		break;
			case 0x05d: map_key_clear(KEY_YELLOW);		break;
			case 0x05e: map_key_clear(KEY_BLUE);		break;
			default:
				return 0;
		}

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER)
		switch(usage->hid & HID_USAGE) {
			case 0x0f6: map_key_clear(KEY_NEXT);            break;
			case 0x0fa: map_key_clear(KEY_BACK);            break;
			default:
				return 0;
		}
	return 1;
}

static int quirk_logitech_wireless(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
		case 0x1001: map_key_clear(KEY_MESSENGER);	break;
		case 0x1003: map_key_clear(KEY_SOUND);		break;
		case 0x1004: map_key_clear(KEY_VIDEO);		break;
		case 0x1005: map_key_clear(KEY_AUDIO);		break;
		case 0x100a: map_key_clear(KEY_DOCUMENTS);	break;
		case 0x1011: map_key_clear(KEY_PREVIOUSSONG);	break;
		case 0x1012: map_key_clear(KEY_NEXTSONG);	break;
		case 0x1013: map_key_clear(KEY_CAMERA);		break;
		case 0x1014: map_key_clear(KEY_MESSENGER);	break;
		case 0x1015: map_key_clear(KEY_RECORD);		break;
		case 0x1016: map_key_clear(KEY_PLAYER);		break;
		case 0x1017: map_key_clear(KEY_EJECTCD);	break;
		case 0x1018: map_key_clear(KEY_MEDIA);		break;
		case 0x1019: map_key_clear(KEY_PROG1);		break;
		case 0x101a: map_key_clear(KEY_PROG2);		break;
		case 0x101b: map_key_clear(KEY_PROG3);		break;
		case 0x101f: map_key_clear(KEY_ZOOMIN);		break;
		case 0x1020: map_key_clear(KEY_ZOOMOUT);	break;
		case 0x1021: map_key_clear(KEY_ZOOMRESET);	break;
		case 0x1023: map_key_clear(KEY_CLOSE);		break;
		case 0x1027: map_key_clear(KEY_MENU);		break;
		/* this one is marked as 'Rotate' */
		case 0x1028: map_key_clear(KEY_ANGLE);		break;
		case 0x1029: map_key_clear(KEY_SHUFFLE);	break;
		case 0x102a: map_key_clear(KEY_BACK);		break;
		case 0x102b: map_key_clear(KEY_CYCLEWINDOWS);	break;
		case 0x1041: map_key_clear(KEY_BATTERY);	break;
		case 0x1042: map_key_clear(KEY_WORDPROCESSOR);	break;
		case 0x1043: map_key_clear(KEY_SPREADSHEET);	break;
		case 0x1044: map_key_clear(KEY_PRESENTATION);	break;
		case 0x1045: map_key_clear(KEY_UNDO);		break;
		case 0x1046: map_key_clear(KEY_REDO);		break;
		case 0x1047: map_key_clear(KEY_PRINT);		break;
		case 0x1048: map_key_clear(KEY_SAVE);		break;
		case 0x1049: map_key_clear(KEY_PROG1);		break;
		case 0x104a: map_key_clear(KEY_PROG2);		break;
		case 0x104b: map_key_clear(KEY_PROG3);		break;
		case 0x104c: map_key_clear(KEY_PROG4);		break;

		default:
			return 0;
	}
	return 1;
}

static int quirk_cherry_genius_29e(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
		case 0x156: map_key_clear(KEY_WORDPROCESSOR);	break;
		case 0x157: map_key_clear(KEY_SPREADSHEET);	break;
		case 0x158: map_key_clear(KEY_PRESENTATION);	break;
		case 0x15c: map_key_clear(KEY_STOP);		break;

		default:
			return 0;
	}
	return 1;
}

static int quirk_btc_8193(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
		case 0x230: map_key(BTN_MOUSE);			break;
		case 0x231: map_rel(REL_WHEEL);			break;
		/* 
		 * this keyboard has a scrollwheel implemented in
		 * totally broken way. We map this usage temporarily
		 * to HWHEEL and handle it in the event quirk handler
		 */
		case 0x232: map_rel(REL_HWHEEL);		break;

		default:
			return 0;
	}
	return 1;
}

static int quirk_sunplus_wdesktop(struct hid_usage *usage, struct input_dev *input,
			      unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
		case 0x2003: map_key_clear(KEY_ZOOMIN);		break;
		case 0x2103: map_key_clear(KEY_ZOOMOUT);	break;
		default:
			return 0;
	}
	return 1;
}

#define VENDOR_ID_BELKIN			0x1020
#define DEVICE_ID_BELKIN_WIRELESS_KEYBOARD	0x0006

#define VENDOR_ID_CHERRY			0x046a
#define DEVICE_ID_CHERRY_CYMOTION		0x0023

#define VENDOR_ID_CHICONY			0x04f2
#define DEVICE_ID_CHICONY_TACTICAL_PAD		0x0418

#define VENDOR_ID_EZKEY				0x0518
#define DEVICE_ID_BTC_8193			0x0002

#define VENDOR_ID_GYRATION			0x0c16
#define DEVICE_ID_GYRATION_REMOTE		0x0002

#define VENDOR_ID_LOGITECH			0x046d
#define DEVICE_ID_LOGITECH_RECEIVER		0xc101
#define DEVICE_ID_S510_RECEIVER			0xc50c
#define DEVICE_ID_S510_RECEIVER_2		0xc517
#define DEVICE_ID_MX3000_RECEIVER		0xc513

#define VENDOR_ID_MICROSOFT			0x045e
#define DEVICE_ID_MS4K				0x00db
#define DEVICE_ID_MS6K				0x00f9
#define DEVICE_IS_MS_PRESENTER_8K_BT		0x0701
#define DEVICE_ID_MS_PRESENTER_8K_USB		0x0713

#define VENDOR_ID_MONTEREY			0x0566
#define DEVICE_ID_GENIUS_KB29E			0x3004

#define VENDOR_ID_PETALYNX			0x18b1
#define DEVICE_ID_PETALYNX_MAXTER_REMOTE	0x0037

#define VENDOR_ID_SUNPLUS			0x04fc
#define DEVICE_ID_SUNPLUS_WDESKTOP		0x05d8

static const struct hid_input_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	int (*quirk)(struct hid_usage *, struct input_dev *, unsigned long **, int *);
} hid_input_blacklist[] = {
	{ VENDOR_ID_BELKIN, DEVICE_ID_BELKIN_WIRELESS_KEYBOARD, quirk_belkin_wkbd },

	{ VENDOR_ID_CHERRY, DEVICE_ID_CHERRY_CYMOTION, quirk_cherry_cymotion },

	{ VENDOR_ID_CHICONY, DEVICE_ID_CHICONY_TACTICAL_PAD, quirk_chicony_tactical_pad },

	{ VENDOR_ID_EZKEY, DEVICE_ID_BTC_8193, quirk_btc_8193 },

	{ VENDOR_ID_GYRATION, DEVICE_ID_GYRATION_REMOTE, quirk_gyration_remote },

	{ VENDOR_ID_LOGITECH, DEVICE_ID_LOGITECH_RECEIVER, quirk_logitech_ultrax_remote },
	{ VENDOR_ID_LOGITECH, DEVICE_ID_S510_RECEIVER, quirk_logitech_wireless },
	{ VENDOR_ID_LOGITECH, DEVICE_ID_S510_RECEIVER_2, quirk_logitech_wireless },
	{ VENDOR_ID_LOGITECH, DEVICE_ID_MX3000_RECEIVER, quirk_logitech_wireless },

	{ VENDOR_ID_MICROSOFT, DEVICE_ID_MS4K, quirk_microsoft_ergonomy_kb },
	{ VENDOR_ID_MICROSOFT, DEVICE_ID_MS6K, quirk_microsoft_ergonomy_kb },
	{ VENDOR_ID_MICROSOFT, DEVICE_IS_MS_PRESENTER_8K_BT, quirk_microsoft_presenter_8k },
	{ VENDOR_ID_MICROSOFT, DEVICE_ID_MS_PRESENTER_8K_USB, quirk_microsoft_presenter_8k },

	{ VENDOR_ID_MONTEREY, DEVICE_ID_GENIUS_KB29E, quirk_cherry_genius_29e },

	{ VENDOR_ID_PETALYNX, DEVICE_ID_PETALYNX_MAXTER_REMOTE, quirk_petalynx_remote },

	{ VENDOR_ID_SUNPLUS, DEVICE_ID_SUNPLUS_WDESKTOP, quirk_sunplus_wdesktop },

	{ 0, 0, NULL }
};

int hidinput_mapping_quirks(struct hid_usage *usage, 
				   struct input_dev *input, 
				   unsigned long **bit, int *max)
{
	struct hid_device *device = input_get_drvdata(input);
	int i = 0;
	
	while (hid_input_blacklist[i].quirk) {
		if (hid_input_blacklist[i].idVendor == device->vendor &&
				hid_input_blacklist[i].idProduct == device->product)
			return hid_input_blacklist[i].quirk(usage, input, bit, max);
		i++;
	}
	return 0;
}

int hidinput_event_quirks(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct input_dev *input;

	input = field->hidinput->input;

	if (((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_5) && (usage->hid == 0x00090005))
		|| ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_7) && (usage->hid == 0x00090007))) {
		if (value) hid->quirks |=  HID_QUIRK_2WHEEL_MOUSE_HACK_ON;
		else       hid->quirks &= ~HID_QUIRK_2WHEEL_MOUSE_HACK_ON;
		return 1;
	}

	if ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_B8) &&
			(usage->type == EV_REL) &&
			(usage->code == REL_WHEEL)) {
		hid->delayed_value = value;
		return 1;
	}

	if ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_B8) &&
			(usage->hid == 0x000100b8)) {
		input_event(input, EV_REL, value ? REL_HWHEEL : REL_WHEEL, hid->delayed_value);
		return 1;
	}

	if ((hid->quirks & HID_QUIRK_INVERT_HWHEEL) && (usage->code == REL_HWHEEL)) {
		input_event(input, usage->type, usage->code, -value);
		return 1;
	}

	if ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_ON) && (usage->code == REL_WHEEL)) {
		input_event(input, usage->type, REL_HWHEEL, value);
		return 1;
	}

	if ((hid->quirks & HID_QUIRK_APPLE_HAS_FN) && hidinput_apple_event(hid, input, usage, value))
		return 1;

	/* Handling MS keyboards special buttons */
	if (hid->quirks & HID_QUIRK_MICROSOFT_KEYS && 
			usage->hid == (HID_UP_MSVENDOR | 0xff05)) {
		int key = 0;
		static int last_key = 0;
		switch (value) {
			case 0x01: key = KEY_F14; break;
			case 0x02: key = KEY_F15; break;
			case 0x04: key = KEY_F16; break;
			case 0x08: key = KEY_F17; break;
			case 0x10: key = KEY_F18; break;
			default: break;
		}
		if (key) {
			input_event(input, usage->type, key, 1);
			last_key = key;
		} else {
			input_event(input, usage->type, last_key, 0);
		}
	}

	/* handle the temporary quirky mapping to HWHEEL */
	if (hid->quirks & HID_QUIRK_HWHEEL_WHEEL_INVERT &&
			usage->type == EV_REL && usage->code == REL_HWHEEL) {
		input_event(input, usage->type, REL_WHEEL, -value);
		return 1;
	}

	/* Gyration MCE remote "Sleep" key */
	if (hid->vendor == VENDOR_ID_GYRATION &&
	    hid->product == DEVICE_ID_GYRATION_REMOTE &&
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_GENDESK &&
	    (usage->hid & 0xff) == 0x82) {
		input_event(input, usage->type, usage->code, 1);
		input_sync(input);
		input_event(input, usage->type, usage->code, 0);
		input_sync(input);
		return 1;
	}
	return 0;
}


