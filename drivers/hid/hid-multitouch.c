/*
 *  HID driver for multitouch panels
 *
 *  Copyright (c) 2010-2011 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010-2011 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2010-2011 Ecole Nationale de l'Aviation Civile, France
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

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/input/mt.h>
#include "usbhid/usbhid.h"


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
#define MT_QUIRK_VALID_IS_INRANGE	(1 << 4)
#define MT_QUIRK_VALID_IS_CONFIDENCE	(1 << 5)
#define MT_QUIRK_EGALAX_XYZ_FIXUP	(1 << 6)
#define MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE	(1 << 7)

struct mt_slot {
	__s32 x, y, p, w, h;
	__s32 contactid;	/* the device ContactID assigned to this slot */
	bool touch_state;	/* is the touch valid? */
	bool seen_in_this_frame;/* has this slot been updated */
};

struct mt_device {
	struct mt_slot curdata;	/* placeholder of incoming data */
	struct mt_class *mtclass;	/* our mt device class */
	unsigned last_field_index;	/* last field index of the report */
	unsigned last_slot_field;	/* the last field of a slot */
	int last_mt_collection;	/* last known mt-related collection */
	__s8 inputmode;		/* InputMode HID feature, -1 if non-existent */
	__u8 num_received;	/* how many contacts we received */
	__u8 num_expected;	/* expected last contact index */
	__u8 maxcontacts;
	bool curvalid;		/* is the current contact valid? */
	struct mt_slot *slots;
};

struct mt_class {
	__s32 name;	/* MT_CLS */
	__s32 quirks;
	__s32 sn_move;	/* Signal/noise ratio for move events */
	__s32 sn_width;	/* Signal/noise ratio for width events */
	__s32 sn_height;	/* Signal/noise ratio for height events */
	__s32 sn_pressure;	/* Signal/noise ratio for pressure events */
	__u8 maxcontacts;
};

/* classes of device behavior */
#define MT_CLS_DEFAULT				0x0001

#define MT_CLS_CONFIDENCE			0x0002
#define MT_CLS_CONFIDENCE_MINUS_ONE		0x0003
#define MT_CLS_DUAL_INRANGE_CONTACTID		0x0004
#define MT_CLS_DUAL_INRANGE_CONTACTNUMBER	0x0005
#define MT_CLS_DUAL_NSMU_CONTACTID		0x0006

/* vendor specific classes */
#define MT_CLS_3M				0x0101
#define MT_CLS_CYPRESS				0x0102
#define MT_CLS_EGALAX				0x0103

#define MT_DEFAULT_MAXCONTACT	10

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

static int find_slot_from_contactid(struct mt_device *td)
{
	int i;
	for (i = 0; i < td->maxcontacts; ++i) {
		if (td->slots[i].contactid == td->curdata.contactid &&
			td->slots[i].touch_state)
			return i;
	}
	for (i = 0; i < td->maxcontacts; ++i) {
		if (!td->slots[i].seen_in_this_frame &&
			!td->slots[i].touch_state)
			return i;
	}
	/* should not occurs. If this happens that means
	 * that the device sent more touches that it says
	 * in the report descriptor. It is ignored then. */
	return -1;
}

struct mt_class mt_classes[] = {
	{ .name = MT_CLS_DEFAULT,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP },
	{ .name = MT_CLS_CONFIDENCE,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE },
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
	{ .name = MT_CLS_DUAL_NSMU_CONTACTID,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.maxcontacts = 2 },

	/*
	 * vendor specific classes
	 */
	{ .name = MT_CLS_3M,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.sn_move = 2048,
		.sn_width = 128,
		.sn_height = 128 },
	{ .name = MT_CLS_CYPRESS,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_CYPRESS,
		.maxcontacts = 10 },
	{ .name = MT_CLS_EGALAX,
		.quirks =  MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_EGALAX_XYZ_FIXUP,
		.maxcontacts = 2,
		.sn_move = 4096,
		.sn_pressure = 32,
	},

	{ }
};

static void mt_feature_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct mt_device *td = hid_get_drvdata(hdev);

	switch (usage->hid) {
	case HID_DG_INPUTMODE:
		td->inputmode = field->report->id;
		break;
	case HID_DG_CONTACTMAX:
		td->maxcontacts = field->value[0];
		if (td->mtclass->maxcontacts)
			/* check if the maxcontacts is given by the class */
			td->maxcontacts = td->mtclass->maxcontacts;

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
}

static int mt_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_class *cls = td->mtclass;
	__s32 quirks = cls->quirks;

	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			if (quirks & MT_QUIRK_EGALAX_XYZ_FIXUP)
				field->logical_maximum = 32760;
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			set_abs(hi->input, ABS_MT_POSITION_X, field,
				cls->sn_move);
			/* touchscreen emulation */
			set_abs(hi->input, ABS_X, field, cls->sn_move);
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_GD_Y:
			if (quirks & MT_QUIRK_EGALAX_XYZ_FIXUP)
				field->logical_maximum = 32760;
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			set_abs(hi->input, ABS_MT_POSITION_Y, field,
				cls->sn_move);
			/* touchscreen emulation */
			set_abs(hi->input, ABS_Y, field, cls->sn_move);
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		case HID_DG_INRANGE:
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_DG_CONFIDENCE:
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_DG_TIPSWITCH:
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			input_set_capability(hi->input, EV_KEY, BTN_TOUCH);
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_DG_CONTACTID:
			if (!td->maxcontacts)
				td->maxcontacts = MT_DEFAULT_MAXCONTACT;
			input_mt_init_slots(hi->input, td->maxcontacts);
			td->last_slot_field = usage->hid;
			td->last_field_index = field->index;
			td->last_mt_collection = usage->collection_index;
			return 1;
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			set_abs(hi->input, ABS_MT_TOUCH_MAJOR, field,
				cls->sn_width);
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			set_abs(hi->input, ABS_MT_TOUCH_MINOR, field,
				cls->sn_height);
			input_set_abs_params(hi->input,
					ABS_MT_ORIENTATION, 0, 1, 0, 0);
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_DG_TIPPRESSURE:
			if (quirks & MT_QUIRK_EGALAX_XYZ_FIXUP)
				field->logical_minimum = 0;
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_PRESSURE);
			set_abs(hi->input, ABS_MT_PRESSURE, field,
				cls->sn_pressure);
			/* touchscreen emulation */
			set_abs(hi->input, ABS_PRESSURE, field,
				cls->sn_pressure);
			if (td->last_mt_collection == usage->collection_index) {
				td->last_slot_field = usage->hid;
				td->last_field_index = field->index;
			}
			return 1;
		case HID_DG_CONTACTCOUNT:
			if (td->last_mt_collection == usage->collection_index)
				td->last_field_index = field->index;
			return 1;
		case HID_DG_CONTACTMAX:
			/* we don't set td->last_slot_field as contactcount and
			 * contact max are global to the report */
			if (td->last_mt_collection == usage->collection_index)
				td->last_field_index = field->index;
			return -1;
		}
		/* let hid-input decide for the others */
		return 0;

	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int mt_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if (usage->type == EV_KEY || usage->type == EV_ABS)
		set_bit(usage->type, hi->input->evbit);

	return -1;
}

static int mt_compute_slot(struct mt_device *td)
{
	__s32 quirks = td->mtclass->quirks;

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTID)
		return td->curdata.contactid;

	if (quirks & MT_QUIRK_CYPRESS)
		return cypress_compute_slot(td);

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTNUMBER)
		return td->num_received;

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE)
		return td->curdata.contactid - 1;

	return find_slot_from_contactid(td);
}

/*
 * this function is called when a whole contact has been processed,
 * so that it can assign it to a slot and store the data there
 */
static void mt_complete_slot(struct mt_device *td)
{
	td->curdata.seen_in_this_frame = true;
	if (td->curvalid) {
		int slotnum = mt_compute_slot(td);

		if (slotnum >= 0 && slotnum < td->maxcontacts)
			td->slots[slotnum] = td->curdata;
	}
	td->num_received++;
}


/*
 * this function is called when a whole packet has been received and processed,
 * so that it can decide what to send to the input layer.
 */
static void mt_emit_event(struct mt_device *td, struct input_dev *input)
{
	int i;

	for (i = 0; i < td->maxcontacts; ++i) {
		struct mt_slot *s = &(td->slots[i]);
		if ((td->mtclass->quirks & MT_QUIRK_NOT_SEEN_MEANS_UP) &&
			!s->seen_in_this_frame) {
			s->touch_state = false;
		}

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER,
			s->touch_state);
		if (s->touch_state) {
			/* this finger is on the screen */
			int wide = (s->w > s->h);
			/* divided by two to match visual scale of touch */
			int major = max(s->w, s->h) >> 1;
			int minor = min(s->w, s->h) >> 1;

			input_event(input, EV_ABS, ABS_MT_POSITION_X, s->x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, s->y);
			input_event(input, EV_ABS, ABS_MT_ORIENTATION, wide);
			input_event(input, EV_ABS, ABS_MT_PRESSURE, s->p);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, minor);
		}
		s->seen_in_this_frame = false;

	}

	input_mt_report_pointer_emulation(input, true);
	input_sync(input);
	td->num_received = 0;
}



static int mt_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct mt_device *td = hid_get_drvdata(hid);
	__s32 quirks = td->mtclass->quirks;

	if (hid->claimed & HID_CLAIMED_INPUT && td->slots) {
		switch (usage->hid) {
		case HID_DG_INRANGE:
			if (quirks & MT_QUIRK_VALID_IS_INRANGE)
				td->curvalid = value;
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
			td->curdata.x = value;
			break;
		case HID_GD_Y:
			td->curdata.y = value;
			break;
		case HID_DG_WIDTH:
			td->curdata.w = value;
			break;
		case HID_DG_HEIGHT:
			td->curdata.h = value;
			break;
		case HID_DG_CONTACTCOUNT:
			/*
			 * Includes multi-packet support where subsequent
			 * packets are sent with zero contactcount.
			 */
			if (value)
				td->num_expected = value;
			break;

		default:
			/* fallback to the generic hidinput handling */
			return 0;
		}

		if (usage->hid == td->last_slot_field) {
			mt_complete_slot(td);
		}

		if (field->index == td->last_field_index
			&& td->num_received >= td->num_expected)
			mt_emit_event(td, field->hidinput->input);

	}

	/* we have handled the hidinput part, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
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
		r->field[0]->value[0] = 0x02;
		usbhid_submit_report(hdev, r, USB_DIR_OUT);
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

	td = kzalloc(sizeof(struct mt_device), GFP_KERNEL);
	if (!td) {
		dev_err(&hdev->dev, "cannot allocate multitouch data\n");
		return -ENOMEM;
	}
	td->mtclass = mtclass;
	td->inputmode = -1;
	td->last_mt_collection = -1;
	hid_set_drvdata(hdev, td);

	ret = hid_parse(hdev);
	if (ret != 0)
		goto fail;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		goto fail;

	td->slots = kzalloc(td->maxcontacts * sizeof(struct mt_slot),
				GFP_KERNEL);
	if (!td->slots) {
		dev_err(&hdev->dev, "cannot allocate multitouch slots\n");
		hid_hw_stop(hdev);
		ret = -ENOMEM;
		goto fail;
	}

	mt_set_input_mode(hdev);

	return 0;

fail:
	kfree(td);
	return ret;
}

#ifdef CONFIG_PM
static int mt_reset_resume(struct hid_device *hdev)
{
	mt_set_input_mode(hdev);
	return 0;
}
#endif

static void mt_remove(struct hid_device *hdev)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	hid_hw_stop(hdev);
	kfree(td->slots);
	kfree(td);
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id mt_devices[] = {

	/* 3M panels */
	{ .driver_data = MT_CLS_3M,
		HID_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M1968) },
	{ .driver_data = MT_CLS_3M,
		HID_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M2256) },
	{ .driver_data = MT_CLS_3M,
		HID_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M3266) },

	/* ActionStar panels */
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_ACTIONSTAR,
			USB_DEVICE_ID_ACTIONSTAR_1011) },

	/* Cando panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH) },
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH_10_1) },
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH_11_6) },
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH_15_6) },

	/* Chunghwa Telecom touch panels */
	{  .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_CHUNGHWAT,
			USB_DEVICE_ID_CHUNGHWAT_MULTITOUCH) },

	/* CVTouch panels */
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_CVTOUCH,
			USB_DEVICE_ID_CVTOUCH_SCREEN) },

	/* Cypress panel */
	{ .driver_data = MT_CLS_CYPRESS,
		HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS,
			USB_DEVICE_ID_CYPRESS_TRUETOUCH) },

	/* eGalax devices (resistive) */
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_480D) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_480E) },

	/* eGalax devices (capacitive) */
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_720C) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_726B) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72A1) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72FA) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7302) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_A001) },

	/* Elo TouchSystems IntelliTouch Plus panel */
	{ .driver_data = MT_CLS_DUAL_NSMU_CONTACTID,
		HID_USB_DEVICE(USB_VENDOR_ID_ELO,
			USB_DEVICE_ID_ELO_TS2515) },

	/* GeneralTouch panel */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		HID_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN7_TWOFINGERS) },

	/* GoodTouch panels */
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_GOODTOUCH,
			USB_DEVICE_ID_GOODTOUCH_000f) },

	/* Ilitek dual touch panel */
	{  .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_ILITEK,
			USB_DEVICE_ID_ILITEK_MULTITOUCH) },

	/* IRTOUCH panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		HID_USB_DEVICE(USB_VENDOR_ID_IRTOUCHSYSTEMS,
			USB_DEVICE_ID_IRTOUCH_INFRARED_USB) },

	/* LG Display panels */
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_LG,
			USB_DEVICE_ID_LG_MULTITOUCH) },

	/* Lumio panels */
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		HID_USB_DEVICE(USB_VENDOR_ID_LUMIO,
			USB_DEVICE_ID_CRYSTALTOUCH) },
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		HID_USB_DEVICE(USB_VENDOR_ID_LUMIO,
			USB_DEVICE_ID_CRYSTALTOUCH_DUAL) },

	/* MosArt panels */
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		HID_USB_DEVICE(USB_VENDOR_ID_ASUS,
			USB_DEVICE_ID_ASUS_T91MT)},
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		HID_USB_DEVICE(USB_VENDOR_ID_ASUS,
			USB_DEVICE_ID_ASUSTEK_MULTITOUCH_YFO) },
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		HID_USB_DEVICE(USB_VENDOR_ID_TURBOX,
			USB_DEVICE_ID_TURBOX_TOUCHSCREEN_MOSART) },

	/* PenMount panels */
	{ .driver_data = MT_CLS_CONFIDENCE,
		HID_USB_DEVICE(USB_VENDOR_ID_PENMOUNT,
			USB_DEVICE_ID_PENMOUNT_PCI) },

	/* PixCir-based panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		HID_USB_DEVICE(USB_VENDOR_ID_HANVON,
			USB_DEVICE_ID_HANVON_MULTITOUCH) },
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_PIXCIR_MULTI_TOUCH) },

	/* Stantum panels */
	{ .driver_data = MT_CLS_CONFIDENCE,
		HID_USB_DEVICE(USB_VENDOR_ID_STANTUM,
			USB_DEVICE_ID_MTP)},
	{ .driver_data = MT_CLS_CONFIDENCE,
		HID_USB_DEVICE(USB_VENDOR_ID_STANTUM_STM,
			USB_DEVICE_ID_MTP_STM)},
	{ .driver_data = MT_CLS_CONFIDENCE,
		HID_USB_DEVICE(USB_VENDOR_ID_STANTUM_SITRONIX,
			USB_DEVICE_ID_MTP_SITRONIX)},

	/* Touch International panels */
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_TOUCH_INTL,
			USB_DEVICE_ID_TOUCH_INTL_MULTI_TOUCH) },

	/* Unitec panels */
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_UNITEC,
			USB_DEVICE_ID_UNITEC_USB_TOUCH_0709) },
	{ .driver_data = MT_CLS_DEFAULT,
		HID_USB_DEVICE(USB_VENDOR_ID_UNITEC,
			USB_DEVICE_ID_UNITEC_USB_TOUCH_0A19) },

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
	.feature_mapping = mt_feature_mapping,
	.usage_table = mt_grabbed_usages,
	.event = mt_event,
#ifdef CONFIG_PM
	.reset_resume = mt_reset_resume,
#endif
};

static int __init mt_init(void)
{
	return hid_register_driver(&mt_driver);
}

static void __exit mt_exit(void)
{
	hid_unregister_driver(&mt_driver);
}

module_init(mt_init);
module_exit(mt_exit);
