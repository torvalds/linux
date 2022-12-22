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

/* Driver data */
struct uclogic_drvdata {
	/* Interface parameters */
	struct uclogic_params params;
	/* Pointer to the replacement report descriptor. NULL if none. */
	__u8 *desc_ptr;
	/*
	 * Size of the replacement report descriptor.
	 * Only valid if desc_ptr is not NULL
	 */
	unsigned int desc_size;
	/* Pen input device */
	struct input_dev *pen_input;
	/* In-range timer */
	struct timer_list inrange_timer;
	/* Last rotary encoder state, or U8_MAX for none */
	u8 re_state;
};

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

static __u8 *uclogic_report_fixup(struct hid_device *hdev, __u8 *rdesc,
					unsigned int *rsize)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->desc_ptr != NULL) {
		rdesc = drvdata->desc_ptr;
		*rsize = drvdata->desc_size;
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

	/* discard the unused pen interface */
	if (params->pen_unused && (field->application == HID_DG_PEN))
		return -1;

	/* let hid-core decide what to do */
	return 0;
}

static int uclogic_input_configured(struct hid_device *hdev,
		struct hid_input *hi)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	struct uclogic_params *params = &drvdata->params;
	char *name;
	const char *suffix = NULL;
	struct hid_field *field;
	size_t len;

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
		suffix = "Pen";
		break;
	case HID_CP_CONSUMER_CONTROL:
		suffix = "Consumer Control";
		break;
	case HID_GD_SYSTEM_CONTROL:
		suffix = "System Control";
		break;
	}

	if (suffix) {
		len = strlen(hdev->name) + 2 + strlen(suffix);
		name = devm_kzalloc(&hi->input->dev, len, GFP_KERNEL);
		if (name) {
			snprintf(name, len, "%s %s", hdev->name, suffix);
			hi->input->name = name;
		}
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
	hid_set_drvdata(hdev, drvdata);

	/* Initialize the device and retrieve interface parameters */
	rc = uclogic_params_init(&drvdata->params, hdev);
	if (rc != 0) {
		hid_err(hdev, "failed probing parameters: %d\n", rc);
		goto failure;
	}
	params_initialized = true;
	hid_dbg(hdev, "parameters:\n" UCLOGIC_PARAMS_FMT_STR,
		UCLOGIC_PARAMS_FMT_ARGS(&drvdata->params));
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

static int uclogic_raw_event(struct hid_device *hdev,
				struct hid_report *report,
				u8 *data, int size)
{
	struct uclogic_drvdata *drvdata = hid_get_drvdata(hdev);
	struct uclogic_params *params = &drvdata->params;

	/* Tweak pen reports, if necessary */
	if (!params->pen_unused &&
	    (report->type == HID_INPUT_REPORT) &&
	    (report->id == params->pen.id) &&
	    (size >= 2)) {
		/* If it's the "virtual" frame controls report */
		if (params->frame.id != 0 &&
		    data[1] & params->pen_frame_flag) {
			/* Change to virtual frame controls report ID */
			data[0] = params->frame.id;
			return 0;
		}
		/* If in-range reports are inverted */
		if (params->pen.inrange ==
			UCLOGIC_PARAMS_PEN_INRANGE_INVERTED) {
			/* Invert the in-range bit */
			data[1] ^= 0x40;
		}
		/*
		 * If report contains fragmented high-resolution pen
		 * coordinates
		 */
		if (size >= 10 && params->pen.fragmented_hires) {
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
		if (params->pen.inrange == UCLOGIC_PARAMS_PEN_INRANGE_NONE) {
			/* Set in-range bit */
			data[1] |= 0x40;
			/* (Re-)start in-range timeout */
			mod_timer(&drvdata->inrange_timer,
					jiffies + msecs_to_jiffies(100));
		}
	}

	/* Tweak frame control reports, if necessary */
	if ((report->type == HID_INPUT_REPORT) &&
	    (report->id == params->frame.id)) {
		/* If need to, and can, set pad device ID for Wacom drivers */
		if (params->frame.dev_id_byte > 0 &&
		    params->frame.dev_id_byte < size) {
			data[params->frame.dev_id_byte] = 0xf;
		}
		/* If need to, and can, read rotary encoder state change */
		if (params->frame.re_lsb > 0 &&
		    params->frame.re_lsb / 8 < size) {
			unsigned int byte = params->frame.re_lsb / 8;
			unsigned int bit = params->frame.re_lsb % 8;

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
				USB_DEVICE_ID_HUION_HS64) },
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
MODULE_LICENSE("GPL");
