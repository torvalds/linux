// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  USB HID quirks support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby <jirislaby@gmail.com>
 *  Copyright (c) 2019 Paul Pawlowski <paul@mrarm.io>
 *  Copyright (c) 2023 Orlando Chamberlain <orlandoch.dev@gmail.com>
 *  Copyright (c) 2024 Aditya Garg <gargaditya08@live.com>
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
#include <linux/string.h>
#include <linux/leds.h>
#include <dt-bindings/leds/common.h>

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
#define APPLE_BACKLIGHT_CTL	BIT(10)
#define APPLE_IS_NON_APPLE	BIT(11)
#define APPLE_MAGIC_BACKLIGHT	BIT(12)

#define APPLE_FLAG_FKEY		0x01

#define HID_COUNTRY_INTERNATIONAL_ISO	13
#define APPLE_BATTERY_TIMEOUT_MS	60000

#define HID_USAGE_MAGIC_BL			0xff00000f
#define APPLE_MAGIC_REPORT_ID_POWER		3
#define APPLE_MAGIC_REPORT_ID_BRIGHTNESS	1

static unsigned int fnmode = 3;
module_param(fnmode, uint, 0644);
MODULE_PARM_DESC(fnmode, "Mode of fn key on Apple keyboards (0 = disabled, "
		"1 = fkeyslast, 2 = fkeysfirst, [3] = auto)");

static int iso_layout = -1;
module_param(iso_layout, int, 0644);
MODULE_PARM_DESC(iso_layout, "Swap the backtick/tilde and greater-than/less-than keys. "
		"([-1] = auto, 0 = disabled, 1 = enabled)");

static unsigned int swap_opt_cmd;
module_param(swap_opt_cmd, uint, 0644);
MODULE_PARM_DESC(swap_opt_cmd, "Swap the Option (\"Alt\") and Command (\"Flag\") keys. "
		"(For people who want to keep Windows PC keyboard muscle memory. "
		"[0] = as-is, Mac layout. 1 = swapped, Windows layout., 2 = swapped, Swap only left side)");

static unsigned int swap_ctrl_cmd;
module_param(swap_ctrl_cmd, uint, 0644);
MODULE_PARM_DESC(swap_ctrl_cmd, "Swap the Control (\"Ctrl\") and Command (\"Flag\") keys. "
		"(For people who are used to Mac shortcuts involving Command instead of Control. "
		"[0] = No change. 1 = Swapped.)");

static unsigned int swap_fn_leftctrl;
module_param(swap_fn_leftctrl, uint, 0644);
MODULE_PARM_DESC(swap_fn_leftctrl, "Swap the Fn and left Control keys. "
		"(For people who want to keep PC keyboard muscle memory. "
		"[0] = as-is, Mac layout, 1 = swapped, PC layout)");

struct apple_non_apple_keyboard {
	char *name;
};

struct apple_sc_backlight {
	struct led_classdev cdev;
	struct hid_device *hdev;
};

struct apple_magic_backlight {
	struct led_classdev cdev;
	struct hid_report *brightness;
	struct hid_report *power;
};

struct apple_sc {
	struct hid_device *hdev;
	unsigned long quirks;
	unsigned int fn_on;
	unsigned int fn_found;
	DECLARE_BITMAP(pressed_numlock, KEY_CNT);
	struct timer_list battery_timer;
	struct apple_sc_backlight *backlight;
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

static const struct apple_key_translation magic_keyboard_2015_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN, APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,   APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_SCALE,          APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_DASHBOARD,      APPLE_FLAG_FKEY },
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

struct apple_backlight_config_report {
	u8 report_id;
	u8 version;
	u16 backlight_off, backlight_on_min, backlight_on_max;
};

struct apple_backlight_set_report {
	u8 report_id;
	u8 version;
	u16 backlight;
	u16 rate;
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

static const struct apple_key_translation macbookpro_no_esc_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_GRAVE,	KEY_ESC },
	{ KEY_1,	KEY_F1 },
	{ KEY_2,	KEY_F2 },
	{ KEY_3,	KEY_F3 },
	{ KEY_4,	KEY_F4 },
	{ KEY_5,	KEY_F5 },
	{ KEY_6,	KEY_F6 },
	{ KEY_7,	KEY_F7 },
	{ KEY_8,	KEY_F8 },
	{ KEY_9,	KEY_F9 },
	{ KEY_0,	KEY_F10 },
	{ KEY_MINUS,	KEY_F11 },
	{ KEY_EQUAL,	KEY_F12 },
	{ KEY_UP,	KEY_PAGEUP },
	{ KEY_DOWN,	KEY_PAGEDOWN },
	{ KEY_LEFT,	KEY_HOME },
	{ KEY_RIGHT,	KEY_END },
	{ }
};

static const struct apple_key_translation macbookpro_dedicated_esc_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_1,	KEY_F1 },
	{ KEY_2,	KEY_F2 },
	{ KEY_3,	KEY_F3 },
	{ KEY_4,	KEY_F4 },
	{ KEY_5,	KEY_F5 },
	{ KEY_6,	KEY_F6 },
	{ KEY_7,	KEY_F7 },
	{ KEY_8,	KEY_F8 },
	{ KEY_9,	KEY_F9 },
	{ KEY_0,	KEY_F10 },
	{ KEY_MINUS,	KEY_F11 },
	{ KEY_EQUAL,	KEY_F12 },
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
	{ KEY_RIGHTMETA, KEY_RIGHTALT },
	{ }
};

static const struct apple_key_translation swapped_option_cmd_left_keys[] = {
	{ KEY_LEFTALT,	KEY_LEFTMETA },
	{ KEY_LEFTMETA,	KEY_LEFTALT },
	{ }
};

static const struct apple_key_translation swapped_ctrl_cmd_keys[] = {
	{ KEY_LEFTCTRL,	KEY_LEFTMETA },
	{ KEY_LEFTMETA,	KEY_LEFTCTRL },
	{ KEY_RIGHTCTRL, KEY_RIGHTMETA },
	{ KEY_RIGHTMETA, KEY_RIGHTCTRL },
	{ }
};

static const struct apple_key_translation swapped_fn_leftctrl_keys[] = {
	{ KEY_FN, KEY_LEFTCTRL },
	{ KEY_LEFTCTRL, KEY_FN },
	{ }
};

static const struct apple_non_apple_keyboard non_apple_keyboards[] = {
	{ "SONiX USB DEVICE" },
	{ "Keychron" },
	{ "AONE" },
	{ "GANSS" },
	{ "Hailuck" },
	{ "Jamesdonkey" },
	{ "A3R" },
	{ "hfd.cn" },
	{ "WKB603" },
};

static bool apple_is_non_apple_keyboard(struct hid_device *hdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(non_apple_keyboards); i++) {
		char *non_apple = non_apple_keyboards[i].name;

		if (strncmp(hdev->name, non_apple, strlen(non_apple)) == 0)
			return true;
	}

	return false;
}

static bool apple_is_omoton_kb066(struct hid_device *hdev)
{
	return hdev->product == USB_DEVICE_ID_APPLE_ALU_WIRELESS_ANSI &&
		strcmp(hdev->name, "Bluetooth Keyboard") == 0;
}

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
	u16 code = usage->code;
	unsigned int real_fnmode;

	if (fnmode == 3) {
		real_fnmode = (asc->quirks & APPLE_IS_NON_APPLE) ? 2 : 1;
	} else {
		real_fnmode = fnmode;
	}

	if (swap_fn_leftctrl) {
		trans = apple_find_translation(swapped_fn_leftctrl_keys, code);

		if (trans)
			code = trans->to;
	}

	if (iso_layout > 0 || (iso_layout < 0 && (asc->quirks & APPLE_ISO_TILDE_QUIRK) &&
			hid->country == HID_COUNTRY_INTERNATIONAL_ISO)) {
		trans = apple_find_translation(apple_iso_keyboard, code);

		if (trans)
			code = trans->to;
	}

	if (swap_opt_cmd) {
		if (swap_opt_cmd == 2)
			trans = apple_find_translation(swapped_option_cmd_left_keys, code);
		else
			trans = apple_find_translation(swapped_option_cmd_keys, code);

		if (trans)
			code = trans->to;
	}

	if (swap_ctrl_cmd) {
		trans = apple_find_translation(swapped_ctrl_cmd_keys, code);

		if (trans)
			code = trans->to;
	}

	if (code == KEY_FN)
		asc->fn_on = !!value;

	if (real_fnmode) {
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
		else if (hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2015 ||
			 hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2015)
			table = magic_keyboard_2015_fn_keys;
		else if (hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2021 ||
			 hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2024 ||
			 hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_FINGERPRINT_2021 ||
			 hid->product == USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2021)
			table = apple2021_fn_keys;
		else if (hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J132 ||
			 hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J680 ||
			 hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J213)
				table = macbookpro_no_esc_fn_keys;
		else if (hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J214K ||
			 hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J223 ||
			 hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J152F)
				table = macbookpro_dedicated_esc_fn_keys;
		else if (hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J140K ||
			 hid->product == USB_DEVICE_ID_APPLE_WELLSPRINGT2_J230K)
				table = apple_fn_keys;
		else if (hid->product >= USB_DEVICE_ID_APPLE_WELLSPRING4_ANSI &&
				hid->product <= USB_DEVICE_ID_APPLE_WELLSPRING4A_JIS)
			table = macbookair_fn_keys;
		else if (hid->product < 0x21d || hid->product >= 0x300)
			table = powerbook_fn_keys;
		else
			table = apple_fn_keys;

		trans = apple_find_translation(table, code);

		if (trans) {
			bool from_is_set = test_bit(trans->from, input->key);
			bool to_is_set = test_bit(trans->to, input->key);

			if (from_is_set)
				code = trans->from;
			else if (to_is_set)
				code = trans->to;

			if (!(from_is_set || to_is_set)) {
				if (trans->flags & APPLE_FLAG_FKEY) {
					switch (real_fnmode) {
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

				if (do_translate)
					code = trans->to;
			}
		}

		if (asc->quirks & APPLE_NUMLOCK_EMULATION &&
				(test_bit(code, asc->pressed_numlock) ||
				test_bit(LED_NUML, input->led))) {
			trans = apple_find_translation(powerbook_numlock_keys, code);

			if (trans) {
				if (value)
					set_bit(code, asc->pressed_numlock);
				else
					clear_bit(code, asc->pressed_numlock);

				code = trans->to;
			}
		}
	}

	if (usage->code != code) {
		input_event_with_scancode(input, usage->type, code, usage->hid, value);

		return 1;
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
static const __u8 *apple_report_fixup(struct hid_device *hdev, __u8 *rdesc,
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
	apple_setup_key_translation(input, magic_keyboard_2015_fn_keys);
	apple_setup_key_translation(input, apple2021_fn_keys);
	apple_setup_key_translation(input, macbookpro_no_esc_fn_keys);
	apple_setup_key_translation(input, macbookpro_dedicated_esc_fn_keys);
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

	if (((asc->quirks & APPLE_HAS_FN) && !asc->fn_found) || apple_is_omoton_kb066(hdev)) {
		hid_info(hdev, "Fn key not found (Apple Wireless Keyboard clone?), disabling Fn key handling\n");
		asc->quirks &= ~APPLE_HAS_FN;
	}

	if (apple_is_non_apple_keyboard(hdev)) {
		hid_info(hdev, "Non-apple keyboard detected; function keys will default to fnmode=2 behavior\n");
		asc->quirks |= APPLE_IS_NON_APPLE;
	}

	return 0;
}

static bool apple_backlight_check_support(struct hid_device *hdev)
{
	int i;
	unsigned int hid;
	struct hid_report *report;

	list_for_each_entry(report, &hdev->report_enum[HID_INPUT_REPORT].report_list, list) {
		for (i = 0; i < report->maxfield; i++) {
			hid = report->field[i]->usage->hid;
			if ((hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR && (hid & HID_USAGE) == 0xf)
				return true;
		}
	}

	return false;
}

static int apple_backlight_set(struct hid_device *hdev, u16 value, u16 rate)
{
	int ret = 0;
	struct apple_backlight_set_report *rep;

	rep = kmalloc(sizeof(*rep), GFP_KERNEL);
	if (rep == NULL)
		return -ENOMEM;

	rep->report_id = 0xB0;
	rep->version = 1;
	rep->backlight = value;
	rep->rate = rate;

	ret = hid_hw_raw_request(hdev, 0xB0u, (u8 *) rep, sizeof(*rep),
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);

	kfree(rep);
	return ret;
}

static int apple_backlight_led_set(struct led_classdev *led_cdev,
	enum led_brightness brightness)
{
	struct apple_sc_backlight *backlight = container_of(led_cdev,
							    struct apple_sc_backlight, cdev);

	return apple_backlight_set(backlight->hdev, brightness, 0);
}

static int apple_backlight_init(struct hid_device *hdev)
{
	int ret;
	struct apple_sc *asc = hid_get_drvdata(hdev);
	struct apple_backlight_config_report *rep;

	if (!apple_backlight_check_support(hdev))
		return -EINVAL;

	rep = kmalloc(0x200, GFP_KERNEL);
	if (rep == NULL)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, 0xBFu, (u8 *) rep, sizeof(*rep),
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0) {
		hid_err(hdev, "backlight request failed: %d\n", ret);
		goto cleanup_and_exit;
	}
	if (ret < 8 || rep->version != 1) {
		hid_err(hdev, "backlight config struct: bad version %i\n", rep->version);
		ret = -EINVAL;
		goto cleanup_and_exit;
	}

	hid_dbg(hdev, "backlight config: off=%u, on_min=%u, on_max=%u\n",
		rep->backlight_off, rep->backlight_on_min, rep->backlight_on_max);

	asc->backlight = devm_kzalloc(&hdev->dev, sizeof(*asc->backlight), GFP_KERNEL);
	if (!asc->backlight) {
		ret = -ENOMEM;
		goto cleanup_and_exit;
	}

	asc->backlight->hdev = hdev;
	asc->backlight->cdev.name = "apple::kbd_backlight";
	asc->backlight->cdev.max_brightness = rep->backlight_on_max;
	asc->backlight->cdev.brightness_set_blocking = apple_backlight_led_set;

	ret = apple_backlight_set(hdev, 0, 0);
	if (ret < 0) {
		hid_err(hdev, "backlight set request failed: %d\n", ret);
		goto cleanup_and_exit;
	}

	ret = devm_led_classdev_register(&hdev->dev, &asc->backlight->cdev);

cleanup_and_exit:
	kfree(rep);
	return ret;
}

static void apple_magic_backlight_report_set(struct hid_report *rep, s32 value, u8 rate)
{
	rep->field[0]->value[0] = value;
	rep->field[1]->value[0] = 0x5e; /* Mimic Windows */
	rep->field[1]->value[0] |= rate << 8;

	hid_hw_request(rep->device, rep, HID_REQ_SET_REPORT);
}

static void apple_magic_backlight_set(struct apple_magic_backlight *backlight,
				     int brightness, char rate)
{
	apple_magic_backlight_report_set(backlight->power, brightness ? 1 : 0, rate);
	if (brightness)
		apple_magic_backlight_report_set(backlight->brightness, brightness, rate);
}

static int apple_magic_backlight_led_set(struct led_classdev *led_cdev,
					 enum led_brightness brightness)
{
	struct apple_magic_backlight *backlight = container_of(led_cdev,
			struct apple_magic_backlight, cdev);

	apple_magic_backlight_set(backlight, brightness, 1);
	return 0;
}

static int apple_magic_backlight_init(struct hid_device *hdev)
{
	struct apple_magic_backlight *backlight;
	struct hid_report_enum *report_enum;

	/*
	 * Ensure this usb endpoint is for the keyboard backlight, not touchbar
	 * backlight.
	 */
	if (hdev->collection[0].usage != HID_USAGE_MAGIC_BL)
		return -ENODEV;

	backlight = devm_kzalloc(&hdev->dev, sizeof(*backlight), GFP_KERNEL);
	if (!backlight)
		return -ENOMEM;

	report_enum = &hdev->report_enum[HID_FEATURE_REPORT];
	backlight->brightness = report_enum->report_id_hash[APPLE_MAGIC_REPORT_ID_BRIGHTNESS];
	backlight->power = report_enum->report_id_hash[APPLE_MAGIC_REPORT_ID_POWER];

	if (!backlight->brightness || !backlight->power)
		return -ENODEV;

	backlight->cdev.name = ":white:" LED_FUNCTION_KBD_BACKLIGHT;
	backlight->cdev.max_brightness = backlight->brightness->field[0]->logical_maximum;
	backlight->cdev.brightness_set_blocking = apple_magic_backlight_led_set;

	apple_magic_backlight_set(backlight, 0, 0);

	return devm_led_classdev_register(&hdev->dev, &backlight->cdev);

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

	if (quirks & APPLE_BACKLIGHT_CTL)
		apple_backlight_init(hdev);

	if (quirks & APPLE_MAGIC_BACKLIGHT) {
		ret = apple_magic_backlight_init(hdev);
		if (ret)
			goto out_err;
	}

	return 0;

out_err:
	del_timer_sync(&asc->battery_timer);
	hid_hw_stop(hdev);
	return ret;
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
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_JIS),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ANSI),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ISO),
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_TILDE_QUIRK },
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
		.driver_data = APPLE_NUMLOCK_EMULATION | APPLE_HAS_FN |
			APPLE_ISO_TILDE_QUIRK },
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
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING2_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING3_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4_JIS),
		.driver_data = APPLE_HAS_FN | APPLE_RDESC_JIS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_ANSI),
		.driver_data = APPLE_HAS_FN },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRING4A_ISO),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
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
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J140K),
		.driver_data = APPLE_HAS_FN | APPLE_BACKLIGHT_CTL | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J132),
		.driver_data = APPLE_HAS_FN | APPLE_BACKLIGHT_CTL | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J680),
		.driver_data = APPLE_HAS_FN | APPLE_BACKLIGHT_CTL | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J213),
		.driver_data = APPLE_HAS_FN | APPLE_BACKLIGHT_CTL | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J214K),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J223),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J230K),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_WELLSPRINGT2_J152F),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
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
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK | APPLE_RDESC_BATTERY },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2024),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK | APPLE_RDESC_BATTERY },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_2024),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_FINGERPRINT_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK | APPLE_RDESC_BATTERY },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_FINGERPRINT_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK | APPLE_RDESC_BATTERY },
	{ HID_BLUETOOTH_DEVICE(BT_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MAGIC_KEYBOARD_NUMPAD_2021),
		.driver_data = APPLE_HAS_FN | APPLE_ISO_TILDE_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_TOUCHBAR_BACKLIGHT),
		.driver_data = APPLE_MAGIC_BACKLIGHT },

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

MODULE_DESCRIPTION("Apple USB HID quirks support for Linux");
MODULE_LICENSE("GPL");
