// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Roccat Arvo driver for Linux
 *
 * Copyright (c) 2011 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 */

/*
 * Roccat Arvo is a gamer keyboard with 5 macro keys that can be configured in
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
#include "hid-roccat-arvo.h"

static struct class *arvo_class;

static ssize_t arvo_sysfs_show_mode_key(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_mode_key temp_buf;
	int retval;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_receive(usb_dev, ARVO_COMMAND_MODE_KEY,
			&temp_buf, sizeof(struct arvo_mode_key));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", temp_buf.state);
}

static ssize_t arvo_sysfs_set_mode_key(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_mode_key temp_buf;
	unsigned long state;
	int retval;

	retval = kstrtoul(buf, 10, &state);
	if (retval)
		return retval;

	temp_buf.command = ARVO_COMMAND_MODE_KEY;
	temp_buf.state = state;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_send(usb_dev, ARVO_COMMAND_MODE_KEY,
			&temp_buf, sizeof(struct arvo_mode_key));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		return retval;

	return size;
}
static DEVICE_ATTR(mode_key, 0660,
		   arvo_sysfs_show_mode_key, arvo_sysfs_set_mode_key);

static ssize_t arvo_sysfs_show_key_mask(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_key_mask temp_buf;
	int retval;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_receive(usb_dev, ARVO_COMMAND_KEY_MASK,
			&temp_buf, sizeof(struct arvo_key_mask));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", temp_buf.key_mask);
}

static ssize_t arvo_sysfs_set_key_mask(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_key_mask temp_buf;
	unsigned long key_mask;
	int retval;

	retval = kstrtoul(buf, 10, &key_mask);
	if (retval)
		return retval;

	temp_buf.command = ARVO_COMMAND_KEY_MASK;
	temp_buf.key_mask = key_mask;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_send(usb_dev, ARVO_COMMAND_KEY_MASK,
			&temp_buf, sizeof(struct arvo_key_mask));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		return retval;

	return size;
}
static DEVICE_ATTR(key_mask, 0660,
		   arvo_sysfs_show_key_mask, arvo_sysfs_set_key_mask);

/* retval is 1-5 on success, < 0 on error */
static int arvo_get_actual_profile(struct usb_device *usb_dev)
{
	struct arvo_actual_profile temp_buf;
	int retval;

	retval = roccat_common2_receive(usb_dev, ARVO_COMMAND_ACTUAL_PROFILE,
			&temp_buf, sizeof(struct arvo_actual_profile));

	if (retval)
		return retval;

	return temp_buf.actual_profile;
}

static ssize_t arvo_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));

	return sysfs_emit(buf, "%d\n", arvo->actual_profile);
}

static ssize_t arvo_sysfs_set_actual_profile(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_actual_profile temp_buf;
	unsigned long profile;
	int retval;

	retval = kstrtoul(buf, 10, &profile);
	if (retval)
		return retval;

	if (profile < 1 || profile > 5)
		return -EINVAL;

	temp_buf.command = ARVO_COMMAND_ACTUAL_PROFILE;
	temp_buf.actual_profile = profile;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_send(usb_dev, ARVO_COMMAND_ACTUAL_PROFILE,
			&temp_buf, sizeof(struct arvo_actual_profile));
	if (!retval) {
		arvo->actual_profile = profile;
		retval = size;
	}
	mutex_unlock(&arvo->arvo_lock);
	return retval;
}
static DEVICE_ATTR(actual_profile, 0660,
		   arvo_sysfs_show_actual_profile,
		   arvo_sysfs_set_actual_profile);

static ssize_t arvo_sysfs_write(struct file *fp,
		struct kobject *kobj, void const *buf,
		loff_t off, size_t count, size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct arvo_device *arvo = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_send(usb_dev, command, buf, real_size);
	mutex_unlock(&arvo->arvo_lock);

	return (retval ? retval : real_size);
}

static ssize_t arvo_sysfs_read(struct file *fp,
		struct kobject *kobj, void *buf, loff_t off,
		size_t count, size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj)->parent->parent;
	struct arvo_device *arvo = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&arvo->arvo_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&arvo->arvo_lock);

	return (retval ? retval : real_size);
}

static ssize_t arvo_sysfs_write_button(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	return arvo_sysfs_write(fp, kobj, buf, off, count,
			sizeof(struct arvo_button), ARVO_COMMAND_BUTTON);
}
static BIN_ATTR(button, 0220, NULL, arvo_sysfs_write_button,
		sizeof(struct arvo_button));

static ssize_t arvo_sysfs_read_info(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	return arvo_sysfs_read(fp, kobj, buf, off, count,
			sizeof(struct arvo_info), ARVO_COMMAND_INFO);
}
static BIN_ATTR(info, 0440, arvo_sysfs_read_info, NULL,
		sizeof(struct arvo_info));

static struct attribute *arvo_attrs[] = {
	&dev_attr_mode_key.attr,
	&dev_attr_key_mask.attr,
	&dev_attr_actual_profile.attr,
	NULL,
};

static struct bin_attribute *arvo_bin_attributes[] = {
	&bin_attr_button,
	&bin_attr_info,
	NULL,
};

static const struct attribute_group arvo_group = {
	.attrs = arvo_attrs,
	.bin_attrs = arvo_bin_attributes,
};

static const struct attribute_group *arvo_groups[] = {
	&arvo_group,
	NULL,
};

static int arvo_init_arvo_device_struct(struct usb_device *usb_dev,
		struct arvo_device *arvo)
{
	int retval;

	mutex_init(&arvo->arvo_lock);

	retval = arvo_get_actual_profile(usb_dev);
	if (retval < 0)
		return retval;
	arvo->actual_profile = retval;

	return 0;
}

static int arvo_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct arvo_device *arvo;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_KEYBOARD) {
		hid_set_drvdata(hdev, NULL);
		return 0;
	}

	arvo = kzalloc(sizeof(*arvo), GFP_KERNEL);
	if (!arvo) {
		hid_err(hdev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, arvo);

	retval = arvo_init_arvo_device_struct(usb_dev, arvo);
	if (retval) {
		hid_err(hdev, "couldn't init struct arvo_device\n");
		goto exit_free;
	}

	retval = roccat_connect(arvo_class, hdev,
			sizeof(struct arvo_roccat_report));
	if (retval < 0) {
		hid_err(hdev, "couldn't init char dev\n");
	} else {
		arvo->chrdev_minor = retval;
		arvo->roccat_claimed = 1;
	}

	return 0;
exit_free:
	kfree(arvo);
	return retval;
}

static void arvo_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct arvo_device *arvo;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_KEYBOARD)
		return;

	arvo = hid_get_drvdata(hdev);
	if (arvo->roccat_claimed)
		roccat_disconnect(arvo->chrdev_minor);
	kfree(arvo);
}

static int arvo_probe(struct hid_device *hdev,
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

	retval = arvo_init_specials(hdev);
	if (retval) {
		hid_err(hdev, "couldn't install keyboard\n");
		goto exit_stop;
	}

	return 0;

exit_stop:
	hid_hw_stop(hdev);
exit:
	return retval;
}

static void arvo_remove(struct hid_device *hdev)
{
	arvo_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void arvo_report_to_chrdev(struct arvo_device const *arvo,
		u8 const *data)
{
	struct arvo_special_report const *special_report;
	struct arvo_roccat_report roccat_report;

	special_report = (struct arvo_special_report const *)data;

	roccat_report.profile = arvo->actual_profile;
	roccat_report.button = special_report->event &
			ARVO_SPECIAL_REPORT_EVENT_MASK_BUTTON;
	if ((special_report->event & ARVO_SPECIAL_REPORT_EVENT_MASK_ACTION) ==
			ARVO_SPECIAL_REPORT_EVENT_ACTION_PRESS)
		roccat_report.action = ARVO_ROCCAT_REPORT_ACTION_PRESS;
	else
		roccat_report.action = ARVO_ROCCAT_REPORT_ACTION_RELEASE;

	roccat_report_event(arvo->chrdev_minor,
			(uint8_t const *)&roccat_report);
}

static int arvo_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct arvo_device *arvo = hid_get_drvdata(hdev);

	if (size != 3)
		return 0;

	if (arvo && arvo->roccat_claimed)
		arvo_report_to_chrdev(arvo, data);

	return 0;
}

static const struct hid_device_id arvo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_ARVO) },
	{ }
};

MODULE_DEVICE_TABLE(hid, arvo_devices);

static struct hid_driver arvo_driver = {
	.name = "arvo",
	.id_table = arvo_devices,
	.probe = arvo_probe,
	.remove = arvo_remove,
	.raw_event = arvo_raw_event
};

static int __init arvo_init(void)
{
	int retval;

	arvo_class = class_create("arvo");
	if (IS_ERR(arvo_class))
		return PTR_ERR(arvo_class);
	arvo_class->dev_groups = arvo_groups;

	retval = hid_register_driver(&arvo_driver);
	if (retval)
		class_destroy(arvo_class);
	return retval;
}

static void __exit arvo_exit(void)
{
	hid_unregister_driver(&arvo_driver);
	class_destroy(arvo_class);
}

module_init(arvo_init);
module_exit(arvo_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Arvo driver");
MODULE_LICENSE("GPL v2");
