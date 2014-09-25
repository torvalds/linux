/*
 *  HID driver for multitouch panels
 *
 *  Copyright (c) 2010-2012 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010-2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2010-2012 Ecole Nationale de l'Aviation Civile, France
 *  Copyright (c) 2012-2013 Red Hat, Inc
 *
 *  This code is partly based on hid-egalax.c:
 *
 *  Copyright (c) 2010 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010 Henrik Rydberg <rydberg@euromail.se>
 *  Copyright (c) 2010 Canonical, Ltd.
 *
 *  This code is partly based on hid-3m-pct.c:
 *
 *  Copyright (c) 2009-2010 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010      Henrik Rydberg <rydberg@euromail.se>
 *  Copyright (c) 2010      Canonical, Ltd.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * This driver is regularly tested thanks to the tool hid-test[1].
 * This tool relies on hid-replay[2] and a database of hid devices[3].
 * Please run these regression tests before patching this module so that
 * your patch won't break existing known devices.
 *
 * [1] https://github.com/bentiss/hid-test
 * [2] https://github.com/bentiss/hid-replay
 * [3] https://github.com/bentiss/hid-devices
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/input/mt.h>
#include <linux/string.h>


MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_DESCRIPTION("HID multitouch panels");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

/* quirks to control the device */
#define MT_QUIRK_NOT_SEEN_MEANS_UP	(1 << 0)
#define MT_QUIRK_SLOT_IS_CONTACTID	(1 << 1)
#define MT_QUIRK_CYPRESS		(1 << 2)
#define MT_QUIRK_SLOT_IS_CONTACTNUMBER	(1 << 3)
#define MT_QUIRK_ALWAYS_VALID		(1 << 4)
#define MT_QUIRK_VALID_IS_INRANGE	(1 << 5)
#define MT_QUIRK_VALID_IS_CONFIDENCE	(1 << 6)
#define MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE	(1 << 8)
#define MT_QUIRK_NO_AREA		(1 << 9)
#define MT_QUIRK_IGNORE_DUPLICATES	(1 << 10)
#define MT_QUIRK_HOVERING		(1 << 11)
#define MT_QUIRK_CONTACT_CNT_ACCURATE	(1 << 12)

#define MT_INPUTMODE_TOUCHSCREEN	0x02
#define MT_INPUTMODE_TOUCHPAD		0x03

struct mt_slot {
	__s32 x, y, cx, cy, p, w, h;
	__s32 contactid;	/* the device ContactID assigned to this slot */
	bool touch_state;	/* is the touch valid? */
	bool inrange_state;	/* is the finger in proximity of the sensor? */
};

struct mt_class {
	__s32 name;	/* MT_CLS */
	__s32 quirks;
	__s32 sn_move;	/* Signal/noise ratio for move events */
	__s32 sn_width;	/* Signal/noise ratio for width events */
	__s32 sn_height;	/* Signal/noise ratio for height events */
	__s32 sn_pressure;	/* Signal/noise ratio for pressure events */
	__u8 maxcontacts;
	bool is_indirect;	/* true for touchpads */
	bool export_all_inputs;	/* do not ignore mouse, keyboards, etc... */
};

struct mt_fields {
	unsigned usages[HID_MAX_FIELDS];
	unsigned int length;
};

struct mt_device {
	struct mt_slot curdata;	/* placeholder of incoming data */
	struct mt_class mtclass;	/* our mt device class */
	struct mt_fields *fields;	/* temporary placeholder for storing the
					   multitouch fields */
	int cc_index;	/* contact count field index in the report */
	int cc_value_index;	/* contact count value index in the field */
	unsigned last_slot_field;	/* the last field of a slot */
	unsigned mt_report_id;	/* the report ID of the multitouch device */
	__s16 inputmode;	/* InputMode HID feature, -1 if non-existent */
	__s16 inputmode_index;	/* InputMode HID feature index in the report */
	__s16 maxcontact_report_id;	/* Maximum Contact Number HID feature,
				   -1 if non-existent */
	__u8 inputmode_value;  /* InputMode HID feature value */
	__u8 num_received;	/* how many contacts we received */
	__u8 num_expected;	/* expected last contact index */
	__u8 maxcontacts;
	__u8 touches_by_report;	/* how many touches are present in one report:
				* 1 means we should use a serial protocol
				* > 1 means hybrid (multitouch) protocol */
	bool serial_maybe;	/* need to check for serial protocol */
	bool curvalid;		/* is the current contact valid? */
	unsigned mt_flags;	/* flags to pass to input-mt */
};

static void mt_post_parse_default_settings(struct mt_device *td);
static void mt_post_parse(struct mt_device *td);

/* classes of device behavior */
#define MT_CLS_DEFAULT				0x0001

#define MT_CLS_SERIAL				0x0002
#define MT_CLS_CONFIDENCE			0x0003
#define MT_CLS_CONFIDENCE_CONTACT_ID		0x0004
#define MT_CLS_CONFIDENCE_MINUS_ONE		0x0005
#define MT_CLS_DUAL_INRANGE_CONTACTID		0x0006
#define MT_CLS_DUAL_INRANGE_CONTACTNUMBER	0x0007
/* reserved					0x0008 */
#define MT_CLS_INRANGE_CONTACTNUMBER		0x0009
#define MT_CLS_NSMU				0x000a
/* reserved					0x0010 */
/* reserved					0x0011 */
#define MT_CLS_WIN_8				0x0012
#define MT_CLS_EXPORT_ALL_INPUTS		0x0013

/* vendor specific classes */
#define MT_CLS_3M				0x0101
/* reserved					0x0102 */
#define MT_CLS_EGALAX				0x0103
#define MT_CLS_EGALAX_SERIAL			0x0104
#define MT_CLS_TOPSEED				0x0105
#define MT_CLS_PANASONIC			0x0106
#define MT_CLS_FLATFROG				0x0107
#define MT_CLS_GENERALTOUCH_TWOFINGERS		0x0108
#define MT_CLS_GENERALTOUCH_PWT_TENFINGERS	0x0109

#define MT_DEFAULT_MAXCONTACT	10
#define MT_MAX_MAXCONTACT	250

#define MT_USB_DEVICE(v, p)	HID_DEVICE(BUS_USB, HID_GROUP_MULTITOUCH, v, p)
#define MT_BT_DEVICE(v, p)	HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_MULTITOUCH, v, p)

/*
 * these device-dependent functions determine what slot corresponds
 * to a valid contact that was just read.
 */

static int cypress_compute_slot(struct mt_device *td)
{
	if (td->curdata.contactid != 0 || td->num_received == 0)
		return td->curdata.contactid;
	else
		return -1;
}

static struct mt_class mt_classes[] = {
	{ .name = MT_CLS_DEFAULT,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE },
	{ .name = MT_CLS_NSMU,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP },
	{ .name = MT_CLS_SERIAL,
		.quirks = MT_QUIRK_ALWAYS_VALID},
	{ .name = MT_CLS_CONFIDENCE,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE },
	{ .name = MT_CLS_CONFIDENCE_CONTACT_ID,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID },
	{ .name = MT_CLS_CONFIDENCE_MINUS_ONE,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE },
	{ .name = MT_CLS_DUAL_INRANGE_CONTACTID,
		.quirks = MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.maxcontacts = 2 },
	{ .name = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		.quirks = MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTNUMBER,
		.maxcontacts = 2 },
	{ .name = MT_CLS_INRANGE_CONTACTNUMBER,
		.quirks = MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTNUMBER },
	{ .name = MT_CLS_WIN_8,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_IGNORE_DUPLICATES |
			MT_QUIRK_HOVERING |
			MT_QUIRK_CONTACT_CNT_ACCURATE },
	{ .name = MT_CLS_EXPORT_ALL_INPUTS,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE,
		.export_all_inputs = true },

	/*
	 * vendor specific classes
	 */
	{ .name = MT_CLS_3M,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.sn_move = 2048,
		.sn_width = 128,
		.sn_height = 128,
		.maxcontacts = 60,
	},
	{ .name = MT_CLS_EGALAX,
		.quirks =  MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_VALID_IS_INRANGE,
		.sn_move = 4096,
		.sn_pressure = 32,
	},
	{ .name = MT_CLS_EGALAX_SERIAL,
		.quirks =  MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_ALWAYS_VALID,
		.sn_move = 4096,
		.sn_pressure = 32,
	},
	{ .name = MT_CLS_TOPSEED,
		.quirks = MT_QUIRK_ALWAYS_VALID,
		.is_indirect = true,
		.maxcontacts = 2,
	},
	{ .name = MT_CLS_PANASONIC,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP,
		.maxcontacts = 4 },
	{ .name	= MT_CLS_GENERALTOUCH_TWOFINGERS,
		.quirks	= MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.maxcontacts = 2
	},
	{ .name	= MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		.quirks	= MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_SLOT_IS_CONTACTID
	},

	{ .name = MT_CLS_FLATFROG,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_NO_AREA,
		.sn_move = 2048,
		.maxcontacts = 40,
	},
	{ }
};

static ssize_t mt_show_quirks(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct mt_device *td = hid_get_drvdata(hdev);

	return sprintf(buf, "%u\n", td->mtclass.quirks);
}

static ssize_t mt_set_quirks(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct mt_device *td = hid_get_drvdata(hdev);

	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	td->mtclass.quirks = val;

	if (td->cc_index < 0)
		td->mtclass.quirks &= ~MT_QUIRK_CONTACT_CNT_ACCURATE;

	return count;
}

static DEVICE_ATTR(quirks, S_IWUSR | S_IRUGO, mt_show_quirks, mt_set_quirks);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_quirks.attr,
	NULL
};

static struct attribute_group mt_attribute_group = {
	.attrs = sysfs_attrs
};

static void mt_feature_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct mt_device *td = hid_get_drvdata(hdev);

	switch (usage->hid) {
	case HID_DG_INPUTMODE:
		/* Ignore if value index is out of bounds. */
		if (usage->usage_index >= field->report_count) {
			dev_err(&hdev->dev, "HID_DG_INPUTMODE out of range\n");
			break;
		}

		td->inputmode = field->report->id;
		td->inputmode_index = usage->usage_index;

		break;
	case HID_DG_CONTACTMAX:
		td->maxcontact_report_id = field->report->id;
		td->maxcontacts = field->value[0];
		if (!td->maxcontacts &&
		    field->logical_maximum <= MT_MAX_MAXCONTACT)
			td->maxcontacts = field->logical_maximum;
		if (td->mtclass.maxcontacts)
			/* check if the maxcontacts is given by the class */
			td->maxcontacts = td->mtclass.maxcontacts;

		break;
	}
}

static void set_abs(struct input_dev *input, unsigned int code,
		struct hid_field *field, int snratio)
{
	int fmin = field->logical_minimum;
	int fmax = field->logical_maximum;
	int fuzz = snratio ? (fmax - fmin) / snratio : 0;
	input_set_abs_params(input, code, fmin, fmax, fuzz, 0);
	input_abs_set_res(input, code, hidinput_calc_abs_res(field, code));
}

static void mt_store_field(struct hid_usage *usage, struct mt_device *td,
		struct hid_input *hi)
{
	struct mt_fields *f = td->fields;

	if (f->length >= HID_MAX_FIELDS)
		return;

	f->usages[f->length++] = usage->hid;
}

static int mt_touch_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_class *cls = &td->mtclass;
	int code;
	struct hid_usage *prev_usage = NULL;

	if (field->application == HID_DG_TOUCHSCREEN)
		td->mt_flags |= INPUT_MT_DIRECT;

	/*
	 * Model touchscreens providing buttons as touchpads.
	 */
	if (field->application == HID_DG_TOUCHPAD ||
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON) {
		td->mt_flags |= INPUT_MT_POINTER;
		td->inputmode_value = MT_INPUTMODE_TOUCHPAD;
	}

	if (usage->usage_index)
		prev_usage = &field->usage[usage->usage_index - 1];

	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			if (prev_usage && (prev_usage->hid == usage->hid)) {
				hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOOL_X);
				set_abs(hi->input, ABS_MT_TOOL_X, field,
					cls->sn_move);
			} else {
				hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
				set_abs(hi->input, ABS_MT_POSITION_X, field,
					cls->sn_move);
			}

			mt_store_field(usage, td, hi);
			return 1;
		case HID_GD_Y:
			if (prev_usage && (prev_usage->hid == usage->hid)) {
				hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOOL_Y);
				set_abs(hi->input, ABS_MT_TOOL_Y, field,
					cls->sn_move);
			} else {
				hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
				set_abs(hi->input, ABS_MT_POSITION_Y, field,
					cls->sn_move);
			}

			mt_store_field(usage, td, hi);
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		case HID_DG_INRANGE:
			if (cls->quirks & MT_QUIRK_HOVERING) {
				hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_DISTANCE);
				input_set_abs_params(hi->input,
					ABS_MT_DISTANCE, 0, 1, 0, 0);
			}
			mt_store_field(usage, td, hi);
			return 1;
		case HID_DG_CONFIDENCE:
			mt_store_field(usage, td, hi);
			return 1;
		case HID_DG_TIPSWITCH:
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			input_set_capability(hi->input, EV_KEY, BTN_TOUCH);
			mt_store_field(usage, td, hi);
			return 1;
		case HID_DG_CONTACTID:
			mt_store_field(usage, td, hi);
			td->touches_by_report++;
			td->mt_report_id = field->report->id;
			return 1;
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			if (!(cls->quirks & MT_QUIRK_NO_AREA))
				set_abs(hi->input, ABS_MT_TOUCH_MAJOR, field,
					cls->sn_width);
			mt_store_field(usage, td, hi);
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			if (!(cls->quirks & MT_QUIRK_NO_AREA)) {
				set_abs(hi->input, ABS_MT_TOUCH_MINOR, field,
					cls->sn_height);
				input_set_abs_params(hi->input,
					ABS_MT_ORIENTATION, 0, 1, 0, 0);
			}
			mt_store_field(usage, td, hi);
			return 1;
		case HID_DG_TIPPRESSURE:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_PRESSURE);
			set_abs(hi->input, ABS_MT_PRESSURE, field,
				cls->sn_pressure);
			mt_store_field(usage, td, hi);
			return 1;
		case HID_DG_CONTACTCOUNT:
			/* Ignore if indexes are out of bounds. */
			if (field->index >= field->report->maxfield ||
			    usage->usage_index >= field->report_count)
				return 1;
			td->cc_index = field->index;
			td->cc_value_index = usage->usage_index;
			return 1;
		case HID_DG_CONTACTMAX:
			/* we don't set td->last_slot_field as contactcount and
			 * contact max are global to the report */
			return -1;
		case HID_DG_TOUCH:
			/* Legacy devices use TIPSWITCH and not TOUCH.
			 * Let's just ignore this field. */
			return -1;
		}
		/* let hid-input decide for the others */
		return 0;

	case HID_UP_BUTTON:
		code = BTN_MOUSE + ((usage->hid - 1) & HID_USAGE);
		hid_map_usage(hi, usage, bit, max, EV_KEY, code);
		input_set_capability(hi->input, EV_KEY, code);
		return 1;

	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int mt_touch_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if (usage->type == EV_KEY || usage->type == EV_ABS)
		set_bit(usage->type, hi->input->evbit);

	return -1;
}

static int mt_compute_slot(struct mt_device *td, struct input_dev *input)
{
	__s32 quirks = td->mtclass.quirks;

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTID)
		return td->curdata.contactid;

	if (quirks & MT_QUIRK_CYPRESS)
		return cypress_compute_slot(td);

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTNUMBER)
		return td->num_received;

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE)
		return td->curdata.contactid - 1;

	return input_mt_get_slot_by_key(input, td->curdata.contactid);
}

/*
 * this function is called when a whole contact has been processed,
 * so that it can assign it to a slot and store the data there
 */
static void mt_complete_slot(struct mt_device *td, struct input_dev *input)
{
	if ((td->mtclass.quirks & MT_QUIRK_CONTACT_CNT_ACCURATE) &&
	    td->num_received >= td->num_expected)
		return;

	if (td->curvalid || (td->mtclass.quirks & MT_QUIRK_ALWAYS_VALID)) {
		int slotnum = mt_compute_slot(td, input);
		struct mt_slot *s = &td->curdata;
		struct input_mt *mt = input->mt;

		if (slotnum < 0 || slotnum >= td->maxcontacts)
			return;

		if ((td->mtclass.quirks & MT_QUIRK_IGNORE_DUPLICATES) && mt) {
			struct input_mt_slot *slot = &mt->slots[slotnum];
			if (input_mt_is_active(slot) &&
			    input_mt_is_used(mt, slot))
				return;
		}

		input_mt_slot(input, slotnum);
		input_mt_report_slot_state(input, MT_TOOL_FINGER,
			s->touch_state || s->inrange_state);
		if (s->touch_state || s->inrange_state) {
			/* this finger is in proximity of the sensor */
			int wide = (s->w > s->h);
			/* divided by two to match visual scale of touch */
			int major = max(s->w, s->h) >> 1;
			int minor = min(s->w, s->h) >> 1;

			input_event(input, EV_ABS, ABS_MT_POSITION_X, s->x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, s->y);
			input_event(input, EV_ABS, ABS_MT_TOOL_X, s->cx);
			input_event(input, EV_ABS, ABS_MT_TOOL_Y, s->cy);
			input_event(input, EV_ABS, ABS_MT_DISTANCE,
				!s->touch_state);
			input_event(input, EV_ABS, ABS_MT_ORIENTATION, wide);
			input_event(input, EV_ABS, ABS_MT_PRESSURE, s->p);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, minor);
		}
	}

	td->num_received++;
}

/*
 * this function is called when a whole packet has been received and processed,
 * so that it can decide what to send to the input layer.
 */
static void mt_sync_frame(struct mt_device *td, struct input_dev *input)
{
	input_mt_sync_frame(input);
	input_sync(input);
	td->num_received = 0;
}

static int mt_touch_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	/* we will handle the hidinput part later, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static void mt_process_mt_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct mt_device *td = hid_get_drvdata(hid);
	__s32 quirks = td->mtclass.quirks;
	struct input_dev *input = field->hidinput->input;

	if (hid->claimed & HID_CLAIMED_INPUT) {
		switch (usage->hid) {
		case HID_DG_INRANGE:
			if (quirks & MT_QUIRK_VALID_IS_INRANGE)
				td->curvalid = value;
			if (quirks & MT_QUIRK_HOVERING)
				td->curdata.inrange_state = value;
			break;
		case HID_DG_TIPSWITCH:
			if (quirks & MT_QUIRK_NOT_SEEN_MEANS_UP)
				td->curvalid = value;
			td->curdata.touch_state = value;
			break;
		case HID_DG_CONFIDENCE:
			if (quirks & MT_QUIRK_VALID_IS_CONFIDENCE)
				td->curvalid = value;
			break;
		case HID_DG_CONTACTID:
			td->curdata.contactid = value;
			break;
		case HID_DG_TIPPRESSURE:
			td->curdata.p = value;
			break;
		case HID_GD_X:
			if (usage->code == ABS_MT_TOOL_X)
				td->curdata.cx = value;
			else
				td->curdata.x = value;
			break;
		case HID_GD_Y:
			if (usage->code == ABS_MT_TOOL_Y)
				td->curdata.cy = value;
			else
				td->curdata.y = value;
			break;
		case HID_DG_WIDTH:
			td->curdata.w = value;
			break;
		case HID_DG_HEIGHT:
			td->curdata.h = value;
			break;
		case HID_DG_CONTACTCOUNT:
			break;
		case HID_DG_TOUCH:
			/* do nothing */
			break;

		default:
			if (usage->type)
				input_event(input, usage->type, usage->code,
						value);
			return;
		}

		if (usage->usage_index + 1 == field->report_count) {
			/* we only take into account the last report. */
			if (usage->hid == td->last_slot_field)
				mt_complete_slot(td, field->hidinput->input);
		}

	}
}

static void mt_touch_report(struct hid_device *hid, struct hid_report *report)
{
	struct mt_device *td = hid_get_drvdata(hid);
	struct hid_field *field;
	unsigned count;
	int r, n;

	/*
	 * Includes multi-packet support where subsequent
	 * packets are sent with zero contactcount.
	 */
	if (td->cc_index >= 0) {
		struct hid_field *field = report->field[td->cc_index];
		int value = field->value[td->cc_value_index];
		if (value)
			td->num_expected = value;
	}

	for (r = 0; r < report->maxfield; r++) {
		field = report->field[r];
		count = field->report_count;

		if (!(HID_MAIN_ITEM_VARIABLE & field->flags))
			continue;

		for (n = 0; n < count; n++)
			mt_process_mt_event(hid, field, &field->usage[n],
					field->value[n]);
	}

	if (td->num_received >= td->num_expected)
		mt_sync_frame(td, report->field[0]->hidinput->input);
}

static void mt_touch_input_configured(struct hid_device *hdev,
					struct hid_input *hi)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_class *cls = &td->mtclass;
	struct input_dev *input = hi->input;

	if (!td->maxcontacts)
		td->maxcontacts = MT_DEFAULT_MAXCONTACT;

	mt_post_parse(td);
	if (td->serial_maybe)
		mt_post_parse_default_settings(td);

	if (cls->is_indirect)
		td->mt_flags |= INPUT_MT_POINTER;

	if (cls->quirks & MT_QUIRK_NOT_SEEN_MEANS_UP)
		td->mt_flags |= INPUT_MT_DROP_UNUSED;

	input_mt_init_slots(input, td->maxcontacts, td->mt_flags);

	td->mt_flags = 0;
}

static int mt_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct mt_device *td = hid_get_drvdata(hdev);

	/*
	 * If mtclass.export_all_inputs is not set, only map fields from
	 * TouchScreen or TouchPad collections. We need to ignore fields
	 * that belong to other collections such as Mouse that might have
	 * the same GenericDesktop usages.
	 */
	if (!td->mtclass.export_all_inputs &&
	    field->application != HID_DG_TOUCHSCREEN &&
	    field->application != HID_DG_PEN &&
	    field->application != HID_DG_TOUCHPAD)
		return -1;

	/*
	 * some egalax touchscreens have "application == HID_DG_TOUCHSCREEN"
	 * for the stylus.
	 */
	if (field->physical == HID_DG_STYLUS)
		return 0;

	if (field->application == HID_DG_TOUCHSCREEN ||
	    field->application == HID_DG_TOUCHPAD)
		return mt_touch_input_mapping(hdev, hi, field, usage, bit, max);

	/* let hid-core decide for the others */
	return 0;
}

static int mt_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	/*
	 * some egalax touchscreens have "application == HID_DG_TOUCHSCREEN"
	 * for the stylus.
	 */
	if (field->physical == HID_DG_STYLUS)
		return 0;

	if (field->application == HID_DG_TOUCHSCREEN ||
	    field->application == HID_DG_TOUCHPAD)
		return mt_touch_input_mapped(hdev, hi, field, usage, bit, max);

	/* let hid-core decide for the others */
	return 0;
}

static int mt_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct mt_device *td = hid_get_drvdata(hid);

	if (field->report->id == td->mt_report_id)
		return mt_touch_event(hid, field, usage, value);

	return 0;
}

static void mt_report(struct hid_device *hid, struct hid_report *report)
{
	struct mt_device *td = hid_get_drvdata(hid);
	struct hid_field *field = report->field[0];

	if (!(hid->claimed & HID_CLAIMED_INPUT))
		return;

	if (report->id == td->mt_report_id)
		return mt_touch_report(hid, report);

	if (field && field->hidinput && field->hidinput->input)
		input_sync(field->hidinput->input);
}

static void mt_set_input_mode(struct hid_device *hdev)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct hid_report *r;
	struct hid_report_enum *re;

	if (td->inputmode < 0)
		return;

	re = &(hdev->report_enum[HID_FEATURE_REPORT]);
	r = re->report_id_hash[td->inputmode];
	if (r) {
		r->field[0]->value[td->inputmode_index] = td->inputmode_value;
		hid_hw_request(hdev, r, HID_REQ_SET_REPORT);
	}
}

static void mt_set_maxcontacts(struct hid_device *hdev)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct hid_report *r;
	struct hid_report_enum *re;
	int fieldmax, max;

	if (td->maxcontact_report_id < 0)
		return;

	if (!td->mtclass.maxcontacts)
		return;

	re = &hdev->report_enum[HID_FEATURE_REPORT];
	r = re->report_id_hash[td->maxcontact_report_id];
	if (r) {
		max = td->mtclass.maxcontacts;
		fieldmax = r->field[0]->logical_maximum;
		max = min(fieldmax, max);
		if (r->field[0]->value[0] != max) {
			r->field[0]->value[0] = max;
			hid_hw_request(hdev, r, HID_REQ_SET_REPORT);
		}
	}
}

static void mt_post_parse_default_settings(struct mt_device *td)
{
	__s32 quirks = td->mtclass.quirks;

	/* unknown serial device needs special quirks */
	if (td->touches_by_report == 1) {
		quirks |= MT_QUIRK_ALWAYS_VALID;
		quirks &= ~MT_QUIRK_NOT_SEEN_MEANS_UP;
		quirks &= ~MT_QUIRK_VALID_IS_INRANGE;
		quirks &= ~MT_QUIRK_VALID_IS_CONFIDENCE;
		quirks &= ~MT_QUIRK_CONTACT_CNT_ACCURATE;
	}

	td->mtclass.quirks = quirks;
}

static void mt_post_parse(struct mt_device *td)
{
	struct mt_fields *f = td->fields;
	struct mt_class *cls = &td->mtclass;

	if (td->touches_by_report > 0) {
		int field_count_per_touch = f->length / td->touches_by_report;
		td->last_slot_field = f->usages[field_count_per_touch - 1];
	}

	if (td->cc_index < 0)
		cls->quirks &= ~MT_QUIRK_CONTACT_CNT_ACCURATE;
}

static void mt_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	char *name;
	const char *suffix = NULL;
	struct hid_field *field = hi->report->field[0];

	if (hi->report->id == td->mt_report_id)
		mt_touch_input_configured(hdev, hi);

	/*
	 * some egalax touchscreens have "application == HID_DG_TOUCHSCREEN"
	 * for the stylus. Check this first, and then rely on the application
	 * field.
	 */
	if (hi->report->field[0]->physical == HID_DG_STYLUS) {
		suffix = "Pen";
		/* force BTN_STYLUS to allow tablet matching in udev */
		__set_bit(BTN_STYLUS, hi->input->keybit);
	} else {
		switch (field->application) {
		case HID_GD_KEYBOARD:
			suffix = "Keyboard";
			break;
		case HID_GD_KEYPAD:
			suffix = "Keypad";
			break;
		case HID_GD_MOUSE:
			suffix = "Mouse";
			break;
		case HID_DG_STYLUS:
			suffix = "Pen";
			/* force BTN_STYLUS to allow tablet matching in udev */
			__set_bit(BTN_STYLUS, hi->input->keybit);
			break;
		case HID_DG_TOUCHSCREEN:
			/* we do not set suffix = "Touchscreen" */
			break;
		case HID_GD_SYSTEM_CONTROL:
			suffix = "System Control";
			break;
		case HID_CP_CONSUMER_CONTROL:
			suffix = "Consumer Control";
			break;
		default:
			suffix = "UNKNOWN";
			break;
		}
	}

	if (suffix) {
		name = devm_kzalloc(&hi->input->dev,
				    strlen(hdev->name) + strlen(suffix) + 2,
				    GFP_KERNEL);
		if (name) {
			sprintf(name, "%s %s", hdev->name, suffix);
			hi->input->name = name;
		}
	}
}

static int mt_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret, i;
	struct mt_device *td;
	struct mt_class *mtclass = mt_classes; /* MT_CLS_DEFAULT */

	for (i = 0; mt_classes[i].name ; i++) {
		if (id->driver_data == mt_classes[i].name) {
			mtclass = &(mt_classes[i]);
			break;
		}
	}

	/* This allows the driver to correctly support devices
	 * that emit events over several HID messages.
	 */
	hdev->quirks |= HID_QUIRK_NO_INPUT_SYNC;

	/*
	 * This allows the driver to handle different input sensors
	 * that emits events through different reports on the same HID
	 * device.
	 */
	hdev->quirks |= HID_QUIRK_MULTI_INPUT;
	hdev->quirks |= HID_QUIRK_NO_EMPTY_INPUT;

	/*
	 * Handle special quirks for Windows 8 certified devices.
	 */
	if (id->group == HID_GROUP_MULTITOUCH_WIN_8)
		/*
		 * Some multitouch screens do not like to be polled for input
		 * reports. Fortunately, the Win8 spec says that all touches
		 * should be sent during each report, making the initialization
		 * of input reports unnecessary.
		 */
		hdev->quirks |= HID_QUIRK_NO_INIT_INPUT_REPORTS;

	td = devm_kzalloc(&hdev->dev, sizeof(struct mt_device), GFP_KERNEL);
	if (!td) {
		dev_err(&hdev->dev, "cannot allocate multitouch data\n");
		return -ENOMEM;
	}
	td->mtclass = *mtclass;
	td->inputmode = -1;
	td->maxcontact_report_id = -1;
	td->inputmode_value = MT_INPUTMODE_TOUCHSCREEN;
	td->cc_index = -1;
	td->mt_report_id = -1;
	hid_set_drvdata(hdev, td);

	td->fields = devm_kzalloc(&hdev->dev, sizeof(struct mt_fields),
				  GFP_KERNEL);
	if (!td->fields) {
		dev_err(&hdev->dev, "cannot allocate multitouch fields data\n");
		return -ENOMEM;
	}

	if (id->vendor == HID_ANY_ID && id->product == HID_ANY_ID)
		td->serial_maybe = true;

	ret = hid_parse(hdev);
	if (ret != 0)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	ret = sysfs_create_group(&hdev->dev.kobj, &mt_attribute_group);

	mt_set_maxcontacts(hdev);
	mt_set_input_mode(hdev);

	/* release .fields memory as it is not used anymore */
	devm_kfree(&hdev->dev, td->fields);
	td->fields = NULL;

	return 0;
}

#ifdef CONFIG_PM
static int mt_reset_resume(struct hid_device *hdev)
{
	mt_set_maxcontacts(hdev);
	mt_set_input_mode(hdev);
	return 0;
}

static int mt_resume(struct hid_device *hdev)
{
	/* Some Elan legacy devices require SET_IDLE to be set on resume.
	 * It should be safe to send it to other devices too.
	 * Tested on 3M, Stantum, Cypress, Zytronic, eGalax, and Elan panels. */

	hid_hw_idle(hdev, 0, 0, HID_REQ_SET_IDLE);

	return 0;
}
#endif

static void mt_remove(struct hid_device *hdev)
{
	sysfs_remove_group(&hdev->dev.kobj, &mt_attribute_group);
	hid_hw_stop(hdev);
}

/*
 * This list contains only:
 * - VID/PID of products not working with the default multitouch handling
 * - 2 generic rules.
 * So there is no point in adding here any device with MT_CLS_DEFAULT.
 */
static const struct hid_device_id mt_devices[] = {

	/* 3M panels */
	{ .driver_data = MT_CLS_3M,
		MT_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M1968) },
	{ .driver_data = MT_CLS_3M,
		MT_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M2256) },
	{ .driver_data = MT_CLS_3M,
		MT_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M3266) },

	/* Anton devices */
	{ .driver_data = MT_CLS_EXPORT_ALL_INPUTS,
		MT_USB_DEVICE(USB_VENDOR_ID_ANTON,
			USB_DEVICE_ID_ANTON_TOUCH_PAD) },

	/* Atmel panels */
	{ .driver_data = MT_CLS_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_ATMEL,
			USB_DEVICE_ID_ATMEL_MXT_DIGITIZER) },

	/* Baanto multitouch devices */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_BAANTO,
			USB_DEVICE_ID_BAANTO_MT_190W2) },

	/* Cando panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH) },
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH_15_6) },

	/* Chunghwa Telecom touch panels */
	{  .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_CHUNGHWAT,
			USB_DEVICE_ID_CHUNGHWAT_MULTITOUCH) },

	/* CVTouch panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_CVTOUCH,
			USB_DEVICE_ID_CVTOUCH_SCREEN) },

	/* eGalax devices (resistive) */
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_480D) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_480E) },

	/* eGalax devices (capacitive) */
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7207) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_720C) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7224) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_722A) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_725E) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7262) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_726B) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72A1) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72AA) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72C4) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72D0) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72FA) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7302) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7349) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_73F7) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_A001) },

	/* Elitegroup panel */
	{ .driver_data = MT_CLS_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_ELITEGROUP,
			USB_DEVICE_ID_ELITEGROUP_05D8) },

	/* Flatfrog Panels */
	{ .driver_data = MT_CLS_FLATFROG,
		MT_USB_DEVICE(USB_VENDOR_ID_FLATFROG,
			USB_DEVICE_ID_MULTITOUCH_3200) },

	/* FocalTech Panels */
	{ .driver_data = MT_CLS_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_CYGNAL,
			USB_DEVICE_ID_FOCALTECH_FTXXXX_MULTITOUCH) },

	/* GeneralTouch panel */
	{ .driver_data = MT_CLS_GENERALTOUCH_TWOFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN7_TWOFINGERS) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PWT_TENFINGERS) },
	{ .driver_data = MT_CLS_GENERALTOUCH_TWOFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_0101) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_0102) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_0106) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_010A) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_E100) },

	/* Gametel game controller */
	{ .driver_data = MT_CLS_NSMU,
		MT_BT_DEVICE(USB_VENDOR_ID_FRUCTEL,
			USB_DEVICE_ID_GAMETEL_MT_MODE) },

	/* GoodTouch panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_GOODTOUCH,
			USB_DEVICE_ID_GOODTOUCH_000f) },

	/* Hanvon panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		MT_USB_DEVICE(USB_VENDOR_ID_HANVON_ALT,
			USB_DEVICE_ID_HANVON_ALT_MULTITOUCH) },

	/* Ilitek dual touch panel */
	{  .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_ILITEK,
			USB_DEVICE_ID_ILITEK_MULTITOUCH) },

	/* MosArt panels */
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		MT_USB_DEVICE(USB_VENDOR_ID_ASUS,
			USB_DEVICE_ID_ASUS_T91MT)},
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		MT_USB_DEVICE(USB_VENDOR_ID_ASUS,
			USB_DEVICE_ID_ASUSTEK_MULTITOUCH_YFO) },
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		MT_USB_DEVICE(USB_VENDOR_ID_TURBOX,
			USB_DEVICE_ID_TURBOX_TOUCHSCREEN_MOSART) },

	/* Panasonic panels */
	{ .driver_data = MT_CLS_PANASONIC,
		MT_USB_DEVICE(USB_VENDOR_ID_PANASONIC,
			USB_DEVICE_ID_PANABOARD_UBT780) },
	{ .driver_data = MT_CLS_PANASONIC,
		MT_USB_DEVICE(USB_VENDOR_ID_PANASONIC,
			USB_DEVICE_ID_PANABOARD_UBT880) },

	/* Novatek Panel */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_NOVATEK,
			USB_DEVICE_ID_NOVATEK_PCT) },

	/* PixArt optical touch screen */
	{ .driver_data = MT_CLS_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_PIXART,
			USB_DEVICE_ID_PIXART_OPTICAL_TOUCH_SCREEN) },
	{ .driver_data = MT_CLS_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_PIXART,
			USB_DEVICE_ID_PIXART_OPTICAL_TOUCH_SCREEN1) },
	{ .driver_data = MT_CLS_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_PIXART,
			USB_DEVICE_ID_PIXART_OPTICAL_TOUCH_SCREEN2) },

	/* PixCir-based panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		MT_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_PIXCIR_MULTI_TOUCH) },

	/* Quanta-based panels */
	{ .driver_data = MT_CLS_CONFIDENCE_CONTACT_ID,
		MT_USB_DEVICE(USB_VENDOR_ID_QUANTA,
			USB_DEVICE_ID_QUANTA_OPTICAL_TOUCH_3001) },

	/* Stantum panels */
	{ .driver_data = MT_CLS_CONFIDENCE,
		MT_USB_DEVICE(USB_VENDOR_ID_STANTUM_STM,
			USB_DEVICE_ID_MTP_STM)},

	/* TopSeed panels */
	{ .driver_data = MT_CLS_TOPSEED,
		MT_USB_DEVICE(USB_VENDOR_ID_TOPSEED2,
			USB_DEVICE_ID_TOPSEED2_PERIPAD_701) },

	/* Touch International panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_TOUCH_INTL,
			USB_DEVICE_ID_TOUCH_INTL_MULTI_TOUCH) },

	/* Unitec panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_UNITEC,
			USB_DEVICE_ID_UNITEC_USB_TOUCH_0709) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_UNITEC,
			USB_DEVICE_ID_UNITEC_USB_TOUCH_0A19) },

	/* Wistron panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_WISTRON,
			USB_DEVICE_ID_WISTRON_OPTICAL_TOUCH) },

	/* XAT */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XAT,
			USB_DEVICE_ID_XAT_CSR) },

	/* Xiroku */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_SPX) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_MPX) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_CSR) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_SPX1) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_MPX1) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_CSR1) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_SPX2) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_MPX2) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_CSR2) },

	/* Generic MT device */
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_MULTITOUCH, HID_ANY_ID, HID_ANY_ID) },

	/* Generic Win 8 certified MT device */
	{  .driver_data = MT_CLS_WIN_8,
		HID_DEVICE(HID_BUS_ANY, HID_GROUP_MULTITOUCH_WIN_8,
			HID_ANY_ID, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, mt_devices);

static const struct hid_usage_id mt_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver mt_driver = {
	.name = "hid-multitouch",
	.id_table = mt_devices,
	.probe = mt_probe,
	.remove = mt_remove,
	.input_mapping = mt_input_mapping,
	.input_mapped = mt_input_mapped,
	.input_configured = mt_input_configured,
	.feature_mapping = mt_feature_mapping,
	.usage_table = mt_grabbed_usages,
	.event = mt_event,
	.report = mt_report,
#ifdef CONFIG_PM
	.reset_resume = mt_reset_resume,
	.resume = mt_resume,
#endif
};
module_hid_driver(mt_driver);
