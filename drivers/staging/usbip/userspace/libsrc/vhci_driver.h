/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifndef _VHCI_DRIVER_H
#define _VHCI_DRIVER_H

#include "usbip.h"



#define MAXNPORT 128

struct class_device {
	char clspath[SYSFS_PATH_MAX];
	char devpath[SYSFS_PATH_MAX];
};

struct usbip_imported_device {
	uint8_t port;
	uint32_t status;

	uint32_t devid;

	uint8_t busnum;
	uint8_t devnum;


	struct dlist *cdev_list;	/* list of class device */
	struct usb_device udev;
};

struct usbip_vhci_driver {
	char sysfs_mntpath[SYSFS_PATH_MAX];
	struct sysfs_device *hc_device; /* /sys/devices/platform/vhci_hcd */

	struct dlist *cdev_list;	/* list of class device */

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
#endif
