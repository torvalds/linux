/*
 *   Apple "Magic" Wireless Mouse driver
 *
 *   Copyright (c) 2010 Michael Poole <mdpoole@troilus.org>
 *   Copyright (c) 2010 Chase Douglas <chase.douglas@canonical.com>
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

#include "hid-ids.h"

static bool emulate_3button = true;
module_param(emulate_3button, bool, 0644);
MODULE_PARM_DESC(emulate_3button, "Emulate a middle button");

static int middle_button_start = -350;
static int middle_button_stop = +350;

static bool emulate_scroll_wheel = true;
module_param(emulate_scroll_wheel, bool, 0644);
MODULE_PARM_DESC(emulate_scroll_wheel, "Emulate a scroll wheel");

static unsigned int scroll_speed = 32;
static int param_set_scroll_speed(const char *val, struct kernel_param *kp) {
	unsigned long speed;
	if (!val || strict_strtoul(val, 0, &speed) || speed > 63)
		return -EINVAL;
	scroll_speed = speed;
	return 0;
}
module_param_call(scroll_speed, param_set_scroll_speed, param_get_uint, &scroll_speed, 0644);
MODULE_PARM_DESC(scroll_speed, "Scroll speed, value from 0 (slow) to 63 (fast)");

static bool scroll_acceleration = false;
module_param(scroll_acceleration, bool, 0644);
MODULE_PARM_DESC(scroll_acceleration, "Accelerate sequential scroll events");

static bool report_touches = true;
module_param(report_touches, bool, 0644);
MODULE_PARM_DESC(report_touches, "Emit touch records (otherwise, only use them for emulation)");

static bool report_undeciphered;
module_param(report_undeciphered, bool, 0644);
MODULE_PARM_DESC(report_undeciphered, "Report undeciphered multi-touch state field using a MSC_RAW event");

#define TRACKPAD_REPORT_ID 0x28
#define MOUSE_REPORT_ID    0x29
#define DOUBLE_REPORT_ID   0xf7
/* These definitions are not precise, but they're close enough.  (Bits
 * 0x03 seem to indicate the aspect ratio of the touch, bits 0x70 seem
 * to be some kind of bit mask -- 0x20 may be a near-field reading,
 * and 0x40 is actual contact, and 0x10 may be a start/stop or change
 * indication.)
 */
#define TOUCH_STATE_MASK  0xf0
#define TOUCH_STATE_NONE  0x00
#define TOUCH_STATE_START 0x30
#define TOUCH_STATE_DRAG  0x40

#define SCROLL_ACCEL_DEFAULT 7

/* Single touch emulation should only begin when no touches are currently down.
 * This is true when single_touch_id is equal to NO_TOUCHES. If multiple touches
 * are down and the touch providing for single touch emulation is lifted,
 * single_touch_id is equal to SINGLE_TOUCH_UP. While single touch emulation is
 * occuring, single_touch_id corresponds with the tracking id of the touch used.
 */
#define NO_TOUCHES -1
#define SINGLE_TOUCH_UP -2

/**
 * struct magicmouse_sc - Tracks Magic Mouse-specific data.
 * @input: Input device through which we report events.
 * @quirks: Currently unused.
 * @ntouches: Number of touches in most recent touch report.
 * @scroll_accel: Number of consecutive scroll motions.
 * @scroll_jiffies: Time of last scroll motion.
 * @touches: Most recent data for a touch, indexed by tracking ID.
 * @tracking_ids: Mapping of current touch input data to @touches.
 */
struct magicmouse_sc {
	struct input_dev *input;
	unsigned long quirks;

	int ntouches;
	int scroll_accel;
	unsigned long scroll_jiffies;

	struct {
		short x;
		short y;
		short scroll_x;
		short scroll_y;
		u8 size;
	} touches[16];
	int tracking_ids[16];
	int single_touch_id;
};

static int magicmouse_firm_touch(struct magicmouse_sc *msc)
{
	int touch = -1;
	int ii;

	/* If there is only one "firm" touch, set touch to its
	 * tracking ID.
	 */
	for (ii = 0; ii < msc->ntouches; ii++) {
		int idx = msc->tracking_ids[ii];
		if (msc->touches[idx].size < 8) {
			/* Ignore this touch. */
		} else if (touch >= 0) {
			touch = -1;
			break;
		} else {
			touch = idx;
		}
	}

	return touch;
}

static void magicmouse_emit_buttons(struct magicmouse_sc *msc, int state)
{
	int last_state = test_bit(BTN_LEFT, msc->input->key) << 0 |
		test_bit(BTN_RIGHT, msc->input->key) << 1 |
		test_bit(BTN_MIDDLE, msc->input->key) << 2;

	if (emulate_3button) {
		int id;

		/* If some button was pressed before, keep it held
		 * down.  Otherwise, if there's exactly one firm
		 * touch, use that to override the mouse's guess.
		 */
		if (state == 0) {
			/* The button was released. */
		} else if (last_state != 0) {
			state = last_state;
		} else if ((id = magicmouse_firm_touch(msc)) >= 0) {
			int x = msc->touches[id].x;
			if (x < middle_button_start)
				state = 1;
			else if (x > middle_button_stop)
				state = 2;
			else
				state = 4;
		} /* else: we keep the mouse's guess */

		input_report_key(msc->input, BTN_MIDDLE, state & 4);
	}

	input_report_key(msc->input, BTN_LEFT, state & 1);
	input_report_key(msc->input, BTN_RIGHT, state & 2);

	if (state != last_state)
		msc->scroll_accel = SCROLL_ACCEL_DEFAULT;
}

static void magicmouse_emit_touch(struct magicmouse_sc *msc, int raw_id, u8 *tdata)
{
	struct input_dev *input = msc->input;
	int id, x, y, size, orientation, touch_major, touch_minor, state, down;

	if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE) {
		id = (tdata[6] << 2 | tdata[5] >> 6) & 0xf;
		x = (tdata[1] << 28 | tdata[0] << 20) >> 20;
		y = -((tdata[2] << 24 | tdata[1] << 16) >> 20);
		size = tdata[5] & 0x3f;
		orientation = (tdata[6] >> 2) - 32;
		touch_major = tdata[3];
		touch_minor = tdata[4];
		state = tdata[7] & TOUCH_STATE_MASK;
		down = state != TOUCH_STATE_NONE;
	} else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD */
		id = (tdata[7] << 2 | tdata[6] >> 6) & 0xf;
		x = (tdata[1] << 27 | tdata[0] << 19) >> 19;
		y = -((tdata[3] << 30 | tdata[2] << 22 | tdata[1] << 14) >> 19);
		size = tdata[6] & 0x3f;
		orientation = (tdata[7] >> 2) - 32;
		touch_major = tdata[4];
		touch_minor = tdata[5];
		state = tdata[8] & TOUCH_STATE_MASK;
		down = state != TOUCH_STATE_NONE;
	}

	/* Store tracking ID and other fields. */
	msc->tracking_ids[raw_id] = id;
	msc->touches[id].x = x;
	msc->touches[id].y = y;
	msc->touches[id].size = size;

	/* If requested, emulate a scroll wheel by detecting small
	 * vertical touch motions.
	 */
	if (emulate_scroll_wheel) {
		unsigned long now = jiffies;
		int step_x = msc->touches[id].scroll_x - x;
		int step_y = msc->touches[id].scroll_y - y;

		/* Calculate and apply the scroll motion. */
		switch (state) {
		case TOUCH_STATE_START:
			msc->touches[id].scroll_x = x;
			msc->touches[id].scroll_y = y;

			/* Reset acceleration after half a second. */
			if (scroll_acceleration && time_before(now,
						msc->scroll_jiffies + HZ / 2))
				msc->scroll_accel = max_t(int,
						msc->scroll_accel - 1, 1);
			else
				msc->scroll_accel = SCROLL_ACCEL_DEFAULT;

			break;
		case TOUCH_STATE_DRAG:
			step_x /= (64 - (int)scroll_speed) * msc->scroll_accel;
			if (step_x != 0) {
				msc->touches[id].scroll_x -= step_x *
					(64 - scroll_speed) * msc->scroll_accel;
				msc->scroll_jiffies = now;
				input_report_rel(input, REL_HWHEEL, -step_x);
			}

			step_y /= (64 - (int)scroll_speed) * msc->scroll_accel;
			if (step_y != 0) {
				msc->touches[id].scroll_y -= step_y *
					(64 - scroll_speed) * msc->scroll_accel;
				msc->scroll_jiffies = now;
				input_report_rel(input, REL_WHEEL, step_y);
			}
			break;
		}
	}

	if (down) {
		msc->ntouches++;
		if (msc->single_touch_id == NO_TOUCHES)
			msc->single_touch_id = id;
	} else if (msc->single_touch_id == id)
		msc->single_touch_id = SINGLE_TOUCH_UP;

	/* Generate the input events for this touch. */
	if (report_touches && down) {
		input_report_abs(input, ABS_MT_TRACKING_ID, id);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major);
		input_report_abs(input, ABS_MT_TOUCH_MINOR, touch_minor);
		input_report_abs(input, ABS_MT_ORIENTATION, orientation);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);

		if (report_undeciphered) {
			if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE)
				input_event(input, EV_MSC, MSC_RAW, tdata[7]);
			else /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD */
				input_event(input, EV_MSC, MSC_RAW, tdata[8]);
		}

		input_mt_sync(input);
	}
}

static int magicmouse_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);
	struct input_dev *input = msc->input;
	int x = 0, y = 0, ii, clicks = 0, npoints;

	switch (data[0]) {
	case TRACKPAD_REPORT_ID:
		/* Expect four bytes of prefix, and N*9 bytes of touch data. */
		if (size < 4 || ((size - 4) % 9) != 0)
			return 0;
		npoints = (size - 4) / 9;
		msc->ntouches = 0;
		for (ii = 0; ii < npoints; ii++)
			magicmouse_emit_touch(msc, ii, data + ii * 9 + 4);

		/* We don't need an MT sync here because trackpad emits a
		 * BTN_TOUCH event in a new frame when all touches are released.
		 */
		if (msc->ntouches == 0)
			msc->single_touch_id = NO_TOUCHES;

		clicks = data[1];

		/* The following bits provide a device specific timestamp. They
		 * are unused here.
		 *
		 * ts = data[1] >> 6 | data[2] << 2 | data[3] << 10;
		 */
		break;
	case MOUSE_REPORT_ID:
		/* Expect six bytes of prefix, and N*8 bytes of touch data. */
		if (size < 6 || ((size - 6) % 8) != 0)
			return 0;
		npoints = (size - 6) / 8;
		msc->ntouches = 0;
		for (ii = 0; ii < npoints; ii++)
			magicmouse_emit_touch(msc, ii, data + ii * 8 + 6);

		if (report_touches && msc->ntouches == 0)
			input_mt_sync(input);

		/* When emulating three-button mode, it is important
		 * to have the current touch information before
		 * generating a click event.
		 */
		x = (int)(((data[3] & 0x0c) << 28) | (data[1] << 22)) >> 22;
		y = (int)(((data[3] & 0x30) << 26) | (data[2] << 22)) >> 22;
		clicks = data[3];

		/* The following bits provide a device specific timestamp. They
		 * are unused here.
		 *
		 * ts = data[3] >> 6 | data[4] << 2 | data[5] << 10;
		 */
		break;
	case DOUBLE_REPORT_ID:
		/* Sometimes the trackpad sends two touch reports in one
		 * packet.
		 */
		magicmouse_raw_event(hdev, report, data + 2, data[1]);
		magicmouse_raw_event(hdev, report, data + 2 + data[1],
			size - 2 - data[1]);
		break;
	default:
		return 0;
	}

	if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE) {
		magicmouse_emit_buttons(msc, clicks & 3);
		input_report_rel(input, REL_X, x);
		input_report_rel(input, REL_Y, y);
	} else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD */
		input_report_key(input, BTN_MOUSE, clicks & 1);
		input_report_key(input, BTN_TOUCH, msc->ntouches > 0);
		input_report_key(input, BTN_TOOL_FINGER, msc->ntouches == 1);
		input_report_key(input, BTN_TOOL_DOUBLETAP, msc->ntouches == 2);
		input_report_key(input, BTN_TOOL_TRIPLETAP, msc->ntouches == 3);
		input_report_key(input, BTN_TOOL_QUADTAP, msc->ntouches == 4);
		if (msc->single_touch_id >= 0) {
			input_report_abs(input, ABS_X,
				msc->touches[msc->single_touch_id].x);
			input_report_abs(input, ABS_Y,
				msc->touches[msc->single_touch_id].y);
		}
	}

	input_sync(input);
	return 1;
}

static void magicmouse_setup_input(struct input_dev *input, struct hid_device *hdev)
{
	__set_bit(EV_KEY, input->evbit);

	if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE) {
		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(BTN_RIGHT, input->keybit);
		if (emulate_3button)
			__set_bit(BTN_MIDDLE, input->keybit);

		__set_bit(EV_REL, input->evbit);
		__set_bit(REL_X, input->relbit);
		__set_bit(REL_Y, input->relbit);
		if (emulate_scroll_wheel) {
			__set_bit(REL_WHEEL, input->relbit);
			__set_bit(REL_HWHEEL, input->relbit);
		}
	} else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD */
		__set_bit(BTN_MOUSE, input->keybit);
		__set_bit(BTN_TOOL_FINGER, input->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
		__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
		__set_bit(BTN_TOOL_QUADTAP, input->keybit);
		__set_bit(BTN_TOUCH, input->keybit);
	}

	if (report_touches) {
		__set_bit(EV_ABS, input->evbit);

		input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, 15, 0, 0);
		input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 4, 0);
		input_set_abs_params(input, ABS_MT_TOUCH_MINOR, 0, 255, 4, 0);
		input_set_abs_params(input, ABS_MT_ORIENTATION, -32, 31, 1, 0);

		/* Note: Touch Y position from the device is inverted relative
		 * to how pointer motion is reported (and relative to how USB
		 * HID recommends the coordinates work).  This driver keeps
		 * the origin at the same position, and just uses the additive
		 * inverse of the reported Y.
		 */
		if (input->id.product == USB_DEVICE_ID_APPLE_MAGICMOUSE) {
			input_set_abs_params(input, ABS_MT_POSITION_X, -1100,
				1358, 4, 0);
			input_set_abs_params(input, ABS_MT_POSITION_Y, -1589,
				2047, 4, 0);
		} else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD */
			input_set_abs_params(input, ABS_X, -2909, 3167, 4, 0);
			input_set_abs_params(input, ABS_Y, -2456, 2565, 4, 0);
			input_set_abs_params(input, ABS_MT_POSITION_X, -2909,
				3167, 4, 0);
			input_set_abs_params(input, ABS_MT_POSITION_Y, -2456,
				2565, 4, 0);
		}
	}

	if (report_undeciphered) {
		__set_bit(EV_MSC, input->evbit);
		__set_bit(MSC_RAW, input->mscbit);
	}
}

static int magicmouse_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);

	if (!msc->input)
		msc->input = hi->input;

	return 0;
}

static int magicmouse_probe(struct hid_device *hdev,
	const struct hid_device_id *id)
{
	__u8 feature[] = { 0xd7, 0x01 };
	struct magicmouse_sc *msc;
	struct hid_report *report;
	int ret;

	msc = kzalloc(sizeof(*msc), GFP_KERNEL);
	if (msc == NULL) {
		dev_err(&hdev->dev, "can't alloc magicmouse descriptor\n");
		return -ENOMEM;
	}

	msc->scroll_accel = SCROLL_ACCEL_DEFAULT;

	msc->quirks = id->driver_data;
	hid_set_drvdata(hdev, msc);

	msc->single_touch_id = NO_TOUCHES;

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "magicmouse hid parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		dev_err(&hdev->dev, "magicmouse hw start failed\n");
		goto err_free;
	}

	/* We do this after hid-input is done parsing reports so that
	 * hid-input uses the most natural button and axis IDs.
	 */
	if (msc->input)
		magicmouse_setup_input(msc->input, hdev);

	if (id->product == USB_DEVICE_ID_APPLE_MAGICMOUSE)
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			MOUSE_REPORT_ID);
	else { /* USB_DEVICE_ID_APPLE_MAGICTRACKPAD */
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			TRACKPAD_REPORT_ID);
		report = hid_register_report(hdev, HID_INPUT_REPORT,
			DOUBLE_REPORT_ID);
	}

	if (!report) {
		dev_err(&hdev->dev, "unable to register touch report\n");
		ret = -ENOMEM;
		goto err_stop_hw;
	}
	report->size = 6;

	ret = hdev->hid_output_raw_report(hdev, feature, sizeof(feature),
			HID_FEATURE_REPORT);
	if (ret != sizeof(feature)) {
		dev_err(&hdev->dev, "unable to request touch data (%d)\n",
				ret);
		goto err_stop_hw;
	}

	return 0;
err_stop_hw:
	hid_hw_stop(hdev);
err_free:
	kfree(msc);
	return ret;
}

static void magicmouse_remove(struct hid_device *hdev)
{
	struct magicmouse_sc *msc = hid_get_drvdata(hdev);

	hid_hw_stop(hdev);
	kfree(msc);
}

static const struct hid_device_id magic_mice[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICMOUSE), .driver_data = 0 },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_APPLE,
		USB_DEVICE_ID_APPLE_MAGICTRACKPAD), .driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(hid, magic_mice);

static struct hid_driver magicmouse_driver = {
	.name = "magicmouse",
	.id_table = magic_mice,
	.probe = magicmouse_probe,
	.remove = magicmouse_remove,
	.raw_event = magicmouse_raw_event,
	.input_mapping = magicmouse_input_mapping,
};

static int __init magicmouse_init(void)
{
	int ret;

	ret = hid_register_driver(&magicmouse_driver);
	if (ret)
		printk(KERN_ERR "can't register magicmouse driver\n");

	return ret;
}

static void __exit magicmouse_exit(void)
{
	hid_unregister_driver(&magicmouse_driver);
}

module_init(magicmouse_init);
module_exit(magicmouse_exit);
MODULE_LICENSE("GPL");
