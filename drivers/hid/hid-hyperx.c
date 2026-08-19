// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for the HyperX QuadCast 2 microphone
 *
 *  The tap-to-mute button is handled entirely in the device firmware: it gates
 *  the audio internally and does not send the Telephony "Phone Mute" usage its
 *  own report descriptor advertises.  The resulting mute state is only reported
 *  through a vendor-defined collection, which hid-input cannot map, so the
 *  button is invisible to userspace.
 *
 *  Copyright (c) 2026 Benjamin Blume <benjaminblume@posteo.de>
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>

#include "hid-ids.h"

#define HYPERX_QC2_REPORT_ID	0x77
#define HYPERX_QC2_EVENT_MUTE	0x06
#define HYPERX_QC2_MUTE_LEN	3

struct hyperx_drvdata {
	struct input_dev *input;
	bool muted;
	bool have_state;
};

static int hyperx_input_configured(struct hid_device *hdev,
				   struct hid_input *hi)
{
	struct hyperx_drvdata *drvdata = hid_get_drvdata(hdev);

	/*
	 * The device exposes several application collections.  Prefer the
	 * telephony one, which already advertises KEY_MICMUTE, and fall back
	 * to the first collection otherwise.
	 */
	if (!drvdata->input || test_bit(KEY_MICMUTE, hi->input->keybit)) {
		drvdata->input = hi->input;
		input_set_capability(drvdata->input, EV_KEY, KEY_MICMUTE);
	}

	return 0;
}

static int hyperx_raw_event(struct hid_device *hdev, struct hid_report *report,
			    u8 *data, int size)
{
	struct hyperx_drvdata *drvdata = hid_get_drvdata(hdev);
	bool muted;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !drvdata->input)
		return 0;

	if (size < HYPERX_QC2_MUTE_LEN || data[0] != HYPERX_QC2_REPORT_ID ||
	    data[1] != HYPERX_QC2_EVENT_MUTE)
		return 0;

	muted = data[2];
	if (drvdata->have_state && muted == drvdata->muted)
		return 0;

	drvdata->muted = muted;
	drvdata->have_state = true;

	/*
	 * The device reports the resulting absolute state, while KEY_MICMUTE is
	 * a momentary key that userspace acts on as a toggle.  Emit one
	 * keypress per state change.
	 */
	input_report_key(drvdata->input, KEY_MICMUTE, 1);
	input_sync(drvdata->input);
	input_report_key(drvdata->input, KEY_MICMUTE, 0);
	input_sync(drvdata->input);

	return 0;
}

static int hyperx_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hyperx_drvdata *drvdata;
	int ret;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	hid_set_drvdata(hdev, drvdata);

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

static const struct hid_device_id hyperx_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HP,
			 USB_PRODUCT_ID_HP_HYPERX_QUADCAST_2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, hyperx_devices);

static struct hid_driver hyperx_driver = {
	.name			= "hyperx",
	.id_table		= hyperx_devices,
	.probe			= hyperx_probe,
	.input_configured	= hyperx_input_configured,
	.raw_event		= hyperx_raw_event,
};
module_hid_driver(hyperx_driver);

MODULE_DESCRIPTION("HID driver for the HyperX QuadCast 2 microphone");
MODULE_AUTHOR("Benjamin Blume <benjaminblume@posteo.de>");
MODULE_LICENSE("GPL");
