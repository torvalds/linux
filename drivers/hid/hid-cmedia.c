/*
 * HID driver for CMedia CM6533 audio jack controls
 *
 * Copyright (C) 2015 Ben Chen <ben_chen@bizlinktech.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

MODULE_AUTHOR("Ben Chen");
MODULE_DESCRIPTION("CM6533 HID jack controls");
MODULE_LICENSE("GPL");

#define CM6533_JD_TYPE_COUNT      1
#define CM6533_JD_RAWEV_LEN	 16
#define CM6533_JD_SFX_OFFSET	  8

/*
*
*CM6533 audio jack HID raw events:
*
*Plug in:
*01000600 002083xx 080008c0 10000000
*about 3 seconds later...
*01000a00 002083xx 08000380 10000000
*01000600 002083xx 08000380 10000000
*
*Plug out:
*01000400 002083xx 080008c0 x0000000
*/

static const u8 ji_sfx[] = { 0x08, 0x00, 0x08, 0xc0 };
static const u8 ji_in[]  = { 0x01, 0x00, 0x06, 0x00 };
static const u8 ji_out[] = { 0x01, 0x00, 0x04, 0x00 };

static int jack_switch_types[CM6533_JD_TYPE_COUNT] = {
	SW_HEADPHONE_INSERT,
};

struct cmhid {
	struct input_dev *input_dev;
	struct hid_device *hid;
	unsigned short switch_map[CM6533_JD_TYPE_COUNT];
};

static void hp_ev(struct hid_device *hid, struct cmhid *cm, int value)
{
	input_report_switch(cm->input_dev, SW_HEADPHONE_INSERT, value);
	input_sync(cm->input_dev);
}

static int cmhid_raw_event(struct hid_device *hid, struct hid_report *report,
	 u8 *data, int len)
{
	struct cmhid *cm = hid_get_drvdata(hid);

	if (len != CM6533_JD_RAWEV_LEN)
		goto out;
	if (memcmp(data+CM6533_JD_SFX_OFFSET, ji_sfx, sizeof(ji_sfx)))
		goto out;

	if (!memcmp(data, ji_out, sizeof(ji_out))) {
		hp_ev(hid, cm, 0);
		goto out;
	}
	if (!memcmp(data, ji_in, sizeof(ji_in))) {
		hp_ev(hid, cm, 1);
		goto out;
	}

out:
	return 0;
}

static int cmhid_input_configured(struct hid_device *hid,
		struct hid_input *hidinput)
{
	struct input_dev *input_dev = hidinput->input;
	struct cmhid *cm = hid_get_drvdata(hid);
	int i;

	cm->input_dev = input_dev;
	memcpy(cm->switch_map, jack_switch_types, sizeof(cm->switch_map));
	input_dev->evbit[0] = BIT(EV_SW);
	for (i = 0; i < CM6533_JD_TYPE_COUNT; i++)
		input_set_capability(cm->input_dev,
				EV_SW, jack_switch_types[i]);
	return 0;
}

static int cmhid_input_mapping(struct hid_device *hid,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	return -1;
}

static int cmhid_probe(struct hid_device *hid, const struct hid_device_id *id)
{
	int ret;
	struct cmhid *cm;

	cm = kzalloc(sizeof(struct cmhid), GFP_KERNEL);
	if (!cm) {
		ret = -ENOMEM;
		goto allocfail;
	}

	cm->hid = hid;

	hid->quirks |= HID_QUIRK_HIDINPUT_FORCE;
	hid_set_drvdata(hid, cm);

	ret = hid_parse(hid);
	if (ret) {
		hid_err(hid, "parse failed\n");
		goto fail;
	}

	ret = hid_hw_start(hid, HID_CONNECT_DEFAULT | HID_CONNECT_HIDDEV_FORCE);
	if (ret) {
		hid_err(hid, "hw start failed\n");
		goto fail;
	}

	return 0;
fail:
	kfree(cm);
allocfail:
	return ret;
}

static void cmhid_remove(struct hid_device *hid)
{
	struct cmhid *cm = hid_get_drvdata(hid);

	hid_hw_stop(hid);
	kfree(cm);
}

static const struct hid_device_id cmhid_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CMEDIA, USB_DEVICE_ID_CM6533) },
	{ }
};
MODULE_DEVICE_TABLE(hid, cmhid_devices);

static struct hid_driver cmhid_driver = {
	.name = "cm6533_jd",
	.id_table = cmhid_devices,
	.raw_event = cmhid_raw_event,
	.input_configured = cmhid_input_configured,
	.probe = cmhid_probe,
	.remove = cmhid_remove,
	.input_mapping = cmhid_input_mapping,
};
module_hid_driver(cmhid_driver);

