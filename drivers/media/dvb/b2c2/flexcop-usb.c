/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-usb.c - covers the USB part.
 *
 * see flexcop.c for copyright information.
 */

#define FC_LOG_PREFIX "flexcop_usb"
#include "flexcop-usb.h"
#include "flexcop-common.h"

/* Version information */
#define DRIVER_VERSION "0.1"
#define DRIVER_NAME "Technisat/B2C2 FlexCop II/IIb/III Digital TV USB Driver"
#define DRIVER_AUTHOR "Patrick Boettcher <patrick.boettcher@desy.de>"

/* debug */
#ifdef CONFIG_DVB_B2C2_FLEXCOP_DEBUG
#define dprintk(level,args...) \
	    do { if ((debug & level)) { printk(args); } } while (0)
#define debug_dump(b,l,method) {\
	int i; \
	for (i = 0; i < l; i++) method("%02x ", b[i]); \
	method("\n");\
}

#define DEBSTATUS ""
#else
#define dprintk(level,args...)
#define debug_dump(b,l,method)
#define DEBSTATUS " (debugging is not enabled)"
#endif

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,ts=2,ctrl=4,i2c=8,v8mem=16 (or-able))." DEBSTATUS);
#undef DEBSTATUS

#define deb_info(args...) dprintk(0x01,args)
#define deb_ts(args...)   dprintk(0x02,args)
#define deb_ctrl(args...) dprintk(0x04,args)
#define deb_i2c(args...)  dprintk(0x08,args)
#define deb_v8(args...)   dprintk(0x10,args)

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
 * DKT 020228
 * - forget about this VENDOR_BUFFER_SIZE, read and write register
 *   deal with DWORD or 4 bytes, that should be should from now on
 * - from now on, we don't support anything older than firm 1.00
 *   I eliminated the write register as a 2 trip of writing hi word and lo word
 *   and force this to write only 4 bytes at a time.
 *   NOTE: this should work with all the firmware from 1.00 and newer
 */
static int flexcop_usb_readwrite_dw(struct flexcop_device *fc, u16 wRegOffsPCI, u32 *val, u8 read)
{
	struct flexcop_usb *fc_usb = fc->bus_specific;
	u8 request = read ? B2C2_USB_READ_REG : B2C2_USB_WRITE_REG;
	u8 request_type = (read ? USB_DIR_IN : USB_DIR_OUT) | USB_TYPE_VENDOR;
	u8 wAddress = B2C2_FLEX_PCIOFFSET_TO_INTERNALADDR(wRegOffsPCI) | (read ? 0x80 : 0);

	int len = usb_control_msg(fc_usb->udev,
			read ? B2C2_USB_CTRL_PIPE_IN : B2C2_USB_CTRL_PIPE_OUT,
			request,
			request_type,  /* 0xc0 read or 0x40 write*/
			wAddress,
			0,
			val,
			sizeof(u32),
			B2C2_WAIT_FOR_OPERATION_RDW * HZ);

	if (len != sizeof(u32)) {
		err("error while %s dword from %d (%d).",read ? "reading" : "writing",
			wAddress,wRegOffsPCI);
		return -EIO;
	}
	return 0;
}

/*
 * DKT 010817 - add support for V8 memory read/write and flash update
 */
static int flexcop_usb_v8_memory_req(struct flexcop_usb *fc_usb,
		flexcop_usb_request_t req, u8 page, u16 wAddress,
		u8 *pbBuffer,u32 buflen)
{
//	u8 dwRequestType;
	u8 request_type = USB_TYPE_VENDOR;
	u16 wIndex;
	int nWaitTime,pipe,len;

	wIndex = page << 8;

	switch (req) {
		case B2C2_USB_READ_V8_MEM:
			nWaitTime = B2C2_WAIT_FOR_OPERATION_V8READ;
			request_type |= USB_DIR_IN;
//			dwRequestType = (u8) RTYPE_READ_V8_MEMORY;
			pipe = B2C2_USB_CTRL_PIPE_IN;
		break;
		case B2C2_USB_WRITE_V8_MEM:
			wIndex |= pbBuffer[0];
			request_type |= USB_DIR_OUT;
			nWaitTime = B2C2_WAIT_FOR_OPERATION_V8WRITE;
//			dwRequestType = (u8) RTYPE_WRITE_V8_MEMORY;
			pipe = B2C2_USB_CTRL_PIPE_OUT;
		break;
		case B2C2_USB_FLASH_BLOCK:
			request_type |= USB_DIR_OUT;
			nWaitTime = B2C2_WAIT_FOR_OPERATION_V8FLASH;
//			dwRequestType = (u8) RTYPE_WRITE_V8_FLASH;
			pipe = B2C2_USB_CTRL_PIPE_OUT;
		break;
		default:
			deb_info("unsupported request for v8_mem_req %x.\n",req);
		return -EINVAL;
	}
	deb_v8("v8mem: %02x %02x %04x %04x, len: %d\n",request_type,req,
			wAddress,wIndex,buflen);

	len = usb_control_msg(fc_usb->udev,pipe,
			req,
			request_type,
			wAddress,
			wIndex,
			pbBuffer,
			buflen,
			nWaitTime * HZ);

	debug_dump(pbBuffer,len,deb_v8);

	return len == buflen ? 0 : -EIO;
}

#define bytes_left_to_read_on_page(paddr,buflen) \
			((V8_MEMORY_PAGE_SIZE - (paddr & V8_MEMORY_PAGE_MASK)) > buflen \
			? buflen : (V8_MEMORY_PAGE_SIZE - (paddr & V8_MEMORY_PAGE_MASK)))

static int flexcop_usb_memory_req(struct flexcop_usb *fc_usb,flexcop_usb_request_t req,
		flexcop_usb_mem_page_t page_start, u32 addr, int extended, u8 *buf, u32 len)
{
	int i,ret = 0;
	u16 wMax;
	u32 pagechunk = 0;

	switch(req) {
		case B2C2_USB_READ_V8_MEM:  wMax = USB_MEM_READ_MAX; break;
		case B2C2_USB_WRITE_V8_MEM:	wMax = USB_MEM_WRITE_MAX; break;
		case B2C2_USB_FLASH_BLOCK:  wMax = USB_FLASH_MAX; break;
		default:
			return -EINVAL;
		break;
	}
	for (i = 0; i < len;) {
		pagechunk = wMax < bytes_left_to_read_on_page(addr,len) ? wMax : bytes_left_to_read_on_page(addr,len);
		deb_info("%x\n",(addr & V8_MEMORY_PAGE_MASK) | (V8_MEMORY_EXTENDED*extended));
		if ((ret = flexcop_usb_v8_memory_req(fc_usb,req,
				page_start + (addr / V8_MEMORY_PAGE_SIZE), /* actual page */
				(addr & V8_MEMORY_PAGE_MASK) | (V8_MEMORY_EXTENDED*extended),
				&buf[i],pagechunk)) < 0)
			return ret;

		addr += pagechunk;
		len -= pagechunk;
	}
	return 0;
}

static int flexcop_usb_get_mac_addr(struct flexcop_device *fc, int extended)
{
	return flexcop_usb_memory_req(fc->bus_specific,B2C2_USB_READ_V8_MEM,
			V8_MEMORY_PAGE_FLASH,0x1f010,1,fc->dvb_adapter.proposed_mac,6);
}

#if 0
static int flexcop_usb_utility_req(struct flexcop_usb *fc_usb, int set,
		flexcop_usb_utility_function_t func, u8 extra, u16 wIndex,
		u16 buflen, u8 *pvBuffer)
{
	u16 wValue;
	u8 request_type = (set ? USB_DIR_OUT : USB_DIR_IN) | USB_TYPE_VENDOR;
//	u8 dwRequestType = (u8) RTYPE_GENERIC,
	int nWaitTime = 2,
		pipe = set ? B2C2_USB_CTRL_PIPE_OUT : B2C2_USB_CTRL_PIPE_IN,
		len;

	wValue = (func << 8) | extra;

	len = usb_control_msg(fc_usb->udev,pipe,
			B2C2_USB_UTILITY,
			request_type,
			wValue,
			wIndex,
			pvBuffer,
			buflen,
			nWaitTime * HZ);
	return len == buflen ? 0 : -EIO;
}
#endif

/* usb i2c stuff */
static int flexcop_usb_i2c_req(struct flexcop_usb *fc_usb,
		flexcop_usb_request_t req, flexcop_usb_i2c_function_t func,
		flexcop_i2c_port_t port, u8 chipaddr, u8 addr, u8 *buf, u8 buflen)
{
	u16 wValue, wIndex;
	int nWaitTime,pipe,len;
//	u8 dwRequestType;
	u8 request_type = USB_TYPE_VENDOR;

	switch (func) {
		case USB_FUNC_I2C_WRITE:
		case USB_FUNC_I2C_MULTIWRITE:
		case USB_FUNC_I2C_REPEATWRITE:
		/* DKT 020208 - add this to support special case of DiSEqC */
		case USB_FUNC_I2C_CHECKWRITE:
			pipe = B2C2_USB_CTRL_PIPE_OUT;
			nWaitTime = 2;
//			dwRequestType = (u8) RTYPE_GENERIC;
			request_type |= USB_DIR_OUT;
		break;
		case USB_FUNC_I2C_READ:
		case USB_FUNC_I2C_REPEATREAD:
			pipe = B2C2_USB_CTRL_PIPE_IN;
			nWaitTime = 2;
//			dwRequestType = (u8) RTYPE_GENERIC;
			request_type |= USB_DIR_IN;
		break;
		default:
			deb_info("unsupported function for i2c_req %x\n",func);
			return -EINVAL;
	}
	wValue = (func << 8 ) | (port << 4);
	wIndex = (chipaddr << 8 ) | addr;

	deb_i2c("i2c %2d: %02x %02x %02x %02x %02x %02x\n",func,request_type,req,
			((wValue && 0xff) << 8),wValue >> 8,((wIndex && 0xff) << 8),wIndex >> 8);

	len = usb_control_msg(fc_usb->udev,pipe,
			req,
			request_type,
			wValue,
			wIndex,
			buf,
			buflen,
			nWaitTime * HZ);

	return len == buflen ? 0 : -EREMOTEIO;
}

/* actual bus specific access functions, make sure prototype are/will be equal to pci */
static flexcop_ibi_value flexcop_usb_read_ibi_reg(struct flexcop_device *fc, flexcop_ibi_register reg)
{
	flexcop_ibi_value val;
	val.raw = 0;
	flexcop_usb_readwrite_dw(fc,reg, &val.raw, 1);
	return val;
}

static int flexcop_usb_write_ibi_reg(struct flexcop_device *fc, flexcop_ibi_register reg, flexcop_ibi_value val)
{
	return flexcop_usb_readwrite_dw(fc,reg, &val.raw, 0);
}

static int flexcop_usb_i2c_request(struct flexcop_device *fc, flexcop_access_op_t op,
		flexcop_i2c_port_t port, u8 chipaddr, u8 addr, u8 *buf, u16 len)
{
	if (op == FC_READ)
		return flexcop_usb_i2c_req(fc->bus_specific,B2C2_USB_I2C_REQUEST,USB_FUNC_I2C_READ,port,chipaddr,addr,buf,len);
	else
		return flexcop_usb_i2c_req(fc->bus_specific,B2C2_USB_I2C_REQUEST,USB_FUNC_I2C_WRITE,port,chipaddr,addr,buf,len);
}

static void flexcop_usb_process_frame(struct flexcop_usb *fc_usb, u8 *buffer, int buffer_length)
{
	u8 *b;
	int l;

	deb_ts("tmp_buffer_length=%d, buffer_length=%d\n", fc_usb->tmp_buffer_length, buffer_length);

	if (fc_usb->tmp_buffer_length > 0) {
		memcpy(fc_usb->tmp_buffer+fc_usb->tmp_buffer_length, buffer, buffer_length);
		fc_usb->tmp_buffer_length += buffer_length;
		b = fc_usb->tmp_buffer;
		l = fc_usb->tmp_buffer_length;
	} else {
		b=buffer;
		l=buffer_length;
	}

	while (l >= 190) {
		if (*b == 0xff)
			switch (*(b+1) & 0x03) {
				case 0x01: /* media packet */
					if ( *(b+2) == 0x47 )
						flexcop_pass_dmx_packets(fc_usb->fc_dev, b+2, 1);
					else
						deb_ts("not ts packet %02x %02x %02x %02x \n", *(b+2), *(b+3), *(b+4), *(b+5) );

					b += 190;
					l -= 190;
				break;
				default:
					deb_ts("wrong packet type\n");
					l = 0;
				break;
			}
		else {
			deb_ts("wrong header\n");
			l = 0;
		}
	}

	if (l>0)
		memcpy(fc_usb->tmp_buffer, b, l);
	fc_usb->tmp_buffer_length = l;
}

static void flexcop_usb_urb_complete(struct urb *urb, struct pt_regs *ptregs)
{
	struct flexcop_usb *fc_usb = urb->context;
	int i;

	if (urb->actual_length > 0)
		deb_ts("urb completed, bufsize: %d actlen; %d\n",urb->transfer_buffer_length, urb->actual_length);

	for (i = 0; i < urb->number_of_packets; i++) {
		if (urb->iso_frame_desc[i].status < 0) {
			err("iso frame descriptor %d has an error: %d\n",i,urb->iso_frame_desc[i].status);
		} else
			if (urb->iso_frame_desc[i].actual_length > 0) {
				deb_ts("passed %d bytes to the demux\n",urb->iso_frame_desc[i].actual_length);

				flexcop_usb_process_frame(fc_usb,
					urb->transfer_buffer + urb->iso_frame_desc[i].offset,
					urb->iso_frame_desc[i].actual_length);
		}
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	usb_submit_urb(urb,GFP_ATOMIC);
}

static int flexcop_usb_stream_control(struct flexcop_device *fc, int onoff)
{
	/* submit/kill iso packets */
	return 0;
}

static void flexcop_usb_transfer_exit(struct flexcop_usb *fc_usb)
{
	int i;
	for (i = 0; i < B2C2_USB_NUM_ISO_URB; i++)
		if (fc_usb->iso_urb[i] != NULL) {
			deb_ts("unlinking/killing urb no. %d\n",i);
			usb_kill_urb(fc_usb->iso_urb[i]);
			usb_free_urb(fc_usb->iso_urb[i]);
		}

	if (fc_usb->iso_buffer != NULL)
		pci_free_consistent(NULL,fc_usb->buffer_size, fc_usb->iso_buffer, fc_usb->dma_addr);
}

static int flexcop_usb_transfer_init(struct flexcop_usb *fc_usb)
{
	u16 frame_size = fc_usb->uintf->cur_altsetting->endpoint[0].desc.wMaxPacketSize;
	int bufsize = B2C2_USB_NUM_ISO_URB * B2C2_USB_FRAMES_PER_ISO * frame_size,i,j,ret;
	int buffer_offset = 0;

	deb_ts("creating %d iso-urbs with %d frames each of %d bytes size = %d.\n",
			B2C2_USB_NUM_ISO_URB, B2C2_USB_FRAMES_PER_ISO, frame_size,bufsize);

	fc_usb->iso_buffer = pci_alloc_consistent(NULL,bufsize,&fc_usb->dma_addr);
	if (fc_usb->iso_buffer == NULL)
		return -ENOMEM;
	memset(fc_usb->iso_buffer, 0, bufsize);
	fc_usb->buffer_size = bufsize;

	/* creating iso urbs */
	for (i = 0; i < B2C2_USB_NUM_ISO_URB; i++)
		if (!(fc_usb->iso_urb[i] = usb_alloc_urb(B2C2_USB_FRAMES_PER_ISO,GFP_ATOMIC))) {
			ret = -ENOMEM;
			goto urb_error;
		}
	/* initialising and submitting iso urbs */
	for (i = 0; i < B2C2_USB_NUM_ISO_URB; i++) {
		int frame_offset = 0;
		struct urb *urb = fc_usb->iso_urb[i];
		deb_ts("initializing and submitting urb no. %d (buf_offset: %d).\n",i,buffer_offset);

		urb->dev = fc_usb->udev;
		urb->context = fc_usb;
		urb->complete = flexcop_usb_urb_complete;
		urb->pipe = B2C2_USB_DATA_PIPE;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;
		urb->number_of_packets = B2C2_USB_FRAMES_PER_ISO;
		urb->transfer_buffer_length = frame_size * B2C2_USB_FRAMES_PER_ISO;
		urb->transfer_buffer = fc_usb->iso_buffer + buffer_offset;

		buffer_offset += frame_size * B2C2_USB_FRAMES_PER_ISO;
		for (j = 0; j < B2C2_USB_FRAMES_PER_ISO; j++) {
			deb_ts("urb no: %d, frame: %d, frame_offset: %d\n",i,j,frame_offset);
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = frame_size;
			frame_offset += frame_size;
		}

		if ((ret = usb_submit_urb(fc_usb->iso_urb[i],GFP_ATOMIC))) {
			err("submitting urb %d failed with %d.",i,ret);
			goto urb_error;
		}
		deb_ts("submitted urb no. %d.\n",i);
	}

/* SRAM */

	flexcop_sram_set_dest(fc_usb->fc_dev,FC_SRAM_DEST_MEDIA | FC_SRAM_DEST_NET |
			FC_SRAM_DEST_CAO | FC_SRAM_DEST_CAI, FC_SRAM_DEST_TARGET_WAN_USB);
	flexcop_wan_set_speed(fc_usb->fc_dev,FC_WAN_SPEED_8MBITS);
	flexcop_sram_ctrl(fc_usb->fc_dev,1,1,1);

	return 0;

urb_error:
	flexcop_usb_transfer_exit(fc_usb);
	return ret;
}

static int flexcop_usb_init(struct flexcop_usb *fc_usb)
{
	/* use the alternate setting with the larges buffer */
	usb_set_interface(fc_usb->udev,0,1);
	switch (fc_usb->udev->speed) {
		case USB_SPEED_LOW:
			err("cannot handle USB speed because it is to sLOW.");
			return -ENODEV;
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
			return -ENODEV;
	}
	usb_set_intfdata(fc_usb->uintf, fc_usb);
	return 0;
}

static void flexcop_usb_exit(struct flexcop_usb *fc_usb)
{
	usb_set_intfdata(fc_usb->uintf, NULL);
}

static int flexcop_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct flexcop_usb *fc_usb = NULL;
	struct flexcop_device *fc = NULL;
	int ret;

	if ((fc = flexcop_device_kmalloc(sizeof(struct flexcop_usb))) == NULL) {
		err("out of memory\n");
		return -ENOMEM;
	}

/* general flexcop init */
	fc_usb = fc->bus_specific;
	fc_usb->fc_dev = fc;

	fc->read_ibi_reg  = flexcop_usb_read_ibi_reg;
	fc->write_ibi_reg = flexcop_usb_write_ibi_reg;
	fc->i2c_request = flexcop_usb_i2c_request;
	fc->get_mac_addr = flexcop_usb_get_mac_addr;

	fc->stream_control = flexcop_usb_stream_control;

	fc->pid_filtering = 1;
	fc->bus_type = FC_USB;

	fc->dev = &udev->dev;
	fc->owner = THIS_MODULE;

/* bus specific part */
	fc_usb->udev = udev;
	fc_usb->uintf = intf;
	if ((ret = flexcop_usb_init(fc_usb)) != 0)
		goto err_kfree;

/* init flexcop */
	if ((ret = flexcop_device_initialize(fc)) != 0)
		goto err_usb_exit;

/* xfer init */
	if ((ret = flexcop_usb_transfer_init(fc_usb)) != 0)
		goto err_fc_exit;

	info("%s successfully initialized and connected.",DRIVER_NAME);
	return 0;

err_fc_exit:
	flexcop_device_exit(fc);
err_usb_exit:
	flexcop_usb_exit(fc_usb);
err_kfree:
	flexcop_device_kfree(fc);
	return ret;
}

static void flexcop_usb_disconnect(struct usb_interface *intf)
{
	struct flexcop_usb *fc_usb = usb_get_intfdata(intf);
	flexcop_usb_transfer_exit(fc_usb);
	flexcop_device_exit(fc_usb->fc_dev);
	flexcop_usb_exit(fc_usb);
	flexcop_device_kfree(fc_usb->fc_dev);
	info("%s successfully deinitialized and disconnected.",DRIVER_NAME);
}

static struct usb_device_id flexcop_usb_table [] = {
	    { USB_DEVICE(0x0af7, 0x0101) },
	    { }
};
MODULE_DEVICE_TABLE (usb, flexcop_usb_table);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver flexcop_usb_driver = {
	.name		= "b2c2_flexcop_usb",
	.probe		= flexcop_usb_probe,
	.disconnect = flexcop_usb_disconnect,
	.id_table	= flexcop_usb_table,
};

/* module stuff */
static int __init flexcop_usb_module_init(void)
{
	int result;
	if ((result = usb_register(&flexcop_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit flexcop_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&flexcop_usb_driver);
}

module_init(flexcop_usb_module_init);
module_exit(flexcop_usb_module_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_NAME);
MODULE_LICENSE("GPL");
