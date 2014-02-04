/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifndef __VHCI_DRIVER_H
#define __VHCI_DRIVER_H

#include <sysfs/libsysfs.h>
#include <stdint.h>

#include "usbip_common.h"

#define USBIP_VHCI_BUS_TYPE "platform"
#define MAXNPORT 128

struct usbip_class_device {
	char class_path[SYSFS_PATH_MAX];
	char dev_path[SYSFS_PATH_MAX];
};

struct usbip_imported_device {
	uint8_t port;
	uint32_t status;

	uint32_t devid;

	uint8_t busnum;
	uint8_t devnum;

	/* usbip_class_device list */
	struct dlist *cdev_list;
	struct usbip_usb_device udev;
};

struct usbip_vhci_driver {
	char sysfs_mntpath[SYSFS_PATH_MAX];

	/* /sys/devices/platform/vhci_hcd */
	struct sysfs_device *hc_device;

	/* usbip_class_device list */
	struct dlist *cdev_list;

	int nports;
	struct usbip_imported_device idev[MAXNPORT];
};


extern struct usbip_vhci_driver *vhci_driver;

int usbip_vhci_driver_open(void);
void usbip_vhci_driver_close(void);

int  usbip_vhci_refresh_device_list(void);


int usbip_vhci_get_free_port(void);
int usbip_vhci_attach_device2(uint8_t port, int sockfd, uint32_t devid,
		uint32_t speed);

/* will be removed */
int usbip_vhci_attach_device(uint8_t port, int sockfd, uint8_t busnum,
		uint8_t devnum, uint32_t speed);

int usbip_vhci_detach_device(uint8_t port);

int usbip_vhci_imported_device_dump(struct usbip_imported_device *idev);

#endif /* __VHCI_DRIVER_H */
