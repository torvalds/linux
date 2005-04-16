/*
 * Copyright (C) 2004 Patrick Boettcher <patrick.boettcher@desy.de>,
 *                    Luca Bertagnolio <>,
 *
 * based on information provided by John Jurrius from BBTI, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/version.h>

#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_filter.h"
#include "dvb_net.h"
#include "dvb_frontend.h"

/* debug */
#define dprintk(level,args...) \
	    do { if ((debug & level)) { printk(args); } } while (0)
#define debug_dump(b,l) if (debug) {\
	int i; deb_xfer("%s: %d > ",__FUNCTION__,l); \
	for (i = 0; i < l; i++) deb_xfer("%02x ", b[i]); \
	deb_xfer("\n");\
}

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,ts=2,ctrl=4 (or-able)).");

#define deb_info(args...) dprintk(0x01,args)
#define deb_ts(args...)   dprintk(0x02,args)
#define deb_ctrl(args...) dprintk(0x04,args)

/* Version information */
#define DRIVER_VERSION "0.0"
#define DRIVER_DESC "Driver for B2C2/Technisat Air/Cable/Sky-2-PC USB devices"
#define DRIVER_AUTHOR "Patrick Boettcher, patrick.boettcher@desy.de"

/* transfer parameters */
#define B2C2_USB_FRAMES_PER_ISO		4
#define B2C2_USB_NUM_ISO_URB		4    /* TODO check out a good value */

#define B2C2_USB_CTRL_PIPE_IN		usb_rcvctrlpipe(b2c2->udev,0)
#define B2C2_USB_CTRL_PIPE_OUT		usb_sndctrlpipe(b2c2->udev,0)
#define B2C2_USB_DATA_PIPE			usb_rcvisocpipe(b2c2->udev,0x81)

struct usb_b2c2_usb {
	struct usb_device *udev;
	struct usb_interface *uintf;

	u8 *iso_buffer;
	int buffer_size;
	dma_addr_t iso_dma_handle;
	struct urb *iso_urb[B2C2_USB_NUM_ISO_URB];
};


/*
 * USB
 * 10 90 34 12 78 56 04 00
 * usb_control_msg(udev, usb_sndctrlpipe(udev,0),
 * 0x90,
 * 0x10,
 * 0x1234,
 * 0x5678,
 * buf,
 * 4,
 * 5*HZ);
 *
 * extern int usb_control_msg(struct usb_device *dev, unsigned int pipe,
 * __u8 request,
 * __u8 requesttype,
 * __u16 value,
 * __u16 index,
 * void *data,
 * __u16 size,
 * int timeout);
 *
 */

/* request types */
typedef enum {

/* something is wrong with this part
	RTYPE_READ_DW         = (1 << 6),
	RTYPE_WRITE_DW_1      = (3 << 6),
	RTYPE_READ_V8_MEMORY  = (6 << 6),
	RTYPE_WRITE_V8_MEMORY = (7 << 6),
	RTYPE_WRITE_V8_FLASH  = (8 << 6),
	RTYPE_GENERIC         = (9 << 6),
*/
	RTYPE_READ_DW = (3 << 6),
	RTYPE_WRITE_DW_1 = (1 << 6),
	
	RTYPE_READ_V8_MEMORY  = (6 << 6),
	RTYPE_WRITE_V8_MEMORY = (7 << 6),
	RTYPE_WRITE_V8_FLASH  = (8 << 6),
	RTYPE_GENERIC         = (9 << 6),
} b2c2_usb_request_type_t;

/* request */
typedef enum {
	B2C2_USB_WRITE_V8_MEM = 0x04,
	B2C2_USB_READ_V8_MEM  = 0x05,
	B2C2_USB_READ_REG     = 0x08,
	B2C2_USB_WRITE_REG    = 0x0A,
/*	B2C2_USB_WRITEREGLO   = 0x0A, */
	B2C2_USB_WRITEREGHI   = 0x0B,
	B2C2_USB_FLASH_BLOCK  = 0x10,
	B2C2_USB_I2C_REQUEST  = 0x11,
	B2C2_USB_UTILITY      = 0x12,
} b2c2_usb_request_t;

/* function definition for I2C_REQUEST */
typedef enum {
	USB_FUNC_I2C_WRITE       = 0x01,
	USB_FUNC_I2C_MULTIWRITE  = 0x02,
	USB_FUNC_I2C_READ        = 0x03,
	USB_FUNC_I2C_REPEATWRITE = 0x04,
	USB_FUNC_GET_DESCRIPTOR  = 0x05,
	USB_FUNC_I2C_REPEATREAD  = 0x06,
/* DKT 020208 - add this to support special case of DiSEqC */
	USB_FUNC_I2C_CHECKWRITE  = 0x07,
	USB_FUNC_I2C_CHECKRESULT = 0x08,
} b2c2_usb_i2c_function_t;

/*
 * function definition for UTILITY request 0x12
 * DKT 020304 - new utility function
 */
typedef enum {
	UTILITY_SET_FILTER          = 0x01,
	UTILITY_DATA_ENABLE         = 0x02,
	UTILITY_FLEX_MULTIWRITE     = 0x03,
	UTILITY_SET_BUFFER_SIZE     = 0x04,
	UTILITY_FLEX_OPERATOR       = 0x05,
	UTILITY_FLEX_RESET300_START = 0x06,
	UTILITY_FLEX_RESET300_STOP  = 0x07,
	UTILITY_FLEX_RESET300       = 0x08,
	UTILITY_SET_ISO_SIZE        = 0x09,
	UTILITY_DATA_RESET          = 0x0A,
	UTILITY_GET_DATA_STATUS     = 0x10,
	UTILITY_GET_V8_REG          = 0x11,
/* DKT 020326 - add function for v1.14 */
	UTILITY_SRAM_WRITE          = 0x12,
	UTILITY_SRAM_READ           = 0x13,
	UTILITY_SRAM_TESTFILL       = 0x14,
	UTILITY_SRAM_TESTSET        = 0x15,
	UTILITY_SRAM_TESTVERIFY     = 0x16,
} b2c2_usb_utility_function_t;

#define B2C2_WAIT_FOR_OPERATION_RW  1  // 1 s
#define B2C2_WAIT_FOR_OPERATION_RDW 3  // 3 s
#define B2C2_WAIT_FOR_OPERATION_WDW 1  // 1 s

#define B2C2_WAIT_FOR_OPERATION_V8READ   3  // 3 s
#define B2C2_WAIT_FOR_OPERATION_V8WRITE  3  // 3 s
#define B2C2_WAIT_FOR_OPERATION_V8FLASH  3  // 3 s

/* JLP 111700: we will include the 1 bit gap between the upper and lower 3 bits
 * in the IBI address, to make the V8 code simpler.
 * PCI ADDRESS FORMAT: 0x71C -> 0000 0111 0001 1100 (these are the six bits used)
 *                  in general: 0000 0HHH 000L LL00
 * IBI ADDRESS FORMAT:                    RHHH BLLL
 *
 * where R is the read(1)/write(0) bit, B is the busy bit
 * and HHH and LLL are the two sets of three bits from the PCI address.
 */
#define B2C2_FLEX_PCIOFFSET_TO_INTERNALADDR(usPCI) (u8) (((usPCI >> 2) & 0x07) + ((usPCI >> 4) & 0x70))
#define B2C2_FLEX_INTERNALADDR_TO_PCIOFFSET(ucAddr) (u16) (((ucAddr & 0x07) << 2) + ((ucAddr & 0x70) << 4))

/*
 * DKT 020228 - forget about this VENDOR_BUFFER_SIZE, read and write register
 * deal with DWORD or 4 bytes, that should be should from now on
 */
static u32 b2c2_usb_read_dw(struct usb_b2c2_usb *b2c2, u16 wRegOffsPCI)
{
	u32 val;
	u16 wAddress = B2C2_FLEX_PCIOFFSET_TO_INTERNALADDR(wRegOffsPCI) | 0x0080;
	int len = usb_control_msg(b2c2->udev,
			B2C2_USB_CTRL_PIPE_IN,
			B2C2_USB_READ_REG,
			RTYPE_READ_DW,
			wAddress,
			0,
			&val,
			sizeof(u32),
			B2C2_WAIT_FOR_OPERATION_RDW * 1000);

	if (len != sizeof(u32)) {
		err("error while reading dword from %d (%d).",wAddress,wRegOffsPCI);
		return -EIO;
	} else
		return val;
}

/*
 * DKT 020228 - from now on, we don't support anything older than firm 1.00
 * I eliminated the write register as a 2 trip of writing hi word and lo word
 * and force this to write only 4 bytes at a time.
 * NOTE: this should work with all the firmware from 1.00 and newer
 */
static int b2c2_usb_write_dw(struct usb_b2c2_usb *b2c2, u16 wRegOffsPCI, u32 val)
{
	u16 wAddress = B2C2_FLEX_PCIOFFSET_TO_INTERNALADDR(wRegOffsPCI);
	int len = usb_control_msg(b2c2->udev,
			B2C2_USB_CTRL_PIPE_OUT,
			B2C2_USB_WRITE_REG,
			RTYPE_WRITE_DW_1,
			wAddress,
			0,
			&val,
			sizeof(u32),
			B2C2_WAIT_FOR_OPERATION_RDW * 1000);

	if (len != sizeof(u32)) {
		err("error while reading dword from %d (%d).",wAddress,wRegOffsPCI);
		return -EIO;
	} else
		return 0;
}

/*
 * DKT 010817 - add support for V8 memory read/write and flash update
 */
static int b2c2_usb_v8_memory_req(struct usb_b2c2_usb *b2c2,
		b2c2_usb_request_t req, u8 page, u16 wAddress,
		u16 buflen, u8 *pbBuffer)
{
	u8 dwRequestType;
	u16 wIndex;
	int nWaitTime,pipe,len;

	wIndex = page << 8;

	switch (req) {
		case B2C2_USB_READ_V8_MEM:
			nWaitTime = B2C2_WAIT_FOR_OPERATION_V8READ;
			dwRequestType = (u8) RTYPE_READ_V8_MEMORY;
			pipe = B2C2_USB_CTRL_PIPE_IN;
		break;
		case B2C2_USB_WRITE_V8_MEM:
			wIndex |= pbBuffer[0];
			nWaitTime = B2C2_WAIT_FOR_OPERATION_V8WRITE;
			dwRequestType = (u8) RTYPE_WRITE_V8_MEMORY;
			pipe = B2C2_USB_CTRL_PIPE_OUT;
		break;
		case B2C2_USB_FLASH_BLOCK:
			nWaitTime = B2C2_WAIT_FOR_OPERATION_V8FLASH;
			dwRequestType = (u8) RTYPE_WRITE_V8_FLASH;
			pipe = B2C2_USB_CTRL_PIPE_OUT;
		break;
		default:
			deb_info("unsupported request for v8_mem_req %x.\n",req);
		return -EINVAL;
	}
	len = usb_control_msg(b2c2->udev,pipe,
			req,
			dwRequestType,
			wAddress,
			wIndex,
			pbBuffer,
			buflen,
			nWaitTime * 1000);
	return len == buflen ? 0 : -EIO;
}

static int b2c2_usb_i2c_req(struct usb_b2c2_usb *b2c2,
		b2c2_usb_request_t req, b2c2_usb_i2c_function_t func,
		u8 port, u8 chipaddr, u8 addr, u8 buflen, u8 *buf)
{
	u16 wValue, wIndex;
	int nWaitTime,pipe,len;
	u8 dwRequestType;

	switch (func) {
		case USB_FUNC_I2C_WRITE:
		case USB_FUNC_I2C_MULTIWRITE:
		case USB_FUNC_I2C_REPEATWRITE:
		/* DKT 020208 - add this to support special case of DiSEqC */
		case USB_FUNC_I2C_CHECKWRITE:
			pipe = B2C2_USB_CTRL_PIPE_OUT;
			nWaitTime = 2;
			dwRequestType = (u8) RTYPE_GENERIC;
		break;
		case USB_FUNC_I2C_READ:
		case USB_FUNC_I2C_REPEATREAD:
			pipe = B2C2_USB_CTRL_PIPE_IN;
			nWaitTime = 2;
			dwRequestType = (u8) RTYPE_GENERIC;
		break;
		default:
			deb_info("unsupported function for i2c_req %x\n",func);
			return -EINVAL;
	}
	wValue = (func << 8 ) | port;
	wIndex = (chipaddr << 8 ) | addr;

	len = usb_control_msg(b2c2->udev,pipe,
			req,
			dwRequestType,
			addr,
			wIndex,
			buf,
			buflen,
			nWaitTime * 1000);
	return len == buflen ? 0 : -EIO;
}

int static b2c2_usb_utility_req(struct usb_b2c2_usb *b2c2, int set,
		b2c2_usb_utility_function_t func, u8 extra, u16 wIndex,
		u16 buflen, u8 *pvBuffer)
{
	u16 wValue;
	int nWaitTime = 2,
		pipe = set ? B2C2_USB_CTRL_PIPE_OUT : B2C2_USB_CTRL_PIPE_IN,
		len;

	wValue = (func << 8) | extra;

	len = usb_control_msg(b2c2->udev,pipe,
			B2C2_USB_UTILITY,
			(u8) RTYPE_GENERIC,
			wValue,
			wIndex,
			pvBuffer,
			buflen,
			nWaitTime * 1000);
	return len == buflen ? 0 : -EIO;
}



static void b2c2_dumpfourreg(struct usb_b2c2_usb *b2c2, u16 offs)
{
	u32 r0,r1,r2,r3;
	r0 = r1 = r2 = r3 = 0;
	r0 = b2c2_usb_read_dw(b2c2,offs);
	r1 = b2c2_usb_read_dw(b2c2,offs + 0x04);
	r2 = b2c2_usb_read_dw(b2c2,offs + 0x08);
	r3 = b2c2_usb_read_dw(b2c2,offs + 0x0c);
	deb_ctrl("dump: offset: %03x, %08x, %08x, %08x, %08x\n",offs,r0,r1,r2,r3);
}

static void b2c2_urb_complete(struct urb *urb, struct pt_regs *ptregs)
{
	struct usb_b2c2_usb *b2c2 = urb->context;
	deb_ts("urb completed, bufsize: %d\n",urb->transfer_buffer_length);

//	urb_submit_urb(urb,GFP_ATOMIC); enable for real action
}

static void b2c2_exit_usb(struct usb_b2c2_usb *b2c2)
{
	int i;
	for (i = 0; i < B2C2_USB_NUM_ISO_URB; i++)
		if (b2c2->iso_urb[i] != NULL) { /* not sure about unlink_urb and iso-urbs TODO */
			deb_info("unlinking/killing urb no. %d\n",i);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,7)
			usb_unlink_urb(b2c2->iso_urb[i]);
#else
			usb_kill_urb(b2c2->iso_urb[i]);
#endif
			usb_free_urb(b2c2->iso_urb[i]);
		}

	if (b2c2->iso_buffer != NULL)
		pci_free_consistent(NULL,b2c2->buffer_size, b2c2->iso_buffer, b2c2->iso_dma_handle);

}

static int b2c2_init_usb(struct usb_b2c2_usb *b2c2)
{
	u16 frame_size = le16_to_cpu(b2c2->uintf->cur_altsetting->endpoint[0].desc.wMaxPacketSize);
	int bufsize = B2C2_USB_NUM_ISO_URB * B2C2_USB_FRAMES_PER_ISO * frame_size,i,j,ret;
	int buffer_offset = 0;

	deb_info("creating %d iso-urbs with %d frames each of %d bytes size = %d.\n",
			B2C2_USB_NUM_ISO_URB, B2C2_USB_FRAMES_PER_ISO, frame_size,bufsize);

	b2c2->iso_buffer = pci_alloc_consistent(NULL,bufsize,&b2c2->iso_dma_handle);
	if (b2c2->iso_buffer == NULL)
		return -ENOMEM;
	memset(b2c2->iso_buffer, 0, bufsize);
	b2c2->buffer_size = bufsize;

	/* creating iso urbs */
	for (i = 0; i < B2C2_USB_NUM_ISO_URB; i++)
		if (!(b2c2->iso_urb[i] = usb_alloc_urb(B2C2_USB_FRAMES_PER_ISO,GFP_ATOMIC))) {
			ret = -ENOMEM;
			goto urb_error;
		}
	/* initialising and submitting iso urbs */
	for (i = 0; i < B2C2_USB_NUM_ISO_URB; i++) {
		int frame_offset = 0;
		struct urb *urb = b2c2->iso_urb[i];
		deb_info("initializing and submitting urb no. %d (buf_offset: %d).\n",i,buffer_offset);

		urb->dev = b2c2->udev;
		urb->context = b2c2;
		urb->complete = b2c2_urb_complete;
		urb->pipe = B2C2_USB_DATA_PIPE;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;
		urb->number_of_packets = B2C2_USB_FRAMES_PER_ISO;
		urb->transfer_buffer_length = frame_size * B2C2_USB_FRAMES_PER_ISO;
		urb->transfer_buffer = b2c2->iso_buffer + buffer_offset;

		buffer_offset += frame_size * B2C2_USB_FRAMES_PER_ISO;
		for (j = 0; j < B2C2_USB_FRAMES_PER_ISO; j++) {
			deb_info("urb no: %d, frame: %d, frame_offset: %d\n",i,j,frame_offset);
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = frame_size;
			frame_offset += frame_size;
		}

		if ((ret = usb_submit_urb(b2c2->iso_urb[i],GFP_ATOMIC))) {
			err("submitting urb %d failed with %d.",i,ret);
			goto urb_error;
		}
		deb_info("submitted urb no. %d.\n",i);
	}

	ret = 0;
	goto success;
urb_error:
	b2c2_exit_usb(b2c2);
success:
	return ret;
}

static int b2c2_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_b2c2_usb *b2c2 = NULL;
	int ret;

	b2c2 = kmalloc(sizeof(struct usb_b2c2_usb),GFP_KERNEL);
	if (b2c2 == NULL) {
		err("no memory");
		return -ENOMEM;
	}
	b2c2->udev = udev;
	b2c2->uintf = intf;

	/* use the alternate setting with the larges buffer */
	usb_set_interface(udev,0,1);

	if ((ret = b2c2_init_usb(b2c2)))
		goto usb_init_error;

	usb_set_intfdata(intf,b2c2);

	switch (udev->speed) {
		case USB_SPEED_LOW:
			err("cannot handle USB speed because it is to sLOW.");
			break;
		case USB_SPEED_FULL:
			info("running at FULL speed.");
			break;
		case USB_SPEED_HIGH:
			info("running at HIGH speed.");
			break;
		case USB_SPEED_UNKNOWN: /* fall through */
		default:
			err("cannot handle USB speed because it is unkown.");
		break;
	}

	b2c2_dumpfourreg(b2c2,0x200);
	b2c2_dumpfourreg(b2c2,0x300);
	b2c2_dumpfourreg(b2c2,0x400);
	b2c2_dumpfourreg(b2c2,0x700);


	if (ret == 0)
		info("%s successfully initialized and connected.",DRIVER_DESC);
	else
		info("%s error while loading driver (%d)",DRIVER_DESC,ret);

	ret = 0;
	goto success;

usb_init_error:
	kfree(b2c2);
success:
	return ret;
}

static void b2c2_usb_disconnect(struct usb_interface *intf)
{
	struct usb_b2c2_usb *b2c2 = usb_get_intfdata(intf);
	usb_set_intfdata(intf,NULL);
	if (b2c2 != NULL) {
		b2c2_exit_usb(b2c2);
		kfree(b2c2);
	}
	info("%s successfully deinitialized and disconnected.",DRIVER_DESC);

}

static struct usb_device_id b2c2_usb_table [] = {
	    { USB_DEVICE(0x0af7, 0x0101) }
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver b2c2_usb_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_b2c2_usb",
	.probe 		= b2c2_usb_probe,
	.disconnect = b2c2_usb_disconnect,
	.id_table 	= b2c2_usb_table,
};

/* module stuff */
static int __init b2c2_usb_init(void)
{
	int result;
	if ((result = usb_register(&b2c2_usb_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit b2c2_usb_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&b2c2_usb_driver);
}

module_init (b2c2_usb_init);
module_exit (b2c2_usb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, b2c2_usb_table);
