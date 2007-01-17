/* 
 * Emagic EMI 2|6 usb audio interface firmware loader.
 * Copyright (C) 2002
 * 	Tapio Laxström (tapio.laxstrom@iptime.fi)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, as published by
 * the Free Software Foundation, version 2.
 * 
 * emi26.c,v 1.13 2002/03/08 13:10:26 tapio Exp
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/delay.h>

#define MAX_INTEL_HEX_RECORD_LENGTH 16
typedef struct _INTEL_HEX_RECORD
{
	__u32	length;
	__u32	address;
	__u32	type;
	__u8	data[MAX_INTEL_HEX_RECORD_LENGTH];
} INTEL_HEX_RECORD, *PINTEL_HEX_RECORD;

/* include firmware (variables) */
#include "emi26_fw.h"

#define EMI26_VENDOR_ID 		0x086a  /* Emagic Soft-und Hardware GmBH */
#define EMI26_PRODUCT_ID		0x0100	/* EMI 2|6 without firmware */
#define EMI26B_PRODUCT_ID		0x0102	/* EMI 2|6 without firmware */

#define ANCHOR_LOAD_INTERNAL	0xA0	/* Vendor specific request code for Anchor Upload/Download (This one is implemented in the core) */
#define ANCHOR_LOAD_EXTERNAL	0xA3	/* This command is not implemented in the core. Requires firmware */
#define ANCHOR_LOAD_FPGA	0xA5	/* This command is not implemented in the core. Requires firmware. Emagic extension */
#define MAX_INTERNAL_ADDRESS	0x1B3F	/* This is the highest internal RAM address for the AN2131Q */
#define CPUCS_REG		0x7F92  /* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */ 
#define INTERNAL_RAM(address)   (address <= MAX_INTERNAL_ADDRESS)

static int emi26_writememory( struct usb_device *dev, int address, unsigned char *data, int length, __u8 bRequest);
static int emi26_set_reset(struct usb_device *dev, unsigned char reset_bit);
static int emi26_load_firmware (struct usb_device *dev);
static int emi26_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void emi26_disconnect(struct usb_interface *intf);
static int __init emi26_init (void);
static void __exit emi26_exit (void);


/* thanks to drivers/usb/serial/keyspan_pda.c code */
static int emi26_writememory (struct usb_device *dev, int address, unsigned char *data, int length, __u8 request)
{
	int result;
	unsigned char *buffer =  kmemdup(data, length, GFP_KERNEL);

	if (!buffer) {
		err("emi26: kmalloc(%d) failed.", length);
		return -ENOMEM;
	}
	/* Note: usb_control_msg returns negative value on error or length of the
	 * 		 data that was written! */
	result = usb_control_msg (dev, usb_sndctrlpipe(dev, 0), request, 0x40, address, 0, buffer, length, 300);
	kfree (buffer);
	return result;
}

/* thanks to drivers/usb/serial/keyspan_pda.c code */
static int emi26_set_reset (struct usb_device *dev, unsigned char reset_bit)
{
	int response;
	info("%s - %d", __FUNCTION__, reset_bit);
	/* printk(KERN_DEBUG "%s - %d", __FUNCTION__, reset_bit); */
	response = emi26_writememory (dev, CPUCS_REG, &reset_bit, 1, 0xa0);
	if (response < 0) {
		err("emi26: set_reset (%d) failed", reset_bit);
	}
	return response;
}

#define FW_LOAD_SIZE		1023

static int emi26_load_firmware (struct usb_device *dev)
{
	int err;
	int i;
	int pos = 0;	/* Position in hex record */
	__u32 addr;	/* Address to write */
	__u8 *buf;

	buf = kmalloc(FW_LOAD_SIZE, GFP_KERNEL);
	if (!buf) {
		err( "%s - error loading firmware: error = %d", __FUNCTION__, -ENOMEM);
		err = -ENOMEM;
		goto wraperr;
	}

	/* Assert reset (stop the CPU in the EMI) */
	err = emi26_set_reset(dev,1);
	if (err < 0) {
		err( "%s - error loading firmware: error = %d", __FUNCTION__, err);
		goto wraperr;
	}

	/* 1. We need to put the loader for the FPGA into the EZ-USB */
	for (i=0; g_Loader[i].type == 0; i++) {
		err = emi26_writememory(dev, g_Loader[i].address, g_Loader[i].data, g_Loader[i].length, ANCHOR_LOAD_INTERNAL);
		if (err < 0) {
			err("%s - error loading firmware: error = %d", __FUNCTION__, err);
			goto wraperr;
		}
	}

	/* De-assert reset (let the CPU run) */
	err = emi26_set_reset(dev,0);
	msleep(250);	/* let device settle */

	/* 2. We upload the FPGA firmware into the EMI
	 * Note: collect up to 1023 (yes!) bytes and send them with
	 * a single request. This is _much_ faster! */
	do {
		i = 0;
		addr = g_bitstream[pos].address;

		/* intel hex records are terminated with type 0 element */
		while ((g_bitstream[pos].type == 0) && (i + g_bitstream[pos].length < FW_LOAD_SIZE)) {
			memcpy(buf + i, g_bitstream[pos].data, g_bitstream[pos].length);
			i += g_bitstream[pos].length;
			pos++;
		}
		err = emi26_writememory(dev, addr, buf, i, ANCHOR_LOAD_FPGA);
		if (err < 0) {
			err("%s - error loading firmware: error = %d", __FUNCTION__, err);
			goto wraperr;
		}
	} while (i > 0);

	/* Assert reset (stop the CPU in the EMI) */
	err = emi26_set_reset(dev,1);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __FUNCTION__, err);
		goto wraperr;
	}

	/* 3. We need to put the loader for the firmware into the EZ-USB (again...) */
	for (i=0; g_Loader[i].type == 0; i++) {
		err = emi26_writememory(dev, g_Loader[i].address, g_Loader[i].data, g_Loader[i].length, ANCHOR_LOAD_INTERNAL);
		if (err < 0) {
			err("%s - error loading firmware: error = %d", __FUNCTION__, err);
			goto wraperr;
		}
	}
	msleep(250);	/* let device settle */

	/* De-assert reset (let the CPU run) */
	err = emi26_set_reset(dev,0);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __FUNCTION__, err);
		goto wraperr;
	}

	/* 4. We put the part of the firmware that lies in the external RAM into the EZ-USB */
	for (i=0; g_Firmware[i].type == 0; i++) {
		if (!INTERNAL_RAM(g_Firmware[i].address)) {
			err = emi26_writememory(dev, g_Firmware[i].address, g_Firmware[i].data, g_Firmware[i].length, ANCHOR_LOAD_EXTERNAL);
			if (err < 0) {
				err("%s - error loading firmware: error = %d", __FUNCTION__, err);
				goto wraperr;
			}
		}
	}
	
	/* Assert reset (stop the CPU in the EMI) */
	err = emi26_set_reset(dev,1);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __FUNCTION__, err);
		goto wraperr;
	}

	for (i=0; g_Firmware[i].type == 0; i++) {
		if (INTERNAL_RAM(g_Firmware[i].address)) {
			err = emi26_writememory(dev, g_Firmware[i].address, g_Firmware[i].data, g_Firmware[i].length, ANCHOR_LOAD_INTERNAL);
			if (err < 0) {
				err("%s - error loading firmware: error = %d", __FUNCTION__, err);
				goto wraperr;
			}
		}
	}

	/* De-assert reset (let the CPU run) */
	err = emi26_set_reset(dev,0);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __FUNCTION__, err);
		goto wraperr;
	}
	msleep(250);	/* let device settle */

	/* return 1 to fail the driver inialization
	 * and give real driver change to load */
	err = 1;

wraperr:
	kfree(buf);
	return err;
}

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(EMI26_VENDOR_ID, EMI26_PRODUCT_ID) },
	{ USB_DEVICE(EMI26_VENDOR_ID, EMI26B_PRODUCT_ID) },
	{ }                                             /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);

static int emi26_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);

	info("%s start", __FUNCTION__); 

	emi26_load_firmware(dev);

	/* do not return the driver context, let real audio driver do that */
	return -EIO;
}

static void emi26_disconnect(struct usb_interface *intf)
{
}

static struct usb_driver emi26_driver = {
	.name		= "emi26 - firmware loader",
	.probe		= emi26_probe,
	.disconnect	= emi26_disconnect,
	.id_table	= id_table,
};

static int __init emi26_init (void)
{
	return usb_register(&emi26_driver);
}

static void __exit emi26_exit (void)
{
	usb_deregister (&emi26_driver);
}

module_init(emi26_init);
module_exit(emi26_exit);

MODULE_AUTHOR("tapio laxström");
MODULE_DESCRIPTION("Emagic EMI 2|6 firmware loader.");
MODULE_LICENSE("GPL");

/* vi:ai:syntax=c:sw=8:ts=8:tw=80
 */
