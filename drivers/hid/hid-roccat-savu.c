/*
 * Roccat Savu driver for Linux
 *
 * Copyright (c) 2012 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* Roccat Savu is a gamer mouse with macro keys that can be configured in
 * 5 profiles.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-savu.h"

static struct class *savu_class;

ROCCAT_COMMON2_BIN_ATTRIBUTE_W(control, 0x4, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(profile, 0x5, 0x03);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(general, 0x6, 0x10);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(buttons, 0x7, 0x2f);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(macro, 0x8, 0x0823);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(info, 0x9, 0x08);
ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(sensor, 0xc, 0x04);

static struct bin_attribute *savu_bin_attrs[] = {
	&bin_attr_control,
	&bin_attr_profile,
	&bin_attr_general,
	&bin_attr_buttons,
	&bin_attr_macro,
	&bin_attr_info,
	&bin_attr_sensor,
	NULL,
};

static const struct attribute_group savu_group = {
	.bin_attrs = savu_bin_attrs,
};

static const struct attribute_group *savu_groups[] = {
	&savu_group,
	NULL,
};

static int savu_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct roccat_common2_device *savu;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE) {
		hid_set_drvdata(hdev, NULL);
		return 0;
	}

	savu = kzalloc(sizeof(*savu), GFP_KERNEL);
	if (!savu) {
		hid_err(hdev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, savu);

	retval = roccat_common2_device_init_struct(usb_dev, savu);
	if (retval) {
		hid_err(hdev, "couldn't init Savu device\n");
		goto exit_free;
	}

	retval = roccat_connect(savu_class, hdev,
			sizeof(struct savu_roccat_report));
	if (retval < 0) {
		hid_err(hdev, "couldn't init char dev\n");
	} else {
		savu->chrdev_minor = retval;
		savu->roccat_claimed = 1;
	}

	return 0;
exit_free:
	kfree(savu);
	return retval;
}

static void savu_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct roccat_common2_device *savu;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return;

	savu = hid_get_drvdata(hdev);
	if (savu->roccat_claimed)
		roccat_disconnect(savu->chrdev_minor);
	kfree(savu);
}

static int savu_probe(struct hid_device *hdev,
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

	retval = savu_init_specials(hdev);
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

static void savu_remove(struct hid_device *hdev)
{
	savu_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void savu_report_to_chrdev(struct roccat_common2_device const *savu,
		u8 const *data)
{
	struct savu_roccat_report roccat_report;
	struct savu_mouse_report_special const *special_report;

	if (data[0] != SAVU_MOUSE_REPORT_NUMBER_SPECIAL)
		return;

	special_report = (struct savu_mouse_report_special const *)data;

	roccat_report.type = special_report->type;
	roccat_report.data[0] = special_report->data[0];
	roccat_report.data[1] = special_report->data[1];
	roccat_report_event(savu->chrdev_minor,
			(uint8_t const *)&roccat_report);
}

static int savu_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct roccat_common2_device *savu = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return 0;

	if (savu == NULL)
		return 0;

	if (savu->roccat_claimed)
		savu_report_to_chrdev(savu, data);

	return 0;
}

static const struct hid_device_id savu_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_SAVU) },
	{ }
};

MODULE_DEVICE_TABLE(hid, savu_devices);

static struct hid_driver savu_driver = {
		.name = "savu",
		.id_table = savu_devices,
		.probe = savu_probe,
		.remove = savu_remove,
		.raw_event = savu_raw_event
};

static int __init savu_init(void)
{
	int retval;

	savu_class = class_create(THIS_MODULE, "savu");
	if (IS_ERR(savu_class))
		return PTR_ERR(savu_class);
	savu_class->dev_groups = savu_groups;

	retval = hid_register_driver(&savu_driver);
	if (retval)
		class_destroy(savu_class);
	return retval;
}

static void __exit savu_exit(void)
{
	hid_unregister_driver(&savu_driver);
	class_destroy(savu_class);
}

module_init(savu_init);
module_exit(savu_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Savu driver");
MODULE_LICENSE("GPL v2");
