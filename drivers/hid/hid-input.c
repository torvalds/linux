// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006-2010 Jiri Kosina
 *
 *  HID to Linux Input mapping
 */

/*
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

struct usage_priority {
	__u32 usage;			/* the HID usage associated */
	bool global;			/* we assume all usages to be slotted,
					 * unless global
					 */
	unsigned int slot_overwrite;	/* for globals: allows to set the usage
					 * before or after the slots
					 */
};

/*
 * hid-input will convert this list into priorities:
 * the first element will have the highest priority
 * (the length of the following array) and the last
 * element the lowest (1).
 *
 * hid-input will then shift the priority by 8 bits to leave some space
 * in case drivers want to interleave other fields.
 *
 * To accommodate slotted devices, the slot priority is
 * defined in the next 8 bits (defined by 0xff - slot).
 *
 * If drivers want to add fields before those, hid-input will
 * leave out the first 8 bits of the priority value.
 *
 * This still leaves us 65535 individual priority values.
 */
static const struct usage_priority hidinput_usages_priorities[] = {
	{ /* Eraser (eraser touching) must always come before tipswitch */
	  .usage = HID_DG_ERASER,
	},
	{ /* Invert must always come before In Range */
	  .usage = HID_DG_INVERT,
	},
	{ /* Is the tip of the tool touching? */
	  .usage = HID_DG_TIPSWITCH,
	},
	{ /* Tip Pressure might emulate tip switch */
	  .usage = HID_DG_TIPPRESSURE,
	},
	{ /* In Range needs to come after the other tool states */
	  .usage = HID_DG_INRANGE,
	},
};

#define map_abs(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_ABS, (c))
#define map_rel(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_REL, (c))
#define map_key(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_KEY, (c))
#define map_led(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_LED, (c))
#define map_msc(c)	hid_map_usage(hidinput, usage, &bit, &max, EV_MSC, (c))

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
		usage->type = EV_KEY;
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
__s32 hidinput_calc_abs_res(const struct hid_field *field, __u16 code)
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
	switch (code) {
	case ABS_X:
	case ABS_Y:
	case ABS_Z:
	case ABS_MT_POSITION_X:
	case ABS_MT_POSITION_Y:
	case ABS_MT_TOOL_X:
	case ABS_MT_TOOL_Y:
	case ABS_MT_TOUCH_MAJOR:
	case ABS_MT_TOUCH_MINOR:
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
		break;

	case ABS_RX:
	case ABS_RY:
	case ABS_RZ:
	case ABS_WHEEL:
	case ABS_TILT_X:
	case ABS_TILT_Y:
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
		break;

	default:
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
	return DIV_ROUND_CLOSEST(logical_extents, physical_extents);
}
EXPORT_SYMBOL_GPL(hidinput_calc_abs_res);

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
#define HID_BATTERY_QUIRK_IGNORE	(1 << 2) /* completely ignore the battery */
#define HID_BATTERY_QUIRK_AVOID_QUERY	(1 << 3) /* do not query the battery */

static const struct hid_device_id hid_battery_quirks[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ISO),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_ALU_WIRELESS_2009_ANSI),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ANSI),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
			       USB_DEVICE_ID_APPLE_ALU_WIRELESS_2011_ISO),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_ALU_WIRELESS_ANSI),
	  HID_BATTERY_QUIRK_PERCENT | HID_BATTERY_QUIRK_FEATURE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ELECOM,
		USB_DEVICE_ID_ELECOM_BM084),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SYMBOL,
		USB_DEVICE_ID_SYMBOL_SCANNER_3),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100CHI_KEYBOARD),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_DINOVO_EDGE_KBD),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_ASUS_TP420IA_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_ASUS_GV301RA_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELAN, USB_DEVICE_ID_ASUS_UX550_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELAN, USB_DEVICE_ID_ASUS_UX550VE_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE, USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_L),
	  HID_BATTERY_QUIRK_AVOID_QUERY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE, USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_PRO_MW),
	  HID_BATTERY_QUIRK_AVOID_QUERY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE, USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_PRO_SW),
	  HID_BATTERY_QUIRK_AVOID_QUERY },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_HP_ENVY_X360_15),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_HP_ENVY_X360_15T_DR100),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_HP_ENVY_X360_EU0009NV),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_HP_SPECTRE_X360_15),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_HP_SPECTRE_X360_13_AW0020NG),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_SURFACE_GO_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_SURFACE_GO2_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ELAN, I2C_DEVICE_ID_LENOVO_YOGA_C630_TOUCHSCREEN),
	  HID_BATTERY_QUIRK_IGNORE },
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

static int hidinput_scale_battery_capacity(struct hid_device *dev,
					   int value)
{
	if (dev->battery_min < dev->battery_max &&
	    value >= dev->battery_min && value <= dev->battery_max)
		value = ((value - dev->battery_min) * 100) /
			(dev->battery_max - dev->battery_min);

	return value;
}

static int hidinput_query_battery_capacity(struct hid_device *dev)
{
	u8 *buf;
	int ret;

	buf = kmalloc(4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(dev, dev->battery_report_id, buf, 4,
				 dev->battery_report_type, HID_REQ_GET_REPORT);
	if (ret < 2) {
		kfree(buf);
		return -ENODATA;
	}

	ret = hidinput_scale_battery_capacity(dev, buf[1]);
	kfree(buf);
	return ret;
}

static int hidinput_get_battery_property(struct power_supply *psy,
					 enum power_supply_property prop,
					 union power_supply_propval *val)
{
	struct hid_device *dev = power_supply_get_drvdata(psy);
	int value;
	int ret = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		if (dev->battery_status != HID_BATTERY_REPORTED &&
		    !dev->battery_avoid_query) {
			value = hidinput_query_battery_capacity(dev);
			if (value < 0)
				return value;
		} else  {
			value = dev->battery_capacity;
		}

		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = dev->name;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		if (dev->battery_status != HID_BATTERY_REPORTED &&
		    !dev->battery_avoid_query) {
			value = hidinput_query_battery_capacity(dev);
			if (value < 0)
				return value;

			dev->battery_capacity = value;
			dev->battery_status = HID_BATTERY_QUERIED;
		}

		if (dev->battery_status == HID_BATTERY_UNKNOWN)
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		else
			val->intval = dev->battery_charge_status;
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

static int hidinput_setup_battery(struct hid_device *dev, unsigned report_type,
				  struct hid_field *field, bool is_percentage)
{
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = { .drv_data = dev, };
	unsigned quirks;
	s32 min, max;
	int error;

	if (dev->battery)
		return 0;	/* already initialized? */

	quirks = find_battery_quirk(dev);

	hid_dbg(dev, "device %x:%x:%x %d quirks %d\n",
		dev->bus, dev->vendor, dev->product, dev->version, quirks);

	if (quirks & HID_BATTERY_QUIRK_IGNORE)
		return 0;

	psy_desc = kzalloc(sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;

	psy_desc->name = kasprintf(GFP_KERNEL, "hid-%s-battery",
				   strlen(dev->uniq) ?
					dev->uniq : dev_name(&dev->dev));
	if (!psy_desc->name) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = hidinput_battery_props;
	psy_desc->num_properties = ARRAY_SIZE(hidinput_battery_props);
	psy_desc->use_for_apm = 0;
	psy_desc->get_property = hidinput_get_battery_property;

	min = field->logical_minimum;
	max = field->logical_maximum;

	if (is_percentage || (quirks & HID_BATTERY_QUIRK_PERCENT)) {
		min = 0;
		max = 100;
	}

	if (quirks & HID_BATTERY_QUIRK_FEATURE)
		report_type = HID_FEATURE_REPORT;

	dev->battery_min = min;
	dev->battery_max = max;
	dev->battery_report_type = report_type;
	dev->battery_report_id = field->report->id;
	dev->battery_charge_status = POWER_SUPPLY_STATUS_DISCHARGING;

	/*
	 * Stylus is normally not connected to the device and thus we
	 * can't query the device and get meaningful battery strength.
	 * We have to wait for the device to report it on its own.
	 */
	dev->battery_avoid_query = report_type == HID_INPUT_REPORT &&
				   field->physical == HID_DG_STYLUS;

	if (quirks & HID_BATTERY_QUIRK_AVOID_QUERY)
		dev->battery_avoid_query = true;

	dev->battery = power_supply_register(&dev->dev, psy_desc, &psy_cfg);
	if (IS_ERR(dev->battery)) {
		error = PTR_ERR(dev->battery);
		hid_warn(dev, "can't register power supply: %d\n", error);
		goto err_free_name;
	}

	power_supply_powers(dev->battery, &dev->dev);
	return 0;

err_free_name:
	kfree(psy_desc->name);
err_free_mem:
	kfree(psy_desc);
	dev->battery = NULL;
	return error;
}

static void hidinput_cleanup_battery(struct hid_device *dev)
{
	const struct power_supply_desc *psy_desc;

	if (!dev->battery)
		return;

	psy_desc = dev->battery->desc;
	power_supply_unregister(dev->battery);
	kfree(psy_desc->name);
	kfree(psy_desc);
	dev->battery = NULL;
}

static void hidinput_update_battery(struct hid_device *dev, int value)
{
	int capacity;

	if (!dev->battery)
		return;

	if (value == 0 || value < dev->battery_min || value > dev->battery_max)
		return;

	capacity = hidinput_scale_battery_capacity(dev, value);

	if (dev->battery_status != HID_BATTERY_REPORTED ||
	    capacity != dev->battery_capacity ||
	    ktime_after(ktime_get_coarse(), dev->battery_ratelimit_time)) {
		dev->battery_capacity = capacity;
		dev->battery_status = HID_BATTERY_REPORTED;
		dev->battery_ratelimit_time =
			ktime_add_ms(ktime_get_coarse(), 30 * 1000);
		power_supply_changed(dev->battery);
	}
}

static bool hidinput_set_battery_charge_status(struct hid_device *dev,
					       unsigned int usage, int value)
{
	switch (usage) {
	case HID_BAT_CHARGING:
		dev->battery_charge_status = value ?
					     POWER_SUPPLY_STATUS_CHARGING :
					     POWER_SUPPLY_STATUS_DISCHARGING;
		power_supply_changed(dev->battery);
		return true;
	}

	return false;
}
#else  /* !CONFIG_HID_BATTERY_STRENGTH */
static int hidinput_setup_battery(struct hid_device *dev, unsigned report_type,
				  struct hid_field *field, bool is_percentage)
{
	return 0;
}

static void hidinput_cleanup_battery(struct hid_device *dev)
{
}

static void hidinput_update_battery(struct hid_device *dev, int value)
{
}

static bool hidinput_set_battery_charge_status(struct hid_device *dev,
					       unsigned int usage, int value)
{
	return false;
}
#endif	/* CONFIG_HID_BATTERY_STRENGTH */

static bool hidinput_field_in_collection(struct hid_device *device, struct hid_field *field,
					 unsigned int type, unsigned int usage)
{
	struct hid_collection *collection;

	collection = &device->collection[field->usage->collection_index];

	return collection->type == type && collection->usage == usage;
}

static void hidinput_configure_usage(struct hid_input *hidinput, struct hid_field *field,
				     struct hid_usage *usage, unsigned int usage_index)
{
	struct input_dev *input = hidinput->input;
	struct hid_device *device = input_get_drvdata(input);
	const struct usage_priority *usage_priority = NULL;
	int max = 0, code;
	unsigned int i = 0;
	unsigned long *bit = NULL;

	field->hidinput = hidinput;

	if (field->flags & HID_MAIN_ITEM_CONSTANT)
		goto ignore;

	/* Ignore if report count is out of bounds. */
	if (field->report_count < 1)
		goto ignore;

	/* only LED usages are supported in output fields */
	if (field->report_type == HID_OUTPUT_REPORT &&
			(usage->hid & HID_USAGE_PAGE) != HID_UP_LED) {
		goto ignore;
	}

	/* assign a priority based on the static list declared here */
	for (i = 0; i < ARRAY_SIZE(hidinput_usages_priorities); i++) {
		if (usage->hid == hidinput_usages_priorities[i].usage) {
			usage_priority = &hidinput_usages_priorities[i];

			field->usages_priorities[usage_index] =
				(ARRAY_SIZE(hidinput_usages_priorities) - i) << 8;
			break;
		}
	}

	/*
	 * For slotted devices, we need to also add the slot index
	 * in the priority.
	 */
	if (usage_priority && usage_priority->global)
		field->usages_priorities[usage_index] |=
			usage_priority->slot_overwrite;
	else
		field->usages_priorities[usage_index] |=
			(0xff - field->slot_idx) << 16;

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
					code += BTN_TRIGGER_HAPPY - 0x10;
				break;
		case HID_GD_GAMEPAD:
				if (code <= 0xf)
					code += BTN_GAMEPAD;
				else
					code += BTN_TRIGGER_HAPPY - 0x10;
				break;
		case HID_CP_CONSUMER_CONTROL:
				if (hidinput_field_in_collection(device, field,
								 HID_COLLECTION_NAMED_ARRAY,
								 HID_CP_PROGRAMMABLEBUTTONS)) {
					if (code <= 0x1d)
						code += KEY_MACRO1;
					else
						code += BTN_TRIGGER_HAPPY - 0x1e;
					break;
				}
				fallthrough;
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

		if ((usage->hid & 0xf0) == 0xa0) {	/* SystemControl */
			switch (usage->hid & 0xf) {
			case 0x9: map_key_clear(KEY_MICMUTE); break;
			default: goto ignore;
			}
			break;
		}

		if ((usage->hid & 0xf0) == 0xb0) {	/* SC - Display */
			switch (usage->hid & 0xf) {
			case 0x05: map_key_clear(KEY_SWITCHVIDEOMODE); break;
			default: goto ignore;
			}
			break;
		}

		/*
		 * Some lazy vendors declare 255 usages for System Control,
		 * leading to the creation of ABS_X|Y axis and too many others.
		 * It wouldn't be a problem if joydev doesn't consider the
		 * device as a joystick then.
		 */
		if (field->application == HID_GD_SYSTEM_CONTROL)
			goto ignore;

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
			if (field->flags & HID_MAIN_ITEM_RELATIVE)
				map_rel(usage->hid & 0xf);
			else
				map_abs_clear(usage->hid & 0xf);
			break;

		case HID_GD_WHEEL:
			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				set_bit(REL_WHEEL, input->relbit);
				map_rel(REL_WHEEL_HI_RES);
			} else {
				map_abs(usage->hid & 0xf);
			}
			break;
		case HID_GD_SLIDER: case HID_GD_DIAL:
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

		case HID_GD_RFKILL_BTN:
			/* MS wireless radio ctl extension, also check CA */
			if (field->application == HID_GD_WIRELESS_RADIO_CTLS) {
				map_key_clear(KEY_RFKILL);
				/* We need to simulate the btn release */
				field->flags |= HID_MAIN_ITEM_RELATIVE;
				break;
			}
			goto unknown;

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
		if ((field->application & 0xff) == 0x01) /* Digitizer */
			__set_bit(INPUT_PROP_POINTER, input->propbit);
		else if ((field->application & 0xff) == 0x02) /* Pen */
			__set_bit(INPUT_PROP_DIRECT, input->propbit);

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
			switch (field->physical) {
			case HID_DG_PUCK:
				map_key(BTN_TOOL_MOUSE);
				break;
			case HID_DG_FINGER:
				map_key(BTN_TOOL_FINGER);
				break;
			default:
				/*
				 * If the physical is not given,
				 * rely on the application.
				 */
				if (!field->physical) {
					switch (field->application) {
					case HID_DG_TOUCHSCREEN:
					case HID_DG_TOUCHPAD:
						map_key_clear(BTN_TOOL_FINGER);
						break;
					default:
						map_key_clear(BTN_TOOL_PEN);
					}
				} else {
					map_key(BTN_TOOL_PEN);
				}
				break;
			}
			break;

		case 0x3b: /* Battery Strength */
			hidinput_setup_battery(device, HID_INPUT_REPORT, field, false);
			usage->type = EV_PWR;
			return;

		case 0x3c: /* Invert */
			device->quirks &= ~HID_QUIRK_NOINVERT;
			map_key_clear(BTN_TOOL_RUBBER);
			break;

		case 0x3d: /* X Tilt */
			map_abs_clear(ABS_TILT_X);
			break;

		case 0x3e: /* Y Tilt */
			map_abs_clear(ABS_TILT_Y);
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

		case 0x45: /* ERASER */
			/*
			 * This event is reported when eraser tip touches the surface.
			 * Actual eraser (BTN_TOOL_RUBBER) is set and released either
			 * by Invert if tool reports proximity or by Eraser directly.
			 */
			if (!test_bit(BTN_TOOL_RUBBER, input->keybit)) {
				device->quirks |= HID_QUIRK_NOINVERT;
				set_bit(BTN_TOOL_RUBBER, input->keybit);
			}
			map_key_clear(BTN_TOUCH);
			break;

		case 0x46: /* TabletPick */
		case 0x5a: /* SecondaryBarrelSwitch */
			map_key_clear(BTN_STYLUS2);
			break;

		case 0x5b: /* TransducerSerialNumber */
		case 0x6e: /* TransducerSerialNumber2 */
			map_msc(MSC_SERIAL);
			break;

		default:  goto unknown;
		}
		break;

	case HID_UP_TELEPHONY:
		switch (usage->hid & HID_USAGE) {
		case 0x2f: map_key_clear(KEY_MICMUTE);		break;
		case 0xb0: map_key_clear(KEY_NUMERIC_0);	break;
		case 0xb1: map_key_clear(KEY_NUMERIC_1);	break;
		case 0xb2: map_key_clear(KEY_NUMERIC_2);	break;
		case 0xb3: map_key_clear(KEY_NUMERIC_3);	break;
		case 0xb4: map_key_clear(KEY_NUMERIC_4);	break;
		case 0xb5: map_key_clear(KEY_NUMERIC_5);	break;
		case 0xb6: map_key_clear(KEY_NUMERIC_6);	break;
		case 0xb7: map_key_clear(KEY_NUMERIC_7);	break;
		case 0xb8: map_key_clear(KEY_NUMERIC_8);	break;
		case 0xb9: map_key_clear(KEY_NUMERIC_9);	break;
		case 0xba: map_key_clear(KEY_NUMERIC_STAR);	break;
		case 0xbb: map_key_clear(KEY_NUMERIC_POUND);	break;
		case 0xbc: map_key_clear(KEY_NUMERIC_A);	break;
		case 0xbd: map_key_clear(KEY_NUMERIC_B);	break;
		case 0xbe: map_key_clear(KEY_NUMERIC_C);	break;
		case 0xbf: map_key_clear(KEY_NUMERIC_D);	break;
		default: goto ignore;
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
		case 0x06d: map_key_clear(KEY_ASPECT_RATIO);	break;

		case 0x06f: map_key_clear(KEY_BRIGHTNESSUP);		break;
		case 0x070: map_key_clear(KEY_BRIGHTNESSDOWN);		break;
		case 0x072: map_key_clear(KEY_BRIGHTNESS_TOGGLE);	break;
		case 0x073: map_key_clear(KEY_BRIGHTNESS_MIN);		break;
		case 0x074: map_key_clear(KEY_BRIGHTNESS_MAX);		break;
		case 0x075: map_key_clear(KEY_BRIGHTNESS_AUTO);		break;

		case 0x079: map_key_clear(KEY_KBDILLUMUP);	break;
		case 0x07a: map_key_clear(KEY_KBDILLUMDOWN);	break;
		case 0x07c: map_key_clear(KEY_KBDILLUMTOGGLE);	break;

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
		case 0x0cf: map_key_clear(KEY_VOICECOMMAND);	break;

		case 0x0d8: map_key_clear(KEY_DICTATE);		break;
		case 0x0d9: map_key_clear(KEY_EMOJI_PICKER);	break;

		case 0x0e0: map_abs_clear(ABS_VOLUME);		break;
		case 0x0e2: map_key_clear(KEY_MUTE);		break;
		case 0x0e5: map_key_clear(KEY_BASSBOOST);	break;
		case 0x0e9: map_key_clear(KEY_VOLUMEUP);	break;
		case 0x0ea: map_key_clear(KEY_VOLUMEDOWN);	break;
		case 0x0f5: map_key_clear(KEY_SLOW);		break;

		case 0x181: map_key_clear(KEY_BUTTONCONFIG);	break;
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
		case 0x18f: map_key_clear(KEY_TASKMANAGER);	break;
		case 0x190: map_key_clear(KEY_JOURNAL);		break;
		case 0x191: map_key_clear(KEY_FINANCE);		break;
		case 0x192: map_key_clear(KEY_CALC);		break;
		case 0x193: map_key_clear(KEY_PLAYER);		break;
		case 0x194: map_key_clear(KEY_FILE);		break;
		case 0x196: map_key_clear(KEY_WWW);		break;
		case 0x199: map_key_clear(KEY_CHAT);		break;
		case 0x19c: map_key_clear(KEY_LOGOFF);		break;
		case 0x19e: map_key_clear(KEY_COFFEE);		break;
		case 0x19f: map_key_clear(KEY_CONTROLPANEL);		break;
		case 0x1a2: map_key_clear(KEY_APPSELECT);		break;
		case 0x1a3: map_key_clear(KEY_NEXT);		break;
		case 0x1a4: map_key_clear(KEY_PREVIOUS);	break;
		case 0x1a6: map_key_clear(KEY_HELP);		break;
		case 0x1a7: map_key_clear(KEY_DOCUMENTS);	break;
		case 0x1ab: map_key_clear(KEY_SPELLCHECK);	break;
		case 0x1ae: map_key_clear(KEY_KEYBOARD);	break;
		case 0x1b1: map_key_clear(KEY_SCREENSAVER);		break;
		case 0x1b4: map_key_clear(KEY_FILE);		break;
		case 0x1b6: map_key_clear(KEY_IMAGES);		break;
		case 0x1b7: map_key_clear(KEY_AUDIO);		break;
		case 0x1b8: map_key_clear(KEY_VIDEO);		break;
		case 0x1bc: map_key_clear(KEY_MESSENGER);	break;
		case 0x1bd: map_key_clear(KEY_INFO);		break;
		case 0x1cb: map_key_clear(KEY_ASSISTANT);	break;
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
		case 0x232: map_key_clear(KEY_FULL_SCREEN);	break;
		case 0x233: map_key_clear(KEY_SCROLLUP);	break;
		case 0x234: map_key_clear(KEY_SCROLLDOWN);	break;
		case 0x238: /* AC Pan */
			set_bit(REL_HWHEEL, input->relbit);
			map_rel(REL_HWHEEL_HI_RES);
			break;
		case 0x23d: map_key_clear(KEY_EDIT);		break;
		case 0x25f: map_key_clear(KEY_CANCEL);		break;
		case 0x269: map_key_clear(KEY_INSERT);		break;
		case 0x26a: map_key_clear(KEY_DELETE);		break;
		case 0x279: map_key_clear(KEY_REDO);		break;

		case 0x289: map_key_clear(KEY_REPLY);		break;
		case 0x28b: map_key_clear(KEY_FORWARDMAIL);	break;
		case 0x28c: map_key_clear(KEY_SEND);		break;

		case 0x29d: map_key_clear(KEY_KBD_LAYOUT_NEXT);	break;

		case 0x2a2: map_key_clear(KEY_ALL_APPLICATIONS);	break;

		case 0x2c7: map_key_clear(KEY_KBDINPUTASSIST_PREV);		break;
		case 0x2c8: map_key_clear(KEY_KBDINPUTASSIST_NEXT);		break;
		case 0x2c9: map_key_clear(KEY_KBDINPUTASSIST_PREVGROUP);		break;
		case 0x2ca: map_key_clear(KEY_KBDINPUTASSIST_NEXTGROUP);		break;
		case 0x2cb: map_key_clear(KEY_KBDINPUTASSIST_ACCEPT);	break;
		case 0x2cc: map_key_clear(KEY_KBDINPUTASSIST_CANCEL);	break;

		case 0x29f: map_key_clear(KEY_SCALE);		break;

		default: map_key_clear(KEY_UNKNOWN);
		}
		break;

	case HID_UP_GENDEVCTRLS:
		switch (usage->hid) {
		case HID_DC_BATTERYSTRENGTH:
			hidinput_setup_battery(device, HID_INPUT_REPORT, field, false);
			usage->type = EV_PWR;
			return;
		}
		goto unknown;

	case HID_UP_BATTERY:
		switch (usage->hid) {
		case HID_BAT_ABSOLUTESTATEOFCHARGE:
			hidinput_setup_battery(device, HID_INPUT_REPORT, field, true);
			usage->type = EV_PWR;
			return;
		case HID_BAT_CHARGING:
			usage->type = EV_PWR;
			return;
		}
		goto unknown;
	case HID_UP_CAMERA:
		switch (usage->hid & HID_USAGE) {
		case 0x020:
			map_key_clear(KEY_CAMERA_FOCUS);	break;
		case 0x021:
			map_key_clear(KEY_CAMERA);		break;
		default:
			goto ignore;
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

	case HID_UP_HPVENDOR2:
		set_bit(EV_REP, input->evbit);
		switch (usage->hid & HID_USAGE) {
		case 0x001: map_key_clear(KEY_MICMUTE);		break;
		case 0x003: map_key_clear(KEY_BRIGHTNESSDOWN);	break;
		case 0x004: map_key_clear(KEY_BRIGHTNESSUP);	break;
		default:    goto ignore;
		}
		break;

	case HID_UP_MSVENDOR:
		goto ignore;

	case HID_UP_CUSTOM: /* Reported on Logitech and Apple USB keyboards */
		set_bit(EV_REP, input->evbit);
		goto ignore;

	case HID_UP_LOGIVENDOR:
		/* intentional fallback */
	case HID_UP_LOGIVENDOR2:
		/* intentional fallback */
	case HID_UP_LOGIVENDOR3:
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
	/* Mapping failed, bail out */
	if (!bit)
		return;

	if (device->driver->input_mapped &&
	    device->driver->input_mapped(device, hidinput, field, usage,
					 &bit, &max) < 0) {
		/*
		 * The driver indicated that no further generic handling
		 * of the usage is desired.
		 */
		return;
	}

	set_bit(usage->type, input->evbit);

	/*
	 * This part is *really* controversial:
	 * - HID aims at being generic so we should do our best to export
	 *   all incoming events
	 * - HID describes what events are, so there is no reason for ABS_X
	 *   to be mapped to ABS_Y
	 * - HID is using *_MISC+N as a default value, but nothing prevents
	 *   *_MISC+N to overwrite a legitimate even, which confuses userspace
	 *   (for instance ABS_MISC + 7 is ABS_MT_SLOT, which has a different
	 *   processing)
	 *
	 * If devices still want to use this (at their own risk), they will
	 * have to use the quirk HID_QUIRK_INCREMENT_USAGE_ON_DUPLICATE, but
	 * the default should be a reliable mapping.
	 */
	while (usage->code <= max && test_and_set_bit(usage->code, bit)) {
		if (device->quirks & HID_QUIRK_INCREMENT_USAGE_ON_DUPLICATE) {
			usage->code = find_next_zero_bit(bit,
							 max + 1,
							 usage->code);
		} else {
			device->status |= HID_STAT_DUP_DETECTED;
			goto ignore;
		}
	}

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

	return;

ignore:
	usage->type = 0;
	usage->code = 0;
}

static void hidinput_handle_scroll(struct hid_usage *usage,
				   struct input_dev *input,
				   __s32 value)
{
	int code;
	int hi_res, lo_res;

	if (value == 0)
		return;

	if (usage->code == REL_WHEEL_HI_RES)
		code = REL_WHEEL;
	else
		code = REL_HWHEEL;

	/*
	 * Windows reports one wheel click as value 120. Where a high-res
	 * scroll wheel is present, a fraction of 120 is reported instead.
	 * Our REL_WHEEL_HI_RES axis does the same because all HW must
	 * adhere to the 120 expectation.
	 */
	hi_res = value * 120/usage->resolution_multiplier;

	usage->wheel_accumulated += hi_res;
	lo_res = usage->wheel_accumulated/120;
	if (lo_res)
		usage->wheel_accumulated -= lo_res * 120;

	input_event(input, EV_REL, code, lo_res);
	input_event(input, EV_REL, usage->code, hi_res);
}

static void hid_report_release_tool(struct hid_report *report, struct input_dev *input,
				    unsigned int tool)
{
	/* if the given tool is not currently reported, ignore */
	if (!test_bit(tool, input->key))
		return;

	/*
	 * if the given tool was previously set, release it,
	 * release any TOUCH and send an EV_SYN
	 */
	input_event(input, EV_KEY, BTN_TOUCH, 0);
	input_event(input, EV_KEY, tool, 0);
	input_event(input, EV_SYN, SYN_REPORT, 0);

	report->tool = 0;
}

static void hid_report_set_tool(struct hid_report *report, struct input_dev *input,
				unsigned int new_tool)
{
	if (report->tool != new_tool)
		hid_report_release_tool(report, input, report->tool);

	input_event(input, EV_KEY, new_tool, 1);
	report->tool = new_tool;
}

void hidinput_hid_event(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct input_dev *input;
	struct hid_report *report = field->report;
	unsigned *quirks = &hid->quirks;

	if (!usage->type)
		return;

	if (usage->type == EV_PWR) {
		bool handled = hidinput_set_battery_charge_status(hid, usage->hid, value);

		if (!handled)
			hidinput_update_battery(hid, value);

		return;
	}

	if (!field->hidinput)
		return;

	input = field->hidinput->input;

	if (usage->hat_min < usage->hat_max || usage->hat_dir) {
		int hat_dir = usage->hat_dir;
		if (!hat_dir)
			hat_dir = (value - usage->hat_min) * 8 / (usage->hat_max - usage->hat_min + 1) + 1;
		if (hat_dir < 0 || hat_dir > 8) hat_dir = 0;
		input_event(input, usage->type, usage->code    , hid_hat_to_axis[hat_dir].x);
		input_event(input, usage->type, usage->code + 1, hid_hat_to_axis[hat_dir].y);
		return;
	}

	/*
	 * Ignore out-of-range values as per HID specification,
	 * section 5.10 and 6.2.25, when NULL state bit is present.
	 * When it's not, clamp the value to match Microsoft's input
	 * driver as mentioned in "Required HID usages for digitizers":
	 * https://msdn.microsoft.com/en-us/library/windows/hardware/dn672278(v=vs.85).asp
	 *
	 * The logical_minimum < logical_maximum check is done so that we
	 * don't unintentionally discard values sent by devices which
	 * don't specify logical min and max.
	 */
	if ((field->flags & HID_MAIN_ITEM_VARIABLE) &&
	    field->logical_minimum < field->logical_maximum) {
		if (field->flags & HID_MAIN_ITEM_NULL_STATE &&
		    (value < field->logical_minimum ||
		     value > field->logical_maximum)) {
			dbg_hid("Ignoring out-of-range value %x\n", value);
			return;
		}
		value = clamp(value,
			      field->logical_minimum,
			      field->logical_maximum);
	}

	switch (usage->hid) {
	case HID_DG_ERASER:
		report->tool_active |= !!value;

		/*
		 * if eraser is set, we must enforce BTN_TOOL_RUBBER
		 * to accommodate for devices not following the spec.
		 */
		if (value)
			hid_report_set_tool(report, input, BTN_TOOL_RUBBER);
		else if (report->tool != BTN_TOOL_RUBBER)
			/* value is off, tool is not rubber, ignore */
			return;
		else if (*quirks & HID_QUIRK_NOINVERT &&
			 !test_bit(BTN_TOUCH, input->key)) {
			/*
			 * There is no invert to release the tool, let hid_input
			 * send BTN_TOUCH with scancode and release the tool after.
			 */
			hid_report_release_tool(report, input, BTN_TOOL_RUBBER);
			return;
		}

		/* let hid-input set BTN_TOUCH */
		break;

	case HID_DG_INVERT:
		report->tool_active |= !!value;

		/*
		 * If invert is set, we store BTN_TOOL_RUBBER.
		 */
		if (value)
			hid_report_set_tool(report, input, BTN_TOOL_RUBBER);
		else if (!report->tool_active)
			/* tool_active not set means Invert and Eraser are not set */
			hid_report_release_tool(report, input, BTN_TOOL_RUBBER);

		/* no further processing */
		return;

	case HID_DG_INRANGE:
		report->tool_active |= !!value;

		if (report->tool_active) {
			/*
			 * if tool is not set but is marked as active,
			 * assume ours
			 */
			if (!report->tool)
				report->tool = usage->code;

			/* drivers may have changed the value behind our back, resend it */
			hid_report_set_tool(report, input, report->tool);
		} else {
			hid_report_release_tool(report, input, usage->code);
		}

		/* reset tool_active for the next event */
		report->tool_active = false;

		/* no further processing */
		return;

	case HID_DG_TIPSWITCH:
		report->tool_active |= !!value;

		/* if tool is set to RUBBER we should ignore the current value */
		if (report->tool == BTN_TOOL_RUBBER)
			return;

		break;

	case HID_DG_TIPPRESSURE:
		if (*quirks & HID_QUIRK_NOTOUCH) {
			int a = field->logical_minimum;
			int b = field->logical_maximum;

			if (value > a + ((b - a) >> 3)) {
				input_event(input, EV_KEY, BTN_TOUCH, 1);
				report->tool_active = true;
			}
		}
		break;

	case HID_UP_PID | 0x83UL: /* Simultaneous Effects Max */
		dbg_hid("Maximum Effects - %d\n",value);
		return;

	case HID_UP_PID | 0x7fUL:
		dbg_hid("PID Pool Report\n");
		return;
	}

	switch (usage->type) {
	case EV_KEY:
		if (usage->code == 0) /* Key 0 is "unassigned", not KEY_UNKNOWN */
			return;
		break;

	case EV_REL:
		if (usage->code == REL_WHEEL_HI_RES ||
		    usage->code == REL_HWHEEL_HI_RES) {
			hidinput_handle_scroll(usage, input, value);
			return;
		}
		break;

	case EV_ABS:
		if ((field->flags & HID_MAIN_ITEM_RELATIVE) &&
		    usage->code == ABS_VOLUME) {
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

		} else if (((*quirks & HID_QUIRK_X_INVERT) && usage->code == ABS_X) ||
			   ((*quirks & HID_QUIRK_Y_INVERT) && usage->code == ABS_Y))
			value = field->logical_maximum - value;
		break;
	}

	/*
	 * Ignore reports for absolute data if the data didn't change. This is
	 * not only an optimization but also fixes 'dead' key reports. Some
	 * RollOver implementations for localized keys (like BACKSLASH/PIPE; HID
	 * 0x31 and 0x32) report multiple keys, even though a localized keyboard
	 * can only have one of them physically available. The 'dead' keys
	 * report constant 0. As all map to the same keycode, they'd confuse
	 * the input layer. If we filter the 'dead' keys on the HID level, we
	 * skip the keycode translation and only forward real events.
	 */
	if (!(field->flags & (HID_MAIN_ITEM_RELATIVE |
	                      HID_MAIN_ITEM_BUFFERED_BYTE)) &&
			      (field->flags & HID_MAIN_ITEM_VARIABLE) &&
	    usage->usage_index < field->maxusage &&
	    value == field->value[usage->usage_index])
		return;

	/* report the usage code as scancode if the key status has changed */
	if (usage->type == EV_KEY &&
	    (!test_bit(usage->code, input->key)) == value)
		input_event(input, EV_MSC, MSC_SCAN, usage->hid);

	input_event(input, usage->type, usage->code, value);

	if ((field->flags & HID_MAIN_ITEM_RELATIVE) &&
	    usage->type == EV_KEY && value) {
		input_sync(input);
		input_event(input, usage->type, usage->code, 0);
	}
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

static int hidinput_find_field(struct hid_device *hid, unsigned int type,
			       unsigned int code, struct hid_field **field)
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

static void hidinput_led_worker(struct work_struct *work)
{
	struct hid_device *hid = container_of(work, struct hid_device,
					      led_work);
	struct hid_field *field;
	struct hid_report *report;
	int ret;
	u32 len;
	__u8 *buf;

	field = hidinput_get_led_field(hid);
	if (!field)
		return;

	/*
	 * field->report is accessed unlocked regarding HID core. So there might
	 * be another incoming SET-LED request from user-space, which changes
	 * the LED state while we assemble our outgoing buffer. However, this
	 * doesn't matter as hid_output_report() correctly converts it into a
	 * boolean value no matter what information is currently set on the LED
	 * field (even garbage). So the remote device will always get a valid
	 * request.
	 * And in case we send a wrong value, a next led worker is spawned
	 * for every SET-LED request so the following worker will send the
	 * correct value, guaranteed!
	 */

	report = field->report;

	/* use custom SET_REPORT request if possible (asynchronous) */
	if (hid->ll_driver->request)
		return hid->ll_driver->request(hid, report, HID_REQ_SET_REPORT);

	/* fall back to generic raw-output-report */
	len = hid_report_len(report);
	buf = hid_alloc_report_buf(report, GFP_KERNEL);
	if (!buf)
		return;

	hid_output_report(report, buf);
	/* synchronous output report */
	ret = hid_hw_output_report(hid, buf, len);
	if (ret == -ENOSYS)
		hid_hw_raw_request(hid, report->id, buf, len, HID_OUTPUT_REPORT,
				HID_REQ_SET_REPORT);
	kfree(buf);
}

static int hidinput_input_event(struct input_dev *dev, unsigned int type,
				unsigned int code, int value)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct hid_field *field;
	int offset;

	if (type == EV_FF)
		return input_ff_event(dev, type, code, value);

	if (type != EV_LED)
		return -1;

	if ((offset = hidinput_find_field(hid, type, code, &field)) == -1) {
		hid_warn(dev, "event field not found\n");
		return -1;
	}

	hid_set_field(field, offset, value);

	schedule_work(&hid->led_work);
	return 0;
}

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

static bool __hidinput_change_resolution_multipliers(struct hid_device *hid,
		struct hid_report *report, bool use_logical_max)
{
	struct hid_usage *usage;
	bool update_needed = false;
	bool get_report_completed = false;
	int i, j;

	if (report->maxfield == 0)
		return false;

	for (i = 0; i < report->maxfield; i++) {
		__s32 value = use_logical_max ?
			      report->field[i]->logical_maximum :
			      report->field[i]->logical_minimum;

		/* There is no good reason for a Resolution
		 * Multiplier to have a count other than 1.
		 * Ignore that case.
		 */
		if (report->field[i]->report_count != 1)
			continue;

		for (j = 0; j < report->field[i]->maxusage; j++) {
			usage = &report->field[i]->usage[j];

			if (usage->hid != HID_GD_RESOLUTION_MULTIPLIER)
				continue;

			/*
			 * If we have more than one feature within this
			 * report we need to fill in the bits from the
			 * others before we can overwrite the ones for the
			 * Resolution Multiplier.
			 *
			 * But if we're not allowed to read from the device,
			 * we just bail. Such a device should not exist
			 * anyway.
			 */
			if (!get_report_completed && report->maxfield > 1) {
				if (hid->quirks & HID_QUIRK_NO_INIT_REPORTS)
					return update_needed;

				hid_hw_request(hid, report, HID_REQ_GET_REPORT);
				hid_hw_wait(hid);
				get_report_completed = true;
			}

			report->field[i]->value[j] = value;
			update_needed = true;
		}
	}

	return update_needed;
}

static void hidinput_change_resolution_multipliers(struct hid_device *hid)
{
	struct hid_report_enum *rep_enum;
	struct hid_report *rep;
	int ret;

	rep_enum = &hid->report_enum[HID_FEATURE_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list) {
		bool update_needed = __hidinput_change_resolution_multipliers(hid,
								     rep, true);

		if (update_needed) {
			ret = __hid_request(hid, rep, HID_REQ_SET_REPORT);
			if (ret) {
				__hidinput_change_resolution_multipliers(hid,
								    rep, false);
				return;
			}
		}
	}

	/* refresh our structs */
	hid_setup_resolution_multiplier(hid);
}

static void report_features(struct hid_device *hid)
{
	struct hid_driver *drv = hid->driver;
	struct hid_report_enum *rep_enum;
	struct hid_report *rep;
	struct hid_usage *usage;
	int i, j;

	rep_enum = &hid->report_enum[HID_FEATURE_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list)
		for (i = 0; i < rep->maxfield; i++) {
			/* Ignore if report count is out of bounds. */
			if (rep->field[i]->report_count < 1)
				continue;

			for (j = 0; j < rep->field[i]->maxusage; j++) {
				usage = &rep->field[i]->usage[j];

				/* Verify if Battery Strength feature is available */
				if (usage->hid == HID_DC_BATTERYSTRENGTH)
					hidinput_setup_battery(hid, HID_FEATURE_REPORT,
							       rep->field[i], false);

				if (drv->feature_mapping)
					drv->feature_mapping(hid, rep->field[i], usage);
			}
		}
}

static struct hid_input *hidinput_allocate(struct hid_device *hid,
					   unsigned int application)
{
	struct hid_input *hidinput = kzalloc(sizeof(*hidinput), GFP_KERNEL);
	struct input_dev *input_dev = input_allocate_device();
	const char *suffix = NULL;
	size_t suffix_len, name_len;

	if (!hidinput || !input_dev)
		goto fail;

	if ((hid->quirks & HID_QUIRK_INPUT_PER_APP) &&
	    hid->maxapplication > 1) {
		switch (application) {
		case HID_GD_KEYBOARD:
			suffix = "Keyboard";
			break;
		case HID_GD_KEYPAD:
			suffix = "Keypad";
			break;
		case HID_GD_MOUSE:
			suffix = "Mouse";
			break;
		case HID_DG_PEN:
			/*
			 * yes, there is an issue here:
			 *  DG_PEN -> "Stylus"
			 *  DG_STYLUS -> "Pen"
			 * But changing this now means users with config snippets
			 * will have to change it and the test suite will not be happy.
			 */
			suffix = "Stylus";
			break;
		case HID_DG_STYLUS:
			suffix = "Pen";
			break;
		case HID_DG_TOUCHSCREEN:
			suffix = "Touchscreen";
			break;
		case HID_DG_TOUCHPAD:
			suffix = "Touchpad";
			break;
		case HID_GD_SYSTEM_CONTROL:
			suffix = "System Control";
			break;
		case HID_CP_CONSUMER_CONTROL:
			suffix = "Consumer Control";
			break;
		case HID_GD_WIRELESS_RADIO_CTLS:
			suffix = "Wireless Radio Control";
			break;
		case HID_GD_SYSTEM_MULTIAXIS:
			suffix = "System Multi Axis";
			break;
		default:
			break;
		}
	}

	if (suffix) {
		name_len = strlen(hid->name);
		suffix_len = strlen(suffix);
		if ((name_len < suffix_len) ||
		    strcmp(hid->name + name_len - suffix_len, suffix)) {
			hidinput->name = kasprintf(GFP_KERNEL, "%s %s",
						   hid->name, suffix);
			if (!hidinput->name)
				goto fail;
		}
	}

	input_set_drvdata(input_dev, hid);
	input_dev->event = hidinput_input_event;
	input_dev->open = hidinput_open;
	input_dev->close = hidinput_close;
	input_dev->setkeycode = hidinput_setkeycode;
	input_dev->getkeycode = hidinput_getkeycode;

	input_dev->name = hidinput->name ? hidinput->name : hid->name;
	input_dev->phys = hid->phys;
	input_dev->uniq = hid->uniq;
	input_dev->id.bustype = hid->bus;
	input_dev->id.vendor  = hid->vendor;
	input_dev->id.product = hid->product;
	input_dev->id.version = hid->version;
	input_dev->dev.parent = &hid->dev;

	hidinput->input = input_dev;
	hidinput->application = application;
	list_add_tail(&hidinput->list, &hid->inputs);

	INIT_LIST_HEAD(&hidinput->reports);

	return hidinput;

fail:
	kfree(hidinput);
	input_free_device(input_dev);
	hid_err(hid, "Out of memory during hid input probe\n");
	return NULL;
}

static bool hidinput_has_been_populated(struct hid_input *hidinput)
{
	int i;
	unsigned long r = 0;

	for (i = 0; i < BITS_TO_LONGS(EV_CNT); i++)
		r |= hidinput->input->evbit[i];

	for (i = 0; i < BITS_TO_LONGS(KEY_CNT); i++)
		r |= hidinput->input->keybit[i];

	for (i = 0; i < BITS_TO_LONGS(REL_CNT); i++)
		r |= hidinput->input->relbit[i];

	for (i = 0; i < BITS_TO_LONGS(ABS_CNT); i++)
		r |= hidinput->input->absbit[i];

	for (i = 0; i < BITS_TO_LONGS(MSC_CNT); i++)
		r |= hidinput->input->mscbit[i];

	for (i = 0; i < BITS_TO_LONGS(LED_CNT); i++)
		r |= hidinput->input->ledbit[i];

	for (i = 0; i < BITS_TO_LONGS(SND_CNT); i++)
		r |= hidinput->input->sndbit[i];

	for (i = 0; i < BITS_TO_LONGS(FF_CNT); i++)
		r |= hidinput->input->ffbit[i];

	for (i = 0; i < BITS_TO_LONGS(SW_CNT); i++)
		r |= hidinput->input->swbit[i];

	return !!r;
}

static void hidinput_cleanup_hidinput(struct hid_device *hid,
		struct hid_input *hidinput)
{
	struct hid_report *report;
	int i, k;

	list_del(&hidinput->list);
	input_free_device(hidinput->input);
	kfree(hidinput->name);

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++) {
		if (k == HID_OUTPUT_REPORT &&
			hid->quirks & HID_QUIRK_SKIP_OUTPUT_REPORTS)
			continue;

		list_for_each_entry(report, &hid->report_enum[k].report_list,
				    list) {

			for (i = 0; i < report->maxfield; i++)
				if (report->field[i]->hidinput == hidinput)
					report->field[i]->hidinput = NULL;
		}
	}

	kfree(hidinput);
}

static struct hid_input *hidinput_match(struct hid_report *report)
{
	struct hid_device *hid = report->device;
	struct hid_input *hidinput;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		if (hidinput->report &&
		    hidinput->report->id == report->id)
			return hidinput;
	}

	return NULL;
}

static struct hid_input *hidinput_match_application(struct hid_report *report)
{
	struct hid_device *hid = report->device;
	struct hid_input *hidinput;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		if (hidinput->application == report->application)
			return hidinput;

		/*
		 * Keep SystemControl and ConsumerControl applications together
		 * with the main keyboard, if present.
		 */
		if ((report->application == HID_GD_SYSTEM_CONTROL ||
		     report->application == HID_CP_CONSUMER_CONTROL) &&
		    hidinput->application == HID_GD_KEYBOARD) {
			return hidinput;
		}
	}

	return NULL;
}

static inline void hidinput_configure_usages(struct hid_input *hidinput,
					     struct hid_report *report)
{
	int i, j, k;
	int first_field_index = 0;
	int slot_collection_index = -1;
	int prev_collection_index = -1;
	unsigned int slot_idx = 0;
	struct hid_field *field;

	/*
	 * First tag all the fields that are part of a slot,
	 * a slot needs to have one Contact ID in the collection
	 */
	for (i = 0; i < report->maxfield; i++) {
		field = report->field[i];

		/* ignore fields without usage */
		if (field->maxusage < 1)
			continue;

		/*
		 * janitoring when collection_index changes
		 */
		if (prev_collection_index != field->usage->collection_index) {
			prev_collection_index = field->usage->collection_index;
			first_field_index = i;
		}

		/*
		 * if we already found a Contact ID in the collection,
		 * tag and continue to the next.
		 */
		if (slot_collection_index == field->usage->collection_index) {
			field->slot_idx = slot_idx;
			continue;
		}

		/* check if the current field has Contact ID */
		for (j = 0; j < field->maxusage; j++) {
			if (field->usage[j].hid == HID_DG_CONTACTID) {
				slot_collection_index = field->usage->collection_index;
				slot_idx++;

				/*
				 * mark all previous fields and this one in the
				 * current collection to be slotted.
				 */
				for (k = first_field_index; k <= i; k++)
					report->field[k]->slot_idx = slot_idx;
				break;
			}
		}
	}

	for (i = 0; i < report->maxfield; i++)
		for (j = 0; j < report->field[i]->maxusage; j++)
			hidinput_configure_usage(hidinput, report->field[i],
						 report->field[i]->usage + j,
						 j);
}

/*
 * Register the input device; print a message.
 * Configure the input layer interface
 * Read all reports and initialize the absolute field values.
 */

int hidinput_connect(struct hid_device *hid, unsigned int force)
{
	struct hid_driver *drv = hid->driver;
	struct hid_report *report;
	struct hid_input *next, *hidinput = NULL;
	unsigned int application;
	int i, k;

	INIT_LIST_HEAD(&hid->inputs);
	INIT_WORK(&hid->led_work, hidinput_led_worker);

	hid->status &= ~HID_STAT_DUP_DETECTED;

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

			application = report->application;

			/*
			 * Find the previous hidinput report attached
			 * to this report id.
			 */
			if (hid->quirks & HID_QUIRK_MULTI_INPUT)
				hidinput = hidinput_match(report);
			else if (hid->maxapplication > 1 &&
				 (hid->quirks & HID_QUIRK_INPUT_PER_APP))
				hidinput = hidinput_match_application(report);

			if (!hidinput) {
				hidinput = hidinput_allocate(hid, application);
				if (!hidinput)
					goto out_unwind;
			}

			hidinput_configure_usages(hidinput, report);

			if (hid->quirks & HID_QUIRK_MULTI_INPUT)
				hidinput->report = report;

			list_add_tail(&report->hidinput_list,
				      &hidinput->reports);
		}
	}

	hidinput_change_resolution_multipliers(hid);

	list_for_each_entry_safe(hidinput, next, &hid->inputs, list) {
		if (drv->input_configured &&
		    drv->input_configured(hid, hidinput))
			goto out_unwind;

		if (!hidinput_has_been_populated(hidinput)) {
			/* no need to register an input device not populated */
			hidinput_cleanup_hidinput(hid, hidinput);
			continue;
		}

		if (input_register_device(hidinput->input))
			goto out_unwind;
		hidinput->registered = true;
	}

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "No inputs registered, leaving\n");
		goto out_unwind;
	}

	if (hid->status & HID_STAT_DUP_DETECTED)
		hid_dbg(hid,
			"Some usages could not be mapped, please use HID_QUIRK_INCREMENT_USAGE_ON_DUPLICATE if this is legitimate.\n");

	return 0;

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
		if (hidinput->registered)
			input_unregister_device(hidinput->input);
		else
			input_free_device(hidinput->input);
		kfree(hidinput->name);
		kfree(hidinput);
	}

	/* led_work is spawned by input_dev callbacks, but doesn't access the
	 * parent input_dev at all. Once all input devices are removed, we
	 * know that led_work will never get restarted, so we can cancel it
	 * synchronously and are safe. */
	cancel_work_sync(&hid->led_work);
}
EXPORT_SYMBOL_GPL(hidinput_disconnect);

#ifdef CONFIG_HID_KUNIT_TEST
#include "hid-input-test.c"
#endif
