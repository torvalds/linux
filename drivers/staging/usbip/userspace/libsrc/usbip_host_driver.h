/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifndef __USBIP_STUB_DRIVER_H
#define __USBIP_STUB_DRIVER_H

#include <stdint.h>
#include "usbip_common.h"

struct usbip_stub_driver {
	int ndevs;
	struct sysfs_driver *sysfs_driver;

	struct dlist *edev_list;	/* list of exported device */
};

struct usbip_exported_device {
	struct sysfs_device *sudev;

	int32_t status;
	struct usbip_usb_device    udev;
	struct usbip_usb_interface uinf[];
};


extern struct usbip_stub_driver *stub_driver;

int usbip_stub_driver_open(void);
void usbip_stub_driver_close(void);

int usbip_stub_refresh_device_list(void);
int usbip_stub_export_device(struct usbip_exported_device *edev, int sockfd);

struct usbip_exported_device *usbip_stub_get_device(int num);

#endif /* __USBIP_STUB_DRIVER_H */
