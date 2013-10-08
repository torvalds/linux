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

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-konepure.h"

static struct class *konepure_class;

static ssize_t konepure_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct konepure_device *konepure = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&konepure->konepure_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&konepure->konepure_lock);

	return retval ? retval : real_size;
}

static ssize_t konepure_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct konepure_device *konepure = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&konepure->konepure_lock);
	retval = roccat_common2_send_with_status(usb_dev, command,
			(void *)buf, real_size);
	mutex_unlock(&konepure->konepure_lock);

	return retval ? retval : real_size;
}

#define KONEPURE_SYSFS_W(thingy, THINGY) \
static ssize_t konepure_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return konepure_sysfs_write(fp, kobj, buf, off, count, \
			KONEPURE_SIZE_ ## THINGY, KONEPURE_COMMAND_ ## THINGY); \
}

#define KONEPURE_SYSFS_R(thingy, THINGY) \
static ssize_t konepure_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return konepure_sysfs_read(fp, kobj, buf, off, count, \
			KONEPURE_SIZE_ ## THINGY, KONEPURE_COMMAND_ ## THINGY); \
}

#define KONEPURE_SYSFS_RW(thingy, THINGY) \
KONEPURE_SYSFS_W(thingy, THINGY) \
KONEPURE_SYSFS_R(thingy, THINGY)

#define KONEPURE_BIN_ATTRIBUTE_RW(thingy, THINGY) \
KONEPURE_SYSFS_RW(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = KONEPURE_SIZE_ ## THINGY, \
	.read = konepure_sysfs_read_ ## thingy, \
	.write = konepure_sysfs_write_ ## thingy \
}

#define KONEPURE_BIN_ATTRIBUTE_R(thingy, THINGY) \
KONEPURE_SYSFS_R(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0440 }, \
	.size = KONEPURE_SIZE_ ## THINGY, \
	.read = konepure_sysfs_read_ ## thingy, \
}

#define KONEPURE_BIN_ATTRIBUTE_W(thingy, THINGY) \
KONEPURE_SYSFS_W(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0220 }, \
	.size = KONEPURE_SIZE_ ## THINGY, \
	.write = konepure_sysfs_write_ ## thingy \
}

KONEPURE_BIN_ATTRIBUTE_RW(actual_profile, ACTUAL_PROFILE);
KONEPURE_BIN_ATTRIBUTE_RW(info, INFO);
KONEPURE_BIN_ATTRIBUTE_RW(sensor, SENSOR);
KONEPURE_BIN_ATTRIBUTE_RW(tcu, TCU);
KONEPURE_BIN_ATTRIBUTE_RW(profile_settings, PROFILE_SETTINGS);
KONEPURE_BIN_ATTRIBUTE_RW(profile_buttons, PROFILE_BUTTONS);
KONEPURE_BIN_ATTRIBUTE_W(control, CONTROL);
KONEPURE_BIN_ATTRIBUTE_W(talk, TALK);
KONEPURE_BIN_ATTRIBUTE_W(macro, MACRO);
KONEPURE_BIN_ATTRIBUTE_R(tcu_image, TCU_IMAGE);

static struct bin_attribute *konepure_bin_attributes[] = {
	&bin_attr_actual_profile,
	&bin_attr_info,
	&bin_attr_sensor,
	&bin_attr_tcu,
	&bin_attr_profile_settings,
	&bin_attr_profile_buttons,
	&bin_attr_control,
	&bin_attr_talk,
	&bin_attr_macro,
	&bin_attr_tcu_image,
	NULL,
};

static const struct attribute_group konepure_group = {
	.bin_attrs = konepure_bin_attributes,
};

static const struct attribute_group *konepure_groups[] = {
	&konepure_group,
	NULL,
};


static int konepure_init_konepure_device_struct(struct usb_device *usb_dev,
		struct konepure_device *konepure)
{
	mutex_init(&konepure->konepure_lock);

	return 0;
}

static int konepure_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct konepure_device *konepure;
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

	retval = konepure_init_konepure_device_struct(usb_dev, konepure);
	if (retval) {
		hid_err(hdev, "couldn't init struct konepure_device\n");
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
	struct konepure_device *konepure;

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
	struct konepure_device *konepure = hid_get_drvdata(hdev);

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
