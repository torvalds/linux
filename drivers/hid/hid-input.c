/*
 * $Id: hid-input.c,v 1.2 2002/04/23 00:59:25 rdamazio Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006-2007 Jiri Kosina
 *
 *  HID to Linux Input mapping
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#include <linux/hid.h>
#include <linux/hid-debug.h>

static int hid_pb_fnmode = 1;
module_param_named(pb_fnmode, hid_pb_fnmode, int, 0644);
MODULE_PARM_DESC(pb_fnmode,
		"Mode of fn key on PowerBooks (0 = disabled, 1 = fkeyslast, 2 = fkeysfirst)");

#define unk	KEY_UNKNOWN

static const unsigned char hid_keyboard[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
	191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,unk,unk,unk,121,unk, 89, 93,124, 92, 94, 95,unk,unk,unk,
	122,123, 90, 91, 85,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140,unk,unk,unk,unk
};

/* extended mapping for certain Logitech hardware (Logitech cordless desktop LX500) */
#define LOGITECH_EXPANDED_KEYMAP_SIZE 80
static int logitech_expanded_keymap[LOGITECH_EXPANDED_KEYMAP_SIZE] = {
	  0,216,  0,213,175,156,  0,  0,  0,  0,
	144,  0,  0,  0,  0,  0,  0,  0,  0,212,
	174,167,152,161,112,  0,  0,  0,154,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,183,184,185,186,187,
	188,189,190,191,192,193,194,  0,  0,  0
};

static const struct {
	__s32 x;
	__s32 y;
}  hid_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

#define map_abs(c)	do { usage->code = c; usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX; } while (0)
#define map_rel(c)	do { usage->code = c; usage->type = EV_REL; bit = input->relbit; max = REL_MAX; } while (0)
#define map_key(c)	do { usage->code = c; usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX; } while (0)
#define map_led(c)	do { usage->code = c; usage->type = EV_LED; bit = input->ledbit; max = LED_MAX; } while (0)

#define map_abs_clear(c)	do { map_abs(c); clear_bit(c, bit); } while (0)
#define map_key_clear(c)	do { map_key(c); clear_bit(c, bit); } while (0)

#ifdef CONFIG_USB_HIDINPUT_POWERBOOK

struct hidinput_key_translation {
	u16 from;
	u16 to;
	u8 flags;
};

#define POWERBOOK_FLAG_FKEY 0x01

static struct hidinput_key_translation powerbook_fn_keys[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_F1,       KEY_BRIGHTNESSDOWN,     POWERBOOK_FLAG_FKEY },
	{ KEY_F2,       KEY_BRIGHTNESSUP,       POWERBOOK_FLAG_FKEY },
	{ KEY_F3,       KEY_MUTE,               POWERBOOK_FLAG_FKEY },
	{ KEY_F4,       KEY_VOLUMEDOWN,         POWERBOOK_FLAG_FKEY },
	{ KEY_F5,       KEY_VOLUMEUP,           POWERBOOK_FLAG_FKEY },
	{ KEY_F6,       KEY_NUMLOCK,            POWERBOOK_FLAG_FKEY },
	{ KEY_F7,       KEY_SWITCHVIDEOMODE,    POWERBOOK_FLAG_FKEY },
	{ KEY_F8,       KEY_KBDILLUMTOGGLE,     POWERBOOK_FLAG_FKEY },
	{ KEY_F9,       KEY_KBDILLUMDOWN,       POWERBOOK_FLAG_FKEY },
	{ KEY_F10,      KEY_KBDILLUMUP,         POWERBOOK_FLAG_FKEY },
	{ KEY_UP,       KEY_PAGEUP },
	{ KEY_DOWN,     KEY_PAGEDOWN },
	{ KEY_LEFT,     KEY_HOME },
	{ KEY_RIGHT,    KEY_END },
	{ }
};

static struct hidinput_key_translation powerbook_numlock_keys[] = {
	{ KEY_J,        KEY_KP1 },
	{ KEY_K,        KEY_KP2 },
	{ KEY_L,        KEY_KP3 },
	{ KEY_U,        KEY_KP4 },
	{ KEY_I,        KEY_KP5 },
	{ KEY_O,        KEY_KP6 },
	{ KEY_7,        KEY_KP7 },
	{ KEY_8,        KEY_KP8 },
	{ KEY_9,        KEY_KP9 },
	{ KEY_M,        KEY_KP0 },
	{ KEY_DOT,      KEY_KPDOT },
	{ KEY_SLASH,    KEY_KPPLUS },
	{ KEY_SEMICOLON, KEY_KPMINUS },
	{ KEY_P,        KEY_KPASTERISK },
	{ KEY_MINUS,    KEY_KPEQUAL },
	{ KEY_0,        KEY_KPSLASH },
	{ KEY_F6,       KEY_NUMLOCK },
	{ KEY_KPENTER,  KEY_KPENTER },
	{ KEY_BACKSPACE, KEY_BACKSPACE },
	{ }
};

static struct hidinput_key_translation powerbook_iso_keyboard[] = {
	{ KEY_GRAVE,    KEY_102ND },
	{ KEY_102ND,    KEY_GRAVE },
	{ }
};

static struct hidinput_key_translation *find_translation(struct hidinput_key_translation *table, u16 from)
{
	struct hidinput_key_translation *trans;

	/* Look for the translation */
	for (trans = table; trans->from; trans++)
		if (trans->from == from)
			return trans;

	return NULL;
}

static int hidinput_pb_event(struct hid_device *hid, struct input_dev *input,
		struct hid_usage *usage, __s32 value)
{
	struct hidinput_key_translation *trans;

	if (usage->code == KEY_FN) {
		if (value) hid->quirks |=  HID_QUIRK_POWERBOOK_FN_ON;
		else       hid->quirks &= ~HID_QUIRK_POWERBOOK_FN_ON;

		input_event(input, usage->type, usage->code, value);

		return 1;
	}

	if (hid_pb_fnmode) {
		int do_translate;

		trans = find_translation(powerbook_fn_keys, usage->code);
		if (trans) {
			if (test_bit(usage->code, hid->pb_pressed_fn))
				do_translate = 1;
			else if (trans->flags & POWERBOOK_FLAG_FKEY)
				do_translate =
					(hid_pb_fnmode == 2 &&  (hid->quirks & HID_QUIRK_POWERBOOK_FN_ON)) ||
					(hid_pb_fnmode == 1 && !(hid->quirks & HID_QUIRK_POWERBOOK_FN_ON));
			else
				do_translate = (hid->quirks & HID_QUIRK_POWERBOOK_FN_ON);

			if (do_translate) {
				if (value)
					set_bit(usage->code, hid->pb_pressed_fn);
				else
					clear_bit(usage->code, hid->pb_pressed_fn);

				input_event(input, usage->type, trans->to, value);

				return 1;
			}
		}

		if (test_bit(usage->code, hid->pb_pressed_numlock) ||
				test_bit(LED_NUML, input->led)) {
			trans = find_translation(powerbook_numlock_keys, usage->code);

			if (trans) {
				if (value)
					set_bit(usage->code, hid->pb_pressed_numlock);
				else
					clear_bit(usage->code, hid->pb_pressed_numlock);

				input_event(input, usage->type, trans->to, value);
			}

			return 1;
		}
	}

	if (hid->quirks & HID_QUIRK_POWERBOOK_ISO_KEYBOARD) {
		trans = find_translation(powerbook_iso_keyboard, usage->code);
		if (trans) {
			input_event(input, usage->type, trans->to, value);
			return 1;
		}
	}

	return 0;
}

static void hidinput_pb_setup(struct input_dev *input)
{
	struct hidinput_key_translation *trans;

	set_bit(KEY_NUMLOCK, input->keybit);

	/* Enable all needed keys */
	for (trans = powerbook_fn_keys; trans->from; trans++)
		set_bit(trans->to, input->keybit);

	for (trans = powerbook_numlock_keys; trans->from; trans++)
		set_bit(trans->to, input->keybit);

	for (trans = powerbook_iso_keyboard; trans->from; trans++)
		set_bit(trans->to, input->keybit);

}
#else
static inline int hidinput_pb_event(struct hid_device *hid, struct input_dev *input,
		struct hid_usage *usage, __s32 value)
{
	return 0;
}

static inline void hidinput_pb_setup(struct input_dev *input)
{
}
#endif

static inline int match_scancode(int code, int scancode)
{
	if (scancode == 0)
		return 1;
	return ((code & (HID_USAGE_PAGE | HID_USAGE)) == scancode);
}

static inline int match_keycode(int code, int keycode)
{
	if (keycode == 0)
		return 1;
	return (code == keycode);
}

static struct hid_usage *hidinput_find_key(struct hid_device *hid,
		int scancode, int keycode)
{
	int i, j, k;
	struct hid_report *report;
	struct hid_usage *usage;

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++) {
		list_for_each_entry(report, &hid->report_enum[k].report_list, list) {
			for (i = 0; i < report->maxfield; i++) {
				for ( j = 0; j < report->field[i]->maxusage; j++) {
					usage = report->field[i]->usage + j;
					if (usage->type == EV_KEY &&
						match_scancode(usage->hid, scancode) &&
						match_keycode(usage->code, keycode))
						return usage;
				}
			}
		}
	}
	return NULL;
}

static int hidinput_getkeycode(struct input_dev *dev, int scancode,
				int *keycode)
{
	struct hid_device *hid = dev->private;
	struct hid_usage *usage;
	
	usage = hidinput_find_key(hid, scancode, 0);
	if (usage) {
		*keycode = usage->code;
		return 0;
	}
	return -EINVAL;
}

static int hidinput_setkeycode(struct input_dev *dev, int scancode,
				int keycode)
{
	struct hid_device *hid = dev->private;
	struct hid_usage *usage;
	int old_keycode;
	
	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;
	
	usage = hidinput_find_key(hid, scancode, 0);
	if (usage) {
		old_keycode = usage->code;
		usage->code = keycode;
		
		clear_bit(old_keycode, dev->keybit);
		set_bit(usage->code, dev->keybit);
		dbg_hid(KERN_DEBUG "Assigned keycode %d to HID usage code %x\n", keycode, scancode);
		/* Set the keybit for the old keycode if the old keycode is used
		 * by another key */
		if (hidinput_find_key (hid, 0, old_keycode))
			set_bit(old_keycode, dev->keybit);
		
		return 0;
	}
	
	return -EINVAL;
}


static void hidinput_configure_usage(struct hid_input *hidinput, struct hid_field *field,
				     struct hid_usage *usage)
{
	struct input_dev *input = hidinput->input;
	struct hid_device *device = input_get_drvdata(input);
	int max = 0, code;
	unsigned long *bit = NULL;

	field->hidinput = hidinput;

	dbg_hid("Mapping: ");
	hid_resolv_usage(usage->hid);
	dbg_hid_line(" ---> ");

	if (field->flags & HID_MAIN_ITEM_CONSTANT)
		goto ignore;

	switch (usage->hid & HID_USAGE_PAGE) {

		case HID_UP_UNDEFINED:
			goto ignore;

		case HID_UP_KEYBOARD:

			set_bit(EV_REP, input->evbit);

			if ((usage->hid & HID_USAGE) < 256) {
				if (!hid_keyboard[usage->hid & HID_USAGE]) goto ignore;
				map_key_clear(hid_keyboard[usage->hid & HID_USAGE]);
			} else
				map_key(KEY_UNKNOWN);

			break;

		case HID_UP_BUTTON:

			code = ((usage->hid - 1) & 0xf);

			switch (field->application) {
				case HID_GD_MOUSE:
				case HID_GD_POINTER:  code += 0x110; break;
				case HID_GD_JOYSTICK: code += 0x120; break;
				case HID_GD_GAMEPAD:  code += 0x130; break;
				default:
					switch (field->physical) {
						case HID_GD_MOUSE:
						case HID_GD_POINTER:  code += 0x110; break;
						case HID_GD_JOYSTICK: code += 0x120; break;
						case HID_GD_GAMEPAD:  code += 0x130; break;
						default:              code += 0x100;
					}
			}

			/* Special handling for Logitech Cordless Desktop */
			if (field->application != HID_GD_MOUSE) {
				if (device->quirks & HID_QUIRK_LOGITECH_EXPANDED_KEYMAP) {
					int hid = usage->hid & HID_USAGE;
					if (hid < LOGITECH_EXPANDED_KEYMAP_SIZE && logitech_expanded_keymap[hid] != 0)
						code = logitech_expanded_keymap[hid];
				}
			} else {
				if (device->quirks & HID_QUIRK_LOGITECH_IGNORE_DOUBLED_WHEEL) {
					int hid = usage->hid & HID_USAGE;
					if (hid == 7 || hid == 8)
						goto ignore;
				}
			}

			map_key(code);
			break;


		case HID_UP_SIMULATION:

			switch (usage->hid & 0xffff) {
				case 0xba: map_abs(ABS_RUDDER);   break;
				case 0xbb: map_abs(ABS_THROTTLE); break;
				case 0xc4: map_abs(ABS_GAS);      break;
				case 0xc5: map_abs(ABS_BRAKE);    break;
				case 0xc8: map_abs(ABS_WHEEL);    break;
				default:   goto ignore;
			}
			break;

		case HID_UP_GENDESK:

			if ((usage->hid & 0xf0) == 0x80) {	/* SystemControl */
				switch (usage->hid & 0xf) {
					case 0x1: map_key_clear(KEY_POWER);  break;
					case 0x2: map_key_clear(KEY_SLEEP);  break;
					case 0x3: map_key_clear(KEY_WAKEUP); break;
					default: goto unknown;
				}
				break;
			}

			if ((usage->hid & 0xf0) == 0x90) {	/* D-pad */
				switch (usage->hid) {
					case HID_GD_UP:	   usage->hat_dir = 1; break;
					case HID_GD_DOWN:  usage->hat_dir = 5; break;
					case HID_GD_RIGHT: usage->hat_dir = 3; break;
					case HID_GD_LEFT:  usage->hat_dir = 7; break;
					default: goto unknown;
				}
				if (field->dpad) {
					map_abs(field->dpad);
					goto ignore;
				}
				map_abs(ABS_HAT0X);
				break;
			}

			switch (usage->hid) {

				/* These usage IDs map directly to the usage codes. */
				case HID_GD_X: case HID_GD_Y: case HID_GD_Z:
				case HID_GD_RX: case HID_GD_RY: case HID_GD_RZ:
				case HID_GD_SLIDER: case HID_GD_DIAL: case HID_GD_WHEEL:
					if (field->flags & HID_MAIN_ITEM_RELATIVE)
						map_rel(usage->hid & 0xf);
					else
						map_abs(usage->hid & 0xf);
					break;

				case HID_GD_HATSWITCH:
					usage->hat_min = field->logical_minimum;
					usage->hat_max = field->logical_maximum;
					map_abs(ABS_HAT0X);
					break;

				case HID_GD_START:	map_key_clear(BTN_START);	break;
				case HID_GD_SELECT:	map_key_clear(BTN_SELECT);	break;

				default: goto unknown;
			}

			break;

		case HID_UP_LED:

			switch (usage->hid & 0xffff) {                        /* HID-Value:                   */
				case 0x01:  map_led (LED_NUML);     break;    /*   "Num Lock"                 */
				case 0x02:  map_led (LED_CAPSL);    break;    /*   "Caps Lock"                */
				case 0x03:  map_led (LED_SCROLLL);  break;    /*   "Scroll Lock"              */
				case 0x04:  map_led (LED_COMPOSE);  break;    /*   "Compose"                  */
				case 0x05:  map_led (LED_KANA);     break;    /*   "Kana"                     */
				case 0x27:  map_led (LED_SLEEP);    break;    /*   "Stand-By"                 */
				case 0x4c:  map_led (LED_SUSPEND);  break;    /*   "System Suspend"           */
				case 0x09:  map_led (LED_MUTE);     break;    /*   "Mute"                     */
				case 0x4b:  map_led (LED_MISC);     break;    /*   "Generic Indicator"        */
				case 0x19:  map_led (LED_MAIL);     break;    /*   "Message Waiting"          */
				case 0x4d:  map_led (LED_CHARGING); break;    /*   "External Power Connected" */

				default: goto ignore;
			}
			break;

		case HID_UP_DIGITIZER:

			switch (usage->hid & 0xff) {

				case 0x30: /* TipPressure */
					if (!test_bit(BTN_TOUCH, input->keybit)) {
						device->quirks |= HID_QUIRK_NOTOUCH;
						set_bit(EV_KEY, input->evbit);
						set_bit(BTN_TOUCH, input->keybit);
					}

					map_abs_clear(ABS_PRESSURE);
					break;

				case 0x32: /* InRange */
					switch (field->physical & 0xff) {
						case 0x21: map_key(BTN_TOOL_MOUSE); break;
						case 0x22: map_key(BTN_TOOL_FINGER); break;
						default: map_key(BTN_TOOL_PEN); break;
					}
					break;

				case 0x3c: /* Invert */
					map_key_clear(BTN_TOOL_RUBBER);
					break;

				case 0x33: /* Touch */
				case 0x42: /* TipSwitch */
				case 0x43: /* TipSwitch2 */
					device->quirks &= ~HID_QUIRK_NOTOUCH;
					map_key_clear(BTN_TOUCH);
					break;

				case 0x44: /* BarrelSwitch */
					map_key_clear(BTN_STYLUS);
					break;

				default:  goto unknown;
			}
			break;

		case HID_UP_CONSUMER:	/* USB HUT v1.1, pages 56-62 */

			switch (usage->hid & HID_USAGE) {
				case 0x000: goto ignore;
				case 0x034: map_key_clear(KEY_SLEEP);		break;
				case 0x036: map_key_clear(BTN_MISC);		break;
				/*
				 * The next three are reported by Belkin wireless
				 * keyboard (1020:0006). These values are "reserved"
				 * in HUT 1.12.
				 */
				case 0x03a: map_key_clear(KEY_SOUND);           break;
				case 0x03b: map_key_clear(KEY_CAMERA);          break;
				case 0x03c: map_key_clear(KEY_DOCUMENTS);       break;

				case 0x040: map_key_clear(KEY_MENU);		break;
				case 0x045: map_key_clear(KEY_RADIO);		break;

				case 0x083: map_key_clear(KEY_LAST);		break;
				case 0x088: map_key_clear(KEY_PC);		break;
				case 0x089: map_key_clear(KEY_TV);		break;
				case 0x08a: map_key_clear(KEY_WWW);		break;
				case 0x08b: map_key_clear(KEY_DVD);		break;
				case 0x08c: map_key_clear(KEY_PHONE);		break;
				case 0x08d: map_key_clear(KEY_PROGRAM);		break;
				case 0x08e: map_key_clear(KEY_VIDEOPHONE);	break;
				case 0x08f: map_key_clear(KEY_GAMES);		break;
				case 0x090: map_key_clear(KEY_MEMO);		break;
				case 0x091: map_key_clear(KEY_CD);		break;
				case 0x092: map_key_clear(KEY_VCR);		break;
				case 0x093: map_key_clear(KEY_TUNER);		break;
				case 0x094: map_key_clear(KEY_EXIT);		break;
				case 0x095: map_key_clear(KEY_HELP);		break;
				case 0x096: map_key_clear(KEY_TAPE);		break;
				case 0x097: map_key_clear(KEY_TV2);		break;
				case 0x098: map_key_clear(KEY_SAT);		break;
				case 0x09a: map_key_clear(KEY_PVR);		break;

				case 0x09c: map_key_clear(KEY_CHANNELUP);	break;
				case 0x09d: map_key_clear(KEY_CHANNELDOWN);	break;
				case 0x0a0: map_key_clear(KEY_VCR2);		break;

				case 0x0b0: map_key_clear(KEY_PLAY);		break;
				case 0x0b1: map_key_clear(KEY_PAUSE);		break;
				case 0x0b2: map_key_clear(KEY_RECORD);		break;
				case 0x0b3: map_key_clear(KEY_FASTFORWARD);	break;
				case 0x0b4: map_key_clear(KEY_REWIND);		break;
				case 0x0b5: map_key_clear(KEY_NEXTSONG);	break;
				case 0x0b6: map_key_clear(KEY_PREVIOUSSONG);	break;
				case 0x0b7: map_key_clear(KEY_STOPCD);		break;
				case 0x0b8: map_key_clear(KEY_EJECTCD);		break;

				case 0x0cd: map_key_clear(KEY_PLAYPAUSE);	break;
			        case 0x0e0: map_abs_clear(ABS_VOLUME);		break;
				case 0x0e2: map_key_clear(KEY_MUTE);		break;
				case 0x0e5: map_key_clear(KEY_BASSBOOST);	break;
				case 0x0e9: map_key_clear(KEY_VOLUMEUP);	break;
				case 0x0ea: map_key_clear(KEY_VOLUMEDOWN);	break;

				/* reserved in HUT 1.12. Reported on Petalynx remote */
				case 0x0f6: map_key_clear(KEY_NEXT);		break;
				case 0x0fa: map_key_clear(KEY_BACK);		break;

				case 0x183: map_key_clear(KEY_CONFIG);		break;
				case 0x184: map_key_clear(KEY_WORDPROCESSOR);	break;
				case 0x185: map_key_clear(KEY_EDITOR);		break;
				case 0x186: map_key_clear(KEY_SPREADSHEET);	break;
				case 0x187: map_key_clear(KEY_GRAPHICSEDITOR);	break;
				case 0x188: map_key_clear(KEY_PRESENTATION);	break;
				case 0x189: map_key_clear(KEY_DATABASE);	break;
				case 0x18a: map_key_clear(KEY_MAIL);		break;
				case 0x18b: map_key_clear(KEY_NEWS);		break;
				case 0x18c: map_key_clear(KEY_VOICEMAIL);	break;
				case 0x18d: map_key_clear(KEY_ADDRESSBOOK);	break;
				case 0x18e: map_key_clear(KEY_CALENDAR);	break;
				case 0x191: map_key_clear(KEY_FINANCE);		break;
				case 0x192: map_key_clear(KEY_CALC);		break;
				case 0x194: map_key_clear(KEY_FILE);		break;
				case 0x196: map_key_clear(KEY_WWW);		break;
				case 0x19e: map_key_clear(KEY_COFFEE);		break;
				case 0x1a6: map_key_clear(KEY_HELP);		break;
				case 0x1a7: map_key_clear(KEY_DOCUMENTS);	break;
				case 0x1bc: map_key_clear(KEY_MESSENGER);	break;
				case 0x1bd: map_key_clear(KEY_INFO);		break;
				case 0x201: map_key_clear(KEY_NEW);		break;
				case 0x202: map_key_clear(KEY_OPEN);		break;
				case 0x203: map_key_clear(KEY_CLOSE);		break;
				case 0x204: map_key_clear(KEY_EXIT);		break;
				case 0x207: map_key_clear(KEY_SAVE);		break;
				case 0x208: map_key_clear(KEY_PRINT);		break;
				case 0x209: map_key_clear(KEY_PROPS);		break;
				case 0x21a: map_key_clear(KEY_UNDO);		break;
				case 0x21b: map_key_clear(KEY_COPY);		break;
				case 0x21c: map_key_clear(KEY_CUT);		break;
				case 0x21d: map_key_clear(KEY_PASTE);		break;
				case 0x21f: map_key_clear(KEY_FIND);		break;
				case 0x221: map_key_clear(KEY_SEARCH);		break;
				case 0x222: map_key_clear(KEY_GOTO);		break;
				case 0x223: map_key_clear(KEY_HOMEPAGE);	break;
				case 0x224: map_key_clear(KEY_BACK);		break;
				case 0x225: map_key_clear(KEY_FORWARD);		break;
				case 0x226: map_key_clear(KEY_STOP);		break;
				case 0x227: map_key_clear(KEY_REFRESH);		break;
				case 0x22a: map_key_clear(KEY_BOOKMARKS);	break;
				case 0x22d: map_key_clear(KEY_ZOOMIN);		break;
				case 0x22e: map_key_clear(KEY_ZOOMOUT);		break;
				case 0x22f: map_key_clear(KEY_ZOOMRESET);	break;
				case 0x233: map_key_clear(KEY_SCROLLUP);	break;
				case 0x234: map_key_clear(KEY_SCROLLDOWN);	break;
				case 0x238: map_rel(REL_HWHEEL);		break;
				case 0x25f: map_key_clear(KEY_CANCEL);		break;
				case 0x279: map_key_clear(KEY_REDO);		break;

				case 0x289: map_key_clear(KEY_REPLY);		break;
				case 0x28b: map_key_clear(KEY_FORWARDMAIL);	break;
				case 0x28c: map_key_clear(KEY_SEND);		break;

				/* Reported on a Cherry Cymotion keyboard */
				case 0x301: map_key_clear(KEY_PROG1);		break;
				case 0x302: map_key_clear(KEY_PROG2);		break;
				case 0x303: map_key_clear(KEY_PROG3);		break;

				/* Reported on certain Logitech wireless keyboards */
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
				case 0x1018: map_key_clear(KEY_MEDIA);          break;
				case 0x1019: map_key_clear(KEY_PROG1);		break;
				case 0x101a: map_key_clear(KEY_PROG2);		break;
				case 0x101b: map_key_clear(KEY_PROG3);		break;
				case 0x101f: map_key_clear(KEY_ZOOMIN);		break;
				case 0x1020: map_key_clear(KEY_ZOOMOUT);	break;
				case 0x1021: map_key_clear(KEY_ZOOMRESET);	break;
				case 0x1023: map_key_clear(KEY_CLOSE);		break;
				case 0x1027: map_key_clear(KEY_MENU);           break;
				/* this one is marked as 'Rotate' */
				case 0x1028: map_key_clear(KEY_ANGLE);		break;
				case 0x1029: map_key_clear(KEY_SHUFFLE);	break;
				case 0x102a: map_key_clear(KEY_BACK);           break;
				case 0x102b: map_key_clear(KEY_CYCLEWINDOWS);   break;
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

				default:    goto ignore;
			}
			break;

		case HID_UP_HPVENDOR:	/* Reported on a Dutch layout HP5308 */

			set_bit(EV_REP, input->evbit);
			switch (usage->hid & HID_USAGE) {
			        case 0x021: map_key_clear(KEY_PRINT);           break;
				case 0x070: map_key_clear(KEY_HP);		break;
				case 0x071: map_key_clear(KEY_CAMERA);		break;
				case 0x072: map_key_clear(KEY_SOUND);		break;
				case 0x073: map_key_clear(KEY_QUESTION);	break;
				case 0x080: map_key_clear(KEY_EMAIL);		break;
				case 0x081: map_key_clear(KEY_CHAT);		break;
				case 0x082: map_key_clear(KEY_SEARCH);		break;
				case 0x083: map_key_clear(KEY_CONNECT);	        break;
				case 0x084: map_key_clear(KEY_FINANCE);		break;
				case 0x085: map_key_clear(KEY_SPORT);		break;
				case 0x086: map_key_clear(KEY_SHOP);	        break;
				default:    goto ignore;
			}
			break;

		case HID_UP_MSVENDOR:

			/* special case - Chicony Chicony KU-0418 tactical pad */
			if (device->vendor == 0x04f2 && device->product == 0x0418) {
				set_bit(EV_REP, input->evbit);
				switch(usage->hid & HID_USAGE) {
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
					default:    goto ignore;
				}
			} else {
				goto ignore;
			}
			break;

		case HID_UP_CUSTOM: /* Reported on Logitech and Powerbook USB keyboards */

			set_bit(EV_REP, input->evbit);
			switch(usage->hid & HID_USAGE) {
				case 0x003:
					/* The fn key on Apple PowerBooks */
					map_key_clear(KEY_FN);
					hidinput_pb_setup(input);
					break;

				default:    goto ignore;
			}
			break;

		case HID_UP_LOGIVENDOR:
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

				/* Reported on Petalynx Maxter remote */
				case 0x05a: map_key_clear(KEY_TEXT);		break;
				case 0x05b: map_key_clear(KEY_RED);		break;
				case 0x05c: map_key_clear(KEY_GREEN);		break;
				case 0x05d: map_key_clear(KEY_YELLOW);		break;
				case 0x05e: map_key_clear(KEY_BLUE);		break;

				default:    goto ignore;
			}
			break;

		case HID_UP_PID:

			switch(usage->hid & HID_USAGE) {
				case 0xa4: map_key_clear(BTN_DEAD);	break;
				default: goto ignore;
			}
			break;

		default:
		unknown:
			if (field->report_size == 1) {
				if (field->report->type == HID_OUTPUT_REPORT) {
					map_led(LED_MISC);
					break;
				}
				map_key(BTN_MISC);
				break;
			}
			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				map_rel(REL_MISC);
				break;
			}
			map_abs(ABS_MISC);
			break;
	}

	if (device->quirks & HID_QUIRK_MIGHTYMOUSE) {
		if (usage->hid == HID_GD_Z)
			map_rel(REL_HWHEEL);
		else if (usage->code == BTN_1)
			map_key(BTN_2);
		else if (usage->code == BTN_2)
			map_key(BTN_1);
	}

	if ((device->quirks & (HID_QUIRK_2WHEEL_MOUSE_HACK_7 | HID_QUIRK_2WHEEL_MOUSE_HACK_5)) &&
		 (usage->type == EV_REL) && (usage->code == REL_WHEEL))
			set_bit(REL_HWHEEL, bit);

	if (((device->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_5) && (usage->hid == 0x00090005))
		|| ((device->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_7) && (usage->hid == 0x00090007)))
		goto ignore;

	if ((device->quirks & HID_QUIRK_BAD_RELATIVE_KEYS) &&
		usage->type == EV_KEY && (field->flags & HID_MAIN_ITEM_RELATIVE))
		field->flags &= ~HID_MAIN_ITEM_RELATIVE;

	set_bit(usage->type, input->evbit);

	if (device->quirks & HID_QUIRK_DUPLICATE_USAGES &&
			(usage->type == EV_KEY ||
			 usage->type == EV_REL ||
			 usage->type == EV_ABS))
		clear_bit(usage->code, bit);

	while (usage->code <= max && test_and_set_bit(usage->code, bit))
		usage->code = find_next_zero_bit(bit, max + 1, usage->code);

	if (usage->code > max)
		goto ignore;


	if (usage->type == EV_ABS) {

		int a = field->logical_minimum;
		int b = field->logical_maximum;

		if ((device->quirks & HID_QUIRK_BADPAD) && (usage->code == ABS_X || usage->code == ABS_Y)) {
			a = field->logical_minimum = 0;
			b = field->logical_maximum = 255;
		}

		if (field->application == HID_GD_GAMEPAD || field->application == HID_GD_JOYSTICK)
			input_set_abs_params(input, usage->code, a, b, (b - a) >> 8, (b - a) >> 4);
		else	input_set_abs_params(input, usage->code, a, b, 0, 0);

	}

	if (usage->type == EV_ABS &&
	    (usage->hat_min < usage->hat_max || usage->hat_dir)) {
		int i;
		for (i = usage->code; i < usage->code + 2 && i <= max; i++) {
			input_set_abs_params(input, i, -1, 1, 0, 0);
			set_bit(i, input->absbit);
		}
		if (usage->hat_dir && !field->dpad)
			field->dpad = usage->code;
	}

	/* for those devices which produce Consumer volume usage as relative,
	 * we emulate pressing volumeup/volumedown appropriate number of times
	 * in hidinput_hid_event()
	 */
	if ((usage->type == EV_ABS) && (field->flags & HID_MAIN_ITEM_RELATIVE) &&
			(usage->code == ABS_VOLUME)) {
		set_bit(KEY_VOLUMEUP, input->keybit);
		set_bit(KEY_VOLUMEDOWN, input->keybit);
	}

	hid_resolv_event(usage->type, usage->code);

	dbg_hid_line("\n");

	return;

ignore:
	dbg_hid_line("IGNORED\n");
	return;
}

void hidinput_hid_event(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct input_dev *input;
	int *quirks = &hid->quirks;

	if (!field->hidinput)
		return;

	input = field->hidinput->input;

	if (!usage->type)
		return;

	if (((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_5) && (usage->hid == 0x00090005))
		|| ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_7) && (usage->hid == 0x00090007))) {
		if (value) hid->quirks |=  HID_QUIRK_2WHEEL_MOUSE_HACK_ON;
		else       hid->quirks &= ~HID_QUIRK_2WHEEL_MOUSE_HACK_ON;
		return;
	}

	if ((hid->quirks & HID_QUIRK_INVERT_HWHEEL) && (usage->code == REL_HWHEEL)) {
		input_event(input, usage->type, usage->code, -value);
		return;
	}

	if ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_ON) && (usage->code == REL_WHEEL)) {
		input_event(input, usage->type, REL_HWHEEL, value);
		return;
	}

	if ((hid->quirks & HID_QUIRK_POWERBOOK_HAS_FN) && hidinput_pb_event(hid, input, usage, value))
		return;

	if (usage->hat_min < usage->hat_max || usage->hat_dir) {
		int hat_dir = usage->hat_dir;
		if (!hat_dir)
			hat_dir = (value - usage->hat_min) * 8 / (usage->hat_max - usage->hat_min + 1) + 1;
		if (hat_dir < 0 || hat_dir > 8) hat_dir = 0;
		input_event(input, usage->type, usage->code    , hid_hat_to_axis[hat_dir].x);
                input_event(input, usage->type, usage->code + 1, hid_hat_to_axis[hat_dir].y);
                return;
        }

	if (usage->hid == (HID_UP_DIGITIZER | 0x003c)) { /* Invert */
		*quirks = value ? (*quirks | HID_QUIRK_INVERT) : (*quirks & ~HID_QUIRK_INVERT);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x0032)) { /* InRange */
		if (value) {
			input_event(input, usage->type, (*quirks & HID_QUIRK_INVERT) ? BTN_TOOL_RUBBER : usage->code, 1);
			return;
		}
		input_event(input, usage->type, usage->code, 0);
		input_event(input, usage->type, BTN_TOOL_RUBBER, 0);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x0030) && (*quirks & HID_QUIRK_NOTOUCH)) { /* Pressure */
		int a = field->logical_minimum;
		int b = field->logical_maximum;
		input_event(input, EV_KEY, BTN_TOUCH, value > a + ((b - a) >> 3));
	}

	if (usage->hid == (HID_UP_PID | 0x83UL)) { /* Simultaneous Effects Max */
		dbg_hid("Maximum Effects - %d\n",value);
		return;
	}

	if (usage->hid == (HID_UP_PID | 0x7fUL)) {
		dbg_hid("PID Pool Report\n");
		return;
	}

	if ((usage->type == EV_KEY) && (usage->code == 0)) /* Key 0 is "unassigned", not KEY_UNKNOWN */
		return;

	if ((usage->type == EV_ABS) && (field->flags & HID_MAIN_ITEM_RELATIVE) &&
			(usage->code == ABS_VOLUME)) {
		int count = abs(value);
		int direction = value > 0 ? KEY_VOLUMEUP : KEY_VOLUMEDOWN;
		int i;

		for (i = 0; i < count; i++) {
			input_event(input, EV_KEY, direction, 1);
			input_sync(input);
			input_event(input, EV_KEY, direction, 0);
			input_sync(input);
		}
		return;
	}

	input_event(input, usage->type, usage->code, value);

	if ((field->flags & HID_MAIN_ITEM_RELATIVE) && (usage->type == EV_KEY))
		input_event(input, usage->type, usage->code, 0);
}

void hidinput_report_event(struct hid_device *hid, struct hid_report *report)
{
	struct hid_input *hidinput;

	list_for_each_entry(hidinput, &hid->inputs, list)
		input_sync(hidinput->input);
}
EXPORT_SYMBOL_GPL(hidinput_report_event);

int hidinput_find_field(struct hid_device *hid, unsigned int type, unsigned int code, struct hid_field **field)
{
	struct hid_report *report;
	int i, j;

	list_for_each_entry(report, &hid->report_enum[HID_OUTPUT_REPORT].report_list, list) {
		for (i = 0; i < report->maxfield; i++) {
			*field = report->field[i];
			for (j = 0; j < (*field)->maxusage; j++)
				if ((*field)->usage[j].type == type && (*field)->usage[j].code == code)
					return j;
		}
	}
	return -1;
}
EXPORT_SYMBOL_GPL(hidinput_find_field);

static int hidinput_open(struct input_dev *dev)
{
	struct hid_device *hid = input_get_drvdata(dev);

	return hid->hid_open(hid);
}

static void hidinput_close(struct input_dev *dev)
{
	struct hid_device *hid = input_get_drvdata(dev);

	hid->hid_close(hid);
}

/*
 * Register the input device; print a message.
 * Configure the input layer interface
 * Read all reports and initialize the absolute field values.
 */

int hidinput_connect(struct hid_device *hid)
{
	struct hid_report *report;
	struct hid_input *hidinput = NULL;
	struct input_dev *input_dev;
	int i, j, k;
	int max_report_type = HID_OUTPUT_REPORT;

	INIT_LIST_HEAD(&hid->inputs);

	for (i = 0; i < hid->maxcollection; i++)
		if (hid->collection[i].type == HID_COLLECTION_APPLICATION ||
		    hid->collection[i].type == HID_COLLECTION_PHYSICAL)
			if (IS_INPUT_APPLICATION(hid->collection[i].usage))
				break;

	if (i == hid->maxcollection && (hid->quirks & HID_QUIRK_HIDINPUT) == 0)
		return -1;

	if (hid->quirks & HID_QUIRK_SKIP_OUTPUT_REPORTS)
		max_report_type = HID_INPUT_REPORT;

	for (k = HID_INPUT_REPORT; k <= max_report_type; k++)
		list_for_each_entry(report, &hid->report_enum[k].report_list, list) {

			if (!report->maxfield)
				continue;

			if (!hidinput) {
				hidinput = kzalloc(sizeof(*hidinput), GFP_KERNEL);
				input_dev = input_allocate_device();
				if (!hidinput || !input_dev) {
					kfree(hidinput);
					input_free_device(input_dev);
					err_hid("Out of memory during hid input probe");
					return -1;
				}

				input_set_drvdata(input_dev, hid);
				input_dev->event = hid->hidinput_input_event;
				input_dev->open = hidinput_open;
				input_dev->close = hidinput_close;
				input_dev->setkeycode = hidinput_setkeycode;
				input_dev->getkeycode = hidinput_getkeycode;

				input_dev->name = hid->name;
				input_dev->phys = hid->phys;
				input_dev->uniq = hid->uniq;
				input_dev->id.bustype = hid->bus;
				input_dev->id.vendor  = hid->vendor;
				input_dev->id.product = hid->product;
				input_dev->id.version = hid->version;
				input_dev->dev.parent = hid->dev;
				hidinput->input = input_dev;
				list_add_tail(&hidinput->list, &hid->inputs);
			}

			for (i = 0; i < report->maxfield; i++)
				for (j = 0; j < report->field[i]->maxusage; j++)
					hidinput_configure_usage(hidinput, report->field[i],
								 report->field[i]->usage + j);

			if (hid->quirks & HID_QUIRK_MULTI_INPUT) {
				/* This will leave hidinput NULL, so that it
				 * allocates another one if we have more inputs on
				 * the same interface. Some devices (e.g. Happ's
				 * UGCI) cram a lot of unrelated inputs into the
				 * same interface. */
				hidinput->report = report;
				input_register_device(hidinput->input);
				hidinput = NULL;
			}
		}

	if (hidinput)
		input_register_device(hidinput->input);

	return 0;
}
EXPORT_SYMBOL_GPL(hidinput_connect);

void hidinput_disconnect(struct hid_device *hid)
{
	struct hid_input *hidinput, *next;

	list_for_each_entry_safe(hidinput, next, &hid->inputs, list) {
		list_del(&hidinput->list);
		input_unregister_device(hidinput->input);
		kfree(hidinput);
	}
}
EXPORT_SYMBOL_GPL(hidinput_disconnect);

