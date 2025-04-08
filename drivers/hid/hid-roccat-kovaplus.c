// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Roccat Kova[+] driver for Linux
 *
 * Copyright (c) 2011 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 */

/*
 * Roccat Kova[+] is a bigger version of the Pyra with two more side buttons.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-kovaplus.h"

static uint profile_numbers[5] = {0, 1, 2, 3, 4};

static uint kovaplus_convert_event_cpi(uint value)
{
	return (value == 7 ? 4 : (value == 4 ? 3 : value));
}

static void kovaplus_profile_activated(struct kovaplus_device *kovaplus,
		uint new_profile_index)
{
	if (new_profile_index >= ARRAY_SIZE(kovaplus->profile_settings))
		return;
	kovaplus->actual_profile = new_profile_index;
	kovaplus->actual_cpi = kovaplus->profile_settings[new_profile_index].cpi_startup_level;
	kovaplus->actual_x_sensitivity = kovaplus->profile_settings[new_profile_index].sensitivity_x;
	kovaplus->actual_y_sensitivity = kovaplus->profile_settings[new_profile_index].sensitivity_y;
}

static int kovaplus_send_control(struct usb_device *usb_dev, uint value,
		enum kovaplus_control_requests request)
{
	int retval;
	struct roccat_common2_control control;

	if ((request == KOVAPLUS_CONTROL_REQUEST_PROFILE_SETTINGS ||
			request == KOVAPLUS_CONTROL_REQUEST_PROFILE_BUTTONS) &&
			value > 4)
		return -EINVAL;

	control.command = ROCCAT_COMMON_COMMAND_CONTROL;
	control.value = value;
	control.request = request;

	retval = roccat_common2_send(usb_dev, ROCCAT_COMMON_COMMAND_CONTROL,
			&control, sizeof(struct roccat_common2_control));

	return retval;
}

static int kovaplus_select_profile(struct usb_device *usb_dev, uint number,
		enum kovaplus_control_requests request)
{
	return kovaplus_send_control(usb_dev, number, request);
}

static int kovaplus_get_profile_settings(struct usb_device *usb_dev,
		struct kovaplus_profile_settings *buf, uint number)
{
	int retval;

	retval = kovaplus_select_profile(usb_dev, number,
			KOVAPLUS_CONTROL_REQUEST_PROFILE_SETTINGS);
	if (retval)
		return retval;

	return roccat_common2_receive(usb_dev, KOVAPLUS_COMMAND_PROFILE_SETTINGS,
			buf, KOVAPLUS_SIZE_PROFILE_SETTINGS);
}

static int kovaplus_get_profile_buttons(struct usb_device *usb_dev,
		struct kovaplus_profile_buttons *buf, int number)
{
	int retval;

	retval = kovaplus_select_profile(usb_dev, number,
			KOVAPLUS_CONTROL_REQUEST_PROFILE_BUTTONS);
	if (retval)
		return retval;

	return roccat_common2_receive(usb_dev, KOVAPLUS_COMMAND_PROFILE_BUTTONS,
			buf, KOVAPLUS_SIZE_PROFILE_BUTTONS);
}

/* retval is 0-4 on success, < 0 on error */
static int kovaplus_get_actual_profile(struct usb_device *usb_dev)
{
	struct kovaplus_actual_profile buf;
	int retval;

	retval = roccat_common2_receive(usb_dev, KOVAPLUS_COMMAND_ACTUAL_PROFILE,
			&buf, sizeof(struct kovaplus_actual_profile));

	return retval ? retval : buf.actual_profile;
}

static int kovaplus_set_actual_profile(struct usb_device *usb_dev,
		int new_profile)
{
	struct kovaplus_actual_profile buf;

	buf.command = KOVAPLUS_COMMAND_ACTUAL_PROFILE;
	buf.size = sizeof(struct kovaplus_actual_profile);
	buf.actual_profile = new_profile;

	return roccat_common2_send_with_status(usb_dev,
			KOVAPLUS_COMMAND_ACTUAL_PROFILE,
			&buf, sizeof(struct kovaplus_actual_profile));
}

static ssize_t kovaplus_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct kovaplus_device *kovaplus = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&kovaplus->kovaplus_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&kovaplus->kovaplus_lock);

	if (retval)
		return retval;

	return real_size;
}

static ssize_t kovaplus_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct kovaplus_device *kovaplus = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&kovaplus->kovaplus_lock);
	retval = roccat_common2_send_with_status(usb_dev, command,
			buf, real_size);
	mutex_unlock(&kovaplus->kovaplus_lock);

	if (retval)
		return retval;

	return real_size;
}

#define KOVAPLUS_SYSFS_W(thingy, THINGY) \
static ssize_t kovaplus_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, const struct bin_attribute *attr, \
		char *buf, loff_t off, size_t count) \
{ \
	return kovaplus_sysfs_write(fp, kobj, buf, off, count, \
			KOVAPLUS_SIZE_ ## THINGY, KOVAPLUS_COMMAND_ ## THINGY); \
}

#define KOVAPLUS_SYSFS_R(thingy, THINGY) \
static ssize_t kovaplus_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, const struct bin_attribute *attr, \
		char *buf, loff_t off, size_t count) \
{ \
	return kovaplus_sysfs_read(fp, kobj, buf, off, count, \
			KOVAPLUS_SIZE_ ## THINGY, KOVAPLUS_COMMAND_ ## THINGY); \
}

#define KOVAPLUS_SYSFS_RW(thingy, THINGY) \
KOVAPLUS_SYSFS_W(thingy, THINGY) \
KOVAPLUS_SYSFS_R(thingy, THINGY)

#define KOVAPLUS_BIN_ATTRIBUTE_RW(thingy, THINGY) \
KOVAPLUS_SYSFS_RW(thingy, THINGY); \
static const struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = KOVAPLUS_SIZE_ ## THINGY, \
	.read_new = kovaplus_sysfs_read_ ## thingy, \
	.write_new = kovaplus_sysfs_write_ ## thingy \
}

#define KOVAPLUS_BIN_ATTRIBUTE_W(thingy, THINGY) \
KOVAPLUS_SYSFS_W(thingy, THINGY); \
static const struct bin_attribute bin_attr_##thingy = { \
	.attr = { .name = #thingy, .mode = 0220 }, \
	.size = KOVAPLUS_SIZE_ ## THINGY, \
	.write_new = kovaplus_sysfs_write_ ## thingy \
}
KOVAPLUS_BIN_ATTRIBUTE_W(control, CONTROL);
KOVAPLUS_BIN_ATTRIBUTE_RW(info, INFO);
KOVAPLUS_BIN_ATTRIBUTE_RW(profile_settings, PROFILE_SETTINGS);
KOVAPLUS_BIN_ATTRIBUTE_RW(profile_buttons, PROFILE_BUTTONS);

static ssize_t kovaplus_sysfs_read_profilex_settings(struct file *fp,
		struct kobject *kobj, const struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	ssize_t retval;

	retval = kovaplus_select_profile(usb_dev, *(uint *)(attr->private),
			KOVAPLUS_CONTROL_REQUEST_PROFILE_SETTINGS);
	if (retval)
		return retval;

	return kovaplus_sysfs_read(fp, kobj, buf, off, count,
			KOVAPLUS_SIZE_PROFILE_SETTINGS,
			KOVAPLUS_COMMAND_PROFILE_SETTINGS);
}

static ssize_t kovaplus_sysfs_read_profilex_buttons(struct file *fp,
		struct kobject *kobj, const struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	ssize_t retval;

	retval = kovaplus_select_profile(usb_dev, *(uint *)(attr->private),
			KOVAPLUS_CONTROL_REQUEST_PROFILE_BUTTONS);
	if (retval)
		return retval;

	return kovaplus_sysfs_read(fp, kobj, buf, off, count,
			KOVAPLUS_SIZE_PROFILE_BUTTONS,
			KOVAPLUS_COMMAND_PROFILE_BUTTONS);
}

#define PROFILE_ATTR(number)						\
static const struct bin_attribute bin_attr_profile##number##_settings = {	\
	.attr = { .name = "profile" #number "_settings", .mode = 0440 },	\
	.size = KOVAPLUS_SIZE_PROFILE_SETTINGS,				\
	.read_new = kovaplus_sysfs_read_profilex_settings,			\
	.private = &profile_numbers[number-1],				\
};									\
static const struct bin_attribute bin_attr_profile##number##_buttons = {	\
	.attr = { .name = "profile" #number "_buttons", .mode = 0440 },	\
	.size = KOVAPLUS_SIZE_PROFILE_BUTTONS,				\
	.read_new = kovaplus_sysfs_read_profilex_buttons,			\
	.private = &profile_numbers[number-1],				\
};
PROFILE_ATTR(1);
PROFILE_ATTR(2);
PROFILE_ATTR(3);
PROFILE_ATTR(4);
PROFILE_ATTR(5);

static ssize_t kovaplus_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kovaplus_device *kovaplus =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return sysfs_emit(buf, "%d\n", kovaplus->actual_profile);
}

static ssize_t kovaplus_sysfs_set_actual_profile(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct kovaplus_device *kovaplus;
	struct usb_device *usb_dev;
	unsigned long profile;
	int retval;
	struct kovaplus_roccat_report roccat_report;

	dev = dev->parent->parent;
	kovaplus = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	retval = kstrtoul(buf, 10, &profile);
	if (retval)
		return retval;

	if (profile >= 5)
		return -EINVAL;

	mutex_lock(&kovaplus->kovaplus_lock);
	retval = kovaplus_set_actual_profile(usb_dev, profile);
	if (retval) {
		mutex_unlock(&kovaplus->kovaplus_lock);
		return retval;
	}

	kovaplus_profile_activated(kovaplus, profile);

	roccat_report.type = KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE_1;
	roccat_report.profile = profile + 1;
	roccat_report.button = 0;
	roccat_report.data1 = profile + 1;
	roccat_report.data2 = 0;
	roccat_report_event(kovaplus->chrdev_minor,
			(uint8_t const *)&roccat_report);

	mutex_unlock(&kovaplus->kovaplus_lock);

	return size;
}
static DEVICE_ATTR(actual_profile, 0660,
		   kovaplus_sysfs_show_actual_profile,
		   kovaplus_sysfs_set_actual_profile);

static ssize_t kovaplus_sysfs_show_actual_cpi(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kovaplus_device *kovaplus =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return sysfs_emit(buf, "%d\n", kovaplus->actual_cpi);
}
static DEVICE_ATTR(actual_cpi, 0440, kovaplus_sysfs_show_actual_cpi, NULL);

static ssize_t kovaplus_sysfs_show_actual_sensitivity_x(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kovaplus_device *kovaplus =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return sysfs_emit(buf, "%d\n", kovaplus->actual_x_sensitivity);
}
static DEVICE_ATTR(actual_sensitivity_x, 0440,
		   kovaplus_sysfs_show_actual_sensitivity_x, NULL);

static ssize_t kovaplus_sysfs_show_actual_sensitivity_y(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kovaplus_device *kovaplus =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return sysfs_emit(buf, "%d\n", kovaplus->actual_y_sensitivity);
}
static DEVICE_ATTR(actual_sensitivity_y, 0440,
		   kovaplus_sysfs_show_actual_sensitivity_y, NULL);

static ssize_t kovaplus_sysfs_show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kovaplus_device *kovaplus;
	struct usb_device *usb_dev;
	struct kovaplus_info info;

	dev = dev->parent->parent;
	kovaplus = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	mutex_lock(&kovaplus->kovaplus_lock);
	roccat_common2_receive(usb_dev, KOVAPLUS_COMMAND_INFO,
			&info, KOVAPLUS_SIZE_INFO);
	mutex_unlock(&kovaplus->kovaplus_lock);

	return sysfs_emit(buf, "%d\n", info.firmware_version);
}
static DEVICE_ATTR(firmware_version, 0440,
		   kovaplus_sysfs_show_firmware_version, NULL);

static struct attribute *kovaplus_attrs[] = {
	&dev_attr_actual_cpi.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_actual_profile.attr,
	&dev_attr_actual_sensitivity_x.attr,
	&dev_attr_actual_sensitivity_y.attr,
	NULL,
};

static const struct bin_attribute *const kovaplus_bin_attributes[] = {
	&bin_attr_control,
	&bin_attr_info,
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

static const struct attribute_group kovaplus_group = {
	.attrs = kovaplus_attrs,
	.bin_attrs_new = kovaplus_bin_attributes,
};

static const struct attribute_group *kovaplus_groups[] = {
	&kovaplus_group,
	NULL,
};

static const struct class kovaplus_class = {
	.name = "kovaplus",
	.dev_groups = kovaplus_groups,
};

static int kovaplus_init_kovaplus_device_struct(struct usb_device *usb_dev,
		struct kovaplus_device *kovaplus)
{
	int retval, i;
	static uint wait = 70; /* device will freeze with just 60 */

	mutex_init(&kovaplus->kovaplus_lock);

	for (i = 0; i < 5; ++i) {
		msleep(wait);
		retval = kovaplus_get_profile_settings(usb_dev,
				&kovaplus->profile_settings[i], i);
		if (retval)
			return retval;

		msleep(wait);
		retval = kovaplus_get_profile_buttons(usb_dev,
				&kovaplus->profile_buttons[i], i);
		if (retval)
			return retval;
	}

	msleep(wait);
	retval = kovaplus_get_actual_profile(usb_dev);
	if (retval < 0)
		return retval;
	kovaplus_profile_activated(kovaplus, retval);

	return 0;
}

static int kovaplus_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct kovaplus_device *kovaplus;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {

		kovaplus = kzalloc(sizeof(*kovaplus), GFP_KERNEL);
		if (!kovaplus) {
			hid_err(hdev, "can't alloc device descriptor\n");
			return -ENOMEM;
		}
		hid_set_drvdata(hdev, kovaplus);

		retval = kovaplus_init_kovaplus_device_struct(usb_dev, kovaplus);
		if (retval) {
			hid_err(hdev, "couldn't init struct kovaplus_device\n");
			goto exit_free;
		}

		retval = roccat_connect(&kovaplus_class, hdev,
					sizeof(struct kovaplus_roccat_report));
		if (retval < 0) {
			hid_err(hdev, "couldn't init char dev\n");
		} else {
			kovaplus->chrdev_minor = retval;
			kovaplus->roccat_claimed = 1;
		}

	} else {
		hid_set_drvdata(hdev, NULL);
	}

	return 0;
exit_free:
	kfree(kovaplus);
	return retval;
}

static void kovaplus_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct kovaplus_device *kovaplus;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {
		kovaplus = hid_get_drvdata(hdev);
		if (kovaplus->roccat_claimed)
			roccat_disconnect(kovaplus->chrdev_minor);
		kfree(kovaplus);
	}
}

static int kovaplus_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int retval;

	if (!hid_is_usb(hdev))
		return -EINVAL;

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

	retval = kovaplus_init_specials(hdev);
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

static void kovaplus_remove(struct hid_device *hdev)
{
	kovaplus_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void kovaplus_keep_values_up_to_date(struct kovaplus_device *kovaplus,
		u8 const *data)
{
	struct kovaplus_mouse_report_button const *button_report;

	if (data[0] != KOVAPLUS_MOUSE_REPORT_NUMBER_BUTTON)
		return;

	button_report = (struct kovaplus_mouse_report_button const *)data;

	switch (button_report->type) {
	case KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE_1:
		kovaplus_profile_activated(kovaplus, button_report->data1 - 1);
		break;
	case KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_CPI:
		kovaplus->actual_cpi = kovaplus_convert_event_cpi(button_report->data1);
		break;
	case KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_SENSITIVITY:
		kovaplus->actual_x_sensitivity = button_report->data1;
		kovaplus->actual_y_sensitivity = button_report->data2;
		break;
	default:
		break;
	}
}

static void kovaplus_report_to_chrdev(struct kovaplus_device const *kovaplus,
		u8 const *data)
{
	struct kovaplus_roccat_report roccat_report;
	struct kovaplus_mouse_report_button const *button_report;

	if (data[0] != KOVAPLUS_MOUSE_REPORT_NUMBER_BUTTON)
		return;

	button_report = (struct kovaplus_mouse_report_button const *)data;

	if (button_report->type == KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_PROFILE_2)
		return;

	roccat_report.type = button_report->type;
	roccat_report.profile = kovaplus->actual_profile + 1;

	if (roccat_report.type == KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_MACRO ||
			roccat_report.type == KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_SHORTCUT ||
			roccat_report.type == KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_QUICKLAUNCH ||
			roccat_report.type == KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_TIMER)
		roccat_report.button = button_report->data1;
	else
		roccat_report.button = 0;

	if (roccat_report.type == KOVAPLUS_MOUSE_REPORT_BUTTON_TYPE_CPI)
		roccat_report.data1 = kovaplus_convert_event_cpi(button_report->data1);
	else
		roccat_report.data1 = button_report->data1;

	roccat_report.data2 = button_report->data2;

	roccat_report_event(kovaplus->chrdev_minor,
			(uint8_t const *)&roccat_report);
}

static int kovaplus_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct kovaplus_device *kovaplus = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return 0;

	if (kovaplus == NULL)
		return 0;

	kovaplus_keep_values_up_to_date(kovaplus, data);

	if (kovaplus->roccat_claimed)
		kovaplus_report_to_chrdev(kovaplus, data);

	return 0;
}

static const struct hid_device_id kovaplus_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_KOVAPLUS) },
	{ }
};

MODULE_DEVICE_TABLE(hid, kovaplus_devices);

static struct hid_driver kovaplus_driver = {
		.name = "kovaplus",
		.id_table = kovaplus_devices,
		.probe = kovaplus_probe,
		.remove = kovaplus_remove,
		.raw_event = kovaplus_raw_event
};

static int __init kovaplus_init(void)
{
	int retval;

	retval = class_register(&kovaplus_class);
	if (retval)
		return retval;

	retval = hid_register_driver(&kovaplus_driver);
	if (retval)
		class_unregister(&kovaplus_class);
	return retval;
}

static void __exit kovaplus_exit(void)
{
	hid_unregister_driver(&kovaplus_driver);
	class_unregister(&kovaplus_class);
}

module_init(kovaplus_init);
module_exit(kovaplus_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Kova[+] driver");
MODULE_LICENSE("GPL v2");
