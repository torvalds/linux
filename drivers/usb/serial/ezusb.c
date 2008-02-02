/*
 * EZ-USB specific functions used by some of the USB to Serial drivers.
 *
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

/* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */
#define CPUCS_REG    0x7F92

int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest)
{
	int result;
	unsigned char *transfer_buffer;

	/* dbg("ezusb_writememory %x, %d", address, length); */
	if (!serial->dev) {
		err("%s - no physical device present, failing.", __FUNCTION__);
		return -ENODEV;
	}

	transfer_buffer = kmemdup(data, length, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&serial->dev->dev, "%s - kmalloc(%d) failed.\n", __FUNCTION__, length);
		return -ENOMEM;
	}
	result = usb_control_msg (serial->dev, usb_sndctrlpipe(serial->dev, 0), bRequest, 0x40, address, 0, transfer_buffer, length, 3000);
	kfree (transfer_buffer);
	return result;
}

int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit)
{
	int response;

	/* dbg("%s - %d", __FUNCTION__, reset_bit); */
	response = ezusb_writememory (serial, CPUCS_REG, &reset_bit, 1, 0xa0);
	if (response < 0)
		dev_err(&serial->dev->dev, "%s- %d failed\n", __FUNCTION__, reset_bit);
	return response;
}


EXPORT_SYMBOL_GPL(ezusb_writememory);
EXPORT_SYMBOL_GPL(ezusb_set_reset);

