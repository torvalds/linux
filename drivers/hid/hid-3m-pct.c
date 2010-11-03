/*
 *  HID driver for 3M PCT multitouch panels
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

MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_DESCRIPTION("3M PCT multitouch panels");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

#define MAX_SLOTS		60
#define MAX_TRKID		USHRT_MAX
#define MAX_EVENTS		360

/* estimated signal-to-noise ratios */
#define SN_MOVE			2048
#define SN_WIDTH		128

struct mmm_finger {
	__s32 x, y, w, h;
	__u16 id;
	bool prev_touch;
	bool touch, valid;
};

struct mmm_data {
	struct mmm_finger f[MAX_SLOTS];
	__u16 id;
	__u8 curid;
	__u8 nexp, nreal;
	bool touch, valid;
};

static int mmm_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	int f1 = field->logical_minimum;
	int f2 = field->logical_maximum;
	int df = f2 - f1;

	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_BUTTON:
		return -1;

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			input_set_abs_params(hi->input, ABS_MT_POSITION_X,
					     f1, f2, df / SN_MOVE, 0);
			/* touchscreen emulation */
			input_set_abs_params(hi->input, ABS_X,
					     f1, f2, df / SN_MOVE, 0);
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			input_set_abs_params(hi->input, ABS_MT_POSITION_Y,
					     f1, f2, df / SN_MOVE, 0);
			/* touchscreen emulation */
			input_set_abs_params(hi->input, ABS_Y,
					     f1, f2, df / SN_MOVE, 0);
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		/* we do not want to map these: no input-oriented meaning */
		case 0x14:
		case 0x23:
		case HID_DG_INPUTMODE:
		case HID_DG_DEVICEINDEX:
		case HID_DG_CONTACTCOUNT:
		case HID_DG_CONTACTMAX:
		case HID_DG_INRANGE:
		case HID_DG_CONFIDENCE:
			return -1;
		case HID_DG_TIPSWITCH:
			/* touchscreen emulation */
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			input_set_capability(hi->input, EV_KEY, BTN_TOUCH);
			return 1;
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			input_set_abs_params(hi->input, ABS_MT_TOUCH_MAJOR,
					     f1, f2, df / SN_WIDTH, 0);
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			input_set_abs_params(hi->input, ABS_MT_TOUCH_MINOR,
					     f1, f2, df / SN_WIDTH, 0);
			input_set_abs_params(hi->input, ABS_MT_ORIENTATION,
					0, 1, 0, 0);
			return 1;
		case HID_DG_CONTACTID:
			field->logical_maximum = MAX_TRKID;
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TRACKING_ID);
			input_set_abs_params(hi->input, ABS_MT_TRACKING_ID,
					     0, MAX_TRKID, 0, 0);
			if (!hi->input->mt)
				input_mt_create_slots(hi->input, MAX_SLOTS);
			input_set_events_per_packet(hi->input, MAX_EVENTS);
			return 1;
		}
		/* let hid-input decide for the others */
		return 0;

	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int mmm_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	/* tell hid-input to skip setup of these event types */
	if (usage->type == EV_KEY || usage->type == EV_ABS)
		set_bit(usage->type, hi->input->evbit);
	return -1;
}

/*
 * this function is called when a whole packet has been received and processed,
 * so that it can decide what to send to the input layer.
 */
static void mmm_filter_event(struct mmm_data *md, struct input_dev *input)
{
	struct mmm_finger *oldest = 0;
	int i;
	for (i = 0; i < MAX_SLOTS; ++i) {
		struct mmm_finger *f = &md->f[i];
		if (!f->valid) {
			/* this finger is just placeholder data, ignore */
			continue;
		}
		input_mt_slot(input, i);
		if (f->touch) {
			/* this finger is on the screen */
			int wide = (f->w > f->h);
			/* divided by two to match visual scale of touch */
			int major = max(f->w, f->h) >> 1;
			int minor = min(f->w, f->h) >> 1;

			if (!f->prev_touch)
				f->id = md->id++;
			input_event(input, EV_ABS, ABS_MT_TRACKING_ID, f->id);
			input_event(input, EV_ABS, ABS_MT_POSITION_X, f->x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, f->y);
			input_event(input, EV_ABS, ABS_MT_ORIENTATION, wide);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
			input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, minor);
			/* touchscreen emulation: pick the oldest contact */
			if (!oldest || ((f->id - oldest->id) & (SHRT_MAX + 1)))
				oldest = f;
		} else {
			/* this finger took off the screen */
			input_event(input, EV_ABS, ABS_MT_TRACKING_ID, -1);
		}
		f->prev_touch = f->touch;
		f->valid = 0;
	}

	/* touchscreen emulation */
	if (oldest) {
		input_event(input, EV_KEY, BTN_TOUCH, 1);
		input_event(input, EV_ABS, ABS_X, oldest->x);
		input_event(input, EV_ABS, ABS_Y, oldest->y);
	} else {
		input_event(input, EV_KEY, BTN_TOUCH, 0);
	}
	input_sync(input);
}

/*
 * this function is called upon all reports
 * so that we can accumulate contact point information,
 * and call input_mt_sync after each point.
 */
static int mmm_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct mmm_data *md = hid_get_drvdata(hid);
	/*
	 * strangely, this function can be called before
	 * field->hidinput is initialized!
	 */
	if (hid->claimed & HID_CLAIMED_INPUT) {
		struct input_dev *input = field->hidinput->input;
		switch (usage->hid) {
		case HID_DG_TIPSWITCH:
			md->touch = value;
			break;
		case HID_DG_CONFIDENCE:
			md->valid = value;
			break;
		case HID_DG_WIDTH:
			if (md->valid)
				md->f[md->curid].w = value;
			break;
		case HID_DG_HEIGHT:
			if (md->valid)
				md->f[md->curid].h = value;
			break;
		case HID_DG_CONTACTID:
			value = clamp_val(value, 0, MAX_SLOTS - 1);
			if (md->valid) {
				md->curid = value;
				md->f[value].touch = md->touch;
				md->f[value].valid = 1;
				md->nreal++;
			}
			break;
		case HID_GD_X:
			if (md->valid)
				md->f[md->curid].x = value;
			break;
		case HID_GD_Y:
			if (md->valid)
				md->f[md->curid].y = value;
			break;
		case HID_DG_CONTACTCOUNT:
			if (value)
				md->nexp = value;
			if (md->nreal >= md->nexp) {
				mmm_filter_event(md, input);
				md->nreal = 0;
			}
			break;
		}
	}

	/* we have handled the hidinput part, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int mmm_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct mmm_data *md;

	hdev->quirks |= HID_QUIRK_NO_INPUT_SYNC;

	md = kzalloc(sizeof(struct mmm_data), GFP_KERNEL);
	if (!md) {
		dev_err(&hdev->dev, "cannot allocate 3M data\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, md);

	ret = hid_parse(hdev);
	if (!ret)
		ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	if (ret)
		kfree(md);
	return ret;
}

static void mmm_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id mmm_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_3M, USB_DEVICE_ID_3M1968) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_3M, USB_DEVICE_ID_3M2256) },
	{ }
};
MODULE_DEVICE_TABLE(hid, mmm_devices);

static const struct hid_usage_id mmm_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver mmm_driver = {
	.name = "3m-pct",
	.id_table = mmm_devices,
	.probe = mmm_probe,
	.remove = mmm_remove,
	.input_mapping = mmm_input_mapping,
	.input_mapped = mmm_input_mapped,
	.usage_table = mmm_grabbed_usages,
	.event = mmm_event,
};

static int __init mmm_init(void)
{
	return hid_register_driver(&mmm_driver);
}

static void __exit mmm_exit(void)
{
	hid_unregister_driver(&mmm_driver);
}

module_init(mmm_init);
module_exit(mmm_exit);

