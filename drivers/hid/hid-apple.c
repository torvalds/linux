/*
 *  USB HID quirks support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby <jirislaby@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "hid-ids.h"

#define APPLE_RDESC_JIS		0x0001
#define APPLE_IGNORE_MOUSE	0x0002
#define APPLE_HAS_FN		0x0004
#define APPLE_HIDDEV		0x0008
#define APPLE_ISO_KEYBOARD	0x0010
#define APPLE_MIGHTYMOUSE	0x0020
#define APPLE_INVERT_HWHEEL	0x0040
#define APPLE_IGNORE_HIDINPUT	0x0080
#define APPLE_NUMLOCK_EMULATION	0x0100

#define APPLE_FLAG_FKEY		0x01

static unsigned int fnmode = 1;
module_param(fnmode, uint, 0644);
MODULE_PARM_DESC(fnmode, "Mode of fn key on Apple keyboards (0 = disabled, "
		"[1] = fkeyslast, 2 = fkeysfirst)");

static unsigned int iso_layout = 1;
module_param(iso_layout, uint, 0644);
MODULE_PARM_DESC(iso_layout, "Enable/Disable hardcoded ISO-layout of the keyboard. "
		"(0 = disabled, [1] = enabled)");

static unsigned int swap_opt_cmd;
module_param(swap_opt_cmd, uint, 0644);
MODULE_PARM_DESC(swap_opt_cmd, "Swap the Option (\"Alt\") and Command (\"Flag\") keys. "
		"(For people who want to keep Windows PC keyboard muscle memory. "
		"[0] = as-is, Mac layout. 1 = swapped, Windows layout.)");

struct apple_sc {
	unsigned long quirks;
	unsigned int fn_on;
	DECLARE_BITMAP(pressed_fn, KEY_CNT);
	DECLARE_BITMAP(pressed_numlock, KEY_CNT);
};

struct apple_key_translation {
	u16 from;
	u16 to;
	u8 flags;
};

static const struct apple_key_translation macbookair_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN, APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,   APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_SCALE,          APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_DASHBOARD,      APPLE_FLAG_FKEY },
	{ KEY_F6,	KEY_PREVIOUSSONG,   APPLE_FLAG_FKEY },
	{ KEY_F7,	KEY_PLAYPAUSE,      APPLE_FLAG_FKEY },
	{ KEY_F8,	KEY_NEXTSONG,       APPLE_FLAG_FKEY },
	{ KEY_F9,	KEY_MUTE,           APPLE_FLAG_FKEY },
	{ KEY_F10,	KEY_VOLUMEDOWN,     APPLE_FLAG_FKEY },
	{ KEY_F11,	KEY_VOLUMEUP,       APPLE_FLAG_FKEY },
	{ KEY_F12,	KEY_EJECTCD,        APPLE_FLAG_FKEY },
	{ KEY_UP,	KEY_PAGEUP },
	{ KEY_DOWN,	KEY_PAGEDOWN },
	{ KEY_LEFT,	KEY_HOME },
	{ KEY_RIGHT,	KEY_END },
	{ }
};

static const struct apple_key_translation apple_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN, APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,   APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_SCALE,          APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_DASHBOARD,      APPLE_FLAG_FKEY },
	{ KEY_F5,	KEY_KBDILLUMDOWN,   APPLE_FLAG_FKEY },
	{ KEY_F6,	KEY_KBDILLUMUP,     APPLE_FLAG_FKEY },
	{ KEY_F7,	KEY_PREVIOUSSONG,   APPLE_FLAG_FKEY },
	{ KEY_F8,	KEY_PLAYPAUSE,      APPLE_FLAG_FKEY },
	{ KEY_F9,	KEY_NEXTSONG,       APPLE_FLAG_FKEY },
	{ KEY_F10,	KEY_MUTE,           APPLE_FLAG_FKEY },
	{ KEY_F11,	KEY_VOLUMEDOWN,     APPLE_FLAG_FKEY },
	{ KEY_F12,	KEY_VOLUMEUP,       APPLE_FLAG_FKEY },
	{ KEY_UP,	KEY_PAGEUP },
	{ KEY_DOWN,	KEY_PAGEDOWN },
	{ KEY_LEFT,	KEY_HOME },
	{ KEY_RIGHT,	KEY_END },
	{ }
};

static const struct apple_key_translation powerbook_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN,     APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,       APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_MUTE,               APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_VOLUMEDOWN,         APPLE_FLAG_FKEY },
	{ KEY_F5,	KEY_VOLUMEUP,           APPLE_FLAG_FKEY },
	{ KEY_F6,	KEY_NUMLOCK,            APPLE_FLAG_FKEY },
	{ KEY_F7,	KEY_SWITCHVIDEOMODE,    APPLE_FLAG_FKEY },
	{ KEY_F8,	KEY_KBDILLUMTOGGLE,     APPLE_FLAG_FKEY },
	{ KEY_F9,	KEY_KBDILLUMDOWN,       APPLE_FLAG_FKEY },
	{ KEY_F10,	KEY_KBDILLUMUP,         APPLE_FLAG_FKEY },
	{ KEY_UP,	KEY_PAGEUP },
	{ KEY_DOWN,	KEY_PAGEDOWN },
	{ KEY_LEFT,	KEY_HOME },
	{ KEY_RIGHT,	KEY_END },
	{ }
};

static const struct apple_key_translation powerbook_numlock_keys[] = {
	{ KEY_J,	KEY_KP1 },
	{ KEY_K,	KEY_KP2 },
	{ KEY_L,	KEY_KP3 },
	{ KEY_U,	KEY_KP4 },
	{ KEY_I,	KEY_KP5 },
	{ KEY_O,	KEY_KP6 },
	{ KEY_7,	KEY_KP7 },
	{ KEY_8,	KEY_KP8 },
	{ KEY_9,	KEY_KP9 },
	{ KEY_M,	KEY_KP0 },
	{ KEY_DOT,	KEY_KPDOT },
	{ KEY_SLASH,	KEY_KPPLUS },
	{ KEY_SEMICOLON, KEY_KPMINUS },
	{ KEY_P,	KEY_KPASTERISK },
	{ KEY_MINUS,	KEY_KPEQUAL },
	{ KEY_0,	KEY_KPSLASH },
	{ KEY_F6,	KEY_NUMLOCK },
	{ KEY_KPENTER,	KEY_KPENTER },
	{ KEY_BACKSPACE, KEY_BACKSPACE },
	{ }
};

static const struct apple_key_translation apple_iso_keyboard[] = {
	{ KEY_GRAVE,	KEY_102ND },
	{ KEY_102ND,	KEY_GRAVE },
	{ }
};

static const struct apple_key_translation swapped_option_cmd_keys[] = {
	{ KEY_LEFTALT,	KEY_LEFTMETA },
	{ KEY_LEFTMETA,	KEY_LEFTALT },
	{ KEY_RIGHTALT,	KEY_RIGHTMETA },
	{ KEY_RIGHTMETA,KEY_RIGHTALT },
	{ }
};

static const struct apple_key_translation *apple_find_translation(
		const struct apple_key_translation *table, u16 from)
{
	const struct apple_key_translation *trans;

	/* Look for the translation */
	for (trans = table; trans->from; trans++)
		if (trans->from == from)
			return trans;

	return NULL;
}

static int hidinput_apple_event(struct hid_device *hid, struct input_dev *input,
		struct hid_usage *usage, __s32 value)
{
	struct apple_sc *asc = hid_get_drvdata(hid);
	const struct apple_key_translation *trans, *table;

	if (usage->code == KEY_FN) {
		asc->fn_on = !!value;
		input_event(input, usage->type, usage->code, value);
		return 1;
	}

	if (fnmode) {
		int do_translate;

		if (hid->product >= USB_DEVICE_ID_APPLE_WELLSPRING4_ANSI &&
				hid->product <= USB_DEVICE_ID_APPLE_WELLSPRING4A_JIS)
			table = macbookair_fn_keys;
		else if (hid->product < 0x21d || hid->product >= 0x300)
			table = powerbook_fn_keys;
		else
			table = apple_fn_keys;

		trans = apple_find_translation (table, usage->code);

		if (trans) {
			if (test_bit(usage->code, asc->pressed_fn))
				do_translate = 1;
			else if (trans->flags & APPLE_FLAG_FKEY)
				do_translate = (fnmode == 2 && asc->fn_on) ||
					(fnmode == 1 && !asc->fn_on);
			else
				do_translate = asc->fn_on;

			if (do_translate) {
				if (value)
					set_bit(usage->code, asc->pressed_fn);
				else
					clear_bit(usage->code, asc->pressed_fn);

				input_event(input, usage->type, trans->to,
						value);

				return 1;
			}
		}

		if (asc->quirks & APPLE_NUMLOCK_EMULATION &&
				(test_bit(usage->code, asc->pressed_numlock) ||
				test_bit(LED_NUML, input->led))) {
			trans = apple_find_translation(powerbook_numlock_keys,
					usage->code);

			if (trans) {
				if (value)
					set_bit(usage->code,
							asc->pressed_numlock);
				else
					clear_bit(usage->code,
							asc->pressed_numlock);

				input_event(input, usage->type, trans->to,
						value);
			}

			return 1;
		}
	}

	if (iso_layout) {
		if (asc->quirks & APPLE_ISO_KEYBOARD) {
			trans = apple_find_translation(apple_iso_keyboard, usage->code);
			if (trans) {
				input_event(input, usage->type, trans->to, value);
				return 1;
			}
		}
	}

	if (swap_opt_cmd) {
		trans = apple_find_translation(swapped_option_cmd_keys, usage->code);
		if (trans) {
			input_event(input, usage->type, trans->to, value);
			return 1;
		}
	}

	return 0;
}

static int apple_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput ||
			!usage->type)
		return 0;

	if ((asc->quirks & APPLE_INVERT_HWHEEL) &&
			usage->code == REL_HWHEEL) {
		input_event(field->hidinput->input, usage->type, usage->code,
				-value);
		return 1;
	}

	if ((asc->quirks & APPLE_HAS_FN) &&
			hidinput_apple_event(hdev, field->hidinput->input,
				usage, value))
		return 1;


	return 0;
}

/*
 * MacBook JIS keyboard has wrong logical maximum
 */
static __u8 *apple_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	if ((asc->quirks & APPLE_RDESC_JIS) && *rsize >= 60 &&
			rdesc[53] == 0x65 && rdesc[59] == 0x65) {
		hid_info(hdev,
			 "fixing up MacBook JIS keyboard report descriptor\n");
		rdesc[53] = rdesc[59] = 0xe7;
	}
	return rdesc;
}

static void apple_setup_input(struct input_dev *input)
{
	const struct apple_key_translation *trans;

	set_bit(KEY_NUMLOCK, input->keybit);

	/* Enable all needed keys */
	for (trans = apple_fn_keys; trans->from; trans++)
		set_bit(trans->to, input->keybit);

	for (trans = powerbook_fn_keys; trans->from; trans++)
		set_bit(trans->to, input->keybit);

	for (trans = powerbook_numlock_keys; trans->from; trans++)
		set_bit(trans->to, input->keybit);

	for (trans = apple_iso_keyboard; trans->from; trans++)
		set_bit(trans->to, input->keybit);
}

static int apple_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if (usage->hid == (HID_UP_CUSTOM | 0x0003)) {
		/* The fn key on Apple USB keyboards */
		set_bit(EV_REP, hi->input->evbit);
		hid_map_usage_clear(hi, usage, bit, max, EV_KEY, KEY_FN);
		apple_setup_input(hi->input);
		return 1;
	}

	/* we want the hid layer to go through standard path (set and ignore) */
	return 0;
}

static int apple_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	if (asc->quirks & APPLE_MIGHTYMOUSE) {
		if (usage->hid == HID_GD_Z)
			hid_map_usage(hi, usage, bit, max, EV_REL, REL_HWHEEL);
		else if (usage->code == BTN_1)
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_2);
		else if (usage->code == BTN_2)
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_1);
	}

	return 0;
}

static int apple_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	struct apple_sc *asc;
	unsigned int connect_mask = HID_CONNECT_DEFAULT;
	int ret;

	asc = devm_kzalloc(&hdev->dev, sizeof(*asc), GFP_KERNEL);
	if (asc == NULL) {
		hid_err(hdev, "can't alloc apple descriptor\n");
		return -ENOMEM;
	}

	asc->quirks = quirks;

	hid_set_drvdata(hdev, asc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	if (quirks & APPLE_HIDDEV)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	if (quirks & APPLE_IGNORE_HIDINPUT)
		connect_mask &= ~HID_CONNECT_HIDINPUT;

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static const struct hid_device_id apple_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MIGHTYMOUSE),
		.driver_data = APPLE_MIGHTYMOUSE | APPLE_INVERT_HWHEEL },

	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_MINI_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_MINI_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_MINI_JIS),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_JIS),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_HF_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_HF_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_HF_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_JIS),
		.driver_data = APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
				USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
				USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING8_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING8_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING8_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING9_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING9_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING9_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_KEYBOARD },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_TP_ONLY),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER1_TP_ONLY),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },

	{ }
};
MODULE_DEVICE_TABLE(hid, apple_devices);

static struct hid_driver apple_driver = {
	.name = "apple",
	.id_table = apple_devices,
	.report_fixup = apple_report_fixup,
	.probe = apple_probe,
	.event = apple_event,
	.input_mapping = apple_input_mapping,
	.input_mapped = apple_input_mapped,
};
module_hid_driver(apple_driver);

MODULE_LICENSE("GPL");
