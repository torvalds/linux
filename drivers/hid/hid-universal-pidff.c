// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID UNIVERSAL PIDFF
 * hid-pidff wrapper for PID-enabled devices
 * Handles device reports, quirks and extends usable button range
 *
 * Copyright (c) 2024, 2025 Oleg Makarenko
 * Copyright (c) 2024, 2025 Tomasz Pakuła
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/input-event-codes.h>
#include "hid-ids.h"
#include "usbhid/hid-pidff.h"

#define JOY_RANGE (BTN_DEAD - BTN_JOYSTICK + 1)

/*
 * Map buttons manually to extend the default joystick button limit
 */
static int universal_pidff_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
		return 0;

	if (field->application != HID_GD_JOYSTICK)
		return 0;

	int button = ((usage->hid - 1) & HID_USAGE);
	int code = button + BTN_JOYSTICK;

	/* Detect the end of JOYSTICK buttons range */
	if (code > BTN_DEAD)
		code = button + KEY_NEXT_FAVORITE - JOY_RANGE;

	/*
	 * Map overflowing buttons to KEY_RESERVED to not ignore
	 * them and let them still trigger MSC_SCAN
	 */
	if (code > KEY_MAX)
		code = KEY_RESERVED;

	hid_map_usage(hi, usage, bit, max, EV_KEY, code);
	hid_dbg(hdev, "Button %d: usage %d", button, code);
	return 1;
}

/*
 * Check if the device is PID and initialize it
 * Add quirks after initialisation
 */
static int universal_pidff_probe(struct hid_device *hdev,
				 const struct hid_device_id *id)
{
	int i, error;
	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "HID parse failed\n");
		goto err;
	}

	error = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (error) {
		hid_err(hdev, "HID hw start failed\n");
		goto err;
	}

	/* Check if device contains PID usage page */
	error = 1;
	for (i = 0; i < hdev->collection_size; i++)
		if ((hdev->collection[i].usage & HID_USAGE_PAGE) == HID_UP_PID) {
			error = 0;
			hid_dbg(hdev, "PID usage page found\n");
			break;
		}

	/*
	 * Do not fail as this might be the second "device"
	 * just for additional buttons/axes. Exit cleanly if force
	 * feedback usage page wasn't found (included devices were
	 * tested and confirmed to be USB PID after all).
	 */
	if (error) {
		hid_dbg(hdev, "PID usage page not found in the descriptor\n");
		return 0;
	}

	/* Check if HID_PID support is enabled */
	int (*init_function)(struct hid_device *, u32);
	init_function = hid_pidff_init_with_quirks;

	if (!init_function) {
		hid_warn(hdev, "HID_PID support not enabled!\n");
		return 0;
	}

	error = init_function(hdev, id->driver_data);
	if (error) {
		hid_warn(hdev, "Error initialising force feedback\n");
		goto err;
	}

	hid_info(hdev, "Universal pidff driver loaded successfully!");

	return 0;
err:
	return error;
}

static int universal_pidff_input_configured(struct hid_device *hdev,
					    struct hid_input *hidinput)
{
	int axis;
	struct input_dev *input = hidinput->input;

	if (!input->absinfo)
		return 0;

	/* Decrease fuzz and deadzone on available axes */
	for (axis = ABS_X; axis <= ABS_BRAKE; axis++) {
		if (!test_bit(axis, input->absbit))
			continue;

		input_set_abs_params(input, axis,
			input->absinfo[axis].minimum,
			input->absinfo[axis].maximum,
			axis == ABS_X ? 0 : 8, 0);
	}

	/* Remove fuzz and deadzone from the second joystick axis */
	if (hdev->vendor == USB_VENDOR_ID_FFBEAST &&
	    hdev->product == USB_DEVICE_ID_FFBEAST_JOYSTICK)
		input_set_abs_params(input, ABS_Y,
			input->absinfo[ABS_Y].minimum,
			input->absinfo[ABS_Y].maximum, 0, 0);

	return 0;
}

static const struct hid_device_id universal_pidff_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R3),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R3_2),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R5),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R5_2),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R9),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R9_2),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R12),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R12_2),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R16_R21),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOZA, USB_DEVICE_ID_MOZA_R16_R21_2),
		.driver_data = HID_PIDFF_QUIRK_FIX_WHEEL_DIRECTION },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CAMMUS, USB_DEVICE_ID_CAMMUS_C5) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CAMMUS, USB_DEVICE_ID_CAMMUS_C12) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_VRS, USB_DEVICE_ID_VRS_DFP),
		.driver_data = HID_PIDFF_QUIRK_PERMISSIVE_CONTROL },
	{ HID_USB_DEVICE(USB_VENDOR_ID_FFBEAST, USB_DEVICE_ID_FFBEAST_JOYSTICK), },
	{ HID_USB_DEVICE(USB_VENDOR_ID_FFBEAST, USB_DEVICE_ID_FFBEAST_RUDDER), },
	{ HID_USB_DEVICE(USB_VENDOR_ID_FFBEAST, USB_DEVICE_ID_FFBEAST_WHEEL) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LITE_STAR, USB_DEVICE_ID_PXN_V10),
		.driver_data = HID_PIDFF_QUIRK_PERIODIC_SINE_ONLY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LITE_STAR, USB_DEVICE_ID_PXN_V12),
		.driver_data = HID_PIDFF_QUIRK_PERIODIC_SINE_ONLY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LITE_STAR, USB_DEVICE_ID_PXN_V12_LITE),
		.driver_data = HID_PIDFF_QUIRK_PERIODIC_SINE_ONLY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LITE_STAR, USB_DEVICE_ID_PXN_V12_LITE_2),
		.driver_data = HID_PIDFF_QUIRK_PERIODIC_SINE_ONLY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LITE_STAR, USB_DEVICE_LITE_STAR_GT987_FF),
		.driver_data = HID_PIDFF_QUIRK_PERIODIC_SINE_ONLY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASETEK, USB_DEVICE_ID_ASETEK_INVICTA) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASETEK, USB_DEVICE_ID_ASETEK_FORTE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASETEK, USB_DEVICE_ID_ASETEK_LA_PRIMA) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASETEK, USB_DEVICE_ID_ASETEK_TONY_KANAAN) },
	{ }
};
MODULE_DEVICE_TABLE(hid, universal_pidff_devices);

static struct hid_driver universal_pidff = {
	.name = "hid-universal-pidff",
	.id_table = universal_pidff_devices,
	.input_mapping = universal_pidff_input_mapping,
	.probe = universal_pidff_probe,
	.input_configured = universal_pidff_input_configured
};
module_hid_driver(universal_pidff);

MODULE_DESCRIPTION("Universal driver for USB PID Force Feedback devices");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oleg Makarenko <oleg@makarenk.ooo>");
MODULE_AUTHOR("Tomasz Pakuła <tomasz.pakula.oficjalny@gmail.com>");
