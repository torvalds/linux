// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *
 *  Copyright (c) 2010-2014 Nikolai Kondrashov
 *  Copyright (c) 2013 Martin Rusko
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
#include <linux/timer.h>
#include "usbhid/usbhid.h"
#include "hid-uclogic-params.h"

#include "hid-ids.h"

/**
 * uclogic_inrange_timeout - handle pen in-range state timeout.
 * Emulate input events normally generated when pen goes out of range for
 * tablets which don't report that.
 *
 * @t:	The timer the timeout handler is attached to, stored in a struct
 *	uclogic_drvdata.
 */
static void uclogic_inrange_timeout(struct timer_list *t)
{
	struct uclogic_drvdata *drvdata = from_timer(drvdata, t,
							inrange_timer);
	struct input_dev *input = drvdata->pen_input;

	if (input == NULL)
		return;
	input_report_abs(input, ABS_PRESSURE, 0);
	/* If BTN_TOUCH state is changing */
	if (test_bit(BTN_TOUCH, input->key)) {
		input_event(input, EV_MSC, MSC_SCAN,
				/* Digitizer Tip Switch usage */
				0xd0042);
		input_report_key(input, BTN_TOUCH, 0);
	}
	input_report_key(input, BTN_TOOL_PEN, 0);
	input_sync(input);
}

static const __u8 *uclogic_report_fixup(struct hid_device *hdev, __u8 *rdesc,
					unsigned int *rsize)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->desc_ptr != NULL) {
		*rsize = drvdata->desc_size;
		return drvdata->desc_ptr;
	}
	return rdesc;
}

static int uclogic_input_mapping(struct hid_device *hdev,
				 struct hid_input *hi,
				 struct hid_field *field,
				 struct hid_usage *usage,
				 unsigned long **bit,
				 int *max)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	struct uclogic_params *params = &drvdata->params;

	/* Discard invalid pen usages */
	if (params->pen.usage_invalid && (field->application == HID_DG_PEN))
		return -1;

	/* Let hid-core decide what to do */
	return 0;
}

static int uclogic_input_configured(struct hid_device *hdev,
		struct hid_input *hi)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	struct uclogic_params *params = &drvdata->params;
	const char *suffix = NULL;
	struct hid_field *field;
	size_t i;
	const struct uclogic_params_frame *frame;

	/* no report associated (HID_QUIRK_MULTI_INPUT not set) */
	if (!hi->report)
		return 0;

	/*
	 * If this is the input corresponding to the pen report
	 * in need of tweaking.
	 */
	if (hi->report->id == params->pen.id) {
		/* Remember the input device so we can simulate events */
		drvdata->pen_input = hi->input;
	}

	/* If it's one of the frame devices */
	for (i = 0; i < ARRAY_SIZE(params->frame_list); i++) {
		frame = &params->frame_list[i];
		if (hi->report->id == frame->id) {
			/* Assign custom suffix, if any */
			suffix = frame->suffix;
			/*
			 * Disable EV_MSC reports for touch ring interfaces to
			 * make the Wacom driver pickup touch ring extents
			 */
			if (frame->touch_byte > 0)
				__clear_bit(EV_MSC, hi->input->evbit);
		}
	}

	if (!suffix) {
		field = hi->report->field[0];

		switch (field->application) {
		case HID_GD_KEYBOARD:
			suffix = "Keyboard";
			break;
		case HID_GD_MOUSE:
			suffix = "Mouse";
			break;
		case HID_GD_KEYPAD:
			suffix = "Pad";
			break;
		case HID_DG_PEN:
		case HID_DG_DIGITIZER:
			suffix = "Pen";
			break;
		case HID_CP_CONSUMER_CONTROL:
			suffix = "Consumer Control";
			break;
		case HID_GD_SYSTEM_CONTROL:
			suffix = "System Control";
			break;
		}
	} else {
		hi->input->name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
						 "%s %s", hdev->name, suffix);
		if (!hi->input->name)
			return -ENOMEM;
	}

	return 0;
}

static int uclogic_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int rc;
	struct uclogic_drvdata *drvdata = NULL;
	bool params_initialized = false;

	if (!hid_is_usb(hdev))
		return -EINVAL;

	/*
	 * libinput requires the pad interface to be on a different node
	 * than the pen, so use QUIRK_MULTI_INPUT for all tablets.
	 */
	hdev->quirks |= HID_QUIRK_MULTI_INPUT;
	hdev->quirks |= HID_QUIRK_HIDINPUT_FORCE;

	/* Allocate and assign driver data */
	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		rc = -ENOMEM;
		goto failure;
	}
	timer_setup(&drvdata->inrange_timer, uclogic_inrange_timeout, 0);
	drvdata->re_state = U8_MAX;
	drvdata->quirks = id->driver_data;
	hid_set_drvdata(hdev, drvdata);

	/* Initialize the device and retrieve interface parameters */
	rc = uclogic_params_init(&drvdata->params, hdev);
	if (rc != 0) {
		hid_err(hdev, "failed probing parameters: %d\n", rc);
		goto failure;
	}
	params_initialized = true;
	hid_dbg(hdev, "parameters:\n");
	uclogic_params_hid_dbg(hdev, &drvdata->params);
	if (drvdata->params.invalid) {
		hid_info(hdev, "interface is invalid, ignoring\n");
		rc = -ENODEV;
		goto failure;
	}

	/* Generate replacement report descriptor */
	rc = uclogic_params_get_desc(&drvdata->params,
				     &drvdata->desc_ptr,
				     &drvdata->desc_size);
	if (rc) {
		hid_err(hdev,
			"failed generating replacement report descriptor: %d\n",
			rc);
		goto failure;
	}

	rc = hid_parse(hdev);
	if (rc) {
		hid_err(hdev, "parse failed\n");
		goto failure;
	}

	rc = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (rc) {
		hid_err(hdev, "hw start failed\n");
		goto failure;
	}

	return 0;
failure:
	/* Assume "remove" might not be called if "probe" failed */
	if (params_initialized)
		uclogic_params_cleanup(&drvdata->params);
	return rc;
}

#ifdef CONFIG_PM
static int uclogic_resume(struct hid_device *hdev)
{
	int rc;
	struct uclogic_params params;

	/* Re-initialize the device, but discard parameters */
	rc = uclogic_params_init(&params, hdev);
	if (rc != 0)
		hid_err(hdev, "failed to re-initialize the device\n");
	else
		uclogic_params_cleanup(&params);

	return rc;
}
#endif

/**
 * uclogic_exec_event_hook - if the received event is hooked schedules the
 * associated work.
 *
 * @p:		Tablet interface report parameters.
 * @event:	Raw event.
 * @size:	The size of event.
 *
 * Returns:
 *	Whether the event was hooked or not.
 */
static bool uclogic_exec_event_hook(struct uclogic_params *p, u8 *event, int size)
{
	struct uclogic_raw_event_hook *curr;

	if (!p->event_hooks)
		return false;

	list_for_each_entry(curr, &p->event_hooks->list, list) {
		if (curr->size == size && memcmp(curr->event, event, size) == 0) {
			schedule_work(&curr->work);
			return true;
		}
	}

	return false;
}

/**
 * uclogic_raw_event_pen - handle raw pen events (pen HID reports).
 *
 * @drvdata:	Driver data.
 * @data:	Report data buffer, can be modified.
 * @size:	Report data size, bytes.
 *
 * Returns:
 *	Negative value on error (stops event delivery), zero for success.
 */
static int uclogic_raw_event_pen(struct uclogic_drvdata *drvdata,
					u8 *data, int size)
{
	struct uclogic_params_pen *pen = &drvdata->params.pen;

	WARN_ON(drvdata == NULL);
	WARN_ON(data == NULL && size != 0);

	/* If in-range reports are inverted */
	if (pen->inrange ==
		UCLOGIC_PARAMS_PEN_INRANGE_INVERTED) {
		/* Invert the in-range bit */
		data[1] ^= 0x40;
	}
	/*
	 * If report contains fragmented high-resolution pen
	 * coordinates
	 */
	if (size >= 10 && pen->fragmented_hires) {
		u8 pressure_low_byte;
		u8 pressure_high_byte;

		/* Lift pressure bytes */
		pressure_low_byte = data[6];
		pressure_high_byte = data[7];
		/*
		 * Move Y coord to make space for high-order X
		 * coord byte
		 */
		data[6] = data[5];
		data[5] = data[4];
		/* Move high-order X coord byte */
		data[4] = data[8];
		/* Move high-order Y coord byte */
		data[7] = data[9];
		/* Place pressure bytes */
		data[8] = pressure_low_byte;
		data[9] = pressure_high_byte;
	}
	/* If we need to emulate in-range detection */
	if (pen->inrange == UCLOGIC_PARAMS_PEN_INRANGE_NONE) {
		/* Set in-range bit */
		data[1] |= 0x40;
		/* (Re-)start in-range timeout */
		mod_timer(&drvdata->inrange_timer,
				jiffies + msecs_to_jiffies(100));
	}
	/* If we report tilt and Y direction is flipped */
	if (size >= 12 && pen->tilt_y_flipped)
		data[11] = -data[11];

	return 0;
}

/**
 * uclogic_raw_event_frame - handle raw frame events (frame HID reports).
 *
 * @drvdata:	Driver data.
 * @frame:	The parameters of the frame controls to handle.
 * @data:	Report data buffer, can be modified.
 * @size:	Report data size, bytes.
 *
 * Returns:
 *	Negative value on error (stops event delivery), zero for success.
 */
static int uclogic_raw_event_frame(
		struct uclogic_drvdata *drvdata,
		const struct uclogic_params_frame *frame,
		u8 *data, int size)
{
	WARN_ON(drvdata == NULL);
	WARN_ON(data == NULL && size != 0);

	/* If need to, and can, set pad device ID for Wacom drivers */
	if (frame->dev_id_byte > 0 && frame->dev_id_byte < size) {
		/* If we also have a touch ring and the finger left it */
		if (frame->touch_byte > 0 && frame->touch_byte < size &&
		    data[frame->touch_byte] == 0) {
			data[frame->dev_id_byte] = 0;
		} else {
			data[frame->dev_id_byte] = 0xf;
		}
	}

	/* If need to, and can, read rotary encoder state change */
	if (frame->re_lsb > 0 && frame->re_lsb / 8 < size) {
		unsigned int byte = frame->re_lsb / 8;
		unsigned int bit = frame->re_lsb % 8;

		u8 change;
		u8 prev_state = drvdata->re_state;
		/* Read Gray-coded state */
		u8 state = (data[byte] >> bit) & 0x3;
		/* Encode state change into 2-bit signed integer */
		if ((prev_state == 1 && state == 0) ||
		    (prev_state == 2 && state == 3)) {
			change = 1;
		} else if ((prev_state == 2 && state == 0) ||
			   (prev_state == 1 && state == 3)) {
			change = 3;
		} else {
			change = 0;
		}
		/* Write change */
		data[byte] = (data[byte] & ~((u8)3 << bit)) |
				(change << bit);
		/* Remember state */
		drvdata->re_state = state;
	}

	/* If need to, and can, transform the touch ring reports */
	if (frame->touch_byte > 0 && frame->touch_byte < size) {
		__s8 value = data[frame->touch_byte];

		if (value != 0) {
			if (frame->touch_flip_at != 0) {
				value = frame->touch_flip_at - value;
				if (value <= 0)
					value = frame->touch_max + value;
			}
			data[frame->touch_byte] = value - 1;
		}
	}

	/* If need to, and can, transform the bitmap dial reports */
	if (frame->bitmap_dial_byte > 0 && frame->bitmap_dial_byte < size) {
		if (data[frame->bitmap_dial_byte] == 2)
			data[frame->bitmap_dial_byte] = -1;
	}

	return 0;
}

static int uclogic_raw_event(struct hid_device *hdev,
				struct hid_report *report,
				u8 *data, int size)
{
	unsigned int report_id = report->id;
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	struct uclogic_params *params = &drvdata->params;
	struct uclogic_params_pen_subreport *subreport;
	struct uclogic_params_pen_subreport *subreport_list_end;
	size_t i;

	/* Do not handle anything but input reports */
	if (report->type != HID_INPUT_REPORT)
		return 0;

	if (uclogic_exec_event_hook(params, data, size))
		return 0;

	while (true) {
		/* Tweak pen reports, if necessary */
		if ((report_id == params->pen.id) && (size >= 2)) {
			subreport_list_end =
				params->pen.subreport_list +
				ARRAY_SIZE(params->pen.subreport_list);
			/* Try to match a subreport */
			for (subreport = params->pen.subreport_list;
			     subreport < subreport_list_end; subreport++) {
				if (subreport->value != 0 &&
				    subreport->value == data[1]) {
					break;
				}
			}
			/* If a subreport matched */
			if (subreport < subreport_list_end) {
				/* Change to subreport ID, and restart */
				report_id = data[0] = subreport->id;
				continue;
			} else {
				return uclogic_raw_event_pen(drvdata, data, size);
			}
		}

		/* Tweak frame control reports, if necessary */
		for (i = 0; i < ARRAY_SIZE(params->frame_list); i++) {
			if (report_id == params->frame_list[i].id) {
				return uclogic_raw_event_frame(
					drvdata, &params->frame_list[i],
					data, size);
			}
		}

		break;
	}

	return 0;
}

static void uclogic_remove(struct hid_device *hdev)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);

	del_timer_sync(&drvdata->inrange_timer);
	hid_hw_stop(hdev);
	kfree(drvdata->desc_ptr);
	uclogic_params_cleanup(&drvdata->params);
}

static const struct hid_device_id uclogic_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_PF1209) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP4030U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP5540U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP8060U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP1062) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_WIRELESS_TABLET_TWHL850) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HUION,
				USB_DEVICE_ID_HUION_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_HUION,
				USB_DEVICE_ID_HUION_TABLET2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_TRUST,
				USB_DEVICE_ID_TRUST_PANORA_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_HUION_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_YIYNOVA_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_81) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_45) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_UGEE_TABLET_47) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_DRAWIMAGE_G3) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGTIZER,
				USB_DEVICE_ID_UGTIZER_TABLET_GP0610) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGTIZER,
				USB_DEVICE_ID_UGTIZER_TABLET_GT5040) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_PARBLO_A610_PRO) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_TABLET_G5) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_TABLET_EX07S) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_TABLET_RAINBOW_CV720) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_G540) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_G640) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO01) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO01_V2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_L) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_PRO_MW),
		.driver_data = UCLOGIC_MOUSE_FRAME_QUIRK | UCLOGIC_BATTERY_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_PRO_S) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_DECO_PRO_SW),
		.driver_data = UCLOGIC_MOUSE_FRAME_QUIRK | UCLOGIC_BATTERY_QUIRK },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UGEE,
				USB_DEVICE_ID_UGEE_XPPEN_TABLET_STAR06) },
	{ }
};
MODULE_DEVICE_TABLE(hid, uclogic_devices);

static struct hid_driver uclogic_driver = {
	.name = "uclogic",
	.id_table = uclogic_devices,
	.probe = uclogic_probe,
	.remove = uclogic_remove,
	.report_fixup = uclogic_report_fixup,
	.raw_event = uclogic_raw_event,
	.input_mapping = uclogic_input_mapping,
	.input_configured = uclogic_input_configured,
#ifdef CONFIG_PM
	.resume	          = uclogic_resume,
	.reset_resume     = uclogic_resume,
#endif
};
module_hid_driver(uclogic_driver);

MODULE_AUTHOR("Martin Rusko");
MODULE_AUTHOR("Nikolai Kondrashov");
MODULE_DESCRIPTION("HID driver for UC-Logic devices not fully compliant with HID standard");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HID driver for UC-Logic devices not fully compliant with HID standard");

#ifdef CONFIG_HID_KUNIT_TEST
#include "hid-uclogic-core-test.c"
#endif
