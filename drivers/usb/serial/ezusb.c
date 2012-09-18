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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>

struct ezusb_fx_type {
	/* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */
	unsigned short cpucs_reg;
	unsigned short max_internal_adress;
};

struct ezusb_fx_type ezusb_fx1 = {
	.cpucs_reg = 0x7F92,
	.max_internal_adress = 0x1B3F,
};

struct ezusb_fx_type ezusb_fx2 = {
	.cpucs_reg = 0xE600,
	.max_internal_adress = 0x3FFF,
};

/* Commands for writing to memory */
#define WRITE_INT_RAM 0xA0
#define WRITE_EXT_RAM 0xA3

int ezusb_writememory(struct usb_device *dev, int address,
				unsigned char *data, int length, __u8 request)
{
	int result;
	unsigned char *transfer_buffer;

	if (!dev)
		return -ENODEV;

	transfer_buffer = kmemdup(data, length, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&dev->dev, "%s - kmalloc(%d) failed.\n",
							__func__, length);
		return -ENOMEM;
	}
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
				 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				 address, 0, transfer_buffer, length, 3000);

	kfree(transfer_buffer);
	return result;
}
EXPORT_SYMBOL_GPL(ezusb_writememory);

int ezusb_set_reset(struct usb_device *dev, unsigned short cpucs_reg,
			 unsigned char reset_bit)
{
	int response = ezusb_writememory(dev, cpucs_reg, &reset_bit, 1, WRITE_INT_RAM);
	if (response < 0)
		dev_err(&dev->dev, "%s-%d failed: %d\n",
						__func__, reset_bit, response);
	return response;
}

int ezusb_fx1_set_reset(struct usb_device *dev, unsigned char reset_bit)
{
	return ezusb_set_reset(dev, ezusb_fx1.cpucs_reg, reset_bit);
}
EXPORT_SYMBOL_GPL(ezusb_fx1_set_reset);

int ezusb_fx2_set_reset(struct usb_device *dev, unsigned char reset_bit)
{
	return ezusb_set_reset(dev, ezusb_fx2.cpucs_reg, reset_bit);
}
EXPORT_SYMBOL_GPL(ezusb_fx2_set_reset);
