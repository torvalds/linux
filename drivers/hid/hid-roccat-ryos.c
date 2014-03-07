/*
 * Roccat Ryos driver for Linux
 *
 * Copyright (c) 2013 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
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
	RYOS_REPORT_NUMBER_SPECIAL = 3,
	RYOS_USB_INTERFACE_PROTOCOL = 0,
};

struct ryos_report_special {
	uint8_t number; /* RYOS_REPORT_NUMBER_SPECIAL */
	uint8_t data[4];
} __packed;

static struct class *ryos_class;

ROCCAT_COMMON2_BIN_ATTRIBUTE_W(control, 0x04, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(profile, 0x05, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(keys_primary, 0x06, 0x7d);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(keys_function, 0x07, 0x5f);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(keys_macro, 0x08, 0x23);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(keys_thumbster, 0x09, 0x17);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(keys_extra, 0x0a, 0x08);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(keys_easyzone, 0x0b, 0x126);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(key_mask, 0x0c, 0x06);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(light, 0x0d, 0x10);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(macro, 0x0e, 0x7d2);
ROCCAT_COMMON2_BIN_ATTRIBUTE_R(info, 0x0f, 0x08);
ROCCAT_COMMON2_BIN_ATTRIBUTE_W(reset, 0x11, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_W(light_control, 0x13, 0x08);
ROCCAT_COMMON2_BIN_ATTRIBUTE_W(talk, 0x16, 0x10);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(stored_lights, 0x17, 0x0566);
ROCCAT_COMMON2_BIN_ATTRIBUTE_W(custom_lights, 0x18, 0x14);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(light_macro, 0x19, 0x07d2);

static struct bin_attribute *ryos_bin_attrs[] = {
	&bin_attr_control,
	&bin_attr_profile,
	&bin_attr_keys_primary,
	&bin_attr_keys_function,
	&bin_attr_keys_macro,
	&bin_attr_keys_thumbster,
	&bin_attr_keys_extra,
	&bin_attr_keys_easyzone,
	&bin_attr_key_mask,
	&bin_attr_light,
	&bin_attr_macro,
	&bin_attr_info,
	&bin_attr_reset,
	&bin_attr_light_control,
	&bin_attr_talk,
	&bin_attr_stored_lights,
	&bin_attr_custom_lights,
	&bin_attr_light_macro,
	NULL,
};

static const struct attribute_group ryos_group = {
	.bin_attrs = ryos_bin_attrs,
};

static const struct attribute_group *ryos_groups[] = {
	&ryos_group,
	NULL,
};

static int ryos_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct roccat_common2_device *ryos;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= RYOS_USB_INTERFACE_PROTOCOL) {
		hid_set_drvdata(hdev, NULL);
		return 0;
	}

	ryos = kzalloc(sizeof(*ryos), GFP_KERNEL);
	if (!ryos) {
		hid_err(hdev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, ryos);

	retval = roccat_common2_device_init_struct(usb_dev, ryos);
	if (retval) {
		hid_err(hdev, "couldn't init Ryos device\n");
		goto exit_free;
	}

	retval = roccat_connect(ryos_class, hdev,
			sizeof(struct ryos_report_special));
	if (retval < 0) {
		hid_err(hdev, "couldn't init char dev\n");
	} else {
		ryos->chrdev_minor = retval;
		ryos->roccat_claimed = 1;
	}

	return 0;
exit_free:
	kfree(ryos);
	return retval;
}

static void ryos_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct roccat_common2_device *ryos;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= RYOS_USB_INTERFACE_PROTOCOL)
		return;

	ryos = hid_get_drvdata(hdev);
	if (ryos->roccat_claimed)
		roccat_disconnect(ryos->chrdev_minor);
	kfree(ryos);
}

static int ryos_probe(struct hid_device *hdev,
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

	retval = ryos_init_specials(hdev);
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

static void ryos_remove(struct hid_device *hdev)
{
	ryos_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static int ryos_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct roccat_common2_device *ryos = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= RYOS_USB_INTERFACE_PROTOCOL)
		return 0;

	if (data[0] != RYOS_REPORT_NUMBER_SPECIAL)
		return 0;

	if (ryos != NULL && ryos->roccat_claimed)
		roccat_report_event(ryos->chrdev_minor, data);

	return 0;
}

static const struct hid_device_id ryos_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_RYOS_MK) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_RYOS_MK_GLOW) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_RYOS_MK_PRO) },
	{ }
};

MODULE_DEVICE_TABLE(hid, ryos_devices);

static struct hid_driver ryos_driver = {
		.name = "ryos",
		.id_table = ryos_devices,
		.probe = ryos_probe,
		.remove = ryos_remove,
		.raw_event = ryos_raw_event
};

static int __init ryos_init(void)
{
	int retval;

	ryos_class = class_create(THIS_MODULE, "ryos");
	if (IS_ERR(ryos_class))
		return PTR_ERR(ryos_class);
	ryos_class->dev_groups = ryos_groups;

	retval = hid_register_driver(&ryos_driver);
	if (retval)
		class_destroy(ryos_class);
	return retval;
}

static void __exit ryos_exit(void)
{
	hid_unregister_driver(&ryos_driver);
	class_destroy(ryos_class);
}

module_init(ryos_init);
module_exit(ryos_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Ryos MK/Glow/Pro driver");
MODULE_LICENSE("GPL v2");
