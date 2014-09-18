/*
 * Roccat Pyra driver for Linux
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
 * Roccat Pyra is a mobile gamer mouse which comes in wired and wireless
 * variant. Wireless variant is not tested.
 * Userland tools can be found at http://sourceforge.net/projects/roccat
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-pyra.h"

static uint profile_numbers[5] = {0, 1, 2, 3, 4};

/* pyra_class is used for creating sysfs attributes via roccat char device */
static struct class *pyra_class;

static void profile_activated(struct pyra_device *pyra,
		unsigned int new_profile)
{
	pyra->actual_profile = new_profile;
	pyra->actual_cpi = pyra->profile_settings[pyra->actual_profile].y_cpi;
}

static int pyra_send_control(struct usb_device *usb_dev, int value,
		enum pyra_control_requests request)
{
	struct roccat_common2_control control;

	if ((request == PYRA_CONTROL_REQUEST_PROFILE_SETTINGS ||
			request == PYRA_CONTROL_REQUEST_PROFILE_BUTTONS) &&
			(value < 0 || value > 4))
		return -EINVAL;

	control.command = ROCCAT_COMMON_COMMAND_CONTROL;
	control.value = value;
	control.request = request;

	return roccat_common2_send(usb_dev, ROCCAT_COMMON_COMMAND_CONTROL,
			&control, sizeof(struct roccat_common2_control));
}

static int pyra_get_profile_settings(struct usb_device *usb_dev,
		struct pyra_profile_settings *buf, int number)
{
	int retval;
	retval = pyra_send_control(usb_dev, number,
			PYRA_CONTROL_REQUEST_PROFILE_SETTINGS);
	if (retval)
		return retval;
	return roccat_common2_receive(usb_dev, PYRA_COMMAND_PROFILE_SETTINGS,
			buf, PYRA_SIZE_PROFILE_SETTINGS);
}

static int pyra_get_settings(struct usb_device *usb_dev,
		struct pyra_settings *buf)
{
	return roccat_common2_receive(usb_dev, PYRA_COMMAND_SETTINGS,
			buf, PYRA_SIZE_SETTINGS);
}

static int pyra_set_settings(struct usb_device *usb_dev,
		struct pyra_settings const *settings)
{
	return roccat_common2_send_with_status(usb_dev,
			PYRA_COMMAND_SETTINGS, settings,
			PYRA_SIZE_SETTINGS);
}

static ssize_t pyra_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&pyra->pyra_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&pyra->pyra_lock);

	if (retval)
		return retval;

	return real_size;
}

static ssize_t pyra_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&pyra->pyra_lock);
	retval = roccat_common2_send_with_status(usb_dev, command, (void *)buf, real_size);
	mutex_unlock(&pyra->pyra_lock);

	if (retval)
		return retval;

	return real_size;
}

#define PYRA_SYSFS_W(thingy, THINGY) \
static ssize_t pyra_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return pyra_sysfs_write(fp, kobj, buf, off, count, \
			PYRA_SIZE_ ## THINGY, PYRA_COMMAND_ ## THINGY); \
}

#define PYRA_SYSFS_R(thingy, THINGY) \
static ssize_t pyra_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, char *buf, \
		loff_t off, size_t count) \
{ \
	return pyra_sysfs_read(fp, kobj, buf, off, count, \
			PYRA_SIZE_ ## THINGY, PYRA_COMMAND_ ## THINGY); \
}

#define PYRA_SYSFS_RW(thingy, THINGY) \
PYRA_SYSFS_W(thingy, THINGY) \
PYRA_SYSFS_R(thingy, THINGY)

#define PYRA_BIN_ATTRIBUTE_RW(thingy, THINGY) \
PYRA_SYSFS_RW(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = PYRA_SIZE_ ## THINGY, \
	.read = pyra_sysfs_read_ ## thingy, \
	.write = pyra_sysfs_write_ ## thingy \
}

#define PYRA_BIN_ATTRIBUTE_R(thingy, THINGY) \
PYRA_SYSFS_R(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0440 }, \
	.size = PYRA_SIZE_ ## THINGY, \
	.read = pyra_sysfs_read_ ## thingy, \
}

#define PYRA_BIN_ATTRIBUTE_W(thingy, THINGY) \
PYRA_SYSFS_W(thingy, THINGY); \
static struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0220 }, \
	.size = PYRA_SIZE_ ## THINGY, \
	.write = pyra_sysfs_write_ ## thingy \
}

PYRA_BIN_ATTRIBUTE_W(control, CONTROL);
PYRA_BIN_ATTRIBUTE_RW(info, INFO);
PYRA_BIN_ATTRIBUTE_RW(profile_settings, PROFILE_SETTINGS);
PYRA_BIN_ATTRIBUTE_RW(profile_buttons, PROFILE_BUTTONS);

static ssize_t pyra_sysfs_read_profilex_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	ssize_t retval;

	retval = pyra_send_control(usb_dev, *(uint *)(attr->private),
			PYRA_CONTROL_REQUEST_PROFILE_SETTINGS);
	if (retval)
		return retval;

	return pyra_sysfs_read(fp, kobj, buf, off, count,
			PYRA_SIZE_PROFILE_SETTINGS,
			PYRA_COMMAND_PROFILE_SETTINGS);
}

static ssize_t pyra_sysfs_read_profilex_buttons(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	ssize_t retval;

	retval = pyra_send_control(usb_dev, *(uint *)(attr->private),
			PYRA_CONTROL_REQUEST_PROFILE_BUTTONS);
	if (retval)
		return retval;

	return pyra_sysfs_read(fp, kobj, buf, off, count,
			PYRA_SIZE_PROFILE_BUTTONS,
			PYRA_COMMAND_PROFILE_BUTTONS);
}

#define PROFILE_ATTR(number)						\
static struct bin_attribute bin_attr_profile##number##_settings = {	\
	.attr = { .name = "profile" #number "_settings", .mode = 0440 },	\
	.size = PYRA_SIZE_PROFILE_SETTINGS,				\
	.read = pyra_sysfs_read_profilex_settings,			\
	.private = &profile_numbers[number-1],				\
};									\
static struct bin_attribute bin_attr_profile##number##_buttons = {	\
	.attr = { .name = "profile" #number "_buttons", .mode = 0440 },	\
	.size = PYRA_SIZE_PROFILE_BUTTONS,				\
	.read = pyra_sysfs_read_profilex_buttons,			\
	.private = &profile_numbers[number-1],				\
};
PROFILE_ATTR(1);
PROFILE_ATTR(2);
PROFILE_ATTR(3);
PROFILE_ATTR(4);
PROFILE_ATTR(5);

static ssize_t pyra_sysfs_write_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval = 0;
	struct pyra_roccat_report roccat_report;
	struct pyra_settings const *settings;

	if (off != 0 || count != PYRA_SIZE_SETTINGS)
		return -EINVAL;

	mutex_lock(&pyra->pyra_lock);

	settings = (struct pyra_settings const *)buf;

	retval = pyra_set_settings(usb_dev, settings);
	if (retval) {
		mutex_unlock(&pyra->pyra_lock);
		return retval;
	}

	profile_activated(pyra, settings->startup_profile);

	roccat_report.type = PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2;
	roccat_report.value = settings->startup_profile + 1;
	roccat_report.key = 0;
	roccat_report_event(pyra->chrdev_minor,
			(uint8_t const *)&roccat_report);

	mutex_unlock(&pyra->pyra_lock);
	return PYRA_SIZE_SETTINGS;
}

PYRA_SYSFS_R(settings, SETTINGS);
static struct bin_attribute bin_attr_settings =
	__BIN_ATTR(settings, (S_IWUSR | S_IRUGO),
		   pyra_sysfs_read_settings, pyra_sysfs_write_settings,
		   PYRA_SIZE_SETTINGS);

static ssize_t pyra_sysfs_show_actual_cpi(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", pyra->actual_cpi);
}
static DEVICE_ATTR(actual_cpi, 0440, pyra_sysfs_show_actual_cpi, NULL);

static ssize_t pyra_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	struct pyra_settings settings;

	mutex_lock(&pyra->pyra_lock);
	roccat_common2_receive(usb_dev, PYRA_COMMAND_SETTINGS,
			&settings, PYRA_SIZE_SETTINGS);
	mutex_unlock(&pyra->pyra_lock);

	return snprintf(buf, PAGE_SIZE, "%d\n", settings.startup_profile);
}
static DEVICE_ATTR(actual_profile, 0440, pyra_sysfs_show_actual_profile, NULL);
static DEVICE_ATTR(startup_profile, 0440, pyra_sysfs_show_actual_profile, NULL);

static ssize_t pyra_sysfs_show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra;
	struct usb_device *usb_dev;
	struct pyra_info info;

	dev = dev->parent->parent;
	pyra = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	mutex_lock(&pyra->pyra_lock);
	roccat_common2_receive(usb_dev, PYRA_COMMAND_INFO,
			&info, PYRA_SIZE_INFO);
	mutex_unlock(&pyra->pyra_lock);

	return snprintf(buf, PAGE_SIZE, "%d\n", info.firmware_version);
}
static DEVICE_ATTR(firmware_version, 0440, pyra_sysfs_show_firmware_version,
		   NULL);

static struct attribute *pyra_attrs[] = {
	&dev_attr_actual_cpi.attr,
	&dev_attr_actual_profile.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_startup_profile.attr,
	NULL,
};

static struct bin_attribute *pyra_bin_attributes[] = {
	&bin_attr_control,
	&bin_attr_info,
	&bin_attr_profile_settings,
	&bin_attr_profile_buttons,
	&bin_attr_settings,
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

static const struct attribute_group pyra_group = {
	.attrs = pyra_attrs,
	.bin_attrs = pyra_bin_attributes,
};

static const struct attribute_group *pyra_groups[] = {
	&pyra_group,
	NULL,
};

static int pyra_init_pyra_device_struct(struct usb_device *usb_dev,
		struct pyra_device *pyra)
{
	struct pyra_settings settings;
	int retval, i;

	mutex_init(&pyra->pyra_lock);

	retval = pyra_get_settings(usb_dev, &settings);
	if (retval)
		return retval;

	for (i = 0; i < 5; ++i) {
		retval = pyra_get_profile_settings(usb_dev,
				&pyra->profile_settings[i], i);
		if (retval)
			return retval;
	}

	profile_activated(pyra, settings.startup_profile);

	return 0;
}

static int pyra_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct pyra_device *pyra;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {

		pyra = kzalloc(sizeof(*pyra), GFP_KERNEL);
		if (!pyra) {
			hid_err(hdev, "can't alloc device descriptor\n");
			return -ENOMEM;
		}
		hid_set_drvdata(hdev, pyra);

		retval = pyra_init_pyra_device_struct(usb_dev, pyra);
		if (retval) {
			hid_err(hdev, "couldn't init struct pyra_device\n");
			goto exit_free;
		}

		retval = roccat_connect(pyra_class, hdev,
				sizeof(struct pyra_roccat_report));
		if (retval < 0) {
			hid_err(hdev, "couldn't init char dev\n");
		} else {
			pyra->chrdev_minor = retval;
			pyra->roccat_claimed = 1;
		}
	} else {
		hid_set_drvdata(hdev, NULL);
	}

	return 0;
exit_free:
	kfree(pyra);
	return retval;
}

static void pyra_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct pyra_device *pyra;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {
		pyra = hid_get_drvdata(hdev);
		if (pyra->roccat_claimed)
			roccat_disconnect(pyra->chrdev_minor);
		kfree(hid_get_drvdata(hdev));
	}
}

static int pyra_probe(struct hid_device *hdev, const struct hid_device_id *id)
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

	retval = pyra_init_specials(hdev);
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

static void pyra_remove(struct hid_device *hdev)
{
	pyra_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void pyra_keep_values_up_to_date(struct pyra_device *pyra,
		u8 const *data)
{
	struct pyra_mouse_event_button const *button_event;

	switch (data[0]) {
	case PYRA_MOUSE_REPORT_NUMBER_BUTTON:
		button_event = (struct pyra_mouse_event_button const *)data;
		switch (button_event->type) {
		case PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2:
			profile_activated(pyra, button_event->data1 - 1);
			break;
		case PYRA_MOUSE_EVENT_BUTTON_TYPE_CPI:
			pyra->actual_cpi = button_event->data1;
			break;
		}
		break;
	}
}

static void pyra_report_to_chrdev(struct pyra_device const *pyra,
		u8 const *data)
{
	struct pyra_roccat_report roccat_report;
	struct pyra_mouse_event_button const *button_event;

	if (data[0] != PYRA_MOUSE_REPORT_NUMBER_BUTTON)
		return;

	button_event = (struct pyra_mouse_event_button const *)data;

	switch (button_event->type) {
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2:
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_CPI:
		roccat_report.type = button_event->type;
		roccat_report.value = button_event->data1;
		roccat_report.key = 0;
		roccat_report_event(pyra->chrdev_minor,
				(uint8_t const *)&roccat_report);
		break;
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_MACRO:
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_SHORTCUT:
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_QUICKLAUNCH:
		if (button_event->data2 == PYRA_MOUSE_EVENT_BUTTON_PRESS) {
			roccat_report.type = button_event->type;
			roccat_report.key = button_event->data1;
			/*
			 * pyra reports profile numbers with range 1-5.
			 * Keeping this behaviour.
			 */
			roccat_report.value = pyra->actual_profile + 1;
			roccat_report_event(pyra->chrdev_minor,
					(uint8_t const *)&roccat_report);
		}
		break;
	}
}

static int pyra_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct pyra_device *pyra = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return 0;

	if (pyra == NULL)
		return 0;

	pyra_keep_values_up_to_date(pyra, data);

	if (pyra->roccat_claimed)
		pyra_report_to_chrdev(pyra, data);

	return 0;
}

static const struct hid_device_id pyra_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT,
			USB_DEVICE_ID_ROCCAT_PYRA_WIRED) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT,
			USB_DEVICE_ID_ROCCAT_PYRA_WIRELESS) },
	{ }
};

MODULE_DEVICE_TABLE(hid, pyra_devices);

static struct hid_driver pyra_driver = {
		.name = "pyra",
		.id_table = pyra_devices,
		.probe = pyra_probe,
		.remove = pyra_remove,
		.raw_event = pyra_raw_event
};

static int __init pyra_init(void)
{
	int retval;

	/* class name has to be same as driver name */
	pyra_class = class_create(THIS_MODULE, "pyra");
	if (IS_ERR(pyra_class))
		return PTR_ERR(pyra_class);
	pyra_class->dev_groups = pyra_groups;

	retval = hid_register_driver(&pyra_driver);
	if (retval)
		class_destroy(pyra_class);
	return retval;
}

static void __exit pyra_exit(void)
{
	hid_unregister_driver(&pyra_driver);
	class_destroy(pyra_class);
}

module_init(pyra_init);
module_exit(pyra_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Pyra driver");
MODULE_LICENSE("GPL v2");
