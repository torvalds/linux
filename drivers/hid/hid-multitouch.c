/*
 *  HID driver for multitouch panels
 *
 *  Copyright (c) 2010-2011 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010-2011 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2010-2011 Ecole Nationale de l'Aviation Civile, France
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
MODULE_DESCRIPTION("HID multitouch panels");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

/* quirks to control the device */
#define MT_QUIRK_NOT_SEEN_MEANS_UP	(1 << 0)
#define MT_QUIRK_SLOT_IS_CONTACTID	(1 << 1)
#define MT_QUIRK_CYPRESS	(1 << 2)
#define MT_QUIRK_SLOT_IS_CONTACTNUMBER	(1 << 3)

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
	__s8 inputmode;		/* InputMode HID feature, -1 if non-existent */
	__u8 num_received;	/* how many contacts we received */
	__u8 num_expected;	/* expected last contact index */
	bool curvalid;		/* is the current contact valid? */
	struct mt_slot slots[0];	/* first slot */
};

struct mt_class {
	__s32 quirks;
	__s32 sn_move;	/* Signal/noise ratio for move events */
	__s32 sn_pressure;	/* Signal/noise ratio for pressure events */
	__u8 maxcontacts;
};

/* classes of device behavior */
#define MT_CLS_DEFAULT 0
#define MT_CLS_DUAL1 1
#define MT_CLS_DUAL2 2
#define MT_CLS_CYPRESS 3

/*
 * these device-dependent functions determine what slot corresponds
 * to a valid contact that was just read.
 */

static int slot_is_contactid(struct mt_device *td)
{
	return td->curdata.contactid;
}

static int slot_is_contactnumber(struct mt_device *td)
{
	return td->num_received;
}

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
	for (i = 0; i < td->mtclass->maxcontacts; ++i) {
		if (td->slots[i].contactid == td->curdata.contactid &&
			td->slots[i].touch_state)
			return i;
	}
	for (i = 0; i < td->mtclass->maxcontacts; ++i) {
		if (!td->slots[i].seen_in_this_frame &&
			!td->slots[i].touch_state)
			return i;
	}
	return -1;
	/* should not occurs. If this happens that means
	 * that the device sent more touches that it says
	 * in the report descriptor. It is ignored then. */
}

struct mt_class mt_classes[] = {
	{ 0, 0, 0, 10 },                             /* MT_CLS_DEFAULT */
	{ MT_QUIRK_SLOT_IS_CONTACTID, 0, 0, 2 },     /* MT_CLS_DUAL1 */
	{ MT_QUIRK_SLOT_IS_CONTACTNUMBER, 0, 0, 10 },    /* MT_CLS_DUAL2 */
	{ MT_QUIRK_CYPRESS | MT_QUIRK_NOT_SEEN_MEANS_UP, 0, 0, 10 }, /* MT_CLS_CYPRESS */
};

static void mt_feature_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage)
{
	if (usage->hid == HID_DG_INPUTMODE) {
		struct mt_device *td = hid_get_drvdata(hdev);
		td->inputmode = field->report->id;
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
	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			set_abs(hi->input, ABS_MT_POSITION_X, field,
				cls->sn_move);
			/* touchscreen emulation */
			set_abs(hi->input, ABS_X, field, cls->sn_move);
			td->last_slot_field = usage->hid;
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			set_abs(hi->input, ABS_MT_POSITION_Y, field,
				cls->sn_move);
			/* touchscreen emulation */
			set_abs(hi->input, ABS_Y, field, cls->sn_move);
			td->last_slot_field = usage->hid;
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		case HID_DG_INRANGE:
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_CONFIDENCE:
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_TIPSWITCH:
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			input_set_capability(hi->input, EV_KEY, BTN_TOUCH);
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_CONTACTID:
			input_mt_init_slots(hi->input,
					td->mtclass->maxcontacts);
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			field->logical_maximum = 1;
			field->logical_minimum = 1;
			set_abs(hi->input, ABS_MT_ORIENTATION, field, 0);
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_TIPPRESSURE:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_PRESSURE);
			set_abs(hi->input, ABS_MT_PRESSURE, field,
				cls->sn_pressure);
			/* touchscreen emulation */
			set_abs(hi->input, ABS_PRESSURE, field,
				cls->sn_pressure);
			td->last_slot_field = usage->hid;
			return 1;
		case HID_DG_CONTACTCOUNT:
			td->last_field_index = field->report->maxfield - 1;
			return 1;
		case HID_DG_CONTACTMAX:
			/* we don't set td->last_slot_field as contactcount and
			 * contact max are global to the report */
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
	struct mt_class *cls = td->mtclass;

	if (cls->quirks & MT_QUIRK_SLOT_IS_CONTACTID)
		return slot_is_contactid(td);

	if (cls->quirks & MT_QUIRK_CYPRESS)
		return cypress_compute_slot(td);

	if (cls->quirks & MT_QUIRK_SLOT_IS_CONTACTNUMBER)
		return slot_is_contactnumber(td);

	return find_slot_from_contactid(td);
}

/*
 * this function is called when a whole contact has been processed,
 * so that it can assign it to a slot and store the data there
 */
static void mt_complete_slot(struct mt_device *td)
{
	if (td->curvalid) {
		struct mt_slot *slot;
		int slotnum = mt_compute_slot(td);

		if (slotnum >= 0 && slotnum < td->mtclass->maxcontacts) {
			slot = td->slots + slotnum;

			memcpy(slot, &(td->curdata), sizeof(struct mt_slot));
			slot->seen_in_this_frame = true;
		}
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

	for (i = 0; i < td->mtclass->maxcontacts; ++i) {
		struct mt_slot *s = &(td->slots[i]);
		if ((td->mtclass->quirks & MT_QUIRK_NOT_SEEN_MEANS_UP) &&
			!s->seen_in_this_frame) {
			/*
			 * this slot does not contain useful data,
			 * notify its closure
			 */
			s->touch_state = false;
		}

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER,
			s->touch_state);
		input_event(input, EV_ABS, ABS_MT_POSITION_X, s->x);
		input_event(input, EV_ABS, ABS_MT_POSITION_Y, s->y);
		input_event(input, EV_ABS, ABS_MT_PRESSURE, s->p);
		input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, s->w);
		input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, s->h);
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

	if (hid->claimed & HID_CLAIMED_INPUT) {
		switch (usage->hid) {
		case HID_DG_INRANGE:
			break;
		case HID_DG_TIPSWITCH:
			td->curvalid = value;
			td->curdata.touch_state = value;
			break;
		case HID_DG_CONFIDENCE:
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
			 * We must not overwrite the previous value (some
			 * devices send one sequence splitted over several
			 * messages)
			 */
			if (value)
				td->num_expected = value - 1;
			break;

		default:
			/* fallback to the generic hidinput handling */
			return 0;
		}
	}

	if (usage->hid == td->last_slot_field)
		mt_complete_slot(td);

	if (field->index == td->last_field_index
		&& td->num_received > td->num_expected)
		mt_emit_event(td, field->hidinput->input);

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
	int ret;
	struct mt_device *td;
	struct mt_class *mtclass = mt_classes + id->driver_data;

	/* This allows the driver to correctly support devices
	 * that emit events over several HID messages.
	 */
	hdev->quirks |= HID_QUIRK_NO_INPUT_SYNC;

	td = kzalloc(sizeof(struct mt_device) +
				mtclass->maxcontacts * sizeof(struct mt_slot),
				GFP_KERNEL);
	if (!td) {
		dev_err(&hdev->dev, "cannot allocate multitouch data\n");
		return -ENOMEM;
	}
	td->mtclass = mtclass;
	td->inputmode = -1;
	hid_set_drvdata(hdev, td);

	ret = hid_parse(hdev);
	if (ret != 0)
		goto fail;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret != 0)
		goto fail;

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
	kfree(td);
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id mt_devices[] = {

	/* Cypress panel */
	{ .driver_data = MT_CLS_CYPRESS,
		HID_USB_DEVICE(USB_VENDOR_ID_CYPRESS,
			USB_DEVICE_ID_CYPRESS_TRUETOUCH) },

	/* GeneralTouch panel */
	{ .driver_data = MT_CLS_DUAL2,
		HID_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN7_TWOFINGERS) },

	/* PixCir-based panels */
	{ .driver_data = MT_CLS_DUAL1,
		HID_USB_DEVICE(USB_VENDOR_ID_HANVON,
			USB_DEVICE_ID_HANVON_MULTITOUCH) },
	{ .driver_data = MT_CLS_DUAL1,
		HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_PIXCIR_MULTI_TOUCH) },

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
