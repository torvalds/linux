// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Plantronics USB HID Driver
 *
 *  Copyright (c) 2014 JD Cole <jd.cole@plantronics.com>
 *  Copyright (c) 2015-2018 Terry Junge <terry.junge@plantronics.com>
 */

/*
 */

#include "hid-ids.h"

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/jiffies.h>

#define PLT_HID_1_0_PAGE	0xffa00000
#define PLT_HID_2_0_PAGE	0xffa20000

#define PLT_BASIC_TELEPHONY	0x0003
#define PLT_BASIC_EXCEPTION	0x0005

#define PLT_VOL_UP		0x00b1
#define PLT_VOL_DOWN		0x00b2

#define PLT1_VOL_UP		(PLT_HID_1_0_PAGE | PLT_VOL_UP)
#define PLT1_VOL_DOWN		(PLT_HID_1_0_PAGE | PLT_VOL_DOWN)
#define PLT2_VOL_UP		(PLT_HID_2_0_PAGE | PLT_VOL_UP)
#define PLT2_VOL_DOWN		(PLT_HID_2_0_PAGE | PLT_VOL_DOWN)

#define PLT_DA60		0xda60
#define PLT_BT300_MIN		0x0413
#define PLT_BT300_MAX		0x0418


#define PLT_ALLOW_CONSUMER (field->application == HID_CP_CONSUMERCONTROL && \
			    (usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER)

#define PLT_QUIRK_DOUBLE_VOLUME_KEYS BIT(0)
#define PLT_QUIRK_FOLLOWED_OPPOSITE_VOLUME_KEYS BIT(1)

#define PLT_DOUBLE_KEY_TIMEOUT 5 /* ms */
#define PLT_FOLLOWED_OPPOSITE_KEY_TIMEOUT 220 /* ms */

struct plt_drv_data {
	unsigned long device_type;
	unsigned long last_volume_key_ts;
	u32 quirks;
};

static int plantronics_input_mapping(struct hid_device *hdev,
				     struct hid_input *hi,
				     struct hid_field *field,
				     struct hid_usage *usage,
				     unsigned long **bit, int *max)
{
	unsigned short mapped_key;
	struct plt_drv_data *drv_data = hid_get_drvdata(hdev);
	unsigned long plt_type = drv_data->device_type;

	/* special case for PTT products */
	if (field->application == HID_GD_JOYSTICK)
		goto defaulted;

	/* handle volume up/down mapping */
	/* non-standard types or multi-HID interfaces - plt_type is PID */
	if (!(plt_type & HID_USAGE_PAGE)) {
		switch (plt_type) {
		case PLT_DA60:
			if (PLT_ALLOW_CONSUMER)
				goto defaulted;
			goto ignored;
		default:
			if (PLT_ALLOW_CONSUMER)
				goto defaulted;
		}
	}
	/* handle standard types - plt_type is 0xffa0uuuu or 0xffa2uuuu */
	/* 'basic telephony compliant' - allow default consumer page map */
	else if ((plt_type & HID_USAGE) >= PLT_BASIC_TELEPHONY &&
		 (plt_type & HID_USAGE) != PLT_BASIC_EXCEPTION) {
		if (PLT_ALLOW_CONSUMER)
			goto defaulted;
	}
	/* not 'basic telephony' - apply legacy mapping */
	/* only map if the field is in the device's primary vendor page */
	else if (!((field->application ^ plt_type) & HID_USAGE_PAGE)) {
		switch (usage->hid) {
		case PLT1_VOL_UP:
		case PLT2_VOL_UP:
			mapped_key = KEY_VOLUMEUP;
			goto mapped;
		case PLT1_VOL_DOWN:
		case PLT2_VOL_DOWN:
			mapped_key = KEY_VOLUMEDOWN;
			goto mapped;
		}
	}

/*
 * Future mapping of call control or other usages,
 * if and when keys are defined would go here
 * otherwise, ignore everything else that was not mapped
 */

ignored:
	return -1;

defaulted:
	hid_dbg(hdev, "usage: %08x (appl: %08x) - defaulted\n",
		usage->hid, field->application);
	return 0;

mapped:
	hid_map_usage_clear(hi, usage, bit, max, EV_KEY, mapped_key);
	hid_dbg(hdev, "usage: %08x (appl: %08x) - mapped to key %d\n",
		usage->hid, field->application, mapped_key);
	return 1;
}

static int plantronics_event(struct hid_device *hdev, struct hid_field *field,
			     struct hid_usage *usage, __s32 value)
{
	struct plt_drv_data *drv_data = hid_get_drvdata(hdev);

	if (drv_data->quirks & PLT_QUIRK_DOUBLE_VOLUME_KEYS) {
		unsigned long prev_ts, cur_ts;

		/* Usages are filtered in plantronics_usages. */

		if (!value) /* Handle key presses only. */
			return 0;

		prev_ts = drv_data->last_volume_key_ts;
		cur_ts = jiffies;
		if (jiffies_to_msecs(cur_ts - prev_ts) <= PLT_DOUBLE_KEY_TIMEOUT)
			return 1; /* Ignore the repeated key. */

		drv_data->last_volume_key_ts = cur_ts;
	}
	if (drv_data->quirks & PLT_QUIRK_FOLLOWED_OPPOSITE_VOLUME_KEYS) {
		unsigned long prev_ts, cur_ts;

		/* Usages are filtered in plantronics_usages. */

		if (!value) /* Handle key presses only. */
			return 0;

		prev_ts = drv_data->last_volume_key_ts;
		cur_ts = jiffies;
		if (jiffies_to_msecs(cur_ts - prev_ts) <= PLT_FOLLOWED_OPPOSITE_KEY_TIMEOUT)
			return 1; /* Ignore the followed opposite volume key. */

		drv_data->last_volume_key_ts = cur_ts;
	}

	return 0;
}

static unsigned long plantronics_device_type(struct hid_device *hdev)
{
	unsigned i, col_page;
	unsigned long plt_type = hdev->product;

	/* multi-HID interfaces? - plt_type is PID */
	if (plt_type >= PLT_BT300_MIN && plt_type <= PLT_BT300_MAX)
		goto exit;

	/* determine primary vendor page */
	for (i = 0; i < hdev->maxcollection; i++) {
		col_page = hdev->collection[i].usage & HID_USAGE_PAGE;
		if (col_page == PLT_HID_2_0_PAGE) {
			plt_type = hdev->collection[i].usage;
			break;
		}
		if (col_page == PLT_HID_1_0_PAGE)
			plt_type = hdev->collection[i].usage;
	}

exit:
	hid_dbg(hdev, "plt_type decoded as: %08lx\n", plt_type);
	return plt_type;
}

static int plantronics_probe(struct hid_device *hdev,
			     const struct hid_device_id *id)
{
	struct plt_drv_data *drv_data;
	int ret;

	drv_data = devm_kzalloc(&hdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	drv_data->device_type = plantronics_device_type(hdev);
	drv_data->quirks = id->driver_data;
	drv_data->last_volume_key_ts = jiffies - msecs_to_jiffies(PLT_DOUBLE_KEY_TIMEOUT);

	hid_set_drvdata(hdev, drv_data);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT |
		HID_CONNECT_HIDINPUT_FORCE | HID_CONNECT_HIDDEV_FORCE);
	if (ret)
		hid_err(hdev, "hw start failed\n");

err:
	return ret;
}

static const struct hid_device_id plantronics_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS,
					 USB_DEVICE_ID_PLANTRONICS_BLACKWIRE_3210_SERIES),
		.driver_data = PLT_QUIRK_DOUBLE_VOLUME_KEYS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS,
					 USB_DEVICE_ID_PLANTRONICS_BLACKWIRE_3220_SERIES),
		.driver_data = PLT_QUIRK_DOUBLE_VOLUME_KEYS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS,
					 USB_DEVICE_ID_PLANTRONICS_BLACKWIRE_3215_SERIES),
		.driver_data = PLT_QUIRK_DOUBLE_VOLUME_KEYS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS,
					 USB_DEVICE_ID_PLANTRONICS_BLACKWIRE_3225_SERIES),
		.driver_data = PLT_QUIRK_DOUBLE_VOLUME_KEYS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS,
					 USB_DEVICE_ID_PLANTRONICS_BLACKWIRE_3325_SERIES),
		.driver_data = PLT_QUIRK_FOLLOWED_OPPOSITE_VOLUME_KEYS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS,
					 USB_DEVICE_ID_PLANTRONICS_ENCOREPRO_500_SERIES),
		.driver_data = PLT_QUIRK_FOLLOWED_OPPOSITE_VOLUME_KEYS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PLANTRONICS, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, plantronics_devices);

static const struct hid_usage_id plantronics_usages[] = {
	{ HID_CP_VOLUMEUP, EV_KEY, HID_ANY_ID },
	{ HID_CP_VOLUMEDOWN, EV_KEY, HID_ANY_ID },
	{ HID_TERMINATOR, HID_TERMINATOR, HID_TERMINATOR }
};

static struct hid_driver plantronics_driver = {
	.name = "plantronics",
	.id_table = plantronics_devices,
	.usage_table = plantronics_usages,
	.input_mapping = plantronics_input_mapping,
	.event = plantronics_event,
	.probe = plantronics_probe,
};
module_hid_driver(plantronics_driver);

MODULE_AUTHOR("JD Cole <jd.cole@plantronics.com>");
MODULE_AUTHOR("Terry Junge <terry.junge@plantronics.com>");
MODULE_DESCRIPTION("Plantronics USB HID Driver");
MODULE_LICENSE("GPL");
