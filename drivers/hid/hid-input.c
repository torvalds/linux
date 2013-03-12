/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006-2010 Jiri Kosina
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

#include "hid-ids.h"

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
	122,123, 90, 91, 85,unk,unk,unk,unk,unk,unk,unk,111,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,179,180,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,111,unk,unk,unk,unk,unk,unk,unk,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140,unk,unk,unk,unk
};

static const struct {
	__s32 x;
	__s32 y;
}  hid_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

#define map_abs(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_ABS, (c))
#define map_rel(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_REL, (c))
#define map_key(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_KEY, (c))
#define map_led(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_LED, (c))

#define map_abs_clear(c)	hid_map_usage_clear(hidinput, usage, &bit, \
		&max, EV_ABS, (c))
#define map_key_clear(c)	hid_map_usage_clear(hidinput, usage, &bit, \
		&max, EV_KEY, (c))

static bool match_scancode(struct hid_usage *usage,
			   unsigned int cur_idx, unsigned int scancode)
{
	return (usage->hid & (HID_USAGE_PAGE | HID_USAGE)) == scancode;
}

static bool match_keycode(struct hid_usage *usage,
			  unsigned int cur_idx, unsigned int keycode)
{
	/*
	 * We should exclude unmapped usages when doing lookup by keycode.
	 */
	return (usage->type == EV_KEY && usage->code == keycode);
}

static bool match_index(struct hid_usage *usage,
			unsigned int cur_idx, unsigned int idx)
{
	return cur_idx == idx;
}

typedef bool (*hid_usage_cmp_t)(struct hid_usage *usage,
				unsigned int cur_idx, unsigned int val);

static struct hid_usage *hidinput_find_key(struct hid_device *hid,
					   hid_usage_cmp_t match,
					   unsigned int value,
					   unsigned int *usage_idx)
{
	unsigned int i, j, k, cur_idx = 0;
	struct hid_report *report;
	struct hid_usage *usage;

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++) {
		list_for_each_entry(report, &hid->report_enum[k].report_list, list) {
			for (i = 0; i < report->maxfield; i++) {
				for (j = 0; j < report->field[i]->maxusage; j++) {
					usage = report->field[i]->usage + j;
					if (usage->type == EV_KEY || usage->type == 0) {
						if (match(usage, cur_idx, value)) {
							if (usage_idx)
								*usage_idx = cur_idx;
							return usage;
						}
						cur_idx++;
					}
				}
			}
		}
	}
	return NULL;
}

static struct hid_usage *hidinput_locate_usage(struct hid_device *hid,
					const struct input_keymap_entry *ke,
					unsigned int *index)
{
	struct hid_usage *usage;
	unsigned int scancode;

	if (ke->flags & INPUT_KEYMAP_BY_INDEX)
		usage = hidinput_find_key(hid, match_index, ke->index, index);
	else if (input_scancode_to_scalar(ke, &scancode) == 0)
		usage = hidinput_find_key(hid, match_scancode, scancode, index);
	else
		usage = NULL;

	return usage;
}

static int hidinput_getkeycode(struct input_dev *dev,
			       struct input_keymap_entry *ke)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct hid_usage *usage;
	unsigned int scancode, index;

	usage = hidinput_locate_usage(hid, ke, &index);
	if (usage) {
		ke->keycode = usage->type == EV_KEY ?
				usage->code : KEY_RESERVED;
		ke->index = index;
		scancode = usage->hid & (HID_USAGE_PAGE | HID_USAGE);
		ke->len = sizeof(scancode);
		memcpy(ke->scancode, &scancode, sizeof(scancode));
		return 0;
	}

	return -EINVAL;
}

static int hidinput_setkeycode(struct input_dev *dev,
			       const struct input_keymap_entry *ke,
			       unsigned int *old_keycode)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct hid_usage *usage;

	usage = hidinput_locate_usage(hid, ke, NULL);
	if (usage) {
		*old_keycode = usage->type == EV_KEY ?
				usage->code : KEY_RESERVED;
		usage->code = ke->keycode;

		clear_bit(*old_keycode, dev->keybit);
		set_bit(usage->code, dev->keybit);
		dbg_hid("Assigned keycode %d to HID usage code %x\n",
			usage->code, usage->hid);

		/*
		 * Set the keybit for the old keycode if the old keycode is used
		 * by another key
		 */
		if (hidinput_find_key(hid, match_keycode, *old_keycode, NULL))
			set_bit(*old_keycode, dev->keybit);

		return 0;
	}

	return -EINVAL;
}


/**
 * hidinput_calc_abs_res - calculate an absolute axis resolution
 * @field: the HID report field to calculate resolution for
 * @code: axis code
 *
 * The formula is:
 *                         (logical_maximum - logical_minimum)
 * resolution = ----------------------------------------------------------
 *              (physical_maximum - physical_minimum) * 10 ^ unit_exponent
 *
 * as seen in the HID specification v1.11 6.2.2.7 Global Items.
 *
 * Only exponent 1 length units are processed. Centimeters and inches are
 * converted to millimeters. Degrees are converted to radians.
 */
static __s32 hidinput_calc_abs_res(const struct hid_field *field, __u16 code)
{
	__s32 unit_exponent = field->unit_exponent;
	__s32 logical_extents = field->logical_maximum -
					field->logical_minimum;
	__s32 physical_extents = field->physical_maximum -
					field->physical_minimum;
	__s32 prev;

	/* Check if the extents are sane */
	if (logical_extents <= 0 || physical_extents <= 0)
		return 0;

	/*
	 * Verify and convert units.
	 * See HID specification v1.11 6.2.2.7 Global Items for unit decoding
	 */
	if (code == ABS_X || code == ABS_Y || code == ABS_Z) {
		if (field->unit == 0x11) {		/* If centimeters */
			/* Convert to millimeters */
			unit_exponent += 1;
		} else if (field->unit == 0x13) {	/* If inches */
			/* Convert to millimeters */
			prev = physical_extents;
			physical_extents *= 254;
			if (physical_extents < prev)
				return 0;
			unit_exponent -= 1;
		} else {
			return 0;
		}
	} else if (code == ABS_RX || code == ABS_RY || code == ABS_RZ) {
		if (field->unit == 0x14) {		/* If degrees */
			/* Convert to radians */
			prev = logical_extents;
			logical_extents *= 573;
			if (logical_extents < prev)
				return 0;
			unit_exponent += 1;
		} else if (field->unit != 0x12) {	/* If not radians */
			return 0;
		}
	} else {
		return 0;
	}

	/* Apply negative unit exponent */
	for (; unit_exponent < 0; unit_exponent++) {
		prev = logical_extents;
		logical_extents *= 10;
		if (logical_extents < prev)
			return 0;
	}
	/* Apply positive unit exponent */
	for (; unit_exponent > 0; unit_exponent--) {
		prev = physical_extents;
		physical_extents *= 10;
		if (physical_extents < prev)
			return 0;
	}

	/* Calculate resolution */
	return logical_extents / physical_extents;
}

#ifdef CONFIG_HID_BATTERY_STRENGTH
static enum power_supply_property hidinput_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_SCOPE,
};

#define HID_BATTERY_QUIRK_PERCENT	(1 << 0) /* always reports percent */
#define HID_BATTERY_QUIRK_FEATURE	(1 << 1) /* ask for feature report */

static const struct hid_device_id hid_battery_quirks[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
			       USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ANSI),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_ALU_WIRELESS_ANSI),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{}
};

static unsigned find_battery_quirk(struct hid_device *hdev)
{
	unsigned quirks = 0;
	const struct hid_device_id *match;

	match = hid_match_id(hdev, hid_battery_quirks);
	if (match != NULL)
		quirks = match->driver_data;

	return quirks;
}

static int hidinput_get_battery_property(struct power_supply *psy,
					 enum power_supply_property prop,
					 union power_supply_propval *val)
{
	struct hid_device *dev = container_of(psy, struct hid_device, battery);
	int ret = 0;
	__u8 buf[2] = {};

	switch (prop) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		ret = dev->hid_get_raw_report(dev, dev->battery_report_id,
					      buf, sizeof(buf),
					      dev->battery_report_type);

		if (ret != 2) {
			if (ret >= 0)
				ret = -EINVAL;
			break;
		}

		if (dev->battery_min < dev->battery_max &&
		    buf[1] >= dev->battery_min &&
		    buf[1] <= dev->battery_max)
			val->intval = (100 * (buf[1] - dev->battery_min)) /
				(dev->battery_max - dev->battery_min);
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = dev->name;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static bool hidinput_setup_battery(struct hid_device *dev, unsigned report_type, struct hid_field *field)
{
	struct power_supply *battery = &dev->battery;
	int ret;
	unsigned quirks;
	s32 min, max;

	if (field->usage->hid != HID_DC_BATTERYSTRENGTH)
		return false;	/* no match */

	if (battery->name != NULL)
		goto out;	/* already initialized? */

	battery->name = kasprintf(GFP_KERNEL, "hid-%s-battery", dev->uniq);
	if (battery->name == NULL)
		goto out;

	battery->type = POWER_SUPPLY_TYPE_BATTERY;
	battery->properties = hidinput_battery_props;
	battery->num_properties = ARRAY_SIZE(hidinput_battery_props);
	battery->use_for_apm = 0;
	battery->get_property = hidinput_get_battery_property;

	quirks = find_battery_quirk(dev);

	hid_dbg(dev, "device %x:%x:%x %d quirks %d\n",
		dev->bus, dev->vendor, dev->product, dev->version, quirks);

	min = field->logical_minimum;
	max = field->logical_maximum;

	if (quirks & HID_BATTERY_QUIRK_PERCENT) {
		min = 0;
		max = 100;
	}

	if (quirks & HID_BATTERY_QUIRK_FEATURE)
		report_type = HID_FEATURE_REPORT;

	dev->battery_min = min;
	dev->battery_max = max;
	dev->battery_report_type = report_type;
	dev->battery_report_id = field->report->id;

	ret = power_supply_register(&dev->dev, battery);
	if (ret != 0) {
		hid_warn(dev, "can't register power supply: %d\n", ret);
		kfree(battery->name);
		battery->name = NULL;
	}

	power_supply_powers(battery, &dev->dev);

out:
	return true;
}

static void hidinput_cleanup_battery(struct hid_device *dev)
{
	if (!dev->battery.name)
		return;

	power_supply_unregister(&dev->battery);
	kfree(dev->battery.name);
	dev->battery.name = NULL;
}
#else  /* !CONFIG_HID_BATTERY_STRENGTH */
static bool hidinput_setup_battery(struct hid_device *dev, unsigned report_type,
				   struct hid_field *field)
{
	return false;
}

static void hidinput_cleanup_battery(struct hid_device *dev)
{
}
#endif	/* CONFIG_HID_BATTERY_STRENGTH */

static void hidinput_configure_usage(struct hid_input *hidinput, struct hid_field *field,
				     struct hid_usage *usage)
{
	struct input_dev *input = hidinput->input;
	struct hid_device *device = input_get_drvdata(input);
	int max = 0, code;
	unsigned long *bit = NULL;

	field->hidinput = hidinput;

	if (field->flags & HID_MAIN_ITEM_CONSTANT)
		goto ignore;

	/* only LED usages are supported in output fields */
	if (field->report_type == HID_OUTPUT_REPORT &&
			(usage->hid & HID_USAGE_PAGE) != HID_UP_LED) {
		goto ignore;
	}

	if (device->driver->input_mapping) {
		int ret = device->driver->input_mapping(device, hidinput, field,
				usage, &bit, &max);
		if (ret > 0)
			goto mapped;
		if (ret < 0)
			goto ignore;
	}

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
		code = ((usage->hid - 1) & HID_USAGE);

		switch (field->application) {
		case HID_GD_MOUSE:
		case HID_GD_POINTER:  code += BTN_MOUSE; break;
		case HID_GD_JOYSTICK:
				if (code <= 0xf)
					code += BTN_JOYSTICK;
				else
					code += BTN_TRIGGER_HAPPY;
				break;
		case HID_GD_GAMEPAD:  code += BTN_GAMEPAD; break;
		default:
			switch (field->physical) {
			case HID_GD_MOUSE:
			case HID_GD_POINTER:  code += BTN_MOUSE; break;
			case HID_GD_JOYSTICK: code += BTN_JOYSTICK; break;
			case HID_GD_GAMEPAD:  code += BTN_GAMEPAD; break;
			default:              code += BTN_MISC;
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
			case 0x4: map_key_clear(KEY_CONTEXT_MENU); break;
			case 0x5: map_key_clear(KEY_MENU); break;
			case 0x6: map_key_clear(KEY_PROG1); break;
			case 0x7: map_key_clear(KEY_HELP); break;
			case 0x8: map_key_clear(KEY_EXIT); break;
			case 0x9: map_key_clear(KEY_SELECT); break;
			case 0xa: map_key_clear(KEY_RIGHT); break;
			case 0xb: map_key_clear(KEY_LEFT); break;
			case 0xc: map_key_clear(KEY_UP); break;
			case 0xd: map_key_clear(KEY_DOWN); break;
			case 0xe: map_key_clear(KEY_POWER2); break;
			case 0xf: map_key_clear(KEY_RESTART); break;
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
		switch (usage->hid & 0xffff) {		      /* HID-Value:                   */
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
		case 0x00: /* Undefined */
			goto ignore;

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

		case 0x46: /* TabletPick */
			map_key_clear(BTN_STYLUS2);
			break;

		case 0x51: /* ContactID */
			device->quirks |= HID_QUIRK_MULTITOUCH;
			goto unknown;

		default:  goto unknown;
		}
		break;

	case HID_UP_CONSUMER:	/* USB HUT v1.12, pages 75-84 */
		switch (usage->hid & HID_USAGE) {
		case 0x000: goto ignore;
		case 0x030: map_key_clear(KEY_POWER);		break;
		case 0x031: map_key_clear(KEY_RESTART);		break;
		case 0x032: map_key_clear(KEY_SLEEP);		break;
		case 0x034: map_key_clear(KEY_SLEEP);		break;
		case 0x035: map_key_clear(KEY_KBDILLUMTOGGLE);	break;
		case 0x036: map_key_clear(BTN_MISC);		break;

		case 0x040: map_key_clear(KEY_MENU);		break; /* Menu */
		case 0x041: map_key_clear(KEY_SELECT);		break; /* Menu Pick */
		case 0x042: map_key_clear(KEY_UP);		break; /* Menu Up */
		case 0x043: map_key_clear(KEY_DOWN);		break; /* Menu Down */
		case 0x044: map_key_clear(KEY_LEFT);		break; /* Menu Left */
		case 0x045: map_key_clear(KEY_RIGHT);		break; /* Menu Right */
		case 0x046: map_key_clear(KEY_ESC);		break; /* Menu Escape */
		case 0x047: map_key_clear(KEY_KPPLUS);		break; /* Menu Value Increase */
		case 0x048: map_key_clear(KEY_KPMINUS);		break; /* Menu Value Decrease */

		case 0x060: map_key_clear(KEY_INFO);		break; /* Data On Screen */
		case 0x061: map_key_clear(KEY_SUBTITLE);	break; /* Closed Caption */
		case 0x063: map_key_clear(KEY_VCR);		break; /* VCR/TV */
		case 0x065: map_key_clear(KEY_CAMERA);		break; /* Snapshot */
		case 0x069: map_key_clear(KEY_RED);		break;
		case 0x06a: map_key_clear(KEY_GREEN);		break;
		case 0x06b: map_key_clear(KEY_BLUE);		break;
		case 0x06c: map_key_clear(KEY_YELLOW);		break;
		case 0x06d: map_key_clear(KEY_ZOOM);		break;

		case 0x082: map_key_clear(KEY_VIDEO_NEXT);	break;
		case 0x083: map_key_clear(KEY_LAST);		break;
		case 0x084: map_key_clear(KEY_ENTER);		break;
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
		case 0x0bc: map_key_clear(KEY_MEDIA_REPEAT);	break;
		case 0x0b9: map_key_clear(KEY_SHUFFLE);		break;
		case 0x0bf: map_key_clear(KEY_SLOW);		break;

		case 0x0cd: map_key_clear(KEY_PLAYPAUSE);	break;
		case 0x0e0: map_abs_clear(ABS_VOLUME);		break;
		case 0x0e2: map_key_clear(KEY_MUTE);		break;
		case 0x0e5: map_key_clear(KEY_BASSBOOST);	break;
		case 0x0e9: map_key_clear(KEY_VOLUMEUP);	break;
		case 0x0ea: map_key_clear(KEY_VOLUMEDOWN);	break;
		case 0x0f5: map_key_clear(KEY_SLOW);		break;

		case 0x182: map_key_clear(KEY_BOOKMARKS);	break;
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
		case 0x193: map_key_clear(KEY_PLAYER);		break;
		case 0x194: map_key_clear(KEY_FILE);		break;
		case 0x196: map_key_clear(KEY_WWW);		break;
		case 0x199: map_key_clear(KEY_CHAT);		break;
		case 0x19c: map_key_clear(KEY_LOGOFF);		break;
		case 0x19e: map_key_clear(KEY_COFFEE);		break;
		case 0x1a6: map_key_clear(KEY_HELP);		break;
		case 0x1a7: map_key_clear(KEY_DOCUMENTS);	break;
		case 0x1ab: map_key_clear(KEY_SPELLCHECK);	break;
		case 0x1ae: map_key_clear(KEY_KEYBOARD);	break;
		case 0x1b6: map_key_clear(KEY_IMAGES);		break;
		case 0x1b7: map_key_clear(KEY_AUDIO);		break;
		case 0x1b8: map_key_clear(KEY_VIDEO);		break;
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
		case 0x23d: map_key_clear(KEY_EDIT);		break;
		case 0x25f: map_key_clear(KEY_CANCEL);		break;
		case 0x269: map_key_clear(KEY_INSERT);		break;
		case 0x26a: map_key_clear(KEY_DELETE);		break;
		case 0x279: map_key_clear(KEY_REDO);		break;

		case 0x289: map_key_clear(KEY_REPLY);		break;
		case 0x28b: map_key_clear(KEY_FORWARDMAIL);	break;
		case 0x28c: map_key_clear(KEY_SEND);		break;

		default:    goto ignore;
		}
		break;

	case HID_UP_GENDEVCTRLS:
		if (hidinput_setup_battery(device, HID_INPUT_REPORT, field))
			goto ignore;
		else
			goto unknown;
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
		goto ignore;

	case HID_UP_CUSTOM: /* Reported on Logitech and Apple USB keyboards */
		set_bit(EV_REP, input->evbit);
		goto ignore;

	case HID_UP_LOGIVENDOR:
		goto ignore;

	case HID_UP_PID:
		switch (usage->hid & HID_USAGE) {
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

mapped:
	if (device->driver->input_mapped && device->driver->input_mapped(device,
				hidinput, field, usage, &bit, &max) < 0)
		goto ignore;

	set_bit(usage->type, input->evbit);

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

		input_abs_set_res(input, usage->code,
				  hidinput_calc_abs_res(field, usage->code));

		/* use a larger default input buffer for MT devices */
		if (usage->code == ABS_MT_POSITION_X && input->hint_events_per_packet == 0)
			input_set_events_per_packet(input, 60);
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

	if (usage->type == EV_KEY) {
		set_bit(EV_MSC, input->evbit);
		set_bit(MSC_SCAN, input->mscbit);
	}

ignore:
	return;

}

void hidinput_hid_event(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct input_dev *input;
	unsigned *quirks = &hid->quirks;

	if (!field->hidinput)
		return;

	input = field->hidinput->input;

	if (!usage->type)
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

	/*
	 * Ignore out-of-range values as per HID specification,
	 * section 5.10 and 6.2.25
	 */
	if ((field->flags & HID_MAIN_ITEM_VARIABLE) &&
	    (value < field->logical_minimum ||
	     value > field->logical_maximum)) {
		dbg_hid("Ignoring out-of-range value %x\n", value);
		return;
	}

	/* report the usage code as scancode if the key status has changed */
	if (usage->type == EV_KEY && !!test_bit(usage->code, input->key) != value)
		input_event(input, EV_MSC, MSC_SCAN, usage->hid);

	input_event(input, usage->type, usage->code, value);

	if ((field->flags & HID_MAIN_ITEM_RELATIVE) && (usage->type == EV_KEY))
		input_event(input, usage->type, usage->code, 0);
}

void hidinput_report_event(struct hid_device *hid, struct hid_report *report)
{
	struct hid_input *hidinput;

	if (hid->quirks & HID_QUIRK_NO_INPUT_SYNC)
		return;

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

struct hid_field *hidinput_get_led_field(struct hid_device *hid)
{
	struct hid_report *report;
	struct hid_field *field;
	int i, j;

	list_for_each_entry(report,
			    &hid->report_enum[HID_OUTPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			for (j = 0; j < field->maxusage; j++)
				if (field->usage[j].type == EV_LED)
					return field;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(hidinput_get_led_field);

unsigned int hidinput_count_leds(struct hid_device *hid)
{
	struct hid_report *report;
	struct hid_field *field;
	int i, j;
	unsigned int count = 0;

	list_for_each_entry(report,
			    &hid->report_enum[HID_OUTPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			for (j = 0; j < field->maxusage; j++)
				if (field->usage[j].type == EV_LED &&
				    field->value[j])
					count += 1;
		}
	}
	return count;
}
EXPORT_SYMBOL_GPL(hidinput_count_leds);

static int hidinput_open(struct input_dev *dev)
{
	struct hid_device *hid = input_get_drvdata(dev);

	return hid_hw_open(hid);
}

static void hidinput_close(struct input_dev *dev)
{
	struct hid_device *hid = input_get_drvdata(dev);

	hid_hw_close(hid);
}

static void report_features(struct hid_device *hid)
{
	struct hid_driver *drv = hid->driver;
	struct hid_report_enum *rep_enum;
	struct hid_report *rep;
	int i, j;

	rep_enum = &hid->report_enum[HID_FEATURE_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list)
		for (i = 0; i < rep->maxfield; i++)
			for (j = 0; j < rep->field[i]->maxusage; j++) {
				/* Verify if Battery Strength feature is available */
				hidinput_setup_battery(hid, HID_FEATURE_REPORT, rep->field[i]);

				if (drv->feature_mapping)
					drv->feature_mapping(hid, rep->field[i],
							     rep->field[i]->usage + j);
			}
}

/*
 * Register the input device; print a message.
 * Configure the input layer interface
 * Read all reports and initialize the absolute field values.
 */

int hidinput_connect(struct hid_device *hid, unsigned int force)
{
	struct hid_report *report;
	struct hid_input *hidinput = NULL;
	struct input_dev *input_dev;
	int i, j, k;

	INIT_LIST_HEAD(&hid->inputs);

	if (!force) {
		for (i = 0; i < hid->maxcollection; i++) {
			struct hid_collection *col = &hid->collection[i];
			if (col->type == HID_COLLECTION_APPLICATION ||
					col->type == HID_COLLECTION_PHYSICAL)
				if (IS_INPUT_APPLICATION(col->usage))
					break;
		}

		if (i == hid->maxcollection)
			return -1;
	}

	report_features(hid);

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++) {
		if (k == HID_OUTPUT_REPORT &&
			hid->quirks & HID_QUIRK_SKIP_OUTPUT_REPORTS)
			continue;

		list_for_each_entry(report, &hid->report_enum[k].report_list, list) {

			if (!report->maxfield)
				continue;

			if (!hidinput) {
				hidinput = kzalloc(sizeof(*hidinput), GFP_KERNEL);
				input_dev = input_allocate_device();
				if (!hidinput || !input_dev) {
					kfree(hidinput);
					input_free_device(input_dev);
					hid_err(hid, "Out of memory during hid input probe\n");
					goto out_unwind;
				}

				input_set_drvdata(input_dev, hid);
				input_dev->event =
					hid->ll_driver->hidinput_input_event;
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
				input_dev->dev.parent = hid->dev.parent;
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
				if (hid->driver->input_register &&
						hid->driver->input_register(hid, hidinput))
					goto out_cleanup;
				if (input_register_device(hidinput->input))
					goto out_cleanup;
				hidinput = NULL;
			}
		}
	}

	if (hid->quirks & HID_QUIRK_MULTITOUCH) {
		/* generic hid does not know how to handle multitouch devices */
		if (hidinput)
			goto out_cleanup;
		goto out_unwind;
	}

	if (hidinput && hid->driver->input_register &&
			hid->driver->input_register(hid, hidinput))
		goto out_cleanup;

	if (hidinput && input_register_device(hidinput->input))
		goto out_cleanup;

	return 0;

out_cleanup:
	list_del(&hidinput->list);
	input_free_device(hidinput->input);
	kfree(hidinput);
out_unwind:
	/* unwind the ones we already registered */
	hidinput_disconnect(hid);

	return -1;
}
EXPORT_SYMBOL_GPL(hidinput_connect);

void hidinput_disconnect(struct hid_device *hid)
{
	struct hid_input *hidinput, *next;

	hidinput_cleanup_battery(hid);

	list_for_each_entry_safe(hidinput, next, &hid->inputs, list) {
		list_del(&hidinput->list);
		input_unregister_device(hidinput->input);
		kfree(hidinput);
	}
}
EXPORT_SYMBOL_GPL(hidinput_disconnect);

