/*
 * Roccat Kone[+] driver for Linux
 *
 * Copyright (c) 2010 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Roccat Kone[+] is an updated/improved version of the Kone with more memory
 * and functionality and without the non-standard behaviours the Kone had.
 * KoneXTD has same capabilities but updated sensor.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-koneplus.h"

static uint profile_numbers[5] = {0, 1, 2, 3, 4};

static struct class *koneplus_class;

static void koneplus_profile_activated(struct koneplus_device *koneplus,
		uint new_profile)
{
	koneplus->actual_profile = new_profile;
}

static int koneplus_send_control(struct usb_device *usb_dev, uint value,
		enum koneplus_control_requests request)
{
	struct roccat_common2_control control;

	if ((request == KONEPLUS_CONTROL_REQUEST_PROFILE_SETTINGS ||
			request == KONEPLUS_CONTROL_REQUEST_PROFILE_BUTTONS) &&
			value > 4)
		return -EINVAL;

	control.command = ROCCAT_COMMON_COMMAND_CONTROL;
	control.value = value;
	control.request = request;

	return roccat_common2_send_with_status(usb_dev,
			ROCCAT_COMMON_COMMAND_CONTROL,
			&control, sizeof(struct roccat_common2_control));
}


/* retval is 0-4 on success, < 0 on error */
static int koneplus_get_actual_profile(struct usb_device *usb_dev)
{
	struct koneplus_actual_profile buf;
	int retval;

	retval = roccat_common2_receive(usb_dev, KONEPLUS_COMMAND_ACTUAL_PROFILE,
			&buf, KONEPLUS_SIZE_ACTUAL_PROFILE);

	return retval ? retval : buf.actual_profile;
}

static int koneplus_set_actual_profile(struct usb_device *usb_dev,
		int new_profile)
{
	struct koneplus_actual_profile buf;

	buf.command = KONEPLUS_COMMAND_ACTUAL_PROFILE;
	buf.size = KONEPLUS_SIZE_ACTUAL_PROFILE;
	buf.actual_profile = new_profile;

	return roccat_common2_send_with_status(usb_dev,
			KONEPLUS_COMMAND_ACTUAL_PROFILE,
			&buf, KONEPLUS_SIZE_ACTUAL_PROFILE);
}

static ssize_t koneplus_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct koneplus_device *koneplus = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&koneplus->koneplus_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&koneplus->koneplus_lock);

	if (retval)
		return retval;

	return real_size;
}

static ssize_t koneplus_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct koneplus_device *koneplus = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&koneplus->koneplus_lock);
	retval = roccat_common2_send_with_status(usb_dev, command,
			buf, real_size);
	mutex_unlock(&koneplus->koneplus_lock);

	if (retval)
		return retval;

	return real_size;
}

#define KONEPLUS_SYSFS_W(thingy, THINGY) \
static ssize_t koneplus_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return koneplus_sysfs_write(fp, kobj, buf, off, count, \
			KONEPLUS_SIZE_ ## THINGY, KONEPLUS_COMMAND_ ## THINGY); \
}

#define KONEPLUS_SYSFS_R(thingy, THINGY) \
static ssize_t koneplus_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return koneplus_sysfs_read(fp, kobj, buf, off, count, \
			KONEPLUS_SIZE_ ## THINGY, KONEPLUS_COMMAND_ ## THINGY); \
}

#define KONEPLUS_SYSFS_RW(thingy, THINGY) \
KONEPLUS_SYSFS_W(thingy, THINGY) \
KONEPLUS_SYSFS_R(thingy, THINGY)

#define KONEPLUS_BIN_ATTRIBUTE_RW(thingy, THINGY) \
KONEPLUS_SYSFS_RW(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = KONEPLUS_SIZE_ ## THINGY, \
	.read = koneplus_sysfs_read_ ## thingy, \
	.write = koneplus_sysfs_write_ ## thingy \
}

#define KONEPLUS_BIN_ATTRIBUTE_R(thingy, THINGY) \
KONEPLUS_SYSFS_R(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0440 }, \
	.size = KONEPLUS_SIZE_ ## THINGY, \
	.read = koneplus_sysfs_read_ ## thingy, \
}

#define KONEPLUS_BIN_ATTRIBUTE_W(thingy, THINGY) \
KONEPLUS_SYSFS_W(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0220 }, \
	.size = KONEPLUS_SIZE_ ## THINGY, \
	.write = koneplus_sysfs_write_ ## thingy \
}
KONEPLUS_BIN_ATTRIBUTE_W(control, CONTROL);
KONEPLUS_BIN_ATTRIBUTE_W(talk, TALK);
KONEPLUS_BIN_ATTRIBUTE_W(macro, MACRO);
KONEPLUS_BIN_ATTRIBUTE_R(tcu_image, TCU_IMAGE);
KONEPLUS_BIN_ATTRIBUTE_RW(info, INFO);
KONEPLUS_BIN_ATTRIBUTE_RW(sensor, SENSOR);
KONEPLUS_BIN_ATTRIBUTE_RW(tcu, TCU);
KONEPLUS_BIN_ATTRIBUTE_RW(profile_settings, PROFILE_SETTINGS);
KONEPLUS_BIN_ATTRIBUTE_RW(profile_buttons, PROFILE_BUTTONS);

static ssize_t koneplus_sysfs_read_profilex_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	ssize_t retval;

	retval = koneplus_send_control(usb_dev, *(uint *)(attr->private),
			KONEPLUS_CONTROL_REQUEST_PROFILE_SETTINGS);
	if (retval)
		return retval;

	return koneplus_sysfs_read(fp, kobj, buf, off, count,
			KONEPLUS_SIZE_PROFILE_SETTINGS,
			KONEPLUS_COMMAND_PROFILE_SETTINGS);
}

static ssize_t koneplus_sysfs_read_profilex_buttons(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	ssize_t retval;

	retval = koneplus_send_control(usb_dev, *(uint *)(attr->private),
			KONEPLUS_CONTROL_REQUEST_PROFILE_BUTTONS);
	if (retval)
		return retval;

	return koneplus_sysfs_read(fp, kobj, buf, off, count,
			KONEPLUS_SIZE_PROFILE_BUTTONS,
			KONEPLUS_COMMAND_PROFILE_BUTTONS);
}

#define PROFILE_ATTR(number)						\
static struct bin_attribute bin_attr_profile##number##_settings = {	\
	.attr = { .name = "profile" #number "_settings", .mode = 0440 },	\
	.size = KONEPLUS_SIZE_PROFILE_SETTINGS,				\
	.read = koneplus_sysfs_read_profilex_settings,			\
	.private = &profile_numbers[number-1],				\
};									\
static struct bin_attribute bin_attr_profile##number##_buttons = {	\
	.attr = { .name = "profile" #number "_buttons", .mode = 0440 },	\
	.size = KONEPLUS_SIZE_PROFILE_BUTTONS,				\
	.read = koneplus_sysfs_read_profilex_buttons,			\
	.private = &profile_numbers[number-1],				\
};
PROFILE_ATTR(1);
PROFILE_ATTR(2);
PROFILE_ATTR(3);
PROFILE_ATTR(4);
PROFILE_ATTR(5);

static ssize_t koneplus_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct koneplus_device *koneplus =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", koneplus->actual_profile);
}

static ssize_t koneplus_sysfs_set_actual_profile(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct koneplus_device *koneplus;
	struct usb_device *usb_dev;
	unsigned long profile;
	int retval;
	struct koneplus_roccat_report roccat_report;

	dev = dev->parent->parent;
	koneplus = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	retval = kstrtoul(buf, 10, &profile);
	if (retval)
		return retval;

	if (profile > 4)
		return -EINVAL;

	mutex_lock(&koneplus->koneplus_lock);

	retval = koneplus_set_actual_profile(usb_dev, profile);
	if (retval) {
		mutex_unlock(&koneplus->koneplus_lock);
		return retval;
	}

	koneplus_profile_activated(koneplus, profile);

	roccat_report.type = KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE;
	roccat_report.data1 = profile + 1;
	roccat_report.data2 = 0;
	roccat_report.profile = profile + 1;
	roccat_report_event(koneplus->chrdev_minor,
			(uint8_t const *)&roccat_report);

	mutex_unlock(&koneplus->koneplus_lock);

	return size;
}
static DEVICE_ATTR(actual_profile, 0660,
		   koneplus_sysfs_show_actual_profile,
		   koneplus_sysfs_set_actual_profile);
static DEVICE_ATTR(startup_profile, 0660,
		   koneplus_sysfs_show_actual_profile,
		   koneplus_sysfs_set_actual_profile);

static ssize_t koneplus_sysfs_show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct koneplus_device *koneplus;
	struct usb_device *usb_dev;
	struct koneplus_info info;

	dev = dev->parent->parent;
	koneplus = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	mutex_lock(&koneplus->koneplus_lock);
	roccat_common2_receive(usb_dev, KONEPLUS_COMMAND_INFO,
			&info, KONEPLUS_SIZE_INFO);
	mutex_unlock(&koneplus->koneplus_lock);

	return snprintf(buf, PAGE_SIZE, "%d\n", info.firmware_version);
}
static DEVICE_ATTR(firmware_version, 0440,
		   koneplus_sysfs_show_firmware_version, NULL);

static struct attribute *koneplus_attrs[] = {
	&dev_attr_actual_profile.attr,
	&dev_attr_startup_profile.attr,
	&dev_attr_firmware_version.attr,
	NULL,
};

static struct bin_attribute *koneplus_bin_attributes[] = {
	&bin_attr_control,
	&bin_attr_talk,
	&bin_attr_macro,
	&bin_attr_tcu_image,
	&bin_attr_info,
	&bin_attr_sensor,
	&bin_attr_tcu,
	&bin_attr_profile_settings,
	&bin_attr_profile_buttons,
	&bin_attr_profile1_settings,
	&bin_attr_profile2_settings,
	&bin_attr_profile3_settings,
	&bin_attr_profile4_settings,
	&bin_attr_profile5_settings,
	&bin_attr_profile1_buttons,
	&bin_attr_profile2_buttons,
	&bin_attr_profile3_buttons,
	&bin_attr_profile4_buttons,
	&bin_attr_profile5_buttons,
	NULL,
};

static const struct attribute_group koneplus_group = {
	.attrs = koneplus_attrs,
	.bin_attrs = koneplus_bin_attributes,
};

static const struct attribute_group *koneplus_groups[] = {
	&koneplus_group,
	NULL,
};

static int koneplus_init_koneplus_device_struct(struct usb_device *usb_dev,
		struct koneplus_device *koneplus)
{
	int retval;

	mutex_init(&koneplus->koneplus_lock);

	retval = koneplus_get_actual_profile(usb_dev);
	if (retval < 0)
		return retval;
	koneplus_profile_activated(koneplus, retval);

	return 0;
}

static int koneplus_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct koneplus_device *koneplus;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {

		koneplus = kzalloc(sizeof(*koneplus), GFP_KERNEL);
		if (!koneplus) {
			hid_err(hdev, "can't alloc device descriptor\n");
			return -ENOMEM;
		}
		hid_set_drvdata(hdev, koneplus);

		retval = koneplus_init_koneplus_device_struct(usb_dev, koneplus);
		if (retval) {
			hid_err(hdev, "couldn't init struct koneplus_device\n");
			goto exit_free;
		}

		retval = roccat_connect(koneplus_class, hdev,
				sizeof(struct koneplus_roccat_report));
		if (retval < 0) {
			hid_err(hdev, "couldn't init char dev\n");
		} else {
			koneplus->chrdev_minor = retval;
			koneplus->roccat_claimed = 1;
		}
	} else {
		hid_set_drvdata(hdev, NULL);
	}

	return 0;
exit_free:
	kfree(koneplus);
	return retval;
}

static void koneplus_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct koneplus_device *koneplus;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {
		koneplus = hid_get_drvdata(hdev);
		if (koneplus->roccat_claimed)
			roccat_disconnect(koneplus->chrdev_minor);
		kfree(koneplus);
	}
}

static int koneplus_probe(struct hid_device *hdev,
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

	retval = koneplus_init_specials(hdev);
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

static void koneplus_remove(struct hid_device *hdev)
{
	koneplus_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void koneplus_keep_values_up_to_date(struct koneplus_device *koneplus,
		u8 const *data)
{
	struct koneplus_mouse_report_button const *button_report;

	switch (data[0]) {
	case KONEPLUS_MOUSE_REPORT_NUMBER_BUTTON:
		button_report = (struct koneplus_mouse_report_button const *)data;
		switch (button_report->type) {
		case KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE:
			koneplus_profile_activated(koneplus, button_report->data1 - 1);
			break;
		}
		break;
	}
}

static void koneplus_report_to_chrdev(struct koneplus_device const *koneplus,
		u8 const *data)
{
	struct koneplus_roccat_report roccat_report;
	struct koneplus_mouse_report_button const *button_report;

	if (data[0] != KONEPLUS_MOUSE_REPORT_NUMBER_BUTTON)
		return;

	button_report = (struct koneplus_mouse_report_button const *)data;

	if ((button_report->type == KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_QUICKLAUNCH ||
			button_report->type == KONEPLUS_MOUSE_REPORT_BUTTON_TYPE_TIMER) &&
			button_report->data2 != KONEPLUS_MOUSE_REPORT_BUTTON_ACTION_PRESS)
		return;

	roccat_report.type = button_report->type;
	roccat_report.data1 = button_report->data1;
	roccat_report.data2 = button_report->data2;
	roccat_report.profile = koneplus->actual_profile + 1;
	roccat_report_event(koneplus->chrdev_minor,
			(uint8_t const *)&roccat_report);
}

static int koneplus_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct koneplus_device *koneplus = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return 0;

	if (koneplus == NULL)
		return 0;

	koneplus_keep_values_up_to_date(koneplus, data);

	if (koneplus->roccat_claimed)
		koneplus_report_to_chrdev(koneplus, data);

	return 0;
}

static const struct hid_device_id koneplus_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_KONEPLUS) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_KONEXTD) },
	{ }
};

MODULE_DEVICE_TABLE(hid, koneplus_devices);

static struct hid_driver koneplus_driver = {
		.name = "koneplus",
		.id_table = koneplus_devices,
		.probe = koneplus_probe,
		.remove = koneplus_remove,
		.raw_event = koneplus_raw_event
};

static int __init koneplus_init(void)
{
	int retval;

	/* class name has to be same as driver name */
	koneplus_class = class_create(THIS_MODULE, "koneplus");
	if (IS_ERR(koneplus_class))
		return PTR_ERR(koneplus_class);
	koneplus_class->dev_groups = koneplus_groups;

	retval = hid_register_driver(&koneplus_driver);
	if (retval)
		class_destroy(koneplus_class);
	return retval;
}

static void __exit koneplus_exit(void)
{
	hid_unregister_driver(&koneplus_driver);
	class_destroy(koneplus_class);
}

module_init(koneplus_init);
module_exit(koneplus_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Kone[+]/XTD driver");
MODULE_LICENSE("GPL v2");
