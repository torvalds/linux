/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __USBIP_HOST_DRIVER_H
#define __USBIP_HOST_DRIVER_H

#include <stdint.h>
#include "usbip_common.h"

struct usbip_host_driver {
	int ndevs;
	struct sysfs_driver *sysfs_driver;
	/* list of exported device */
	struct dlist *edev_list;
};

struct usbip_exported_device {
	struct sysfs_device *sudev;
	int32_t status;
	struct usbip_usb_device udev;
	struct usbip_usb_interface uinf[];
};

extern struct usbip_host_driver *host_driver;

int usbip_host_driver_open(void);
void usbip_host_driver_close(void);

int usbip_host_refresh_device_list(void);
int usbip_host_export_device(struct usbip_exported_device *edev, int sockfd);
struct usbip_exported_device *usbip_host_get_device(int num);

#endif /* __USBIP_HOST_DRIVER_H */
