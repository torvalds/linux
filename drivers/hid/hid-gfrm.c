// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for Google Fiber TV Box remote controls
 *
 * Copyright (c) 2014-2015 Google Inc.
 *
 * Author: Petri Gynther <pgynther@google.com>
 */
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>

#include "hid-ids.h"

#define GFRM100  1  /* Google Fiber GFRM100 (Bluetooth classic) */
#define GFRM200  2  /* Google Fiber GFRM200 (Bluetooth LE) */

#define GFRM100_SEARCH_KEY_REPORT_ID   0xF7
#define GFRM100_SEARCH_KEY_DOWN        0x0
#define GFRM100_SEARCH_KEY_AUDIO_DATA  0x1
#define GFRM100_SEARCH_KEY_UP          0x2

static u8 search_key_dn[3] = {0x40, 0x21, 0x02};
static u8 search_key_up[3] = {0x40, 0x00, 0x00};

static int gfrm_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	unsigned long hdev_type = (unsigned long) hid_get_drvdata(hdev);

	if (hdev_type == GFRM100) {
		if (usage->hid == (HID_UP_CONSUMER | 0x4)) {
			/* Consumer.0004 -> KEY_INFO */
			hid_map_usage_clear(hi, usage, bit, max, EV_KEY, KEY_INFO);
			return 1;
		}

		if (usage->hid == (HID_UP_CONSUMER | 0x41)) {
			/* Consumer.0041 -> KEY_OK */
			hid_map_usage_clear(hi, usage, bit, max, EV_KEY, KEY_OK);
			return 1;
		}
	}

	return 0;
}

static int gfrm_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	unsigned long hdev_type = (unsigned long) hid_get_drvdata(hdev);
	int ret = 0;

	if (hdev_type != GFRM100)
		return 0;

	if (size < 2 || data[0] != GFRM100_SEARCH_KEY_REPORT_ID)
		return 0;

	/*
	 * Convert GFRM100 Search key reports into Consumer.0221 (Key.Search)
	 * reports. Ignore audio data.
	 */
	switch (data[1]) {
	case GFRM100_SEARCH_KEY_DOWN:
		ret = hid_report_raw_event(hdev, HID_INPUT_REPORT, search_key_dn,
					   sizeof(search_key_dn), 1);
		break;

	case GFRM100_SEARCH_KEY_AUDIO_DATA:
		break;

	case GFRM100_SEARCH_KEY_UP:
		ret = hid_report_raw_event(hdev, HID_INPUT_REPORT, search_key_up,
					   sizeof(search_key_up), 1);
		break;

	default:
		break;
	}

	return (ret < 0) ? ret : -1;
}

static int gfrm_input_configured(struct hid_device *hid, struct hid_input *hidinput)
{
	/*
	 * Enable software autorepeat with:
	 * - repeat delay: 400 msec
	 * - repeat period: 100 msec
	 */
	input_enable_softrepeat(hidinput->input, 400, 100);
	return 0;
}

static int gfrm_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	hid_set_drvdata(hdev, (void *) id->driver_data);

	ret = hid_parse(hdev);
	if (ret)
		goto done;

	if (id->driver_data == GFRM100) {
		/*
		 * GFRM100 HID Report Descriptor does not describe the Search
		 * key reports. Thus, we need to add it manually here, so that
		 * those reports reach gfrm_raw_event() from hid_input_report().
		 */
		if (!hid_register_report(hdev, HID_INPUT_REPORT,
					 GFRM100_SEARCH_KEY_REPORT_ID, 0)) {
			ret = -ENOMEM;
			goto done;
		}
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
done:
	return ret;
}

static void gfrm_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	hid_set_drvdata(hdev, NULL);
}

static const struct hid_device_id gfrm_devices[] = {
	{ HID_BLUETOOTH_DEVICE(0x58, 0x2000),
		.driver_data = GFRM100 },
	{ HID_BLUETOOTH_DEVICE(0x471, 0x2210),
		.driver_data = GFRM200 },
	{ }
};
MODULE_DEVICE_TABLE(hid, gfrm_devices);

static struct hid_driver gfrm_driver = {
	.name = "gfrm",
	.id_table = gfrm_devices,
	.probe = gfrm_probe,
	.remove = gfrm_remove,
	.input_mapping = gfrm_input_mapping,
	.raw_event = gfrm_raw_event,
	.input_configured = gfrm_input_configured,
};

module_hid_driver(gfrm_driver);

MODULE_AUTHOR("Petri Gynther <pgynther@google.com>");
MODULE_DESCRIPTION("Google Fiber TV Box remote control driver");
MODULE_LICENSE("GPL");
