/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fieldbus Device Driver Core
 *
 */

#ifndef __FIELDBUS_DEV_H
#define __FIELDBUS_DEV_H

#include <linux/cdev.h>
#include <linux/wait.h>

enum fieldbus_dev_type {
	FIELDBUS_DEV_TYPE_UNKNOWN = 0,
	FIELDBUS_DEV_TYPE_PROFINET,
};

/**
 * struct fieldbus_dev - Fieldbus device
 * @read_area:		[DRIVER] function to read the process data area of the
 *				 device. same parameters/return values as
 *				 the read function in struct file_operations
 * @write_area:		[DRIVER] function to write to the process data area of
 *				 the device. same parameters/return values as
 *				 the write function in struct file_operations
 * @write_area_sz	[DRIVER] size of the writable process data area
 * @read_area_sz	[DRIVER] size of the readable process data area
 * @card_name		[DRIVER] name of the card, e.g. "ACME Inc. profinet"
 * @fieldbus_type	[DRIVER] fieldbus type of this device, e.g.
 *					FIELDBUS_DEV_TYPE_PROFINET
 * @enable_get		[DRIVER] function which returns true if the card
 *				 is enabled, false otherwise
 * @fieldbus_id_get	[DRIVER] function to retrieve the unique fieldbus id
 *				 by which this device can be identified;
 *				 return value follows the snprintf convention
 * @simple_enable_set	[DRIVER] (optional) function to enable the device
 *				 according to its default settings
 * @parent		[DRIVER] (optional) the device's parent device
 */
struct fieldbus_dev {
	ssize_t (*read_area)(struct fieldbus_dev *fbdev, char __user *buf,
			     size_t size, loff_t *offset);
	ssize_t (*write_area)(struct fieldbus_dev *fbdev,
			      const char __user *buf, size_t size,
			      loff_t *offset);
	size_t write_area_sz, read_area_sz;
	const char *card_name;
	enum fieldbus_dev_type fieldbus_type;
	bool (*enable_get)(struct fieldbus_dev *fbdev);
	int (*fieldbus_id_get)(struct fieldbus_dev *fbdev, char *buf,
			       size_t max_size);
	int (*simple_enable_set)(struct fieldbus_dev *fbdev, bool enable);
	struct device *parent;

	/* private data */
	int id;
	struct cdev cdev;
	struct device *dev;
	int dc_event;
	wait_queue_head_t dc_wq;
	bool online;
};

#if IS_ENABLED(CONFIG_FIELDBUS_DEV)

/**
 * fieldbus_dev_unregister()
 *	- unregister a previously registered fieldbus device
 * @fb:		Device structure previously registered
 **/
void fieldbus_dev_unregister(struct fieldbus_dev *fb);

/**
 * fieldbus_dev_register()
 *	- register a device with the fieldbus device subsystem
 * @fb:		Device structure filled by the device driver
 **/
int __must_check fieldbus_dev_register(struct fieldbus_dev *fb);

/**
 * fieldbus_dev_area_updated()
 *	- notify the subsystem that an external fieldbus controller updated
 *			the process data area
 * @fb:		Device structure
 **/
void fieldbus_dev_area_updated(struct fieldbus_dev *fb);

/**
 * fieldbus_dev_online_changed()
 *	- notify the subsystem that the fieldbus online status changed
 * @fb:		Device structure
 **/
void fieldbus_dev_online_changed(struct fieldbus_dev *fb, bool online);

#else /* IS_ENABLED(CONFIG_FIELDBUS_DEV) */

static inline void fieldbus_dev_unregister(struct fieldbus_dev *fb) {}
static inline int __must_check fieldbus_dev_register(struct fieldbus_dev *fb)
{
	return -ENOTSUPP;
}

static inline void fieldbus_dev_area_updated(struct fieldbus_dev *fb) {}
static inline void fieldbus_dev_online_changed(struct fieldbus_dev *fb,
					       bool online) {}

#endif /* IS_ENABLED(CONFIG_FIELDBUS_DEV) */
#endif /* __FIELDBUS_DEV_H */
