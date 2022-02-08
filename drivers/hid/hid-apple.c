// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "hid-ids.h"

#define APPLE_RDESC_JIS		BIT(0)
#define APPLE_IGNORE_MOUSE	BIT(1)
#define APPLE_HAS_FN		BIT(2)
/* BIT(3) reserved, was: APPLE_HIDDEV */
#define APPLE_ISO_TILDE_QUIRK	BIT(4)
#define APPLE_MIGHTYMOUSE	BIT(5)
#define APPLE_INVERT_HWHEEL	BIT(6)
/* BIT(7) reserved, was: APPLE_IGNORE_HIDINPUT */
#define APPLE_NUMLOCK_EMULATION	BIT(8)
#define APPLE_RDESC_BATTERY	BIT(9)

#define APPLE_FLAG_FKEY		0x01

#define HID_COUNTRY_INTERNATIONAL_ISO	13
#define APPLE_BATTERY_TIMEOUT_MS	60000

static unsigned int fnmode = 1;
module_param(fnmode, uint, 0644);
MODULE_PARM_DESC(fnmode, "Mode of fn key on Apple keyboards (0 = disabled, "
		"[1] = fkeyslast, 2 = fkeysfirst)");

static int iso_layout = -1;
module_param(iso_layout, int, 0644);
MODULE_PARM_DESC(iso_layout, "Swap the backtick/tilde and greater-than/less-than keys. "
		"([-1] = auto, 0 = disabled, 1 = enabled)");

static unsigned int swap_opt_cmd;
module_param(swap_opt_cmd, uint, 0644);
MODULE_PARM_DESC(swap_opt_cmd, "Swap the Option (\"Alt\") and Command (\"Flag\") keys. "
		"(For people who want to keep Windows PC keyboard muscle memory. "
		"[0] = as-is, Mac layout. 1 = swapped, Windows layout.)");

static unsigned int swap_fn_leftctrl;
module_param(swap_fn_leftctrl, uint, 0644);
MODULE_PARM_DESC(swap_fn_leftctrl, "Swap the Fn and left Control keys. "
		"(For people who want to keep PC keyboard muscle memory. "
		"[0] = as-is, Mac layout, 1 = swapped, PC layout)");

struct apple_sc {
	struct hid_device *hdev;
	unsigned long quirks;
	unsigned int fn_on;
	unsigned int fn_found;
	DECLARE_BITMAP(pressed_numlock, KEY_CNT);
	struct timer_list battery_timer;
};

struct apple_key_translation {
	u16 from;
	u16 to;
	u8 flags;
};

static const struct apple_key_translation magic_keyboard_alu_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN, APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,   APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_SCALE,          APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_DASHBOARD,      APPLE_FLAG_FKEY },
	{ KEY_F6,	KEY_NUMLOCK,        APPLE_FLAG_FKEY },
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

static const struct apple_key_translation apple2021_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN, APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,   APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_SCALE,          APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_SEARCH,         APPLE_FLAG_FKEY },
	{ KEY_F5,	KEY_MICMUTE,        APPLE_FLAG_FKEY },
	{ KEY_F6,	KEY_SLEEP,          APPLE_FLAG_FKEY },
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

static const struct apple_key_translation swapped_fn_leftctrl_keys[] = {
	{ KEY_FN, KEY_LEFTCTRL },
	{ }
};

static inline void apple_setup_key_translation(struct input_dev *input,
		const struct apple_key_translation *table)
{
	const struct apple_key_translation *trans;

	for (trans = table; trans->from; trans++)
		set_bit(trans->to, input->keybit);
}

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

static void input_event_with_scancode(struct input_dev *input,
		__u8 type, __u16 code, unsigned int hid, __s32 value)
{
	if (type == EV_KEY &&
	    (!test_bit(code, input->key)) == value)
		input_event(input, EV_MSC, MSC_SCAN, hid);
	input_event(input, type, code, value);
}

static int hidinput_apple_event(struct hid_device *hid, struct input_dev *input,
		struct hid_usage *usage, __s32 value)
{
	struct apple_sc *asc = hid_get_drvdata(hid);
	const struct apple_key_translation *trans, *table;
	bool do_translate;
	u16 code = 0;

	u16 fn_keycode = (swap_fn_leftctrl) ? (KEY_LEFTCTRL) : (KEY_FN);

	if (usage->code == fn_keycode) {
		asc->fn_on = !!value;
		input_event_with_scancode(input, usage->type, KEY_FN,
				usage->hid, value);
		return 1;
	}

	if (fnmode) {
		if (hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_ANSI ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_ISO ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_JIS ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ANSI ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ISO ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_JIS ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ANSI ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ISO ||
		    hid->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_JIS)
			table = magic_keyboard_alu_fn_keys;
		else if (hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2021 ||
			 hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_FINGERPRINT_2021 ||
			 hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2021)
			table = apple2021_fn_keys;
		else if (hid->product >= USB_DEVICE_ID_APPLE_WELLSPRING4_ANSI &&
				hid->product <= USB_DEVICE_ID_APPLE_WELLSPRING4A_JIS)
			table = macbookair_fn_keys;
		else if (hid->product < 0x21d || hid->product >= 0x300)
			table = powerbook_fn_keys;
		else
			table = apple_fn_keys;

		trans = apple_find_translation (table, usage->code);

		if (trans) {
			if (test_bit(trans->from, input->key))
				code = trans->from;
			else if (test_bit(trans->to, input->key))
				code = trans->to;

			if (!code) {
				if (trans->flags & APPLE_FLAG_FKEY) {
					switch (fnmode) {
					case 1:
						do_translate = !asc->fn_on;
						break;
					case 2:
						do_translate = asc->fn_on;
						break;
					default:
						/* should never happen */
						do_translate = false;
					}
				} else {
					do_translate = asc->fn_on;
				}

				code = do_translate ? trans->to : trans->from;
			}

			input_event_with_scancode(input, usage->type, code,
					usage->hid, value);
			return 1;
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

				input_event_with_scancode(input, usage->type,
						trans->to, usage->hid, value);
			}

			return 1;
		}
	}

	if (iso_layout > 0 || (iso_layout < 0 && (asc->quirks & APPLE_ISO_TILDE_QUIRK) &&
			hid->country == HID_COUNTRY_INTERNATIONAL_ISO)) {
		trans = apple_find_translation(apple_iso_keyboard, usage->code);
		if (trans) {
			input_event_with_scancode(input, usage->type,
					trans->to, usage->hid, value);
			return 1;
		}
	}

	if (swap_opt_cmd) {
		trans = apple_find_translation(swapped_option_cmd_keys, usage->code);
		if (trans) {
			input_event_with_scancode(input, usage->type,
					trans->to, usage->hid, value);
			return 1;
		}
	}

	if (swap_fn_leftctrl) {
		trans = apple_find_translation(swapped_fn_leftctrl_keys, usage->code);
		if (trans) {
			input_event_with_scancode(input, usage->type,
					trans->to, usage->hid, value);
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
		input_event_with_scancode(field->hidinput->input, usage->type,
				usage->code, usage->hid, -value);
		return 1;
	}

	if ((asc->quirks & APPLE_HAS_FN) &&
			hidinput_apple_event(hdev, field->hidinput->input,
				usage, value))
		return 1;


	return 0;
}

static int apple_fetch_battery(struct hid_device *hdev)
{
#ifdef CONFIG_HID_BATTERY_STRENGTH
	struct apple_sc *asc = hid_get_drvdata(hdev);
	struct hid_report_enum *report_enum;
	struct hid_report *report;

	if (!(asc->quirks & APPLE_RDESC_BATTERY) || !hdev->battery)
		return -1;

	report_enum = &hdev->report_enum[hdev->battery_report_type];
	report = report_enum->report_id_hash[hdev->battery_report_id];

	if (!report || report->maxfield < 1)
		return -1;

	if (hdev->battery_capacity == hdev->battery_max)
		return -1;

	hid_hw_request(hdev, report, HID_REQ_GET_REPORT);
	return 0;
#else
	return -1;
#endif
}

static void apple_battery_timer_tick(struct timer_list *t)
{
	struct apple_sc *asc = from_timer(asc, t, battery_timer);
	struct hid_device *hdev = asc->hdev;

	if (apple_fetch_battery(hdev) == 0) {
		mod_timer(&asc->battery_timer,
			  jiffies + msecs_to_jiffies(APPLE_BATTERY_TIMEOUT_MS));
	}
}

/*
 * MacBook JIS keyboard has wrong logical maximum
 * Magic Keyboard JIS has wrong logical maximum
 */
static __u8 *apple_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	if(*rsize >=71 && rdesc[70] == 0x65 && rdesc[64] == 0x65) {
		hid_info(hdev,
			 "fixing up Magic Keyboard JIS report descriptor\n");
		rdesc[64] = rdesc[70] = 0xe7;
	}

	if ((asc->quirks & APPLE_RDESC_JIS) && *rsize >= 60 &&
			rdesc[53] == 0x65 && rdesc[59] == 0x65) {
		hid_info(hdev,
			 "fixing up MacBook JIS keyboard report descriptor\n");
		rdesc[53] = rdesc[59] = 0xe7;
	}

	/*
	 * Change the usage from:
	 *   0x06, 0x00, 0xff, // Usage Page (Vendor Defined Page 1)  0
	 *   0x09, 0x0b,       // Usage (Vendor Usage 0x0b)           3
	 * To:
	 *   0x05, 0x01,       // Usage Page (Generic Desktop)        0
	 *   0x09, 0x06,       // Usage (Keyboard)                    2
	 */
	if ((asc->quirks & APPLE_RDESC_BATTERY) && *rsize == 83 &&
	    rdesc[46] == 0x84 && rdesc[58] == 0x85) {
		hid_info(hdev,
			 "fixing up Magic Keyboard battery report descriptor\n");
		*rsize = *rsize - 1;
		rdesc = kmemdup(rdesc + 1, *rsize, GFP_KERNEL);
		if (!rdesc)
			return NULL;

		rdesc[0] = 0x05;
		rdesc[1] = 0x01;
		rdesc[2] = 0x09;
		rdesc[3] = 0x06;
	}

	return rdesc;
}

static void apple_setup_input(struct input_dev *input)
{
	set_bit(KEY_NUMLOCK, input->keybit);

	/* Enable all needed keys */
	apple_setup_key_translation(input, apple_fn_keys);
	apple_setup_key_translation(input, powerbook_fn_keys);
	apple_setup_key_translation(input, powerbook_numlock_keys);
	apple_setup_key_translation(input, apple_iso_keyboard);
	apple_setup_key_translation(input, magic_keyboard_alu_fn_keys);
	apple_setup_key_translation(input, apple2021_fn_keys);

	if (swap_fn_leftctrl)
		apple_setup_key_translation(input, swapped_fn_leftctrl_keys);
}

static int apple_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	if (usage->hid == (HID_UP_CUSTOM | 0x0003) ||
			usage->hid == (HID_UP_MSVENDOR | 0x0003) ||
			usage->hid == (HID_UP_HPVENDOR2 | 0x0003)) {
		/* The fn key on Apple USB keyboards */
		set_bit(EV_REP, hi->input->evbit);
		hid_map_usage_clear(hi, usage, bit, max, EV_KEY, KEY_FN);
		asc->fn_found = true;
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

static int apple_input_configured(struct hid_device *hdev,
		struct hid_input *hidinput)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	if ((asc->quirks & APPLE_HAS_FN) && !asc->fn_found) {
		hid_info(hdev, "Fn key not found (Apple Wireless Keyboard clone?), disabling Fn key handling\n");
		asc->quirks &= ~APPLE_HAS_FN;
	}

	return 0;
}

static int apple_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	struct apple_sc *asc;
	int ret;

	asc = devm_kzalloc(&hdev->dev, sizeof(*asc), GFP_KERNEL);
	if (asc == NULL) {
		hid_err(hdev, "can't alloc apple descriptor\n");
		return -ENOMEM;
	}

	asc->hdev = hdev;
	asc->quirks = quirks;

	hid_set_drvdata(hdev, asc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	timer_setup(&asc->battery_timer, apple_battery_timer_tick, 0);
	mod_timer(&asc->battery_timer,
		  jiffies + msecs_to_jiffies(APPLE_BATTERY_TIMEOUT_MS));
	apple_fetch_battery(hdev);

	return 0;
}

static void apple_remove(struct hid_device *hdev)
{
	struct apple_sc *asc = hid_get_drvdata(hdev);

	del_timer_sync(&asc->battery_timer);

	hid_hw_stop(hdev);
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
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_MINI_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_MINI_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_MINI_JIS),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_JIS),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_HF_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_HF_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_HF_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_REVB_JIS),
		.driver_data = APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_TILDE_QUIRK },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_TILDE_QUIRK },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
				USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
				USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2015),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK | APPLE_RDESC_BATTERY },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2015),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2015),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK | APPLE_RDESC_BATTERY },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2015),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_ISO),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING6A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING5A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING7A_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING8_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING8_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING8_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING9_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING9_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING9_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_TILDE_QUIRK },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_TP_ONLY),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER1_TP_ONLY),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_FINGERPRINT_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_FINGERPRINT_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },

	{ }
};
MODULE_DEVICE_TABLE(hid, apple_devices);

static struct hid_driver apple_driver = {
	.name = "apple",
	.id_table = apple_devices,
	.report_fixup = apple_report_fixup,
	.probe = apple_probe,
	.remove = apple_remove,
	.event = apple_event,
	.input_mapping = apple_input_mapping,
	.input_mapped = apple_input_mapped,
	.input_configured = apple_input_configured,
};
module_hid_driver(apple_driver);

MODULE_LICENSE("GPL");
