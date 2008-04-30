/* 
 * Emagic EMI 2|6 usb audio interface firmware loader.
 * Copyright (C) 2002
 * 	Tapio Laxström (tapio.laxstrom@iptime.fi)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, as published by
 * the Free Software Foundation, version 2.
 * 
 * $Id: emi62.c,v 1.15 2002/04/23 06:13:59 tapio Exp $
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
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

/* include firmware (variables)*/

/* FIXME: This is quick and dirty solution! */
#define SPDIF	/* if you want SPDIF comment next line */
//#undef SPDIF	/* if you want MIDI uncomment this line */ 

#ifdef SPDIF
#  include "emi62_fw_s.h" /* spdif fw */
#else
#  include "emi62_fw_m.h" /* midi fw */
#endif

#define EMI62_VENDOR_ID 		0x086a  /* Emagic Soft-und Hardware GmBH */
#define EMI62_PRODUCT_ID		0x0110	/* EMI 6|2m without firmware */

#define ANCHOR_LOAD_INTERNAL	0xA0	/* Vendor specific request code for Anchor Upload/Download (This one is implemented in the core) */
#define ANCHOR_LOAD_EXTERNAL	0xA3	/* This command is not implemented in the core. Requires firmware */
#define ANCHOR_LOAD_FPGA	0xA5	/* This command is not implemented in the core. Requires firmware. Emagic extension */
#define MAX_INTERNAL_ADDRESS	0x1B3F	/* This is the highest internal RAM address for the AN2131Q */
#define CPUCS_REG		0x7F92  /* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */ 
#define INTERNAL_RAM(address)   (address <= MAX_INTERNAL_ADDRESS)

static int emi62_writememory( struct usb_device *dev, int address, unsigned char *data, int length, __u8 bRequest);
static int emi62_set_reset(struct usb_device *dev, unsigned char reset_bit);
static int emi62_load_firmware (struct usb_device *dev);
static int emi62_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void emi62_disconnect(struct usb_interface *intf);
static int __init emi62_init (void);
static void __exit emi62_exit (void);


/* thanks to drivers/usb/serial/keyspan_pda.c code */
static int emi62_writememory (struct usb_device *dev, int address, unsigned char *data, int length, __u8 request)
{
	int result;
	unsigned char *buffer =  kmemdup(data, length, GFP_KERNEL);

	if (!buffer) {
		err("emi62: kmalloc(%d) failed.", length);
		return -ENOMEM;
	}
	/* Note: usb_control_msg returns negative value on error or length of the
	 * 		 data that was written! */
	result = usb_control_msg (dev, usb_sndctrlpipe(dev, 0), request, 0x40, address, 0, buffer, length, 300);
	kfree (buffer);
	return result;
}

/* thanks to drivers/usb/serial/keyspan_pda.c code */
static int emi62_set_reset (struct usb_device *dev, unsigned char reset_bit)
{
	int response;
	info("%s - %d", __func__, reset_bit);
	
	response = emi62_writememory (dev, CPUCS_REG, &reset_bit, 1, 0xa0);
	if (response < 0) {
		err("emi62: set_reset (%d) failed", reset_bit);
	}
	return response;
}

#define FW_LOAD_SIZE		1023

static int emi62_load_firmware (struct usb_device *dev)
{
	int err;
	int i;
	int pos = 0;	/* Position in hex record */
	__u32 addr;	/* Address to write */
	__u8 *buf;

	dev_dbg(&dev->dev, "load_firmware\n");
	buf = kmalloc(FW_LOAD_SIZE, GFP_KERNEL);
	if (!buf) {
		err( "%s - error loading firmware: error = %d", __func__, -ENOMEM);
		err = -ENOMEM;
		goto wraperr;
	}

	/* Assert reset (stop the CPU in the EMI) */
	err = emi62_set_reset(dev,1);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __func__, err);
		goto wraperr;
	}

	/* 1. We need to put the loader for the FPGA into the EZ-USB */
	for (i=0; g_emi62_loader[i].type == 0; i++) {
		err = emi62_writememory(dev, g_emi62_loader[i].address, g_emi62_loader[i].data, g_emi62_loader[i].length, ANCHOR_LOAD_INTERNAL);
		if (err < 0) {
			err("%s - error loading firmware: error = %d", __func__, err);
			goto wraperr;
		}
	}

	/* De-assert reset (let the CPU run) */
	err = emi62_set_reset(dev,0);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __func__, err);
		goto wraperr;
	}
	msleep(250);	/* let device settle */

	/* 2. We upload the FPGA firmware into the EMI
	 * Note: collect up to 1023 (yes!) bytes and send them with
	 * a single request. This is _much_ faster! */
	do {
		i = 0;
		addr = g_emi62bs[pos].address;

		/* intel hex records are terminated with type 0 element */
		while ((g_emi62bs[pos].type == 0) && (i + g_emi62bs[pos].length < FW_LOAD_SIZE)) {
			memcpy(buf + i, g_emi62bs[pos].data, g_emi62bs[pos].length);
			i += g_emi62bs[pos].length;
			pos++;
		}
		err = emi62_writememory(dev, addr, buf, i, ANCHOR_LOAD_FPGA);
		if (err < 0) {
			err("%s - error loading firmware: error = %d", __func__, err);
			goto wraperr;
		}
	} while (i > 0);

	/* Assert reset (stop the CPU in the EMI) */
	err = emi62_set_reset(dev,1);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __func__, err);
		goto wraperr;
	}

	/* 3. We need to put the loader for the firmware into the EZ-USB (again...) */
	for (i=0; g_emi62_loader[i].type == 0; i++) {
		err = emi62_writememory(dev, g_emi62_loader[i].address, g_emi62_loader[i].data, g_emi62_loader[i].length, ANCHOR_LOAD_INTERNAL);
		if (err < 0) {
			err("%s - error loading firmware: error = %d", __func__, err);
			goto wraperr;
		}
	}

	/* De-assert reset (let the CPU run) */
	err = emi62_set_reset(dev,0);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __func__, err);
		goto wraperr;
	}
	msleep(250);	/* let device settle */

	/* 4. We put the part of the firmware that lies in the external RAM into the EZ-USB */

/* FIXME: quick and dirty ifdefs */
#ifdef SPDIF
	for (i=0; g_HexSpdifFw62[i].type == 0; i++) {
		if (!INTERNAL_RAM(g_HexSpdifFw62[i].address)) {
			err = emi62_writememory(dev, g_HexSpdifFw62[i].address, g_HexSpdifFw62[i].data, g_HexSpdifFw62[i].length, ANCHOR_LOAD_EXTERNAL);
			if (err < 0) {
				err("%s - error loading firmware: error = %d", __func__, err);
				goto wraperr;
			}
		}
	}
#else /* MIDI */
	for (i=0; g_HexMidiFw62[i].type == 0; i++) {
		if (!INTERNAL_RAM(g_HexMidiFw62[i].address)) {
			err = emi62_writememory(dev, g_HexMidiFw62[i].address, g_HexMidiFw62[i].data, g_HexMidiFw62[i].length, ANCHOR_LOAD_EXTERNAL);
			if (err < 0) {
				err("%s - error loading firmware: error = %d\n", __func__, err);
				goto wraperr;
				return err;
			}
		}
	}
#endif	
	/* Assert reset (stop the CPU in the EMI) */
	err = emi62_set_reset(dev,1);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __func__, err);
		goto wraperr;
	}

/* FIXME: quick and dirty ifdefs */
#ifdef SPDIF
	for (i=0; g_HexSpdifFw62[i].type == 0; i++) {
		if (INTERNAL_RAM(g_HexSpdifFw62[i].address)) {
			err = emi62_writememory(dev, g_HexSpdifFw62[i].address, g_HexSpdifFw62[i].data, g_HexSpdifFw62[i].length, ANCHOR_LOAD_INTERNAL);
			if (err < 0) {
				err("%s - error loading firmware: error = %d", __func__, err);
				goto wraperr;
			}
		}
	}
#else /* MIDI */
	for (i=0; g_HexMidiFw62[i].type == 0; i++) {
		if (INTERNAL_RAM(g_HexMidiFw62[i].address)) {
			err = emi62_writememory(dev, g_HexMidiFw62[i].address, g_HexMidiFw62[i].data, g_HexMidiFw62[i].length, ANCHOR_LOAD_INTERNAL);
			if (err < 0) {
				err("%s - error loading firmware: error = %d\n", __func__, err);
				goto wraperr;
			}
		}
	}
#endif
	
	/* De-assert reset (let the CPU run) */
	err = emi62_set_reset(dev,0);
	if (err < 0) {
		err("%s - error loading firmware: error = %d", __func__, err);
		goto wraperr;
	}
	msleep(250);	/* let device settle */

	kfree(buf);

	/* return 1 to fail the driver inialization
	 * and give real driver change to load */
	return 1;

wraperr:
	kfree(buf);
	dev_err(&dev->dev, "Error\n");
	return err;
}

static __devinitdata struct usb_device_id id_table [] = {
	{ USB_DEVICE(EMI62_VENDOR_ID, EMI62_PRODUCT_ID) },
	{ }                                             /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);

static int emi62_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	dev_dbg(&intf->dev, "emi62_probe\n");

	info("%s start", __func__);

	emi62_load_firmware(dev);

	/* do not return the driver context, let real audio driver do that */
	return -EIO;
}

static void emi62_disconnect(struct usb_interface *intf)
{
}

static struct usb_driver emi62_driver = {
	.name		= "emi62 - firmware loader",
	.probe		= emi62_probe,
	.disconnect	= emi62_disconnect,
	.id_table	= id_table,
};

static int __init emi62_init (void)
{
	int retval;
	retval = usb_register (&emi62_driver);
	if (retval)
		printk(KERN_ERR "adi-emi: registration failed\n");
	return retval;
}

static void __exit emi62_exit (void)
{
	usb_deregister (&emi62_driver);
}

module_init(emi62_init);
module_exit(emi62_exit);

MODULE_AUTHOR("Tapio Laxström");
MODULE_DESCRIPTION("Emagic EMI 6|2m firmware loader.");
MODULE_LICENSE("GPL");

/* vi:ai:syntax=c:sw=8:ts=8:tw=80
 */
