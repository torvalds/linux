/*
 *  HID driver for Cando dual-touch panels
 *
 *  Copyright (c) 2010 Stephane Chatty <chatty@enac.fr>
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

MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_DESCRIPTION("Cando dual-touch panel");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

struct cando_data {
	__u16 x, y;
	__u8 id;
	__s8 oldest;		/* id of the oldest finger in previous frame */
	bool valid;		/* valid finger data, or just placeholder? */
	bool first;		/* is this the first finger in this frame? */
	__s8 firstid;		/* id of the first finger in the frame */
	__u16 firstx, firsty;	/* (x, y) of the first finger in the frame */
};

static int cando_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			/* touchscreen emulation */
			input_set_abs_params(hi->input, ABS_X,
						field->logical_minimum,
						field->logical_maximum, 0, 0);
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			/* touchscreen emulation */
			input_set_abs_params(hi->input, ABS_Y,
						field->logical_minimum,
						field->logical_maximum, 0, 0);
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		case HID_DG_TIPSWITCH:
		case HID_DG_CONTACTMAX:
			return -1;
		case HID_DG_INRANGE:
			/* touchscreen emulation */
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			return 1;
		case HID_DG_CONTACTID:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TRACKING_ID);
			return 1;
		}
		return 0;
	}

	return 0;
}

static int cando_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if (usage->type == EV_KEY || usage->type == EV_ABS)
		clear_bit(usage->code, *bit);

	return 0;
}

/*
 * this function is called when a whole finger has been parsed,
 * so that it can decide what to send to the input layer.
 */
static void cando_filter_event(struct cando_data *td, struct input_dev *input)
{
	td->first = !td->first; /* touchscreen emulation */

	if (!td->valid) {
		/*
		 * touchscreen emulation: if this is the second finger and
		 * the first was valid, the first was the oldest; if the
		 * first was not valid and there was a valid finger in the
		 * previous frame, this is a release.
		 */
		if (td->first) {
			td->firstid = -1;
		} else if (td->firstid >= 0) {
			input_event(input, EV_ABS, ABS_X, td->firstx);
			input_event(input, EV_ABS, ABS_Y, td->firsty);
			td->oldest = td->firstid;
		} else if (td->oldest >= 0) {
			input_event(input, EV_KEY, BTN_TOUCH, 0);
			td->oldest = -1;
		}

		return;
	}
	
	input_event(input, EV_ABS, ABS_MT_TRACKING_ID, td->id);
	input_event(input, EV_ABS, ABS_MT_POSITION_X, td->x);
	input_event(input, EV_ABS, ABS_MT_POSITION_Y, td->y);

	input_mt_sync(input);

	/*
	 * touchscreen emulation: if there was no touching finger previously,
	 * emit touch event
	 */
	if (td->oldest < 0) {
		input_event(input, EV_KEY, BTN_TOUCH, 1);
		td->oldest = td->id;
	}

	/*
	 * touchscreen emulation: if this is the first finger, wait for the
	 * second; the oldest is then the second if it was the oldest already
	 * or if there was no first, the first otherwise.
	 */
	if (td->first) {
		td->firstx = td->x;
		td->firsty = td->y;
		td->firstid = td->id;
	} else {
		int x, y, oldest;
		if (td->id == td->oldest || td->firstid < 0) {
			x = td->x;
			y = td->y;
			oldest = td->id;
		} else {
			x = td->firstx;
			y = td->firsty;
			oldest = td->firstid;
		}
		input_event(input, EV_ABS, ABS_X, x);
		input_event(input, EV_ABS, ABS_Y, y);
		td->oldest = oldest;
	}
}


static int cando_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct cando_data *td = hid_get_drvdata(hid);

	if (hid->claimed & HID_CLAIMED_INPUT) {
		struct input_dev *input = field->hidinput->input;

		switch (usage->hid) {
		case HID_DG_INRANGE:
			td->valid = value;
			break;
		case HID_DG_CONTACTID:
			td->id = value;
			break;
		case HID_GD_X:
			td->x = value;
			break;
		case HID_GD_Y:
			td->y = value;
			cando_filter_event(td, input);
			break;
		case HID_DG_TIPSWITCH:
			/* avoid interference from generic hidinput handling */
			break;

		default:
			/* fallback to the generic hidinput handling */
			return 0;
		}
	}

	/* we have handled the hidinput part, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int cando_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct cando_data *td;

	td = kmalloc(sizeof(struct cando_data), GFP_KERNEL);
	if (!td) {
		hid_err(hdev, "cannot allocate Cando Touch data\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, td);
	td->first = false;
	td->oldest = -1;
	td->valid = false;

	ret = hid_parse(hdev);
	if (!ret)
		ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	if (ret)
		kfree(td);

	return ret;
}

static void cando_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id cando_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH_11_6) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CANDO,
		USB_DEVICE_ID_CANDO_MULTI_TOUCH_15_6) },
	{ }
};
MODULE_DEVICE_TABLE(hid, cando_devices);

static const struct hid_usage_id cando_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver cando_driver = {
	.name = "cando-touch",
	.id_table = cando_devices,
	.probe = cando_probe,
	.remove = cando_remove,
	.input_mapping = cando_input_mapping,
	.input_mapped = cando_input_mapped,
	.usage_table = cando_grabbed_usages,
	.event = cando_event,
};

static int __init cando_init(void)
{
	return hid_register_driver(&cando_driver);
}

static void __exit cando_exit(void)
{
	hid_unregister_driver(&cando_driver);
}

module_init(cando_init);
module_exit(cando_exit);

