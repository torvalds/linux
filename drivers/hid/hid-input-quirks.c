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

static const struct hid_input_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	int (*quirk)(struct hid_usage *, struct hid_input *, unsigned long **,
			int *);
} hid_input_blacklist[] = {
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
	return 0;
}


