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
 * TODO implement notification mechanism for overlong macro execution
 *      If user wants to execute an overlong macro only the names of macroset
 *      and macro are given. Should userland tap hidraw or is there an
 *      additional streaming mechanism?
 *
 * TODO is it possible to overwrite group for sysfs attributes via udev?
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "hid-ids.h"
#include "hid-roccat.h"
#include "hid-roccat-kone.h"

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
	int len;
	unsigned char *data;

	data = kmalloc(1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	do {
		/*
		 * Mouse needs 50 msecs until it says ok, but there are
		 * 30 more msecs needed for next write to work.
		 */
		msleep(80);

		len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
				USB_REQ_CLEAR_FEATURE,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE |
				USB_DIR_IN,
				kone_command_confirm_write, 0, data, 1,
				USB_CTRL_SET_TIMEOUT);

		if (len != 1) {
			kfree(data);
			return -EIO;
		}

		/*
		 * value of 3 seems to mean something like
		 * "not finished yet, but it looks good"
		 * So check again after a moment.
		 */
	} while (*data == 3);

	if (*data == 1) { /* everything alright */
		kfree(data);
		return 0;
	} else { /* unknown answer */
		dev_err(&usb_dev->dev, "got retval %d when checking write\n",
				*data);
		kfree(data);
		return -EIO;
	}
}

/*
 * Reads settings from mouse and stores it in @buf
 * @buf has to be alloced with GFP_KERNEL
 * On success returns 0
 * On failure returns errno
 */
static int kone_get_settings(struct usb_device *usb_dev,
		struct kone_settings *buf)
{
	int len;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			USB_REQ_CLEAR_FEATURE,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			kone_command_settings, 0, buf,
			sizeof(struct kone_settings), USB_CTRL_SET_TIMEOUT);

	if (len != sizeof(struct kone_settings))
		return -EIO;

	return 0;
}

/*
 * Writes settings from @buf to mouse
 * On success returns 0
 * On failure returns errno
 */
static int kone_set_settings(struct usb_device *usb_dev,
		struct kone_settings const *settings)
{
	int len;

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_CONFIGURATION,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			kone_command_settings, 0, (char *)settings,
			sizeof(struct kone_settings),
			USB_CTRL_SET_TIMEOUT);

	if (len != sizeof(struct kone_settings))
		return -EIO;

	if (kone_check_write(usb_dev))
		return -EIO;

	return 0;
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
			kone_command_profile, number, (char *)profile,
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
	int len;
	uint8_t *data;

	data = kmalloc(1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			USB_REQ_CLEAR_FEATURE,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			kone_command_weight, 0, data, 1, USB_CTRL_SET_TIMEOUT);

	if (len != 1) {
		kfree(data);
		return -EIO;
	}
	*result = (int)*data;
	kfree(data);
	return 0;
}

/*
 * Reads firmware_version of mouse and stores it in @result
 * On success returns 0
 * On failure returns errno
 */
static int kone_get_firmware_version(struct usb_device *usb_dev, int *result)
{
	int len;
	unsigned char *data;

	data = kmalloc(2, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			USB_REQ_CLEAR_FEATURE,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			kone_command_firmware_version, 0, data, 2,
			USB_CTRL_SET_TIMEOUT);

	if (len != 2) {
		kfree(data);
		return -EIO;
	}
	*result = le16_to_cpu(*data);
	kfree(data);
	return 0;
}

static ssize_t kone_sysfs_read_settings(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	struct device *dev = container_of(kobj, struct device, kobj);
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct kone_settings))
		return 0;

	if (off + count > sizeof(struct kone_settings))
		count = sizeof(struct kone_settings) - off;

	mutex_lock(&kone->kone_lock);
	memcpy(buf, &kone->settings + off, count);
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
	struct device *dev = container_of(kobj, struct device, kobj);
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval = 0, difference;

	/* I need to get my data in one piece */
	if (off != 0 || count != sizeof(struct kone_settings))
		return -EINVAL;

	mutex_lock(&kone->kone_lock);
	difference = memcmp(buf, &kone->settings, sizeof(struct kone_settings));
	if (difference) {
		retval = kone_set_settings(usb_dev,
				(struct kone_settings const *)buf);
		if (!retval)
			memcpy(&kone->settings, buf,
					sizeof(struct kone_settings));
	}
	mutex_unlock(&kone->kone_lock);

	if (retval)
		return retval;

	/*
	 * If we get here, treat settings as okay and update actual values
	 * according to startup_profile
	 */
	kone->actual_profile = kone->settings.startup_profile;
	kone->actual_dpi = kone->profiles[kone->actual_profile - 1].startup_dpi;

	return sizeof(struct kone_settings);
}

static ssize_t kone_sysfs_read_profilex(struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count, int number) {
	struct device *dev = container_of(kobj, struct device, kobj);
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct kone_profile))
		return 0;

	if (off + count > sizeof(struct kone_profile))
		count = sizeof(struct kone_profile) - off;

	mutex_lock(&kone->kone_lock);
	memcpy(buf, &kone->profiles[number - 1], sizeof(struct kone_profile));
	mutex_unlock(&kone->kone_lock);

	return count;
}

static ssize_t kone_sysfs_read_profile1(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_read_profilex(kobj, attr, buf, off, count, 1);
}

static ssize_t kone_sysfs_read_profile2(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_read_profilex(kobj, attr, buf, off, count, 2);
}

static ssize_t kone_sysfs_read_profile3(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_read_profilex(kobj, attr, buf, off, count, 3);
}

static ssize_t kone_sysfs_read_profile4(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_read_profilex(kobj, attr, buf, off, count, 4);
}

static ssize_t kone_sysfs_read_profile5(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_read_profilex(kobj, attr, buf, off, count, 5);
}

/* Writes data only if different to stored data */
static ssize_t kone_sysfs_write_profilex(struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count, int number) {
	struct device *dev = container_of(kobj, struct device, kobj);
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	struct kone_profile *profile;
	int retval = 0, difference;

	/* I need to get my data in one piece */
	if (off != 0 || count != sizeof(struct kone_profile))
		return -EINVAL;

	profile = &kone->profiles[number - 1];

	mutex_lock(&kone->kone_lock);
	difference = memcmp(buf, profile, sizeof(struct kone_profile));
	if (difference) {
		retval = kone_set_profile(usb_dev,
				(struct kone_profile const *)buf, number);
		if (!retval)
			memcpy(profile, buf, sizeof(struct kone_profile));
	}
	mutex_unlock(&kone->kone_lock);

	if (retval)
		return retval;

	return sizeof(struct kone_profile);
}

static ssize_t kone_sysfs_write_profile1(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_write_profilex(kobj, attr, buf, off, count, 1);
}

static ssize_t kone_sysfs_write_profile2(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_write_profilex(kobj, attr, buf, off, count, 2);
}

static ssize_t kone_sysfs_write_profile3(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_write_profilex(kobj, attr, buf, off, count, 3);
}

static ssize_t kone_sysfs_write_profile4(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_write_profilex(kobj, attr, buf, off, count, 4);
}

static ssize_t kone_sysfs_write_profile5(struct file *fp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t off, size_t count) {
	return kone_sysfs_write_profilex(kobj, attr, buf, off, count, 5);
}

static ssize_t kone_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->actual_profile);
}

static ssize_t kone_sysfs_show_actual_dpi(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->actual_dpi);
}

/* weight is read each time, since we don't get informed when it's changed */
static ssize_t kone_sysfs_show_weight(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int weight = 0;
	int retval;

	mutex_lock(&kone->kone_lock);
	retval = kone_get_weight(usb_dev, &weight);
	mutex_unlock(&kone->kone_lock);

	if (retval)
		return retval;
	return snprintf(buf, PAGE_SIZE, "%d\n", weight);
}

static ssize_t kone_sysfs_show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->firmware_version);
}

static ssize_t kone_sysfs_show_tcu(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->settings.tcu);
}

static int kone_tcu_command(struct usb_device *usb_dev, int number)
{
	int len;
	char *value;

	value = kmalloc(1, GFP_KERNEL);
	if (!value)
		return -ENOMEM;

	*value = number;

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_CONFIGURATION,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			kone_command_calibrate, 0, value, 1,
			USB_CTRL_SET_TIMEOUT);

	kfree(value);
	return ((len != 1) ? -EIO : 0);
}

/*
 * Calibrating the tcu is the only action that changes settings data inside the
 * mouse, so this data needs to be reread
 */
static ssize_t kone_sysfs_set_tcu(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;
	unsigned long state;

	retval = strict_strtoul(buf, 10, &state);
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
	}

	retval = size;
exit_no_settings:
	dev_err(&usb_dev->dev, "couldn't read settings\n");
exit_unlock:
	mutex_unlock(&kone->kone_lock);
	return retval;
}

static ssize_t kone_sysfs_show_startup_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	return snprintf(buf, PAGE_SIZE, "%d\n", kone->settings.startup_profile);
}

static ssize_t kone_sysfs_set_startup_profile(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct kone_device *kone = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;
	unsigned long new_startup_profile;

	retval = strict_strtoul(buf, 10, &new_startup_profile);
	if (retval)
		return retval;

	if (new_startup_profile  < 1 || new_startup_profile > 5)
		return -EINVAL;

	mutex_lock(&kone->kone_lock);

	kone->settings.startup_profile = new_startup_profile;
	kone_set_settings_checksum(&kone->settings);

	retval = kone_set_settings(usb_dev, &kone->settings);

	mutex_unlock(&kone->kone_lock);

	if (retval)
		return retval;

	/* changing the startup profile immediately activates this profile */
	kone->actual_profile = new_startup_profile;
	kone->actual_dpi = kone->profiles[kone->actual_profile - 1].startup_dpi;

	return size;
}

/*
 * This file is used by userland software to find devices that are handled by
 * this driver. This provides a consistent way for actual and older kernels
 * where this driver replaced usbhid instead of generic-usb.
 * Driver capabilities are determined by version number.
 */
static ssize_t kone_sysfs_show_driver_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, ROCCAT_KONE_DRIVER_VERSION "\n");
}

/*
 * Read actual dpi settings.
 * Returns raw value for further processing. Refer to enum kone_polling_rates to
 * get real value.
 */
static DEVICE_ATTR(actual_dpi, 0440, kone_sysfs_show_actual_dpi, NULL);

static DEVICE_ATTR(actual_profile, 0440, kone_sysfs_show_actual_profile, NULL);

/*
 * The mouse can be equipped with one of four supplied weights from 5 to 20
 * grams which are recognized and its value can be read out.
 * This returns the raw value reported by the mouse for easy evaluation by
 * software. Refer to enum kone_weights to get corresponding real weight.
 */
static DEVICE_ATTR(weight, 0440, kone_sysfs_show_weight, NULL);

/*
 * Prints firmware version stored in mouse as integer.
 * The raw value reported by the mouse is returned for easy evaluation, to get
 * the real version number the decimal point has to be shifted 2 positions to
 * the left. E.g. a value of 138 means 1.38.
 */
static DEVICE_ATTR(firmware_version, 0440,
		kone_sysfs_show_firmware_version, NULL);

/*
 * Prints state of Tracking Control Unit as number where 0 = off and 1 = on
 * Writing 0 deactivates tcu and writing 1 calibrates and activates the tcu
 */
static DEVICE_ATTR(tcu, 0660, kone_sysfs_show_tcu, kone_sysfs_set_tcu);

/* Prints and takes the number of the profile the mouse starts with */
static DEVICE_ATTR(startup_profile, 0660,
		kone_sysfs_show_startup_profile,
		kone_sysfs_set_startup_profile);

static DEVICE_ATTR(kone_driver_version, 0440,
		kone_sysfs_show_driver_version, NULL);

static struct attribute *kone_attributes[] = {
		&dev_attr_actual_dpi.attr,
		&dev_attr_actual_profile.attr,
		&dev_attr_weight.attr,
		&dev_attr_firmware_version.attr,
		&dev_attr_tcu.attr,
		&dev_attr_startup_profile.attr,
		&dev_attr_kone_driver_version.attr,
		NULL
};

static struct attribute_group kone_attribute_group = {
		.attrs = kone_attributes
};

static struct bin_attribute kone_settings_attr = {
	.attr = { .name = "settings", .mode = 0660 },
	.size = sizeof(struct kone_settings),
	.read = kone_sysfs_read_settings,
	.write = kone_sysfs_write_settings
};

static struct bin_attribute kone_profile1_attr = {
	.attr = { .name = "profile1", .mode = 0660 },
	.size = sizeof(struct kone_profile),
	.read = kone_sysfs_read_profile1,
	.write = kone_sysfs_write_profile1
};

static struct bin_attribute kone_profile2_attr = {
	.attr = { .name = "profile2", .mode = 0660 },
	.size = sizeof(struct kone_profile),
	.read = kone_sysfs_read_profile2,
	.write = kone_sysfs_write_profile2
};

static struct bin_attribute kone_profile3_attr = {
	.attr = { .name = "profile3", .mode = 0660 },
	.size = sizeof(struct kone_profile),
	.read = kone_sysfs_read_profile3,
	.write = kone_sysfs_write_profile3
};

static struct bin_attribute kone_profile4_attr = {
	.attr = { .name = "profile4", .mode = 0660 },
	.size = sizeof(struct kone_profile),
	.read = kone_sysfs_read_profile4,
	.write = kone_sysfs_write_profile4
};

static struct bin_attribute kone_profile5_attr = {
	.attr = { .name = "profile5", .mode = 0660 },
	.size = sizeof(struct kone_profile),
	.read = kone_sysfs_read_profile5,
	.write = kone_sysfs_write_profile5
};

static int kone_create_sysfs_attributes(struct usb_interface *intf)
{
	int retval;

	retval = sysfs_create_group(&intf->dev.kobj, &kone_attribute_group);
	if (retval)
		goto exit_1;

	retval = sysfs_create_bin_file(&intf->dev.kobj, &kone_settings_attr);
	if (retval)
		goto exit_2;

	retval = sysfs_create_bin_file(&intf->dev.kobj, &kone_profile1_attr);
	if (retval)
		goto exit_3;

	retval = sysfs_create_bin_file(&intf->dev.kobj, &kone_profile2_attr);
	if (retval)
		goto exit_4;

	retval = sysfs_create_bin_file(&intf->dev.kobj, &kone_profile3_attr);
	if (retval)
		goto exit_5;

	retval = sysfs_create_bin_file(&intf->dev.kobj, &kone_profile4_attr);
	if (retval)
		goto exit_6;

	retval = sysfs_create_bin_file(&intf->dev.kobj, &kone_profile5_attr);
	if (retval)
		goto exit_7;

	return 0;

exit_7:
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile4_attr);
exit_6:
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile3_attr);
exit_5:
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile2_attr);
exit_4:
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile1_attr);
exit_3:
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_settings_attr);
exit_2:
	sysfs_remove_group(&intf->dev.kobj, &kone_attribute_group);
exit_1:
	return retval;
}

static void kone_remove_sysfs_attributes(struct usb_interface *intf)
{
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile5_attr);
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile4_attr);
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile3_attr);
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile2_attr);
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_profile1_attr);
	sysfs_remove_bin_file(&intf->dev.kobj, &kone_settings_attr);
	sysfs_remove_group(&intf->dev.kobj, &kone_attribute_group);
}

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

	kone->actual_profile = kone->settings.startup_profile;
	kone->actual_dpi = kone->profiles[kone->actual_profile].startup_dpi;

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
		if (!kone) {
			dev_err(&hdev->dev, "can't alloc device descriptor\n");
			return -ENOMEM;
		}
		hid_set_drvdata(hdev, kone);

		retval = kone_init_kone_device_struct(usb_dev, kone);
		if (retval) {
			dev_err(&hdev->dev,
					"couldn't init struct kone_device\n");
			goto exit_free;
		}

		retval = roccat_connect(hdev);
		if (retval < 0) {
			dev_err(&hdev->dev, "couldn't init char dev\n");
			/* be tolerant about not getting chrdev */
		} else {
			kone->roccat_claimed = 1;
			kone->chrdev_minor = retval;
		}

		retval = kone_create_sysfs_attributes(intf);
		if (retval) {
			dev_err(&hdev->dev, "cannot create sysfs files\n");
			goto exit_free;
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
		kone_remove_sysfs_attributes(intf);
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
		dev_err(&hdev->dev, "parse failed\n");
		goto exit;
	}

	retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (retval) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto exit;
	}

	retval = kone_init_specials(hdev);
	if (retval) {
		dev_err(&hdev->dev, "couldn't install mouse\n");
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
	case kone_mouse_event_osd_profile:
		kone->actual_profile = event->value;
		kone->actual_dpi = kone->profiles[kone->actual_profile - 1].
				startup_dpi;
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
				(uint8_t *)&roccat_report,
				sizeof(struct kone_roccat_report));
		break;
	case kone_mouse_event_call_overlong_macro:
		if (event->value == kone_keystroke_action_press) {
			roccat_report.event = kone_mouse_event_call_overlong_macro;
			roccat_report.value = kone->actual_profile;
			roccat_report.key = event->macro_key;
			roccat_report_event(kone->chrdev_minor,
					(uint8_t *)&roccat_report,
					sizeof(struct kone_roccat_report));
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
	return hid_register_driver(&kone_driver);
}

static void __exit kone_exit(void)
{
	hid_unregister_driver(&kone_driver);
}

module_init(kone_init);
module_exit(kone_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Kone driver");
MODULE_LICENSE("GPL v2");
