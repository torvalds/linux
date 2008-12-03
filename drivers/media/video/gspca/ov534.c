/*
 * ov534/ov772x gspca driver
 * Copyright (C) 2008 Antonio Ospite <ospite@studenti.unina.it>
 *
 * Based on a prototype written by Mark Ferrell <majortrips@gmail.com>
 * USB protocol reverse engineered by Jim Paris <jim@jtan.com>
 * https://jim.sh/svn/jim/devl/playstation/ps3/eye/test/
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

#define MODULE_NAME "ov534"

#include "gspca.h"

#define OV534_REG_ADDRESS	0xf1	/* ? */
#define OV534_REG_SUBADDR	0xf2
#define OV534_REG_WRITE		0xf3
#define OV534_REG_READ		0xf4
#define OV534_REG_OPERATION	0xf5
#define OV534_REG_STATUS	0xf6

#define OV534_OP_WRITE_3	0x37
#define OV534_OP_WRITE_2	0x33
#define OV534_OP_READ_2		0xf9

#define CTRL_TIMEOUT 500

MODULE_AUTHOR("Antonio Ospite <ospite@studenti.unina.it>");
MODULE_DESCRIPTION("GSPCA/OV534 USB Camera Driver");
MODULE_LICENSE("GPL");

/* global parameters */
static int frame_rate;

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
};

/* V4L2 controls supported by the driver */
static struct ctrl sd_ctrls[] = {
};

static struct v4l2_pix_format vga_mode[] = {
	{640, 480, V4L2_PIX_FMT_YUYV, V4L2_FIELD_NONE,
	 .bytesperline = 640 * 2,
	 .sizeimage = 640 * 480 * 2,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 .priv = 0},
};

static void ov534_reg_write(struct usb_device *udev, u16 reg, u16 val)
{
	u16 data = val;
	int ret;

	PDEBUG(D_USBO, "reg=0x%04x, val=0%04x", reg, val);
	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      0x1,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0, reg, &data, 1, CTRL_TIMEOUT);
	if (ret < 0)
		PDEBUG(D_ERR, "write failed");
}

static u16 ov534_reg_read(struct usb_device *udev, u16 reg)
{
	u16 data;
	int ret;

	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      0x1,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x0, reg, &data, 1, CTRL_TIMEOUT);
	PDEBUG(D_USBI, "reg=0x%04x, data=0x%04x", reg, data);
	if (ret < 0)
		PDEBUG(D_ERR, "read failed");
	return data;
}

static void ov534_reg_verify_write(struct usb_device *udev, u16 reg, u16 val)
{
	u16 data;

	ov534_reg_write(udev, reg, val);
	data = ov534_reg_read(udev, reg);
	if (data != val) {
		PDEBUG(D_ERR | D_USBO,
		       "unexpected result from read: 0x%04x != 0x%04x", val,
		       data);
	}
}

/* Two bits control LED: 0x21 bit 7 and 0x23 bit 7.
 * (direction and output)? */
static void ov534_set_led(struct usb_device *udev, int status)
{
	u16 data;

	PDEBUG(D_CONF, "led status: %d", status);

	data = ov534_reg_read(udev, 0x21);
	data |= 0x80;
	ov534_reg_write(udev, 0x21, data);

	data = ov534_reg_read(udev, 0x23);
	if (status)
		data |= 0x80;
	else
		data &= ~(0x80);

	ov534_reg_write(udev, 0x23, data);
}

static int sccb_check_status(struct usb_device *udev)
{
	u16 data;
	int i;

	for (i = 0; i < 5; i++) {
		data = ov534_reg_read(udev, OV534_REG_STATUS);

		switch (data & 0xFF) {
		case 0x00:
			return 1;
		case 0x04:
			return 0;
		case 0x03:
			break;
		default:
			PDEBUG(D_ERR, "sccb status 0x%02x, attempt %d/5\n",
			       data, i + 1);
		}
	}
	return 0;
}

static void sccb_reg_write(struct usb_device *udev, u16 reg, u16 val)
{
	PDEBUG(D_USBO, "reg: 0x%04x, val: 0x%04x", reg, val);
	ov534_reg_write(udev, OV534_REG_SUBADDR, reg);
	ov534_reg_write(udev, OV534_REG_WRITE, val);
	ov534_reg_write(udev, OV534_REG_OPERATION, OV534_OP_WRITE_3);

	if (!sccb_check_status(udev))
		PDEBUG(D_ERR, "sccb_reg_write failed");
}

/* setup method */
static void ov534_setup(struct usb_device *udev)
{
	ov534_reg_verify_write(udev, 0xe7, 0x3a);

	ov534_reg_write(udev, OV534_REG_ADDRESS, 0x60);
	ov534_reg_write(udev, OV534_REG_ADDRESS, 0x60);
	ov534_reg_write(udev, OV534_REG_ADDRESS, 0x60);
	ov534_reg_write(udev, OV534_REG_ADDRESS, 0x42);

	ov534_reg_verify_write(udev, 0xc2, 0x0c);
	ov534_reg_verify_write(udev, 0x88, 0xf8);
	ov534_reg_verify_write(udev, 0xc3, 0x69);
	ov534_reg_verify_write(udev, 0x89, 0xff);
	ov534_reg_verify_write(udev, 0x76, 0x03);
	ov534_reg_verify_write(udev, 0x92, 0x01);
	ov534_reg_verify_write(udev, 0x93, 0x18);
	ov534_reg_verify_write(udev, 0x94, 0x10);
	ov534_reg_verify_write(udev, 0x95, 0x10);
	ov534_reg_verify_write(udev, 0xe2, 0x00);
	ov534_reg_verify_write(udev, 0xe7, 0x3e);

	ov534_reg_write(udev, 0x1c, 0x0a);
	ov534_reg_write(udev, 0x1d, 0x22);
	ov534_reg_write(udev, 0x1d, 0x06);

	ov534_reg_verify_write(udev, 0x96, 0x00);

	ov534_reg_write(udev, 0x97, 0x20);
	ov534_reg_write(udev, 0x97, 0x20);
	ov534_reg_write(udev, 0x97, 0x20);
	ov534_reg_write(udev, 0x97, 0x0a);
	ov534_reg_write(udev, 0x97, 0x3f);
	ov534_reg_write(udev, 0x97, 0x4a);
	ov534_reg_write(udev, 0x97, 0x20);
	ov534_reg_write(udev, 0x97, 0x15);
	ov534_reg_write(udev, 0x97, 0x0b);

	ov534_reg_verify_write(udev, 0x8e, 0x40);
	ov534_reg_verify_write(udev, 0x1f, 0x81);
	ov534_reg_verify_write(udev, 0x34, 0x05);
	ov534_reg_verify_write(udev, 0xe3, 0x04);
	ov534_reg_verify_write(udev, 0x88, 0x00);
	ov534_reg_verify_write(udev, 0x89, 0x00);
	ov534_reg_verify_write(udev, 0x76, 0x00);
	ov534_reg_verify_write(udev, 0xe7, 0x2e);
	ov534_reg_verify_write(udev, 0x31, 0xf9);
	ov534_reg_verify_write(udev, 0x25, 0x42);
	ov534_reg_verify_write(udev, 0x21, 0xf0);

	ov534_reg_write(udev, 0x1c, 0x00);
	ov534_reg_write(udev, 0x1d, 0x40);
	ov534_reg_write(udev, 0x1d, 0x02);
	ov534_reg_write(udev, 0x1d, 0x00);
	ov534_reg_write(udev, 0x1d, 0x02);
	ov534_reg_write(udev, 0x1d, 0x57);
	ov534_reg_write(udev, 0x1d, 0xff);

	ov534_reg_verify_write(udev, 0x8d, 0x1c);
	ov534_reg_verify_write(udev, 0x8e, 0x80);
	ov534_reg_verify_write(udev, 0xe5, 0x04);

	ov534_set_led(udev, 1);

	sccb_reg_write(udev, 0x12, 0x80);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x11, 0x01);

	ov534_set_led(udev, 0);

	sccb_reg_write(udev, 0x3d, 0x03);
	sccb_reg_write(udev, 0x17, 0x26);
	sccb_reg_write(udev, 0x18, 0xa0);
	sccb_reg_write(udev, 0x19, 0x07);
	sccb_reg_write(udev, 0x1a, 0xf0);
	sccb_reg_write(udev, 0x32, 0x00);
	sccb_reg_write(udev, 0x29, 0xa0);
	sccb_reg_write(udev, 0x2c, 0xf0);
	sccb_reg_write(udev, 0x65, 0x20);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x42, 0x7f);
	sccb_reg_write(udev, 0x63, 0xe0);
	sccb_reg_write(udev, 0x64, 0xff);
	sccb_reg_write(udev, 0x66, 0x00);
	sccb_reg_write(udev, 0x13, 0xf0);
	sccb_reg_write(udev, 0x0d, 0x41);
	sccb_reg_write(udev, 0x0f, 0xc5);
	sccb_reg_write(udev, 0x14, 0x11);

	ov534_set_led(udev, 1);

	sccb_reg_write(udev, 0x22, 0x7f);
	sccb_reg_write(udev, 0x23, 0x03);
	sccb_reg_write(udev, 0x24, 0x40);
	sccb_reg_write(udev, 0x25, 0x30);
	sccb_reg_write(udev, 0x26, 0xa1);
	sccb_reg_write(udev, 0x2a, 0x00);
	sccb_reg_write(udev, 0x2b, 0x00);
	sccb_reg_write(udev, 0x6b, 0xaa);
	sccb_reg_write(udev, 0x13, 0xff);

	ov534_set_led(udev, 0);

	sccb_reg_write(udev, 0x90, 0x05);
	sccb_reg_write(udev, 0x91, 0x01);
	sccb_reg_write(udev, 0x92, 0x03);
	sccb_reg_write(udev, 0x93, 0x00);
	sccb_reg_write(udev, 0x94, 0x60);
	sccb_reg_write(udev, 0x95, 0x3c);
	sccb_reg_write(udev, 0x96, 0x24);
	sccb_reg_write(udev, 0x97, 0x1e);
	sccb_reg_write(udev, 0x98, 0x62);
	sccb_reg_write(udev, 0x99, 0x80);
	sccb_reg_write(udev, 0x9a, 0x1e);
	sccb_reg_write(udev, 0x9b, 0x08);
	sccb_reg_write(udev, 0x9c, 0x20);
	sccb_reg_write(udev, 0x9e, 0x81);

	ov534_set_led(udev, 1);

	sccb_reg_write(udev, 0xa6, 0x04);
	sccb_reg_write(udev, 0x7e, 0x0c);
	sccb_reg_write(udev, 0x7f, 0x16);
	sccb_reg_write(udev, 0x80, 0x2a);
	sccb_reg_write(udev, 0x81, 0x4e);
	sccb_reg_write(udev, 0x82, 0x61);
	sccb_reg_write(udev, 0x83, 0x6f);
	sccb_reg_write(udev, 0x84, 0x7b);
	sccb_reg_write(udev, 0x85, 0x86);
	sccb_reg_write(udev, 0x86, 0x8e);
	sccb_reg_write(udev, 0x87, 0x97);
	sccb_reg_write(udev, 0x88, 0xa4);
	sccb_reg_write(udev, 0x89, 0xaf);
	sccb_reg_write(udev, 0x8a, 0xc5);
	sccb_reg_write(udev, 0x8b, 0xd7);
	sccb_reg_write(udev, 0x8c, 0xe8);
	sccb_reg_write(udev, 0x8d, 0x20);

	sccb_reg_write(udev, 0x0c, 0x90);

	ov534_reg_verify_write(udev, 0xc0, 0x50);
	ov534_reg_verify_write(udev, 0xc1, 0x3c);
	ov534_reg_verify_write(udev, 0xc2, 0x0c);

	ov534_set_led(udev, 1);

	sccb_reg_write(udev, 0x2b, 0x00);
	sccb_reg_write(udev, 0x22, 0x7f);
	sccb_reg_write(udev, 0x23, 0x03);
	sccb_reg_write(udev, 0x11, 0x01);
	sccb_reg_write(udev, 0x0c, 0xd0);
	sccb_reg_write(udev, 0x64, 0xff);
	sccb_reg_write(udev, 0x0d, 0x41);

	sccb_reg_write(udev, 0x14, 0x41);
	sccb_reg_write(udev, 0x0e, 0xcd);
	sccb_reg_write(udev, 0xac, 0xbf);
	sccb_reg_write(udev, 0x8e, 0x00);
	sccb_reg_write(udev, 0x0c, 0xd0);

	ov534_reg_write(udev, 0xe0, 0x09);
	ov534_set_led(udev, 0);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct cam *cam;

	cam = &gspca_dev->cam;

	cam->epaddr = 0x01;
	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);

	cam->bulk_size = vga_mode[0].sizeimage;
	cam->bulk_nurbs = 2;

	PDEBUG(D_PROBE, "bulk_size = %d", cam->bulk_size);

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	int fr;

	ov534_setup(gspca_dev->dev);

	fr = frame_rate;

	switch (fr) {
	case 50:
		sccb_reg_write(gspca_dev->dev, 0x11, 0x01);
		sccb_reg_write(gspca_dev->dev, 0x0d, 0x41);
		ov534_reg_verify_write(gspca_dev->dev, 0xe5, 0x02);
		break;
	case 40:
		sccb_reg_write(gspca_dev->dev, 0x11, 0x02);
		sccb_reg_write(gspca_dev->dev, 0x0d, 0xc1);
		ov534_reg_verify_write(gspca_dev->dev, 0xe5, 0x04);
		break;
/*	case 30: */
	default:
		fr = 30;
		sccb_reg_write(gspca_dev->dev, 0x11, 0x04);
		sccb_reg_write(gspca_dev->dev, 0x0d, 0x81);
		ov534_reg_verify_write(gspca_dev->dev, 0xe5, 0x02);
		break;
	case 15:
		sccb_reg_write(gspca_dev->dev, 0x11, 0x03);
		sccb_reg_write(gspca_dev->dev, 0x0d, 0x41);
		ov534_reg_verify_write(gspca_dev->dev, 0xe5, 0x04);
		break;
	}

	PDEBUG(D_PROBE, "frame_rate: %d", fr);

	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	PDEBUG(D_PROBE, "width = %d, height = %d",
	       gspca_dev->width, gspca_dev->height);

	gspca_dev->cam.bulk_size = gspca_dev->width * gspca_dev->height * 2;

	/* start streaming data */
	ov534_set_led(gspca_dev->dev, 1);
	ov534_reg_write(gspca_dev->dev, 0xe0, 0x00);

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	/* stop streaming data */
	ov534_reg_write(gspca_dev->dev, 0xe0, 0x09);
	ov534_set_led(gspca_dev->dev, 0);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev, struct gspca_frame *frame,
			__u8 *data, int len)
{
	/*
	 * The current camera setup doesn't stream the last pixel, so we set it
	 * to a dummy value
	 */
	__u8 last_pixel[4] = { 0, 0, 0, 0 };
	int framesize = gspca_dev->cam.bulk_size;

	if (len == framesize - 4) {
		frame =
		    gspca_frame_add(gspca_dev, FIRST_PACKET, frame, data, len);
		frame =
		    gspca_frame_add(gspca_dev, LAST_PACKET, frame, last_pixel,
				    4);
	} else
		PDEBUG(D_PACK, "packet len = %d, framesize = %d", len,
		       framesize);
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name     = MODULE_NAME,
	.ctrls    = sd_ctrls,
	.nctrls   = ARRAY_SIZE(sd_ctrls),
	.config   = sd_config,
	.init     = sd_init,
	.start    = sd_start,
	.stopN    = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x06f8, 0x3002)},	/* Hercules Blog Webcam */
	{USB_DEVICE(0x06f8, 0x3003)},	/* Hercules Dualpix HD Weblog */
	{USB_DEVICE(0x1415, 0x2000)},	/* Sony HD Eye for PS3 (SLEH 00201) */
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
			       THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name       = MODULE_NAME,
	.id_table   = device_table,
	.probe      = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend    = gspca_suspend,
	.resume     = gspca_resume,
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

module_param(frame_rate, int, 0644);
MODULE_PARM_DESC(frame_rate, "Frame rate (15, 30, 40, 50)");
