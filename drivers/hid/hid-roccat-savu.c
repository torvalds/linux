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

static ssize_t savu_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct savu_device *savu = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&savu->savu_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&savu->savu_lock);

	return retval ? retval : real_size;
}

static ssize_t savu_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct savu_device *savu = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&savu->savu_lock);
	retval = roccat_common2_send_with_status(usb_dev, command,
			(void *)buf, real_size);
	mutex_unlock(&savu->savu_lock);

	return retval ? retval : real_size;
}

#define SAVU_SYSFS_W(thingy, THINGY) \
static ssize_t savu_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return savu_sysfs_write(fp, kobj, buf, off, count, \
			SAVU_SIZE_ ## THINGY, SAVU_COMMAND_ ## THINGY); \
}

#define SAVU_SYSFS_R(thingy, THINGY) \
static ssize_t savu_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return savu_sysfs_read(fp, kobj, buf, off, count, \
			SAVU_SIZE_ ## THINGY, SAVU_COMMAND_ ## THINGY); \
}

#define SAVU_SYSFS_RW(thingy, THINGY) \
SAVU_SYSFS_W(thingy, THINGY) \
SAVU_SYSFS_R(thingy, THINGY)

#define SAVU_BIN_ATTRIBUTE_RW(thingy, THINGY) \
{ \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = SAVU_SIZE_ ## THINGY, \
	.read = savu_sysfs_read_ ## thingy, \
	.write = savu_sysfs_write_ ## thingy \
}

#define SAVU_BIN_ATTRIBUTE_R(thingy, THINGY) \
{ \
	.attr = { .name = #thingy, .mode = 0440 }, \
	.size = SAVU_SIZE_ ## THINGY, \
	.read = savu_sysfs_read_ ## thingy, \
}

#define SAVU_BIN_ATTRIBUTE_W(thingy, THINGY) \
{ \
	.attr = { .name = #thingy, .mode = 0220 }, \
	.size = SAVU_SIZE_ ## THINGY, \
	.write = savu_sysfs_write_ ## thingy \
}

SAVU_SYSFS_W(control, CONTROL)
SAVU_SYSFS_RW(profile, PROFILE)
SAVU_SYSFS_RW(general, GENERAL)
SAVU_SYSFS_RW(buttons, BUTTONS)
SAVU_SYSFS_RW(macro, MACRO)
SAVU_SYSFS_R(info, INFO)
SAVU_SYSFS_RW(sensor, SENSOR)

static struct bin_attribute savu_bin_attributes[] = {
	SAVU_BIN_ATTRIBUTE_W(control, CONTROL),
	SAVU_BIN_ATTRIBUTE_RW(profile, PROFILE),
	SAVU_BIN_ATTRIBUTE_RW(general, GENERAL),
	SAVU_BIN_ATTRIBUTE_RW(buttons, BUTTONS),
	SAVU_BIN_ATTRIBUTE_RW(macro, MACRO),
	SAVU_BIN_ATTRIBUTE_R(info, INFO),
	SAVU_BIN_ATTRIBUTE_RW(sensor, SENSOR),
	__ATTR_NULL
};

static int savu_init_savu_device_struct(struct usb_device *usb_dev,
		struct savu_device *savu)
{
	mutex_init(&savu->savu_lock);

	return 0;
}

static int savu_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct savu_device *savu;
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

	retval = savu_init_savu_device_struct(usb_dev, savu);
	if (retval) {
		hid_err(hdev, "couldn't init struct savu_device\n");
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
	struct savu_device *savu;

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

static void savu_report_to_chrdev(struct savu_device const *savu,
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
	struct savu_device *savu = hid_get_drvdata(hdev);

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
	savu_class->dev_bin_attrs = savu_bin_attributes;

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
