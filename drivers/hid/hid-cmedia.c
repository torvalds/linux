// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID driver for CMedia CM6533 audio jack controls
 * and HS100B mute buttons
 *
 * Copyright (C) 2015 Ben Chen <ben_chen@bizlinktech.com>
 * Copyright (C) 2021 Thomas Weißschuh <linux@weissschuh.net>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

MODULE_AUTHOR("Ben Chen");
MODULE_AUTHOR("Thomas Weißschuh");
MODULE_DESCRIPTION("CM6533 HID jack controls and HS100B mute button");
MODULE_LICENSE("GPL");

#define CM6533_JD_TYPE_COUNT      1
#define CM6533_JD_RAWEV_LEN	 16
#define CM6533_JD_SFX_OFFSET	  8

#define HS100B_RDESC_ORIG_SIZE   60

/* Fixed report descriptor of HS-100B audio chip
 * Bit 4 is an abolute Microphone mute usage instead of being unassigned.
 */
static const __u8 hs100b_rdesc_fixed[] = {
	0x05, 0x0C,         /*  Usage Page (Consumer),          */
	0x09, 0x01,         /*  Usage (Consumer Control),       */
	0xA1, 0x01,         /*  Collection (Application),       */
	0x15, 0x00,         /*      Logical Minimum (0),        */
	0x25, 0x01,         /*      Logical Maximum (1),        */
	0x09, 0xE9,         /*      Usage (Volume Inc),         */
	0x09, 0xEA,         /*      Usage (Volume Dec),         */
	0x75, 0x01,         /*      Report Size (1),            */
	0x95, 0x02,         /*      Report Count (2),           */
	0x81, 0x02,         /*      Input (Variable),           */
	0x09, 0xE2,         /*      Usage (Mute),               */
	0x95, 0x01,         /*      Report Count (1),           */
	0x81, 0x06,         /*      Input (Variable, Relative), */
	0x05, 0x0B,         /*      Usage Page (Telephony),     */
	0x09, 0x2F,         /*      Usage (2Fh),                */
	0x81, 0x02,         /*      Input (Variable),           */
	0x09, 0x20,         /*      Usage (20h),                */
	0x81, 0x06,         /*      Input (Variable, Relative), */
	0x05, 0x0C,         /*      Usage Page (Consumer),      */
	0x09, 0x00,         /*      Usage (00h),                */
	0x95, 0x03,         /*      Report Count (3),           */
	0x81, 0x02,         /*      Input (Variable),           */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),      */
	0x09, 0x00,         /*      Usage (00h),                */
	0x75, 0x08,         /*      Report Size (8),            */
	0x95, 0x03,         /*      Report Count (3),           */
	0x81, 0x02,         /*      Input (Variable),           */
	0x09, 0x00,         /*      Usage (00h),                */
	0x95, 0x04,         /*      Report Count (4),           */
	0x91, 0x02,         /*      Output (Variable),          */
	0xC0                /*  End Collection                  */
};

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

static const __u8 *cmhid_hs100b_report_fixup(struct hid_device *hid, __u8 *rdesc,
				       unsigned int *rsize)
{
	if (*rsize == HS100B_RDESC_ORIG_SIZE) {
		hid_info(hid, "Fixing CMedia HS-100B report descriptor\n");
		*rsize = sizeof(hs100b_rdesc_fixed);
		return hs100b_rdesc_fixed;
	}
	return rdesc;
}

static const struct hid_device_id cmhid_hs100b_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CMEDIA, USB_DEVICE_ID_CMEDIA_HS100B) },
	{ }
};
MODULE_DEVICE_TABLE(hid, cmhid_hs100b_devices);

static struct hid_driver cmhid_hs100b_driver = {
	.name = "cmedia_hs100b",
	.id_table = cmhid_hs100b_devices,
	.report_fixup = cmhid_hs100b_report_fixup,
};

static int cmedia_init(void)
{
	int ret;

	ret = hid_register_driver(&cmhid_driver);
	if (ret)
		return ret;

	ret = hid_register_driver(&cmhid_hs100b_driver);
	if (ret)
		hid_unregister_driver(&cmhid_driver);

	return ret;
}
module_init(cmedia_init);

static void cmedia_exit(void)
{
		hid_unregister_driver(&cmhid_driver);
		hid_unregister_driver(&cmhid_hs100b_driver);
}
module_exit(cmedia_exit);
