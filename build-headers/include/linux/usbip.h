/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *	usbip.h
 *
 *	USBIP uapi defines and function prototypes etc.
*/

#ifndef _LINUX_USBIP_H
#define _LINUX_USBIP_H

/* usbip device status - exported in usbip device sysfs status */
enum usbip_device_status {
	/* sdev is available. */
	SDEV_ST_AVAILABLE = 0x01,
	/* sdev is now used. */
	SDEV_ST_USED,
	/* sdev is unusable because of a fatal error. */
	SDEV_ST_ERROR,

	/* vdev does not connect a remote device. */
	VDEV_ST_NULL,
	/* vdev is used, but the USB address is not assigned yet */
	VDEV_ST_NOTASSIGNED,
	VDEV_ST_USED,
	VDEV_ST_ERROR
};
#endif /* _LINUX_USBIP_H */
