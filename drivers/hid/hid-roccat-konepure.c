/*
 * Roccat KonePure driver for Linux
 *
 * Copyright (c) 2012 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Roccat KonePure is a smaller version of KoneXTD with less buttons and lights.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"

enum {
	KONEPURE_MOUSE_REPORT_NUMBER_BUTTON = 3,
};

struct konepure_mouse_report_button {
	uint8_t report_number; /* always KONEPURE_MOUSE_REPORT_NUMBER_BUTTON */
	uint8_t zero;
	uint8_t type;
	uint8_t data1;
	uint8_t data2;
	uint8_t zero2;
	uint8_t unknown[2];
} __packed;

static struct class *konepure_class;

ROCCAT_COMMON2_BIN_ATTRIBUTE_W(control, 0x04, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(actual_profile, 0x05, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(profile_settings, 0x06, 0x1f);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(profile_buttons, 0x07, 0x3b);
ROCCAT_COMMON2_BIN_ATTRIBUTE_W(macro, 0x08, 0x0822);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(info, 0x09, 0x06);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(tcu, 0x0c, 0x04);
ROCCAT_COMMON2_BIN_ATTRIBUTE_R(tcu_image, 0x0c, 0x0404);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(sensor, 0x0f, 0x06);
ROCCAT_COMMON2_BIN_ATTRIBUTE_W(talk, 0x10, 0x10);

static struct bin_attribute *konepure_bin_attrs[] = {
	&bin_attr_actual_profile,
	&bin_attr_control,
	&bin_attr_info,
	&bin_attr_talk,
	&bin_attr_macro,
	&bin_attr_sensor,
	&bin_attr_tcu,
	&bin_attr_tcu_image,
	&bin_attr_profile_settings,
	&bin_attr_profile_buttons,
	NULL,
};

static const struct attribute_group konepure_group = {
	.bin_attrs = konepure_bin_attrs,
};

static const struct attribute_group *konepure_groups[] = {
	&konepure_group,
	NULL,
};

static int konepure_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct roccat_common2_device *konepure;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE) {
		hid_set_drvdata(hdev, NULL);
		return 0;
	}

	konepure = kzalloc(sizeof(*konepure), GFP_KERNEL);
	if (!konepure) {
		hid_err(hdev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, konepure);

	retval = roccat_common2_device_init_struct(usb_dev, konepure);
	if (retval) {
		hid_err(hdev, "couldn't init KonePure device\n");
		goto exit_free;
	}

	retval = roccat_connect(konepure_class, hdev,
			sizeof(struct konepure_mouse_report_button));
	if (retval < 0) {
		hid_err(hdev, "couldn't init char dev\n");
	} else {
		konepure->chrdev_minor = retval;
		konepure->roccat_claimed = 1;
	}

	return 0;
exit_free:
	kfree(konepure);
	return retval;
}

static void konepure_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct roccat_common2_device *konepure;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return;

	konepure = hid_get_drvdata(hdev);
	if (konepure->roccat_claimed)
		roccat_disconnect(konepure->chrdev_minor);
	kfree(konepure);
}

static int konepure_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int retval;

	retval = hid_parse(hdev);
	if (retval) {
		hid_err(hdev, "parse failed\n");
		goto exit;
	}

	retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (retval) {
		hid_err(hdev, "hw start failed\n");
		goto exit;
	}

	retval = konepure_init_specials(hdev);
	if (retval) {
		hid_err(hdev, "couldn't install mouse\n");
		goto exit_stop;
	}

	return 0;

exit_stop:
	hid_hw_stop(hdev);
exit:
	return retval;
}

static void konepure_remove(struct hid_device *hdev)
{
	konepure_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static int konepure_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct roccat_common2_device *konepure = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return 0;

	if (data[0] != KONEPURE_MOUSE_REPORT_NUMBER_BUTTON)
		return 0;

	if (konepure != NULL && konepure->roccat_claimed)
		roccat_report_event(konepure->chrdev_minor, data);

	return 0;
}

static const struct hid_device_id konepure_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_KONEPURE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_KONEPURE_OPTICAL) },
	{ }
};

MODULE_DEVICE_TABLE(hid, konepure_devices);

static struct hid_driver konepure_driver = {
		.name = "konepure",
		.id_table = konepure_devices,
		.probe = konepure_probe,
		.remove = konepure_remove,
		.raw_event = konepure_raw_event
};

static int __init konepure_init(void)
{
	int retval;

	konepure_class = class_create(THIS_MODULE, "konepure");
	if (IS_ERR(konepure_class))
		return PTR_ERR(konepure_class);
	konepure_class->dev_groups = konepure_groups;

	retval = hid_register_driver(&konepure_driver);
	if (retval)
		class_destroy(konepure_class);
	return retval;
}

static void __exit konepure_exit(void)
{
	hid_unregister_driver(&konepure_driver);
	class_destroy(konepure_class);
}

module_init(konepure_init);
module_exit(konepure_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat KonePure/Optical driver");
MODULE_LICENSE("GPL v2");
