/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HID_ROCCAT_COMMON_H
#define __HID_ROCCAT_COMMON_H

/*
 * Copyright (c) 2011 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 */

#include <linux/usb.h>
#include <linux/types.h>

enum roccat_common2_commands {
	ROCCAT_COMMON_COMMAND_CONTROL = 0x4,
};

struct roccat_common2_control {
	uint8_t command;
	uint8_t value;
	uint8_t request; /* always 0 on requesting write check */
} __packed;

int roccat_common2_receive(struct usb_device *usb_dev, uint report_id,
		void *data, uint size);
int roccat_common2_send(struct usb_device *usb_dev, uint report_id,
		void const *data, uint size);
int roccat_common2_send_with_status(struct usb_device *usb_dev,
		uint command, void const *buf, uint size);

struct roccat_common2_device {
	int roccat_claimed;
	int chrdev_minor;
	struct mutex lock;
};

int roccat_common2_device_init_struct(struct usb_device *usb_dev,
		struct roccat_common2_device *dev);
ssize_t roccat_common2_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command);
ssize_t roccat_common2_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command);

#define ROCCAT_COMMON2_SYSFS_W(thingy, COMMAND, SIZE) \
static ssize_t roccat_common2_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, const struct bin_attribute *attr, \
		char *buf, loff_t off, size_t count) \
{ \
	return roccat_common2_sysfs_write(fp, kobj, buf, off, count, \
			SIZE, COMMAND); \
}

#define ROCCAT_COMMON2_SYSFS_R(thingy, COMMAND, SIZE) \
static ssize_t roccat_common2_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, const struct bin_attribute *attr, \
		char *buf, loff_t off, size_t count) \
{ \
	return roccat_common2_sysfs_read(fp, kobj, buf, off, count, \
			SIZE, COMMAND); \
}

#define ROCCAT_COMMON2_SYSFS_RW(thingy, COMMAND, SIZE) \
ROCCAT_COMMON2_SYSFS_W(thingy, COMMAND, SIZE) \
ROCCAT_COMMON2_SYSFS_R(thingy, COMMAND, SIZE)

#define ROCCAT_COMMON2_BIN_ATTRIBUTE_RW(thingy, COMMAND, SIZE) \
ROCCAT_COMMON2_SYSFS_RW(thingy, COMMAND, SIZE); \
static const struct bin_attribute bin_attr_ ## thingy = { \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = SIZE, \
	.read_new = roccat_common2_sysfs_read_ ## thingy, \
	.write_new = roccat_common2_sysfs_write_ ## thingy \
}

#define ROCCAT_COMMON2_BIN_ATTRIBUTE_R(thingy, COMMAND, SIZE) \
ROCCAT_COMMON2_SYSFS_R(thingy, COMMAND, SIZE); \
static const struct bin_attribute bin_attr_ ## thingy = { \
	.attr = { .name = #thingy, .mode = 0440 }, \
	.size = SIZE, \
	.read_new = roccat_common2_sysfs_read_ ## thingy, \
}

#define ROCCAT_COMMON2_BIN_ATTRIBUTE_W(thingy, COMMAND, SIZE) \
ROCCAT_COMMON2_SYSFS_W(thingy, COMMAND, SIZE); \
static const struct bin_attribute bin_attr_ ## thingy = { \
	.attr = { .name = #thingy, .mode = 0220 }, \
	.size = SIZE, \
	.write_new = roccat_common2_sysfs_write_ ## thingy \
}

#endif
