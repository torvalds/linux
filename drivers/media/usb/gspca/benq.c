// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Benq DC E300 subdriver
 *
 * Copyright (C) 2009 Jean-Francois Moine (http://moinejf.free.fr)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "benq"

#include "gspca.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("Benq DC E300 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
};

static const struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG},
};

static void sd_isoc_irq(struct urb *urb);

/* -- write a register -- */
static void reg_w(struct gspca_dev *gspca_dev,
			u16 value, u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x02,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			NULL,
			0,
			500);
	if (ret < 0) {
		pr_err("reg_w err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(vga_mode);
	gspca_dev->cam.no_urb_create = 1;
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct urb *urb;
	int i, n;

	/* create 4 URBs - 2 on endpoint 0x83 and 2 on 0x082 */
#if MAX_NURBS < 4
#error "Not enough URBs in the gspca table"
#endif
#define SD_PKT_SZ 64
#define SD_NPKT 32
	for (n = 0; n < 4; n++) {
		urb = usb_alloc_urb(SD_NPKT, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		gspca_dev->urb[n] = urb;
		urb->transfer_buffer = usb_alloc_coherent(gspca_dev->dev,
						SD_PKT_SZ * SD_NPKT,
						GFP_KERNEL,
						&urb->transfer_dma);

		if (urb->transfer_buffer == NULL) {
			pr_err("usb_alloc_coherent failed\n");
			return -ENOMEM;
		}
		urb->dev = gspca_dev->dev;
		urb->context = gspca_dev;
		urb->transfer_buffer_length = SD_PKT_SZ * SD_NPKT;
		urb->pipe = usb_rcvisocpipe(gspca_dev->dev,
					n & 1 ? 0x82 : 0x83);
		urb->transfer_flags = URB_ISO_ASAP
					| URB_NO_TRANSFER_DMA_MAP;
		urb->interval = 1;
		urb->complete = sd_isoc_irq;
		urb->number_of_packets = SD_NPKT;
		for (i = 0; i < SD_NPKT; i++) {
			urb->iso_frame_desc[i].length = SD_PKT_SZ;
			urb->iso_frame_desc[i].offset = SD_PKT_SZ * i;
		}
	}

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_interface *intf;

	reg_w(gspca_dev, 0x003c, 0x0003);
	reg_w(gspca_dev, 0x003c, 0x0004);
	reg_w(gspca_dev, 0x003c, 0x0005);
	reg_w(gspca_dev, 0x003c, 0x0006);
	reg_w(gspca_dev, 0x003c, 0x0007);

	intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
	usb_set_interface(gspca_dev->dev, gspca_dev->iface,
					intf->num_altsetting - 1);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* isoc packet */
			int len)		/* iso packet length */
{
	/* unused */
}

/* reception of an URB */
static void sd_isoc_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	struct urb *urb0;
	u8 *data;
	int i, st;

	gspca_dbg(gspca_dev, D_PACK, "sd isoc irq\n");
	if (!gspca_dev->streaming)
		return;
	if (urb->status != 0) {
		if (urb->status == -ESHUTDOWN)
			return;		/* disconnection */
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			return;
#endif
		pr_err("urb status: %d\n", urb->status);
		return;
	}

	/* if this is a control URN (ep 0x83), wait */
	if (urb == gspca_dev->urb[0] || urb == gspca_dev->urb[2])
		return;

	/* scan both received URBs */
	if (urb == gspca_dev->urb[1])
		urb0 = gspca_dev->urb[0];
	else
		urb0 = gspca_dev->urb[2];
	for (i = 0; i < urb->number_of_packets; i++) {

		/* check the packet status and length */
		if (urb0->iso_frame_desc[i].actual_length != SD_PKT_SZ
		    || urb->iso_frame_desc[i].actual_length != SD_PKT_SZ) {
			gspca_err(gspca_dev, "ISOC bad lengths %d / %d\n",
				  urb0->iso_frame_desc[i].actual_length,
				  urb->iso_frame_desc[i].actual_length);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}
		st = urb0->iso_frame_desc[i].status;
		if (st == 0)
			st = urb->iso_frame_desc[i].status;
		if (st) {
			pr_err("ISOC data error: [%d] status=%d\n",
				i, st);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}

		/*
		 * The images are received in URBs of different endpoints
		 * (0x83 and 0x82).
		 * Image pieces in URBs of ep 0x83 are continuated in URBs of
		 * ep 0x82 of the same index.
		 * The packets in the URBs of endpoint 0x83 start with:
		 *	- 80 ba/bb 00 00 = start of image followed by 'ff d8'
		 *	- 04 ba/bb oo oo = image piece
		 *		where 'oo oo' is the image offset
						(not checked)
		 *	- (other -> bad frame)
		 * The images are JPEG encoded with full header and
		 * normal ff escape.
		 * The end of image ('ff d9') may occur in any URB.
		 * (not checked)
		 */
		data = (u8 *) urb0->transfer_buffer
					+ urb0->iso_frame_desc[i].offset;
		if (data[0] == 0x80 && (data[1] & 0xfe) == 0xba) {

			/* new image */
			gspca_frame_add(gspca_dev, LAST_PACKET,
					NULL, 0);
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					data + 4, SD_PKT_SZ - 4);
		} else if (data[0] == 0x04 && (data[1] & 0xfe) == 0xba) {
			gspca_frame_add(gspca_dev, INTER_PACKET,
					data + 4, SD_PKT_SZ - 4);
		} else {
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}
		data = (u8 *) urb->transfer_buffer
					+ urb->iso_frame_desc[i].offset;
		gspca_frame_add(gspca_dev, INTER_PACKET,
				data, SD_PKT_SZ);
	}

	/* resubmit the URBs */
	st = usb_submit_urb(urb0, GFP_ATOMIC);
	if (st < 0)
		pr_err("usb_submit_urb(0) ret %d\n", st);
	st = usb_submit_urb(urb, GFP_ATOMIC);
	if (st < 0)
		pr_err("usb_submit_urb() ret %d\n", st);
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x04a5, 0x3035)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
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
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
