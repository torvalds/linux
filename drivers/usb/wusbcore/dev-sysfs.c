// SPDX-License-Identifier: GPL-2.0
/*
 * WUSB devices
 * sysfs bindings
 *
 * Copyright (C) 2007 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * Get them out of the way...
 */

#include <linux/jiffies.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>
#include "wusbhc.h"

static ssize_t wusb_disconnect_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct usb_device *usb_dev;
	struct wusbhc *wusbhc;
	unsigned command;
	u8 port_idx;

	if (sscanf(buf, "%u", &command) != 1)
		return -EINVAL;
	if (command == 0)
		return size;
	usb_dev = to_usb_device(dev);
	wusbhc = wusbhc_get_by_usb_dev(usb_dev);
	if (wusbhc == NULL)
		return -ENODEV;

	mutex_lock(&wusbhc->mutex);
	port_idx = wusb_port_no_to_idx(usb_dev->portnum);
	__wusbhc_dev_disable(wusbhc, port_idx);
	mutex_unlock(&wusbhc->mutex);
	wusbhc_put(wusbhc);
	return size;
}
static DEVICE_ATTR_WO(wusb_disconnect);

static ssize_t wusb_cdid_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	ssize_t result;
	struct wusb_dev *wusb_dev;

	wusb_dev = wusb_dev_get_by_usb_dev(to_usb_device(dev));
	if (wusb_dev == NULL)
		return -ENODEV;
	result = sprintf(buf, "%16ph\n", wusb_dev->cdid.data);
	wusb_dev_put(wusb_dev);
	return result;
}
static DEVICE_ATTR_RO(wusb_cdid);

static ssize_t wusb_ck_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int result;
	struct usb_device *usb_dev;
	struct wusbhc *wusbhc;
	struct wusb_ckhdid ck;

	result = sscanf(buf,
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n",
			&ck.data[0] , &ck.data[1],
			&ck.data[2] , &ck.data[3],
			&ck.data[4] , &ck.data[5],
			&ck.data[6] , &ck.data[7],
			&ck.data[8] , &ck.data[9],
			&ck.data[10], &ck.data[11],
			&ck.data[12], &ck.data[13],
			&ck.data[14], &ck.data[15]);
	if (result != 16)
		return -EINVAL;

	usb_dev = to_usb_device(dev);
	wusbhc = wusbhc_get_by_usb_dev(usb_dev);
	if (wusbhc == NULL)
		return -ENODEV;
	result = wusb_dev_4way_handshake(wusbhc, usb_dev->wusb_dev, &ck);
	memzero_explicit(&ck, sizeof(ck));
	wusbhc_put(wusbhc);
	return result < 0 ? result : size;
}
static DEVICE_ATTR_WO(wusb_ck);

static struct attribute *wusb_dev_attrs[] = {
		&dev_attr_wusb_disconnect.attr,
		&dev_attr_wusb_cdid.attr,
		&dev_attr_wusb_ck.attr,
		NULL,
};

static const struct attribute_group wusb_dev_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = wusb_dev_attrs,
};

int wusb_dev_sysfs_add(struct wusbhc *wusbhc, struct usb_device *usb_dev,
		       struct wusb_dev *wusb_dev)
{
	int result = sysfs_create_group(&usb_dev->dev.kobj,
					&wusb_dev_attr_group);
	struct device *dev = &usb_dev->dev;
	if (result < 0)
		dev_err(dev, "Cannot register WUSB-dev attributes: %d\n",
			result);
	return result;
}

void wusb_dev_sysfs_rm(struct wusb_dev *wusb_dev)
{
	struct usb_device *usb_dev = wusb_dev->usb_dev;
	if (usb_dev)
		sysfs_remove_group(&usb_dev->dev.kobj, &wusb_dev_attr_group);
}
