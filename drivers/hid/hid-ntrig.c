/*
 *  HID driver for N-Trig touchscreens
 *
 *  Copyright (c) 2008 Rafi Rubin
 *  Copyright (c) 2009 Stephane Chatty
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

#include "hid-ids.h"

#define NTRIG_DUPLICATE_USAGES	0x001

#define nt_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))

struct ntrig_data {
	/* Incoming raw values for a single contact */
	__u16 x, y, w, h;
	__u16 id;
	__u8 confidence;

	bool reading_mt;
	__u8 first_contact_confidence;

	__u8 mt_footer[4];
	__u8 mt_foot_count;
};

/*
 * this driver is aimed at two firmware versions in circulation:
 *  - dual pen/finger single touch
 *  - finger multitouch, pen not working
 */

static int ntrig_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	/* No special mappings needed for the pen and single touch */
	if (field->physical)
		return 0;

	switch (usage->hid & HID_USAGE_PAGE) {
	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			input_set_abs_params(hi->input, ABS_X,
					field->logical_minimum,
					field->logical_maximum, 0, 0);
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			input_set_abs_params(hi->input, ABS_Y,
					field->logical_minimum,
					field->logical_maximum, 0, 0);
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		/* we do not want to map these for now */
		case HID_DG_CONTACTID: /* Not trustworthy, squelch for now */
		case HID_DG_INPUTMODE:
		case HID_DG_DEVICEINDEX:
		case HID_DG_CONTACTMAX:
			return -1;

		/* width/height mapped on TouchMajor/TouchMinor/Orientation */
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			input_set_abs_params(hi->input, ABS_MT_ORIENTATION,
					0, 1, 0, 0);
			return 1;
		}
		return 0;

	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int ntrig_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	/* No special mappings needed for the pen and single touch */
	if (field->physical)
		return 0;

	if (usage->type == EV_KEY || usage->type == EV_REL
			|| usage->type == EV_ABS)
		clear_bit(usage->code, *bit);

	return 0;
}

/*
 * this function is called upon all reports
 * so that we can filter contact point information,
 * decide whether we are in multi or single touch mode
 * and call input_mt_sync after each point if necessary
 */
static int ntrig_event (struct hid_device *hid, struct hid_field *field,
		                        struct hid_usage *usage, __s32 value)
{
	struct input_dev *input = field->hidinput->input;
	struct ntrig_data *nd = hid_get_drvdata(hid);

	/* No special handling needed for the pen */
	if (field->application == HID_DG_PEN)
		return 0;

        if (hid->claimed & HID_CLAIMED_INPUT) {
		switch (usage->hid) {
		case 0xff000001:
			/* Tag indicating the start of a multitouch group */
			nd->reading_mt = 1;
			nd->first_contact_confidence = 0;
			break;
		case HID_DG_CONFIDENCE:
			nd->confidence = value;
			break;
		case HID_GD_X:
			nd->x = value;
			/* Clear the contact footer */
			nd->mt_foot_count = 0;
			break;
		case HID_GD_Y:
			nd->y = value;
			break;
		case HID_DG_CONTACTID:
			nd->id = value;
			break;
		case HID_DG_WIDTH:
			nd->w = value;
			break;
		case HID_DG_HEIGHT:
			nd->h = value;
			/*
			 * when in single touch mode, this is the last
			 * report received in a finger event. We want
			 * to emit a normal (X, Y) position
			 */
			if (!nd->reading_mt) {
				input_report_key(input, BTN_TOOL_DOUBLETAP,
						 (nd->confidence != 0));
				input_event(input, EV_ABS, ABS_X, nd->x);
				input_event(input, EV_ABS, ABS_Y, nd->y);
			}
			break;
		case 0xff000002:
			/*
			 * we receive this when the device is in multitouch
			 * mode. The first of the three values tagged with
			 * this usage tells if the contact point is real
			 * or a placeholder
			 */

			/* Shouldn't get more than 4 footer packets, so skip */
			if (nd->mt_foot_count >= 4)
				break;

			nd->mt_footer[nd->mt_foot_count++] = value;

			/* if the footer isn't complete break */
			if (nd->mt_foot_count != 4)
				break;

			/* Pen activity signal, trigger end of touch. */
			if (nd->mt_footer[2]) {
				nd->confidence = 0;
				break;
			}

			/* If the contact was invalid */
			if (!(nd->confidence && nd->mt_footer[0])
					|| nd->w <= 250
					|| nd->h <= 190) {
				nd->confidence = 0;
				break;
			}

			/* emit a normal (X, Y) for the first point only */
			if (nd->id == 0) {
				nd->first_contact_confidence = nd->confidence;
				input_event(input, EV_ABS, ABS_X, nd->x);
				input_event(input, EV_ABS, ABS_Y, nd->y);
			}
			input_event(input, EV_ABS, ABS_MT_POSITION_X, nd->x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, nd->y);
			if (nd->w > nd->h) {
				input_event(input, EV_ABS,
						ABS_MT_ORIENTATION, 1);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MAJOR, nd->w);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MINOR, nd->h);
			} else {
				input_event(input, EV_ABS,
						ABS_MT_ORIENTATION, 0);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MAJOR, nd->h);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MINOR, nd->w);
			}
			input_mt_sync(field->hidinput->input);
			break;

		case HID_DG_CONTACTCOUNT: /* End of a multitouch group */
			if (!nd->reading_mt)
				break;

			nd->reading_mt = 0;

			if (nd->first_contact_confidence) {
				switch (value) {
				case 0:	/* for single touch devices */
				case 1:
					input_report_key(input,
							BTN_TOOL_DOUBLETAP, 1);
					break;
				case 2:
					input_report_key(input,
							BTN_TOOL_TRIPLETAP, 1);
					break;
				case 3:
				default:
					input_report_key(input,
							BTN_TOOL_QUADTAP, 1);
				}
				input_report_key(input, BTN_TOUCH, 1);
			} else {
				input_report_key(input,
						BTN_TOOL_DOUBLETAP, 0);
				input_report_key(input,
						BTN_TOOL_TRIPLETAP, 0);
				input_report_key(input,
						BTN_TOOL_QUADTAP, 0);
			}
			break;

		default:
			/* fallback to the generic hidinput handling */
			return 0;
		}
	}

	/* we have handled the hidinput part, now remains hiddev */
	if ((hid->claimed & HID_CLAIMED_HIDDEV) && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int ntrig_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct ntrig_data *nd;
	struct hid_input *hidinput;
	struct input_dev *input;

	if (id->driver_data)
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;

	nd = kmalloc(sizeof(struct ntrig_data), GFP_KERNEL);
	if (!nd) {
		dev_err(&hdev->dev, "cannot allocate N-Trig data\n");
		return -ENOMEM;
	}

	nd->reading_mt = 0;
	hid_set_drvdata(hdev, nd);

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto err_free;
	}


	list_for_each_entry(hidinput, &hdev->inputs, list) {
		input = hidinput->input;
		switch (hidinput->report->field[0]->application) {
		case HID_DG_PEN:
			input->name = "N-Trig Pen";
			break;
		case HID_DG_TOUCHSCREEN:
			__clear_bit(BTN_TOOL_PEN, input->keybit);
			/*
			 * A little something special to enable
			 * two and three finger taps.
			 */
			__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
			__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
			__set_bit(BTN_TOOL_QUADTAP, input->keybit);
			/*
			 * The physical touchscreen (single touch)
			 * input has a value for physical, whereas
			 * the multitouch only has logical input
			 * fields.
			 */
			input->name =
				(hidinput->report->field[0]
				 ->physical) ?
				"N-Trig Touchscreen" :
				"N-Trig MultiTouch";
			break;
		}
	}

	return 0;
err_free:
	kfree(nd);
	return ret;
}

static void ntrig_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
}

static const struct hid_device_id ntrig_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ }
};
MODULE_DEVICE_TABLE(hid, ntrig_devices);

static const struct hid_usage_id ntrig_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1 }
};

static struct hid_driver ntrig_driver = {
	.name = "ntrig",
	.id_table = ntrig_devices,
	.probe = ntrig_probe,
	.remove = ntrig_remove,
	.input_mapping = ntrig_input_mapping,
	.input_mapped = ntrig_input_mapped,
	.usage_table = ntrig_grabbed_usages,
	.event = ntrig_event,
};

static int __init ntrig_init(void)
{
	return hid_register_driver(&ntrig_driver);
}

static void __exit ntrig_exit(void)
{
	hid_unregister_driver(&ntrig_driver);
}

module_init(ntrig_init);
module_exit(ntrig_exit);
MODULE_LICENSE("GPL");
