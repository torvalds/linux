/*
 * Fujifilm Finepix subdriver
 *
 * Copyright (C) 2008 Frank Zago
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define MODULE_NAME "finepix"

#include "gspca.h"

MODULE_AUTHOR("Frank Zago <frank@zago.net>");
MODULE_DESCRIPTION("Fujifilm FinePix USB V4L2 driver");
MODULE_LICENSE("GPL");

/* Default timeout, in ms */
#define FPIX_TIMEOUT (HZ / 10)

/* Maximum transfer size to use. The windows driver reads by chunks of
 * 0x2000 bytes, so do the same. Note: reading more seems to work
 * too. */
#define FPIX_MAX_TRANSFER 0x2000

/* Structure to hold all of our device specific stuff */
struct usb_fpix {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	/*
	 * USB stuff
	 */
	struct usb_ctrlrequest ctrlreq;
	struct urb *control_urb;
	struct timer_list bulk_timer;

	enum {
		FPIX_NOP,	/* inactive, else streaming */
		FPIX_RESET,	/* must reset */
		FPIX_REQ_FRAME,	/* requesting a frame */
		FPIX_READ_FRAME,	/* reading frame */
	} state;

	/*
	 * Driver stuff
	 */
	struct delayed_work wqe;
	struct completion can_close;
	int streaming;
};

/* Delay after which claim the next frame. If the delay is too small,
 * the camera will return old frames. On the 4800Z, 20ms is bad, 25ms
 * will fail every 4 or 5 frames, but 30ms is perfect. */
#define NEXT_FRAME_DELAY  (((HZ * 30) + 999) / 1000)

#define dev_new_state(new_state) {				\
		PDEBUG(D_STREAM, "new state from %d to %d at %s:%d",	\
			dev->state, new_state, __func__, __LINE__);	\
		dev->state = new_state;					\
}

/* These cameras only support 320x200. */
static const struct v4l2_pix_format fpix_mode[1] = {
	{ 320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0}
};

/* Reads part of a frame */
static void read_frame_part(struct usb_fpix *dev)
{
	int ret;

	PDEBUG(D_STREAM, "read_frame_part");

	/* Reads part of a frame */
	ret = usb_submit_urb(dev->gspca_dev.urb[0], GFP_ATOMIC);
	if (ret) {
		dev_new_state(FPIX_RESET);
		schedule_delayed_work(&dev->wqe, 1);
		PDEBUG(D_STREAM, "usb_submit_urb failed with %d",
			ret);
	} else {
		/* Sometimes we never get a callback, so use a timer.
		 * Is this masking a bug somewhere else? */
		dev->bulk_timer.expires = jiffies + msecs_to_jiffies(150);
		add_timer(&dev->bulk_timer);
	}
}

/* Callback for URBs. */
static void urb_callback(struct urb *urb)
{
	struct gspca_dev *gspca_dev = urb->context;
	struct usb_fpix *dev = (struct usb_fpix *) gspca_dev;

	PDEBUG(D_PACK,
		"enter urb_callback - status=%d, length=%d",
		urb->status, urb->actual_length);

	if (dev->state == FPIX_READ_FRAME)
		del_timer(&dev->bulk_timer);

	if (urb->status != 0) {
		/* We kill a stuck urb every 50 frames on average, so don't
		 * display a log message for that. */
		if (urb->status != -ECONNRESET)
			PDEBUG(D_STREAM, "bad URB status %d", urb->status);
		dev_new_state(FPIX_RESET);
		schedule_delayed_work(&dev->wqe, 1);
	}

	switch (dev->state) {
	case FPIX_REQ_FRAME:
		dev_new_state(FPIX_READ_FRAME);
		read_frame_part(dev);
		break;

	case FPIX_READ_FRAME: {
		unsigned char *data = urb->transfer_buffer;
		struct gspca_frame *frame;

		frame = gspca_get_i_frame(&dev->gspca_dev);
		if (frame == NULL)
			gspca_dev->last_packet_type = DISCARD_PACKET;
		if (urb->actual_length < FPIX_MAX_TRANSFER ||
			(data[urb->actual_length-2] == 0xff &&
				data[urb->actual_length-1] == 0xd9)) {

			/* If the result is less than what was asked
			 * for, then it's the end of the
			 * frame. Sometime the jpeg is not complete,
			 * but there's nothing we can do. We also end
			 * here if the the jpeg ends right at the end
			 * of the frame. */
			if (frame)
				gspca_frame_add(gspca_dev, LAST_PACKET,
						frame,
						data, urb->actual_length);
			dev_new_state(FPIX_REQ_FRAME);
			schedule_delayed_work(&dev->wqe, NEXT_FRAME_DELAY);
		} else {

			/* got a partial image */
			if (frame)
				gspca_frame_add(gspca_dev,
						gspca_dev->last_packet_type
								== LAST_PACKET
						? FIRST_PACKET : INTER_PACKET,
						frame,
					data, urb->actual_length);
			read_frame_part(dev);
		}
		break;
	    }

	case FPIX_NOP:
	case FPIX_RESET:
		PDEBUG(D_STREAM, "invalid state %d", dev->state);
		break;
	}
}

/* Request a new frame */
static void request_frame(struct usb_fpix *dev)
{
	int ret;
	struct gspca_dev *gspca_dev = &dev->gspca_dev;

	/* Setup command packet */
	memset(gspca_dev->usb_buf, 0, 12);
	gspca_dev->usb_buf[0] = 0xd3;
	gspca_dev->usb_buf[7] = 0x01;

	/* Request a frame */
	dev->ctrlreq.bRequestType =
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	dev->ctrlreq.bRequest = USB_REQ_GET_STATUS;
	dev->ctrlreq.wValue = 0;
	dev->ctrlreq.wIndex = 0;
	dev->ctrlreq.wLength = cpu_to_le16(12);

	usb_fill_control_urb(dev->control_urb,
			     gspca_dev->dev,
			     usb_sndctrlpipe(gspca_dev->dev, 0),
			     (unsigned char *) &dev->ctrlreq,
			     gspca_dev->usb_buf,
			     12, urb_callback, gspca_dev);

	ret = usb_submit_urb(dev->control_urb, GFP_ATOMIC);
	if (ret) {
		dev_new_state(FPIX_RESET);
		schedule_delayed_work(&dev->wqe, 1);
		PDEBUG(D_STREAM, "usb_submit_urb failed with %d", ret);
	}
}

/*--------------------------------------------------------------------------*/

/* State machine. */
static void fpix_sm(struct work_struct *work)
{
	struct usb_fpix *dev = container_of(work, struct usb_fpix, wqe.work);

	PDEBUG(D_STREAM, "fpix_sm state %d", dev->state);

	/* verify that the device wasn't unplugged */
	if (!dev->gspca_dev.present) {
		PDEBUG(D_STREAM, "device is gone");
		dev_new_state(FPIX_NOP);
		complete(&dev->can_close);
		return;
	}

	if (!dev->streaming) {
		PDEBUG(D_STREAM, "stopping state machine");
		dev_new_state(FPIX_NOP);
		complete(&dev->can_close);
		return;
	}

	switch (dev->state) {
	case FPIX_RESET:
		dev_new_state(FPIX_REQ_FRAME);
		schedule_delayed_work(&dev->wqe, HZ / 10);
		break;

	case FPIX_REQ_FRAME:
		/* get an image */
		request_frame(dev);
		break;

	case FPIX_NOP:
	case FPIX_READ_FRAME:
		PDEBUG(D_STREAM, "invalid state %d", dev->state);
		break;
	}
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		const struct usb_device_id *id)
{
	struct cam *cam = &gspca_dev->cam;

	cam->cam_mode = fpix_mode;
	cam->nmodes = 1;
	cam->epaddr = 0x01;	/* todo: correct for all cams? */
	cam->bulk_size = FPIX_MAX_TRANSFER;

/*	gspca_dev->nbalt = 1;	 * use bulk transfer */
	return 0;
}

/* Stop streaming and free the ressources allocated by sd_start. */
static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_fpix *dev = (struct usb_fpix *) gspca_dev;

	dev->streaming = 0;

	/* Stop the state machine */
	if (dev->state != FPIX_NOP)
		wait_for_completion(&dev->can_close);
}

/* called on streamoff with alt 0 and disconnect */
static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct usb_fpix *dev = (struct usb_fpix *) gspca_dev;

	usb_free_urb(dev->control_urb);
	dev->control_urb = NULL;
}

/* Kill an URB that hasn't completed. */
static void timeout_kill(unsigned long data)
{
	struct urb *urb = (struct urb *) data;

	usb_unlink_urb(urb);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct usb_fpix *dev = (struct usb_fpix *) gspca_dev;

	INIT_DELAYED_WORK(&dev->wqe, fpix_sm);

	init_timer(&dev->bulk_timer);
	dev->bulk_timer.function = timeout_kill;

	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct usb_fpix *dev = (struct usb_fpix *) gspca_dev;
	int ret;
	int size_ret;

	/* Init the device */
	memset(gspca_dev->usb_buf, 0, 12);
	gspca_dev->usb_buf[0] = 0xc6;
	gspca_dev->usb_buf[8] = 0x20;

	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			USB_REQ_GET_STATUS,
			USB_DIR_OUT | USB_TYPE_CLASS |
			USB_RECIP_INTERFACE, 0, 0, gspca_dev->usb_buf,
			12, FPIX_TIMEOUT);

	if (ret != 12) {
		PDEBUG(D_STREAM, "usb_control_msg failed (%d)", ret);
		ret = -EIO;
		goto error;
	}

	/* Read the result of the command. Ignore the result, for it
	 * varies with the device. */
	ret = usb_bulk_msg(gspca_dev->dev,
			usb_rcvbulkpipe(gspca_dev->dev,
					gspca_dev->cam.epaddr),
			gspca_dev->usb_buf, FPIX_MAX_TRANSFER, &size_ret,
			FPIX_TIMEOUT);
	if (ret != 0) {
		PDEBUG(D_STREAM, "usb_bulk_msg failed (%d)", ret);
		ret = -EIO;
		goto error;
	}

	/* Request a frame, but don't read it */
	memset(gspca_dev->usb_buf, 0, 12);
	gspca_dev->usb_buf[0] = 0xd3;
	gspca_dev->usb_buf[7] = 0x01;

	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			USB_REQ_GET_STATUS,
			USB_DIR_OUT | USB_TYPE_CLASS |
			USB_RECIP_INTERFACE, 0, 0, gspca_dev->usb_buf,
			12, FPIX_TIMEOUT);
	if (ret != 12) {
		PDEBUG(D_STREAM, "usb_control_msg failed (%d)", ret);
		ret = -EIO;
		goto error;
	}

	/* Again, reset bulk in endpoint */
	usb_clear_halt(gspca_dev->dev, gspca_dev->cam.epaddr);

	/* Allocate a control URB */
	dev->control_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->control_urb) {
		PDEBUG(D_STREAM, "No free urbs available");
		ret = -EIO;
		goto error;
	}

	/* Various initializations. */
	init_completion(&dev->can_close);
	dev->bulk_timer.data = (unsigned long)dev->gspca_dev.urb[0];
	dev->gspca_dev.urb[0]->complete = urb_callback;
	dev->streaming = 1;

	/* Schedule a frame request. */
	dev_new_state(FPIX_REQ_FRAME);
	schedule_delayed_work(&dev->wqe, 1);

	return 0;

error:
	/* Free the ressources */
	sd_stopN(gspca_dev);
	sd_stop0(gspca_dev);
	return ret;
}

/* Table of supported USB devices */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x04cb, 0x0104)},
	{USB_DEVICE(0x04cb, 0x0109)},
	{USB_DEVICE(0x04cb, 0x010b)},
	{USB_DEVICE(0x04cb, 0x010f)},
	{USB_DEVICE(0x04cb, 0x0111)},
	{USB_DEVICE(0x04cb, 0x0113)},
	{USB_DEVICE(0x04cb, 0x0115)},
	{USB_DEVICE(0x04cb, 0x0117)},
	{USB_DEVICE(0x04cb, 0x0119)},
	{USB_DEVICE(0x04cb, 0x011b)},
	{USB_DEVICE(0x04cb, 0x011d)},
	{USB_DEVICE(0x04cb, 0x0121)},
	{USB_DEVICE(0x04cb, 0x0123)},
	{USB_DEVICE(0x04cb, 0x0125)},
	{USB_DEVICE(0x04cb, 0x0127)},
	{USB_DEVICE(0x04cb, 0x0129)},
	{USB_DEVICE(0x04cb, 0x012b)},
	{USB_DEVICE(0x04cb, 0x012d)},
	{USB_DEVICE(0x04cb, 0x012f)},
	{USB_DEVICE(0x04cb, 0x0131)},
	{USB_DEVICE(0x04cb, 0x013b)},
	{USB_DEVICE(0x04cb, 0x013d)},
	{USB_DEVICE(0x04cb, 0x013f)},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
};

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id,
			&sd_desc,
			sizeof(struct usb_fpix),
			THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	if (usb_register(&sd_driver) < 0)
		return -1;
	PDEBUG(D_PROBE, "registered");
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
