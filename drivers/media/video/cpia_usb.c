/*
 * cpia_usb CPiA USB driver
 *
 * Supports CPiA based parallel port Video Camera's.
 *
 * Copyright (C) 1999        Jochen Scharrlach <Jochen.Scharrlach@schwaben.de>
 * Copyright (C) 1999, 2000  Johannes Erdfelt <johannes@erdfelt.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* define _CPIA_DEBUG_ for verbose debug output (see cpia.h) */
/* #define _CPIA_DEBUG_  1 */  

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/usb.h>

#include "cpia.h"

#define USB_REQ_CPIA_GRAB_FRAME			0xC1
#define USB_REQ_CPIA_UPLOAD_FRAME		0xC2
#define  WAIT_FOR_NEXT_FRAME			0
#define  FORCE_FRAME_UPLOAD			1

#define FRAMES_PER_DESC		10
#define FRAME_SIZE_PER_DESC	960	/* Shouldn't be hardcoded */
#define CPIA_NUMSBUF		2
#define STREAM_BUF_SIZE		(PAGE_SIZE * 4)
#define SCRATCH_BUF_SIZE	(STREAM_BUF_SIZE * 2)

struct cpia_sbuf {
	char *data;
	struct urb *urb;
};

#define FRAMEBUF_LEN (CPIA_MAX_FRAME_SIZE+100)
enum framebuf_status {
	FRAME_EMPTY,
	FRAME_READING,
	FRAME_READY,
	FRAME_ERROR,
};

struct framebuf {
	int length;
	enum framebuf_status status;
	u8 data[FRAMEBUF_LEN];
	struct framebuf *next;
};

struct usb_cpia {
	/* Device structure */
	struct usb_device *dev;

	unsigned char iface;
	wait_queue_head_t wq_stream;

	int cursbuf;		/* Current receiving sbuf */
	struct cpia_sbuf sbuf[CPIA_NUMSBUF];		/* Double buffering */

	int streaming;
	int open;
	int present;
	struct framebuf *buffers[3];
	struct framebuf *curbuff, *workbuff;
};

static int cpia_usb_open(void *privdata);
static int cpia_usb_registerCallback(void *privdata, void (*cb) (void *cbdata),
			             void *cbdata);
static int cpia_usb_transferCmd(void *privdata, u8 *command, u8 *data);
static int cpia_usb_streamStart(void *privdata);
static int cpia_usb_streamStop(void *privdata);
static int cpia_usb_streamRead(void *privdata, u8 *frame, int noblock);
static int cpia_usb_close(void *privdata);

#define ABOUT "USB driver for Vision CPiA based cameras"

static struct cpia_camera_ops cpia_usb_ops = {
	cpia_usb_open,
	cpia_usb_registerCallback,
	cpia_usb_transferCmd,
	cpia_usb_streamStart,
	cpia_usb_streamStop,
	cpia_usb_streamRead,
	cpia_usb_close,
	0,
	THIS_MODULE
};

static LIST_HEAD(cam_list);
static spinlock_t cam_list_lock_usb;

static void cpia_usb_complete(struct urb *urb, struct pt_regs *regs)
{
	int i;
	char *cdata;
	struct usb_cpia *ucpia;

	if (!urb || !urb->context)
		return;

	ucpia = (struct usb_cpia *) urb->context;

	if (!ucpia->dev || !ucpia->streaming || !ucpia->present || !ucpia->open)
		return;

	if (ucpia->workbuff->status == FRAME_EMPTY) {
		ucpia->workbuff->status = FRAME_READING;
		ucpia->workbuff->length = 0;
	}
 		  
	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;

		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		if (st)
			printk(KERN_DEBUG "cpia data error: [%d] len=%d, status=%X\n", i, n, st);

		if (FRAMEBUF_LEN < ucpia->workbuff->length + n) {
			printk(KERN_DEBUG "cpia: scratch buf overflow!scr_len: %d, n: %d\n", ucpia->workbuff->length, n);
			return;
		}
	    
		if (n) {
			if ((ucpia->workbuff->length > 0) || 
			    (0x19 == cdata[0] && 0x68 == cdata[1])) {
				memcpy(ucpia->workbuff->data + ucpia->workbuff->length, cdata, n);
				ucpia->workbuff->length += n;
			} else
				DBG("Ignoring packet!\n");
		} else {
			if (ucpia->workbuff->length > 4 &&
			    0xff == ucpia->workbuff->data[ucpia->workbuff->length-1] &&
			    0xff == ucpia->workbuff->data[ucpia->workbuff->length-2] &&
			    0xff == ucpia->workbuff->data[ucpia->workbuff->length-3] &&
			    0xff == ucpia->workbuff->data[ucpia->workbuff->length-4]) {
				ucpia->workbuff->status = FRAME_READY;
				ucpia->curbuff = ucpia->workbuff;
				ucpia->workbuff = ucpia->workbuff->next;
				ucpia->workbuff->status = FRAME_EMPTY;
				ucpia->workbuff->length = 0;
		  
				if (waitqueue_active(&ucpia->wq_stream))
					wake_up_interruptible(&ucpia->wq_stream);
			}
		}
	}

	/* resubmit */
	urb->dev = ucpia->dev;
	if ((i = usb_submit_urb(urb, GFP_ATOMIC)) != 0)
		printk(KERN_ERR "%s: usb_submit_urb ret %d\n", __FUNCTION__,  i);
}

static int cpia_usb_open(void *privdata)
{
	struct usb_cpia *ucpia = (struct usb_cpia *) privdata;
	struct urb *urb;
	int ret, retval = 0, fx, err;
  
	if (!ucpia)
		return -EINVAL;

	ucpia->sbuf[0].data = kmalloc(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!ucpia->sbuf[0].data)
		return -EINVAL;

	ucpia->sbuf[1].data = kmalloc(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!ucpia->sbuf[1].data) {
		retval = -EINVAL;
		goto error_0;
	}
	
	ret = usb_set_interface(ucpia->dev, ucpia->iface, 3);
	if (ret < 0) {
		printk(KERN_ERR "cpia_usb_open: usb_set_interface error (ret = %d)\n", ret);
		retval = -EBUSY;
		goto error_1;
	}

	ucpia->buffers[0]->status = FRAME_EMPTY;
	ucpia->buffers[0]->length = 0;
	ucpia->buffers[1]->status = FRAME_EMPTY;
	ucpia->buffers[1]->length = 0;
	ucpia->buffers[2]->status = FRAME_EMPTY;
	ucpia->buffers[2]->length = 0;
	ucpia->curbuff = ucpia->buffers[0];
	ucpia->workbuff = ucpia->buffers[1];

	/* We double buffer the Iso lists, and also know the polling
	 * interval is every frame (1 == (1 << (bInterval -1))).
	 */
	urb = usb_alloc_urb(FRAMES_PER_DESC, GFP_KERNEL);
	if (!urb) {
		printk(KERN_ERR "cpia_init_isoc: usb_alloc_urb 0\n");
		retval = -ENOMEM;
		goto error_1;
	}

	ucpia->sbuf[0].urb = urb;
	urb->dev = ucpia->dev;
	urb->context = ucpia;
	urb->pipe = usb_rcvisocpipe(ucpia->dev, 1);
	urb->transfer_flags = URB_ISO_ASAP;
	urb->transfer_buffer = ucpia->sbuf[0].data;
	urb->complete = cpia_usb_complete;
	urb->number_of_packets = FRAMES_PER_DESC;
	urb->interval = 1;
	urb->transfer_buffer_length = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
		urb->iso_frame_desc[fx].offset = FRAME_SIZE_PER_DESC * fx;
		urb->iso_frame_desc[fx].length = FRAME_SIZE_PER_DESC;
	}

	urb = usb_alloc_urb(FRAMES_PER_DESC, GFP_KERNEL);
	if (!urb) {
		printk(KERN_ERR "cpia_init_isoc: usb_alloc_urb 1\n");
		retval = -ENOMEM;
		goto error_urb0;
	}

	ucpia->sbuf[1].urb = urb;
	urb->dev = ucpia->dev;
	urb->context = ucpia;
	urb->pipe = usb_rcvisocpipe(ucpia->dev, 1);
	urb->transfer_flags = URB_ISO_ASAP;
	urb->transfer_buffer = ucpia->sbuf[1].data;
	urb->complete = cpia_usb_complete;
	urb->number_of_packets = FRAMES_PER_DESC;
	urb->interval = 1;
	urb->transfer_buffer_length = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
		urb->iso_frame_desc[fx].offset = FRAME_SIZE_PER_DESC * fx;
		urb->iso_frame_desc[fx].length = FRAME_SIZE_PER_DESC;
	}

	/* queue the ISO urbs, and resubmit in the completion handler */
	err = usb_submit_urb(ucpia->sbuf[0].urb, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "cpia_init_isoc: usb_submit_urb 0 ret %d\n",
			err);
		goto error_urb1;
	}
	err = usb_submit_urb(ucpia->sbuf[1].urb, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "cpia_init_isoc: usb_submit_urb 1 ret %d\n",
			err);
		goto error_urb1;
	}

	ucpia->streaming = 1;
	ucpia->open = 1;

	return 0;

error_urb1:		/* free urb 1 */
	usb_free_urb(ucpia->sbuf[1].urb);
	ucpia->sbuf[1].urb = NULL;
error_urb0:		/* free urb 0 */
	usb_free_urb(ucpia->sbuf[0].urb);
	ucpia->sbuf[0].urb = NULL;
error_1:
	kfree (ucpia->sbuf[1].data);
	ucpia->sbuf[1].data = NULL;
error_0:
	kfree (ucpia->sbuf[0].data);
	ucpia->sbuf[0].data = NULL;
	
	return retval;
}

//
// convenience functions
//

/****************************************************************************
 *
 *  WritePacket
 *
 ***************************************************************************/
static int WritePacket(struct usb_device *udev, const u8 *packet, u8 *buf, size_t size)
{
	if (!packet)
		return -EINVAL;

	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			 packet[1] + (packet[0] << 8),
			 USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			 packet[2] + (packet[3] << 8), 
			 packet[4] + (packet[5] << 8), buf, size, 1000);
}

/****************************************************************************
 *
 *  ReadPacket
 *
 ***************************************************************************/
static int ReadPacket(struct usb_device *udev, u8 *packet, u8 *buf, size_t size)
{
	if (!packet || size <= 0)
		return -EINVAL;

	return usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			 packet[1] + (packet[0] << 8),
			 USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			 packet[2] + (packet[3] << 8), 
			 packet[4] + (packet[5] << 8), buf, size, 1000);
}

static int cpia_usb_transferCmd(void *privdata, u8 *command, u8 *data)
{
	int err = 0;
	int databytes;
	struct usb_cpia *ucpia = (struct usb_cpia *)privdata;
	struct usb_device *udev = ucpia->dev;

	if (!udev) {
		DBG("Internal driver error: udev is NULL\n");
		return -EINVAL;
	}

	if (!command) {
		DBG("Internal driver error: command is NULL\n");
		return -EINVAL;
	}

	databytes = (((int)command[7])<<8) | command[6];

	if (command[0] == DATA_IN) {
		u8 buffer[8];

		if (!data) {
			DBG("Internal driver error: data is NULL\n");
			return -EINVAL;
		}

		err = ReadPacket(udev, command, buffer, 8);
		if (err < 0)
			return err;

		memcpy(data, buffer, databytes);
	} else if(command[0] == DATA_OUT)
		WritePacket(udev, command, data, databytes);
	else {
		DBG("Unexpected first byte of command: %x\n", command[0]);
		err = -EINVAL;
	}

	return 0;
}

static int cpia_usb_registerCallback(void *privdata, void (*cb) (void *cbdata),
	void *cbdata)
{
	return -ENODEV;
}

static int cpia_usb_streamStart(void *privdata)
{
	return -ENODEV;
}

static int cpia_usb_streamStop(void *privdata)
{
	return -ENODEV;
}

static int cpia_usb_streamRead(void *privdata, u8 *frame, int noblock)
{
	struct usb_cpia *ucpia = (struct usb_cpia *) privdata;
	struct framebuf *mybuff;

	if (!ucpia || !ucpia->present)
		return -1;
  
	if (ucpia->curbuff->status != FRAME_READY)
		interruptible_sleep_on(&ucpia->wq_stream);
	else
		DBG("Frame already waiting!\n");

	mybuff = ucpia->curbuff;

	if (!mybuff)
		return -1;
  
	if (mybuff->status != FRAME_READY || mybuff->length < 4) {
		DBG("Something went wrong!\n");
		return -1;
	}

	memcpy(frame, mybuff->data, mybuff->length);
	mybuff->status = FRAME_EMPTY;
  
/*   DBG("read done, %d bytes, Header: %x/%x, Footer: %x%x%x%x\n",  */
/*       mybuff->length, frame[0], frame[1], */
/*       frame[mybuff->length-4], frame[mybuff->length-3],  */
/*       frame[mybuff->length-2], frame[mybuff->length-1]); */

	return mybuff->length;
}

static void cpia_usb_free_resources(struct usb_cpia *ucpia, int try)
{
	if (!ucpia->streaming)
		return;

	ucpia->streaming = 0;

	/* Set packet size to 0 */
	if (try) {
		int ret;

		ret = usb_set_interface(ucpia->dev, ucpia->iface, 0);
		if (ret < 0) {
			printk(KERN_ERR "usb_set_interface error (ret = %d)\n", ret);
			return;
		}
	}

	/* Unschedule all of the iso td's */
	if (ucpia->sbuf[1].urb) {
		usb_kill_urb(ucpia->sbuf[1].urb);
		usb_free_urb(ucpia->sbuf[1].urb);
		ucpia->sbuf[1].urb = NULL;
	}

	kfree(ucpia->sbuf[1].data);
	ucpia->sbuf[1].data = NULL;
 
	if (ucpia->sbuf[0].urb) {
		usb_kill_urb(ucpia->sbuf[0].urb);
		usb_free_urb(ucpia->sbuf[0].urb);
		ucpia->sbuf[0].urb = NULL;
	}

	kfree(ucpia->sbuf[0].data);
	ucpia->sbuf[0].data = NULL;
}

static int cpia_usb_close(void *privdata)
{
	struct usb_cpia *ucpia = (struct usb_cpia *) privdata;

	if(!ucpia)
		return -ENODEV;

	ucpia->open = 0;

	/* ucpia->present = 0 protects against trying to reset the
	 * alt setting if camera is physically disconnected while open */
	cpia_usb_free_resources(ucpia, ucpia->present);

	return 0;
}

int cpia_usb_init(void)
{
	/* return -ENODEV; */
	return 0;
}

/* Probing and initializing */

static int cpia_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_cpia *ucpia;
	struct cam_data *cam;
	int ret;
  
	/* A multi-config CPiA camera? */
	if (udev->descriptor.bNumConfigurations != 1)
		return -ENODEV;

	interface = intf->cur_altsetting;

	printk(KERN_INFO "USB CPiA camera found\n");

	ucpia = kmalloc(sizeof(*ucpia), GFP_KERNEL);
	if (!ucpia) {
		printk(KERN_ERR "couldn't kmalloc cpia struct\n");
		return -ENOMEM;
	}

	memset(ucpia, 0, sizeof(*ucpia));

	ucpia->dev = udev;
	ucpia->iface = interface->desc.bInterfaceNumber;
	init_waitqueue_head(&ucpia->wq_stream);

	ucpia->buffers[0] = vmalloc(sizeof(*ucpia->buffers[0]));
	if (!ucpia->buffers[0]) {
		printk(KERN_ERR "couldn't vmalloc frame buffer 0\n");
		goto fail_alloc_0;
	}

	ucpia->buffers[1] = vmalloc(sizeof(*ucpia->buffers[1]));
	if (!ucpia->buffers[1]) {
		printk(KERN_ERR "couldn't vmalloc frame buffer 1\n");
		goto fail_alloc_1;
	}

	ucpia->buffers[2] = vmalloc(sizeof(*ucpia->buffers[2]));
	if (!ucpia->buffers[2]) {
		printk(KERN_ERR "couldn't vmalloc frame buffer 2\n");
		goto fail_alloc_2;
	}

	ucpia->buffers[0]->next = ucpia->buffers[1];
	ucpia->buffers[1]->next = ucpia->buffers[2];
	ucpia->buffers[2]->next = ucpia->buffers[0];

	ret = usb_set_interface(udev, ucpia->iface, 0);
	if (ret < 0) {
		printk(KERN_ERR "cpia_probe: usb_set_interface error (ret = %d)\n", ret);
		/* goto fail_all; */
	}

	/* Before register_camera, important */
	ucpia->present = 1;
  
	cam = cpia_register_camera(&cpia_usb_ops, ucpia);
	if (!cam) {
		LOG("failed to cpia_register_camera\n");
		goto fail_all;
	}

	spin_lock( &cam_list_lock_usb );
	list_add( &cam->cam_data_list, &cam_list );
	spin_unlock( &cam_list_lock_usb );

	usb_set_intfdata(intf, cam);
	return 0;

fail_all:
	vfree(ucpia->buffers[2]);
	ucpia->buffers[2] = NULL;
fail_alloc_2:
	vfree(ucpia->buffers[1]);
	ucpia->buffers[1] = NULL;
fail_alloc_1:
	vfree(ucpia->buffers[0]);
	ucpia->buffers[0] = NULL;
fail_alloc_0:
	kfree(ucpia);
	return -EIO;
}

static void cpia_disconnect(struct usb_interface *intf);

static struct usb_device_id cpia_id_table [] = {
	{ USB_DEVICE(0x0553, 0x0002) },
	{ USB_DEVICE(0x0813, 0x0001) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, cpia_id_table);
MODULE_LICENSE("GPL");


static struct usb_driver cpia_driver = {
	.owner		= THIS_MODULE,
	.name		= "cpia",
	.probe		= cpia_probe,
	.disconnect	= cpia_disconnect,
	.id_table	= cpia_id_table,
};

static void cpia_disconnect(struct usb_interface *intf)
{
	struct cam_data *cam = usb_get_intfdata(intf);
	struct usb_cpia *ucpia;
	struct usb_device *udev;
  
	usb_set_intfdata(intf, NULL);
	if (!cam)
		return;

	ucpia = (struct usb_cpia *) cam->lowlevel_data;
	spin_lock( &cam_list_lock_usb );
	list_del(&cam->cam_data_list);
	spin_unlock( &cam_list_lock_usb );
	
	ucpia->present = 0;

	cpia_unregister_camera(cam);
	if(ucpia->open)
		cpia_usb_close(cam->lowlevel_data);

	ucpia->curbuff->status = FRAME_ERROR;

	if (waitqueue_active(&ucpia->wq_stream))
		wake_up_interruptible(&ucpia->wq_stream);

	udev = interface_to_usbdev(intf);

	ucpia->curbuff = ucpia->workbuff = NULL;

	vfree(ucpia->buffers[2]);
	ucpia->buffers[2] = NULL;

	vfree(ucpia->buffers[1]);
	ucpia->buffers[1] = NULL;

	vfree(ucpia->buffers[0]);
	ucpia->buffers[0] = NULL;

	cam->lowlevel_data = NULL;
	kfree(ucpia);
}

static int __init usb_cpia_init(void)
{
	printk(KERN_INFO "%s v%d.%d.%d\n",ABOUT, 
	       CPIA_USB_MAJ_VER,CPIA_USB_MIN_VER,CPIA_USB_PATCH_VER);

	spin_lock_init(&cam_list_lock_usb);
	return usb_register(&cpia_driver);
}

static void __exit usb_cpia_cleanup(void)
{
	usb_deregister(&cpia_driver);
}


module_init (usb_cpia_init);
module_exit (usb_cpia_cleanup);

