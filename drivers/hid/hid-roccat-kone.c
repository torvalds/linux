/*
 * Roccat Kone driver for Linux
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
 * Roccat Kone is a gamer mouse which consists of a mouse part and a keyboard
 * part. The keyboard part enables the mouse to execute stored macros with mixed
 * key- and button-events.
 *
 * TODO implement on-the-fly polling-rate change
 *      The windows driver has the ability to change the polling rate of the
 *      device on the press of a mousebutton.
 *      Is it possible to remove and reinstall the urb in raw-event- or any
 *      other handler, or to defer this action to be executed somewhere else?
 *
 * TODO is it possible to overwrite group for sysfs attributes via udev?
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-kone.h"

static uint profile_numbers[5] = {0, 1, 2, 3, 4};

static void kone_profile_activated(struct kone_device *kone, uint new_profile)
{
	kone->actual_profile = new_profile;
	kone->actual_dpi = kone->profiles[new_profile - 1].startup_dpi;
}

static void kone_profile_report(struct kone_device *kone, uint new_profile)
{
	struct kone_roccat_report roccat_report;

	roccat_report.event = kone_mouse_event_switch_profile;
	roccat_report.value = new_profile;
	roccat_report.key = 0;
	roccat_report_event(kone->chrdev_minor, (uint8_t *)&roccat_report);
}

static int kone_receive(struct usb_device *usb_dev, uint usb_command,
		void *data, uint size)
{
	char *buf;
	int len;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			HID_REQ_GET_REPORT,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			usb_command, 0, buf, size, USB_CTRL_SET_TIMEOUT);

	memcpy(data, buf, size);
	kfree(buf);
	return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}

static int kone_send(struct usb_device *usb_dev, uint usb_command,
		void const *data, uint size)
{
	char *buf;
	int len;

	buf = kmemdup(data, size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			HID_REQ_SET_REPORT,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			usb_command, 0, buf, size, USB_CTRL_SET_TIMEOUT);

	kfree(buf);
	return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}

/* kone_class is used for creating sysfs attributes via roccat char device */
static struct class *kone_class;

static void kone_set_settings_checksum(struct kone_settings *settings)
{
	uint16_t checksum = 0;
	unsigned char *address = (unsigned char *)settings;
	int i;

	for (i = 0; i < sizeof(struct kone_settings) - 2; ++i, ++address)
		checksum += *address;
	settings->checksum = cpu_to_le16(checksum);
}

/*
 * Checks success after writing data to mouse
 * On success returns 0
 * On failure returns errno
 */
static int kone_check_write(struct usb_device *usb_dev)
{
	int retval;
	uint8_t data;

	do {
		/*
		 * Mouse needs 50 msecs until it says ok, but there are
		 * 30 more msecs needed for next write to work.
		 */
		msleep(80);

		retval = kone_receive(usb_dev,
				kone_command_confirm_write, &data, 1);
		if (retval)
			return retval;

		/*
		 * value of 3 seems to mean something like
		 * "not finished yet, but it looks good"
		 * So check again after a moment.
		 */
	} while (data == 3);

	if (data == 1) /* everything alright */
		return 0;

	/* unknown answer */
	dev_err(&usb_dev->dev, "got retval %d when checking write\n", data);
	return -EIO;
}

/*
 * Reads settings from mouse and stores it in @buf
 * On success returns 0
 * On failure returns errno
 */
static int kone_get_settings(struct usb_device *usb_dev,
		struct kone_settings *buf)
{
	return kone_receive(usb_dev, kone_command_settings, buf,
			sizeof(struct kone_settings));
}

/*
 * Writes settings from @buf to mouse
 * On success returns 0
 * On failure returns errno
 */
static int kone_set_settings(struct usb_device *usb_dev,
		struct kone_settings const *settings)
{
	int retval;

	retval = kone_send(usb_dev, kone_command_settings,
			settings, sizeof(struct kone_settings));
	if (retval)
		return retval;
	return kone_check_write(usb_dev);
}

/*
 * Reads profile data from mouse and stores it in @buf
 * @number: profile number to read
 * On success returns 0
 * On failure returns errno
 */
static int kone_get_profile(struct usb_device *usb_dev,
		struct kone_profile *buf, int number)
{
	int len;

	if (number < 1 || number > 5)
		return -EINVAL;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			USB_REQ_CLEAR_FEATURE,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			kone_command_profile, number, buf,
			sizeof(struct kone_profile), USB_CTRL_SET_TIMEOUT);

	if (len != sizeof(struct kone_profile))
		return -EIO;

	return 0;
}

/*
 * Writes profile data to mouse.
 * @number: profile number to write
 * On success returns 0
 * On failure returns errno
 */
static int kone_set_profile(struct usb_device *usb_dev,
		struct kone_profile const *profile, int number)
{
	int len;

	if (number < 1 || number > 5)
		return -EINVAL;

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_CONFIGURATION,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			kone_command_profile, number, (void *)profile,
			sizeof(struct kone_profile),
			USB_CTRL_SET_TIMEOUT);

	if (len != sizeof(struct kone_profile))
		return len;

	if (kone_check_write(usb_dev))
		return -EIO;

	return 0;
}

/*
 * Reads value of "fast-clip-weight" and stores it in @result
 * On success returns 0
 * On failure returns errno
 */
static int kone_get_weight(struct usb_device *usb_dev, int *result)
{
	int retval;
	uint8_t data;

	retval = kone_receive(usb_dev, kone_command_weight, &data, 1);

	if (retval)
		return retval;

	*result = (int)data;
	return 0;
}

/*
 * Reads firmware_version of mouse and stores it in @result
 * On success returns 0
 * On failure returns errno
 */
static int kone_get_firmware_version(struct usb_device *usb_dev, int *result)
{
	int retval;
	uint16_t data;

	retval = kone_receive(usb_dev, kone_command_firmware_version,
			&data, 2);
	if (retval)
		return retval;

	*result = le16_to_cpu(data);
	return 0;
}

static ssize_t kone_sysfs_read_settings(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct kone_settings))
		return 0;

	if (off + count > sizeof(struct kone_settings))
		count = sizeof(struct kone_settings) - off;

	mutex_lock(&kone->kone_lock);
	memcpy(buf, ((char const *)&kone->settings) + off, count);
	mutex_unlock(&kone->kone_lock);

	return count;
}

/*
 * Writing settings automatically activates startup_profile.
 * This function keeps values in kone_device up to date and assumes that in
 * case of error the old data is still valid
 */
static ssize_t kone_sysfs_write_settings(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval = 0, difference, old_profile;

	/* I need to get my data in one piece */
	if (off != 0 || count != sizeof(struct kone_settings))
		return -EINVAL;

	mutex_lock(&kone->kone_lock);
	difference = memcmp(buf, &kone->settings, sizeof(struct kone_settings));
	if (difference) {
		retval = kone_set_settings(usb_dev,
				(struct kone_settings const *)buf);
		if (retval) {
			mutex_unlock(&kone->kone_lock);
			return retval;
		}

		old_profile = kone->settings.startup_profile;
		memcpy(&kone->settings, buf, sizeof(struct kone_settings));

		kone_profile_activated(kone, kone->settings.startup_profile);

		if (kone->settings.startup_profile != old_profile)
			kone_profile_report(kone, kone->settings.startup_profile);
	}
	mutex_unlock(&kone->kone_lock);

	return sizeof(struct kone_settings);
}
static BIN_ATTR(settings, 0660, kone_sysfs_read_settings,
		kone_sysfs_write_settings, sizeof(struct kone_settings));

static ssize_t kone_sysfs_read_profilex(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count) {
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct kone_profile))
		return 0;

	if (off + count > sizeof(struct kone_profile))
		count = sizeof(struct kone_profile) - off;

	mutex_lock(&kone->kone_lock);
	memcpy(buf, ((char const *)&kone->profiles[*(uint *)(attr->private)]) + off, count);
	mutex_unlock(&kone->kone_lock);

	return count;
}

/* Writes data only if different to stored data */
static ssize_t kone_sysfs_write_profilex(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count) {
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	struct kone_profile *profile;
	int retval = 0, difference;

	/* I need to get my data in one piece */
	if (off != 0 || count != sizeof(struct kone_profile))
		return -EINVAL;

	profile = &kone->profiles[*(uint *)(attr->private)];

	mutex_lock(&kone->kone_lock);
	difference = memcmp(buf, profile, sizeof(struct kone_profile));
	if (difference) {
		retval = kone_set_profile(usb_dev,
				(struct kone_profile const *)buf,
				*(uint *)(attr->private) + 1);
		if (!retval)
			memcpy(profile, buf, sizeof(struct kone_profile));
	}
	mutex_unlock(&kone->kone_lock);

	if (retval)
		return retval;

	return sizeof(struct kone_profile);
}
#define PROFILE_ATTR(number)					\
static struct bin_attribute bin_attr_profile##number = {	\
	.attr = { .name = "profile" #number, .mode = 0660 },	\
	.size = sizeof(struct kone_profile),			\
	.read = kone_sysfs_read_profilex,			\
	.write = kone_sysfs_write_profilex,			\
	.private = &profile_numbers[number-1],			\
}
PROFILE_ATTR(1);
PROFILE_ATTR(2);
PROFILE_ATTR(3);
PROFILE_ATTR(4);
PROFILE_ATTR(5);

static ssize_t kone_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->actual_profile);
}
static DEVICE_ATTR(actual_profile, 0440, kone_sysfs_show_actual_profile, NULL);

static ssize_t kone_sysfs_show_actual_dpi(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->actual_dpi);
}
static DEVICE_ATTR(actual_dpi, 0440, kone_sysfs_show_actual_dpi, NULL);

/* weight is read each time, since we don't get informed when it's changed */
static ssize_t kone_sysfs_show_weight(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone;
	struct usb_device *usb_dev;
	int weight = 0;
	int retval;

	dev = dev->parent->parent;
	kone = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	mutex_lock(&kone->kone_lock);
	retval = kone_get_weight(usb_dev, &weight);
	mutex_unlock(&kone->kone_lock);

	if (retval)
		return retval;
	return snprintf(buf, PAGE_SIZE, "%d\n", weight);
}
static DEVICE_ATTR(weight, 0440, kone_sysfs_show_weight, NULL);

static ssize_t kone_sysfs_show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->firmware_version);
}
static DEVICE_ATTR(firmware_version, 0440, kone_sysfs_show_firmware_version,
		   NULL);

static ssize_t kone_sysfs_show_tcu(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->settings.tcu);
}

static int kone_tcu_command(struct usb_device *usb_dev, int number)
{
	unsigned char value;

	value = number;
	return kone_send(usb_dev, kone_command_calibrate, &value, 1);
}

/*
 * Calibrating the tcu is the only action that changes settings data inside the
 * mouse, so this data needs to be reread
 */
static ssize_t kone_sysfs_set_tcu(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct kone_device *kone;
	struct usb_device *usb_dev;
	int retval;
	unsigned long state;

	dev = dev->parent->parent;
	kone = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	retval = kstrtoul(buf, 10, &state);
	if (retval)
		return retval;

	if (state != 0 && state != 1)
		return -EINVAL;

	mutex_lock(&kone->kone_lock);

	if (state == 1) { /* state activate */
		retval = kone_tcu_command(usb_dev, 1);
		if (retval)
			goto exit_unlock;
		retval = kone_tcu_command(usb_dev, 2);
		if (retval)
			goto exit_unlock;
		ssleep(5); /* tcu needs this time for calibration */
		retval = kone_tcu_command(usb_dev, 3);
		if (retval)
			goto exit_unlock;
		retval = kone_tcu_command(usb_dev, 0);
		if (retval)
			goto exit_unlock;
		retval = kone_tcu_command(usb_dev, 4);
		if (retval)
			goto exit_unlock;
		/*
		 * Kone needs this time to settle things.
		 * Reading settings too early will result in invalid data.
		 * Roccat's driver waits 1 sec, maybe this time could be
		 * shortened.
		 */
		ssleep(1);
	}

	/* calibration changes values in settings, so reread */
	retval = kone_get_settings(usb_dev, &kone->settings);
	if (retval)
		goto exit_no_settings;

	/* only write settings back if activation state is different */
	if (kone->settings.tcu != state) {
		kone->settings.tcu = state;
		kone_set_settings_checksum(&kone->settings);

		retval = kone_set_settings(usb_dev, &kone->settings);
		if (retval) {
			dev_err(&usb_dev->dev, "couldn't set tcu state\n");
			/*
			 * try to reread valid settings into buffer overwriting
			 * first error code
			 */
			retval = kone_get_settings(usb_dev, &kone->settings);
			if (retval)
				goto exit_no_settings;
			goto exit_unlock;
		}
		/* calibration resets profile */
		kone_profile_activated(kone, kone->settings.startup_profile);
	}

	retval = size;
exit_no_settings:
	dev_err(&usb_dev->dev, "couldn't read settings\n");
exit_unlock:
	mutex_unlock(&kone->kone_lock);
	return retval;
}
static DEVICE_ATTR(tcu, 0660, kone_sysfs_show_tcu, kone_sysfs_set_tcu);

static ssize_t kone_sysfs_show_startup_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->settings.startup_profile);
}

static ssize_t kone_sysfs_set_startup_profile(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct kone_device *kone;
	struct usb_device *usb_dev;
	int retval;
	unsigned long new_startup_profile;

	dev = dev->parent->parent;
	kone = hid_get_drvdata(dev_get_drvdata(dev));
	usb_dev = interface_to_usbdev(to_usb_interface(dev));

	retval = kstrtoul(buf, 10, &new_startup_profile);
	if (retval)
		return retval;

	if (new_startup_profile  < 1 || new_startup_profile > 5)
		return -EINVAL;

	mutex_lock(&kone->kone_lock);

	kone->settings.startup_profile = new_startup_profile;
	kone_set_settings_checksum(&kone->settings);

	retval = kone_set_settings(usb_dev, &kone->settings);
	if (retval) {
		mutex_unlock(&kone->kone_lock);
		return retval;
	}

	/* changing the startup profile immediately activates this profile */
	kone_profile_activated(kone, new_startup_profile);
	kone_profile_report(kone, new_startup_profile);

	mutex_unlock(&kone->kone_lock);
	return size;
}
static DEVICE_ATTR(startup_profile, 0660, kone_sysfs_show_startup_profile,
		   kone_sysfs_set_startup_profile);

static struct attribute *kone_attrs[] = {
	/*
	 * Read actual dpi settings.
	 * Returns raw value for further processing. Refer to enum
	 * kone_polling_rates to get real value.
	 */
	&dev_attr_actual_dpi.attr,
	&dev_attr_actual_profile.attr,

	/*
	 * The mouse can be equipped with one of four supplied weights from 5
	 * to 20 grams which are recognized and its value can be read out.
	 * This returns the raw value reported by the mouse for easy evaluation
	 * by software. Refer to enum kone_weights to get corresponding real
	 * weight.
	 */
	&dev_attr_weight.attr,

	/*
	 * Prints firmware version stored in mouse as integer.
	 * The raw value reported by the mouse is returned for easy evaluation,
	 * to get the real version number the decimal point has to be shifted 2
	 * positions to the left. E.g. a value of 138 means 1.38.
	 */
	&dev_attr_firmware_version.attr,

	/*
	 * Prints state of Tracking Control Unit as number where 0 = off and
	 * 1 = on. Writing 0 deactivates tcu and writing 1 calibrates and
	 * activates the tcu
	 */
	&dev_attr_tcu.attr,

	/* Prints and takes the number of the profile the mouse starts with */
	&dev_attr_startup_profile.attr,
	NULL,
};

static struct bin_attribute *kone_bin_attributes[] = {
	&bin_attr_settings,
	&bin_attr_profile1,
	&bin_attr_profile2,
	&bin_attr_profile3,
	&bin_attr_profile4,
	&bin_attr_profile5,
	NULL,
};

static const struct attribute_group kone_group = {
	.attrs = kone_attrs,
	.bin_attrs = kone_bin_attributes,
};

static const struct attribute_group *kone_groups[] = {
	&kone_group,
	NULL,
};

static int kone_init_kone_device_struct(struct usb_device *usb_dev,
		struct kone_device *kone)
{
	uint i;
	int retval;

	mutex_init(&kone->kone_lock);

	for (i = 0; i < 5; ++i) {
		retval = kone_get_profile(usb_dev, &kone->profiles[i], i + 1);
		if (retval)
			return retval;
	}

	retval = kone_get_settings(usb_dev, &kone->settings);
	if (retval)
		return retval;

	retval = kone_get_firmware_version(usb_dev, &kone->firmware_version);
	if (retval)
		return retval;

	kone_profile_activated(kone, kone->settings.startup_profile);

	return 0;
}

/*
 * Since IGNORE_MOUSE quirk moved to hid-apple, there is no way to bind only to
 * mousepart if usb_hid is compiled into the kernel and kone is compiled as
 * module.
 * Secial behaviour is bound only to mousepart since only mouseevents contain
 * additional notifications.
 */
static int kone_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct kone_device *kone;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {

		kone = kzalloc(sizeof(*kone), GFP_KERNEL);
		if (!kone)
			return -ENOMEM;
		hid_set_drvdata(hdev, kone);

		retval = kone_init_kone_device_struct(usb_dev, kone);
		if (retval) {
			hid_err(hdev, "couldn't init struct kone_device\n");
			goto exit_free;
		}

		retval = roccat_connect(kone_class, hdev,
				sizeof(struct kone_roccat_report));
		if (retval < 0) {
			hid_err(hdev, "couldn't init char dev\n");
			/* be tolerant about not getting chrdev */
		} else {
			kone->roccat_claimed = 1;
			kone->chrdev_minor = retval;
		}
	} else {
		hid_set_drvdata(hdev, NULL);
	}

	return 0;
exit_free:
	kfree(kone);
	return retval;
}

static void kone_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct kone_device *kone;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {
		kone = hid_get_drvdata(hdev);
		if (kone->roccat_claimed)
			roccat_disconnect(kone->chrdev_minor);
		kfree(hid_get_drvdata(hdev));
	}
}

static int kone_probe(struct hid_device *hdev, const struct hid_device_id *id)
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

	retval = kone_init_specials(hdev);
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

static void kone_remove(struct hid_device *hdev)
{
	kone_remove_specials(hdev);
	hid_hw_stop(hdev);
}

/* handle special events and keep actual profile and dpi values up to date */
static void kone_keep_values_up_to_date(struct kone_device *kone,
		struct kone_mouse_event const *event)
{
	switch (event->event) {
	case kone_mouse_event_switch_profile:
		kone->actual_dpi = kone->profiles[event->value - 1].
				startup_dpi;
	case kone_mouse_event_osd_profile:
		kone->actual_profile = event->value;
		break;
	case kone_mouse_event_switch_dpi:
	case kone_mouse_event_osd_dpi:
		kone->actual_dpi = event->value;
		break;
	}
}

static void kone_report_to_chrdev(struct kone_device const *kone,
		struct kone_mouse_event const *event)
{
	struct kone_roccat_report roccat_report;

	switch (event->event) {
	case kone_mouse_event_switch_profile:
	case kone_mouse_event_switch_dpi:
	case kone_mouse_event_osd_profile:
	case kone_mouse_event_osd_dpi:
		roccat_report.event = event->event;
		roccat_report.value = event->value;
		roccat_report.key = 0;
		roccat_report_event(kone->chrdev_minor,
				(uint8_t *)&roccat_report);
		break;
	case kone_mouse_event_call_overlong_macro:
	case kone_mouse_event_multimedia:
		if (event->value == kone_keystroke_action_press) {
			roccat_report.event = event->event;
			roccat_report.value = kone->actual_profile;
			roccat_report.key = event->macro_key;
			roccat_report_event(kone->chrdev_minor,
					(uint8_t *)&roccat_report);
		}
		break;
	}

}

/*
 * Is called for keyboard- and mousepart.
 * Only mousepart gets informations about special events in its extended event
 * structure.
 */
static int kone_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	struct kone_device *kone = hid_get_drvdata(hdev);
	struct kone_mouse_event *event = (struct kone_mouse_event *)data;

	/* keyboard events are always processed by default handler */
	if (size != sizeof(struct kone_mouse_event))
		return 0;

	if (kone == NULL)
		return 0;

	/*
	 * Firmware 1.38 introduced new behaviour for tilt and special buttons.
	 * Pressed button is reported in each movement event.
	 * Workaround sends only one event per press.
	 */
	if (memcmp(&kone->last_mouse_event.tilt, &event->tilt, 5))
		memcpy(&kone->last_mouse_event, event,
				sizeof(struct kone_mouse_event));
	else
		memset(&event->tilt, 0, 5);

	kone_keep_values_up_to_date(kone, event);

	if (kone->roccat_claimed)
		kone_report_to_chrdev(kone, event);

	return 0; /* always do further processing */
}

static const struct hid_device_id kone_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_KONE) },
	{ }
};

MODULE_DEVICE_TABLE(hid, kone_devices);

static struct hid_driver kone_driver = {
		.name = "kone",
		.id_table = kone_devices,
		.probe = kone_probe,
		.remove = kone_remove,
		.raw_event = kone_raw_event
};

static int __init kone_init(void)
{
	int retval;

	/* class name has to be same as driver name */
	kone_class = class_create(THIS_MODULE, "kone");
	if (IS_ERR(kone_class))
		return PTR_ERR(kone_class);
	kone_class->dev_groups = kone_groups;

	retval = hid_register_driver(&kone_driver);
	if (retval)
		class_destroy(kone_class);
	return retval;
}

static void __exit kone_exit(void)
{
	hid_unregister_driver(&kone_driver);
	class_destroy(kone_class);
}

module_init(kone_init);
module_exit(kone_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Kone driver");
MODULE_LICENSE("GPL v2");
