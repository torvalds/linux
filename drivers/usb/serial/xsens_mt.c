/*
 * Xsens MT USB driver
 *
 * Copyright (C) 2013 Xsens <info@xsens.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version
 *  2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

#define XSENS_VID 0x2639

#define MTi_10_IMU_PID		0x0001
#define MTi_20_VRU_PID		0x0002
#define MTi_30_AHRS_PID		0x0003

#define MTi_100_IMU_PID		0x0011
#define MTi_200_VRU_PID		0x0012
#define MTi_300_AHRS_PID	0x0013

#define MTi_G_700_GPS_INS_PID	0x0017

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(XSENS_VID, MTi_10_IMU_PID) },
	{ USB_DEVICE(XSENS_VID, MTi_20_VRU_PID) },
	{ USB_DEVICE(XSENS_VID, MTi_30_AHRS_PID) },

	{ USB_DEVICE(XSENS_VID, MTi_100_IMU_PID) },
	{ USB_DEVICE(XSENS_VID, MTi_200_VRU_PID) },
	{ USB_DEVICE(XSENS_VID, MTi_300_AHRS_PID) },

	{ USB_DEVICE(XSENS_VID, MTi_G_700_GPS_INS_PID) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static int has_required_endpoints(const struct usb_host_interface *interface)
{
	__u8 i;
	int has_bulk_in = 0;
	int has_bulk_out = 0;

	for (i = 0; i < interface->desc.bNumEndpoints; ++i) {
		if (usb_endpoint_is_bulk_in(&interface->endpoint[i].desc))
			has_bulk_in = 1;
		else if (usb_endpoint_is_bulk_out(&interface->endpoint[i].desc))
			has_bulk_out = 1;
	}

	return has_bulk_in && has_bulk_out;
}

static int xsens_mt_probe(struct usb_serial *serial,
					const struct usb_device_id *id)
{
	if (!has_required_endpoints(serial->interface->cur_altsetting))
		return -ENODEV;
	return 0;
}

static struct usb_serial_driver xsens_mt_device = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "xsens_mt",
	},
	.id_table = id_table,
	.num_ports = 1,

	.probe = xsens_mt_probe,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&xsens_mt_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_LICENSE("GPL");
