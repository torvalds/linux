/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifndef _USBIP_COMMON_H
#define _USBIP_COMMON_H

#include <unistd.h>
#include <stdint.h>
#include <syslog.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include <sysfs/libsysfs.h>
#include <netdb.h>
#include <sys/socket.h>

#ifndef USBIDS_FILE
#define USBIDS_FILE "/usr/share/hwdata/usb.ids"
#endif

#ifndef VHCI_STATE_PATH
#define VHCI_STATE_PATH "/var/run/vhci_hcd"
#endif

//#include <linux/usb_ch9.h>
enum usb_device_speed {
	USB_SPEED_UNKNOWN = 0,                  /* enumerating */
	USB_SPEED_LOW, USB_SPEED_FULL,          /* usb 1.1 */
	USB_SPEED_HIGH,                         /* usb 2.0 */
	USB_SPEED_VARIABLE                      /* wireless (usb 2.5) */
};

/* FIXME: how to sync with drivers/usbip_common.h ? */
enum usbip_device_status{
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

extern int usbip_use_syslog;
extern int usbip_use_stderr;
extern int usbip_use_debug ;

#define err(fmt, args...)	do { \
	if (usbip_use_syslog) { \
		syslog(LOG_ERR, "usbip err: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__,  ##args); \
	} \
	if (usbip_use_stderr) { \
		fprintf(stderr, "usbip err: %13s:%4d (%-12s) " fmt "\n", \
			__FILE__, __LINE__, __FUNCTION__,  ##args); \
	} \
} while (0)

#define notice(fmt, args...)	do { \
	if (usbip_use_syslog) { \
		syslog(LOG_DEBUG, "usbip: " fmt, ##args); \
	} \
	if (usbip_use_stderr) { \
		fprintf(stderr, "usbip: " fmt "\n",  ##args); \
	} \
} while (0)

#define info(fmt, args...)	do { \
	if (usbip_use_syslog) { \
		syslog(LOG_DEBUG, fmt, ##args); \
	} \
	if (usbip_use_stderr) { \
		fprintf(stderr, fmt "\n",  ##args); \
	} \
} while (0)

#define dbg(fmt, args...)	do { \
	if (usbip_use_debug) { \
		if (usbip_use_syslog) { \
			syslog(LOG_DEBUG, "usbip dbg: %13s:%4d (%-12s) " fmt, \
				__FILE__, __LINE__, __FUNCTION__,  ##args); \
		} \
		if (usbip_use_stderr) { \
			fprintf(stderr, "usbip dbg: %13s:%4d (%-12s) " fmt "\n", \
				__FILE__, __LINE__, __FUNCTION__,  ##args); \
		} \
	} \
} while (0)


#define BUG()	do { err("sorry, it's a bug"); abort(); } while (0)


struct usb_interface {
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t padding;	/* alignment */
} __attribute__((packed));



struct usb_device {
	char path[SYSFS_PATH_MAX];
	char busid[SYSFS_BUS_ID_SIZE];

	uint32_t busnum;
	uint32_t devnum;
	uint32_t speed;

	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;

	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bConfigurationValue;
	uint8_t bNumConfigurations;
	uint8_t bNumInterfaces;
} __attribute__((packed));

#define to_string(s)	#s

void dump_usb_interface(struct usb_interface *);
void dump_usb_device(struct usb_device *);
int read_usb_device(struct sysfs_device *sdev, struct usb_device *udev);
int read_attr_value(struct sysfs_device *dev, const char *name, const char *format);
int read_usb_interface(struct usb_device *udev, int i, struct usb_interface *uinf);

const char *usbip_speed_string(int num);
const char *usbip_status_string(int32_t status);

int usbip_names_init(char *);
void usbip_names_free(void);
void usbip_names_get_product(char *buff, size_t size, uint16_t vendor, uint16_t product);
void usbip_names_get_class(char *buff, size_t size, uint8_t class, uint8_t subclass, uint8_t protocol);

#endif
