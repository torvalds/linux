/*
 *  HID-input usage mapping quirks
 *
 *  This is used to handle HID-input mappings for devices violating
 *  HUT 1.12 specification.
 *
 * Copyright (c) 2007-2008 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License
 */

#include <linux/input.h>
#include <linux/hid.h>

#define map_key_clear(c)	hid_map_usage_clear(hidinput, usage, bit, \
		max, EV_KEY, (c))

static int quirk_gyration_remote(struct hid_usage *usage,
		struct hid_input *hidinput, unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	set_bit(EV_REP, hidinput->input->evbit);
	switch(usage->hid & HID_USAGE) {
		/* Reported on Gyration MCE Remote */
		case 0x00d: map_key_clear(KEY_HOME);		break;
		case 0x024: map_key_clear(KEY_DVD);		break;
		case 0x025: map_key_clear(KEY_PVR);		break;
		case 0x046: map_key_clear(KEY_MEDIA);		break;
		case 0x047: map_key_clear(KEY_MP3);		break;
		case 0x049: map_key_clear(KEY_CAMERA);		break;
		case 0x04a: map_key_clear(KEY_VIDEO);		break;

		default:
			return 0;
	}
	return 1;
}

#define VENDOR_ID_GYRATION			0x0c16
#define DEVICE_ID_GYRATION_REMOTE		0x0002

static const struct hid_input_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	int (*quirk)(struct hid_usage *, struct hid_input *, unsigned long **,
			int *);
} hid_input_blacklist[] = {
	{ VENDOR_ID_GYRATION, DEVICE_ID_GYRATION_REMOTE, quirk_gyration_remote },

	{ 0, 0, NULL }
};

int hidinput_mapping_quirks(struct hid_usage *usage, 
		struct hid_input *hi, unsigned long **bit, int *max)
{
	struct hid_device *device = input_get_drvdata(hi->input);
	int i = 0;
	
	while (hid_input_blacklist[i].quirk) {
		if (hid_input_blacklist[i].idVendor == device->vendor &&
				hid_input_blacklist[i].idProduct == device->product)
			return hid_input_blacklist[i].quirk(usage, hi, bit,
					max);
		i++;
	}
	return 0;
}

int hidinput_event_quirks(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct input_dev *input;

	input = field->hidinput->input;

	/* Gyration MCE remote "Sleep" key */
	if (hid->vendor == VENDOR_ID_GYRATION &&
	    hid->product == DEVICE_ID_GYRATION_REMOTE &&
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_GENDESK &&
	    (usage->hid & 0xff) == 0x82) {
		input_event(input, usage->type, usage->code, 1);
		input_sync(input);
		input_event(input, usage->type, usage->code, 0);
		input_sync(input);
		return 1;
	}
	return 0;
}


