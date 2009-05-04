/*
 * Syntek DV4000 (STK014) subdriver
 *
 * Copyright (C) 2008 Jean-Francois Moine (http://moinejf.free.fr)
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

#define MODULE_NAME "stk014"

#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("Syntek DV4000 (STK014) USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;
	unsigned char lightfreq;
	u8 quality;
#define QUALITY_MIN 60
#define QUALITY_MAX 95
#define QUALITY_DEF 80

	u8 *jpeg_hdr;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setfreq(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getfreq(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define BRIGHTNESS_DEF 127
		.default_value = BRIGHTNESS_DEF,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
	{
	    {
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define CONTRAST_DEF 127
		.default_value = CONTRAST_DEF,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Color",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define COLOR_DEF 127
		.default_value = COLOR_DEF,
	    },
	    .set = sd_setcolors,
	    .get = sd_getcolors,
	},
	{
	    {
		.id	 = V4L2_CID_POWER_LINE_FREQUENCY,
		.type    = V4L2_CTRL_TYPE_MENU,
		.name    = "Light frequency filter",
		.minimum = 1,
		.maximum = 2,	/* 0: 0, 1: 50Hz, 2:60Hz */
		.step    = 1,
#define FREQ_DEF 1
		.default_value = FREQ_DEF,
	    },
	    .set = sd_setfreq,
	    .get = sd_getfreq,
	},
};

static const struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

/* -- read a register -- */
static int reg_r(struct gspca_dev *gspca_dev,
			__u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x00,
			index,
			gspca_dev->usb_buf, 1,
			500);
	if (ret < 0) {
		PDEBUG(D_ERR, "reg_r err %d", ret);
		return ret;
	}
	return gspca_dev->usb_buf[0];
}

/* -- write a register -- */
static int reg_w(struct gspca_dev *gspca_dev,
			__u16 index, __u16 value)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			NULL,
			0,
			500);
	if (ret < 0)
		PDEBUG(D_ERR, "reg_w err %d", ret);
	return ret;
}

/* -- get a bulk value (4 bytes) -- */
static int rcv_val(struct gspca_dev *gspca_dev,
			int ads)
{
	struct usb_device *dev = gspca_dev->dev;
	int alen, ret;

	reg_w(gspca_dev, 0x634, (ads >> 16) & 0xff);
	reg_w(gspca_dev, 0x635, (ads >> 8) & 0xff);
	reg_w(gspca_dev, 0x636, ads & 0xff);
	reg_w(gspca_dev, 0x637, 0);
	reg_w(gspca_dev, 0x638, 4);	/* len & 0xff */
	reg_w(gspca_dev, 0x639, 0);	/* len >> 8 */
	reg_w(gspca_dev, 0x63a, 0);
	reg_w(gspca_dev, 0x63b, 0);
	reg_w(gspca_dev, 0x630, 5);
	ret = usb_bulk_msg(dev,
			usb_rcvbulkpipe(dev, 0x05),
			gspca_dev->usb_buf,
			4,		/* length */
			&alen,
			500);		/* timeout in milliseconds */
	return ret;
}

/* -- send a bulk value -- */
static int snd_val(struct gspca_dev *gspca_dev,
			int ads,
			unsigned int val)
{
	struct usb_device *dev = gspca_dev->dev;
	int alen, ret;
	__u8 seq = 0;

	if (ads == 0x003f08) {
		ret = reg_r(gspca_dev, 0x0704);
		if (ret < 0)
			goto ko;
		ret = reg_r(gspca_dev, 0x0705);
		if (ret < 0)
			goto ko;
		seq = ret;		/* keep the sequence number */
		ret = reg_r(gspca_dev, 0x0650);
		if (ret < 0)
			goto ko;
		reg_w(gspca_dev, 0x654, seq);
	} else {
		reg_w(gspca_dev, 0x654, (ads >> 16) & 0xff);
	}
	reg_w(gspca_dev, 0x655, (ads >> 8) & 0xff);
	reg_w(gspca_dev, 0x656, ads & 0xff);
	reg_w(gspca_dev, 0x657, 0);
	reg_w(gspca_dev, 0x658, 0x04);	/* size */
	reg_w(gspca_dev, 0x659, 0);
	reg_w(gspca_dev, 0x65a, 0);
	reg_w(gspca_dev, 0x65b, 0);
	reg_w(gspca_dev, 0x650, 5);
	gspca_dev->usb_buf[0] = val >> 24;
	gspca_dev->usb_buf[1] = val >> 16;
	gspca_dev->usb_buf[2] = val >> 8;
	gspca_dev->usb_buf[3] = val;
	ret = usb_bulk_msg(dev,
			usb_sndbulkpipe(dev, 6),
			gspca_dev->usb_buf,
			4,
			&alen,
			500);	/* timeout in milliseconds */
	if (ret < 0)
		goto ko;
	if (ads == 0x003f08) {
		seq += 4;
		seq &= 0x3f;
		reg_w(gspca_dev, 0x705, seq);
	}
	return ret;
ko:
	PDEBUG(D_ERR, "snd_val err %d", ret);
	return ret;
}

/* set a camera parameter */
static int set_par(struct gspca_dev *gspca_dev,
		   int parval)
{
	return snd_val(gspca_dev, 0x003f08, parval);
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int parval;

	parval = 0x06000000		/* whiteness */
		+ (sd->brightness << 16);
	set_par(gspca_dev, parval);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int parval;

	parval = 0x07000000		/* contrast */
		+ (sd->contrast << 16);
	set_par(gspca_dev, parval);
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int parval;

	parval = 0x08000000		/* saturation */
		+ (sd->colors << 16);
	set_par(gspca_dev, parval);
}

static void setfreq(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	set_par(gspca_dev, sd->lightfreq == 1
			? 0x33640000		/* 50 Hz */
			: 0x33780000);		/* 60 Hz */
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(vga_mode);
	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->colors = COLOR_DEF;
	sd->lightfreq = FREQ_DEF;
	sd->quality = QUALITY_DEF;
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	int ret;

	/* check if the device responds */
	usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);
	ret = reg_r(gspca_dev, 0x0740);
	if (ret < 0)
		return ret;
	if (ret != 0xff) {
		PDEBUG(D_ERR|D_STREAM, "init reg: 0x%02x", ret);
		return -1;
	}
	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret, value;

	/* create the JPEG header */
	sd->jpeg_hdr = kmalloc(JPEG_HDR_SZ, GFP_KERNEL);
	jpeg_define(sd->jpeg_hdr, gspca_dev->height, gspca_dev->width,
			0x22);		/* JPEG 411 */
	jpeg_set_qual(sd->jpeg_hdr, sd->quality);

	/* work on alternate 1 */
	usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);

	set_par(gspca_dev, 0x10000000);
	set_par(gspca_dev, 0x00000000);
	set_par(gspca_dev, 0x8002e001);
	set_par(gspca_dev, 0x14000000);
	if (gspca_dev->width > 320)
		value = 0x8002e001;		/* 640x480 */
	else
		value = 0x4001f000;		/* 320x240 */
	set_par(gspca_dev, value);
	ret = usb_set_interface(gspca_dev->dev,
					gspca_dev->iface,
					gspca_dev->alt);
	if (ret < 0) {
		PDEBUG(D_ERR|D_STREAM, "set intf %d %d failed",
			gspca_dev->iface, gspca_dev->alt);
		goto out;
	}
	ret = reg_r(gspca_dev, 0x0630);
	if (ret < 0)
		goto out;
	rcv_val(gspca_dev, 0x000020);	/* << (value ff ff ff ff) */
	ret = reg_r(gspca_dev, 0x0650);
	if (ret < 0)
		goto out;
	snd_val(gspca_dev, 0x000020, 0xffffffff);
	reg_w(gspca_dev, 0x0620, 0);
	reg_w(gspca_dev, 0x0630, 0);
	reg_w(gspca_dev, 0x0640, 0);
	reg_w(gspca_dev, 0x0650, 0);
	reg_w(gspca_dev, 0x0660, 0);
	setbrightness(gspca_dev);		/* whiteness */
	setcontrast(gspca_dev);			/* contrast */
	setcolors(gspca_dev);			/* saturation */
	set_par(gspca_dev, 0x09800000);		/* Red ? */
	set_par(gspca_dev, 0x0a800000);		/* Green ? */
	set_par(gspca_dev, 0x0b800000);		/* Blue ? */
	set_par(gspca_dev, 0x0d030000);		/* Gamma ? */
	setfreq(gspca_dev);			/* light frequency */

	/* start the video flow */
	set_par(gspca_dev, 0x01000000);
	set_par(gspca_dev, 0x01000000);
	PDEBUG(D_STREAM, "camera started alt: 0x%02x", gspca_dev->alt);
	return 0;
out:
	PDEBUG(D_ERR|D_STREAM, "camera start err %d", ret);
	return ret;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;

	set_par(gspca_dev, 0x02000000);
	set_par(gspca_dev, 0x02000000);
	usb_set_interface(dev, gspca_dev->iface, 1);
	reg_r(gspca_dev, 0x0630);
	rcv_val(gspca_dev, 0x000020);	/* << (value ff ff ff ff) */
	reg_r(gspca_dev, 0x0650);
	snd_val(gspca_dev, 0x000020, 0xffffffff);
	reg_w(gspca_dev, 0x0620, 0);
	reg_w(gspca_dev, 0x0630, 0);
	reg_w(gspca_dev, 0x0640, 0);
	reg_w(gspca_dev, 0x0650, 0);
	reg_w(gspca_dev, 0x0660, 0);
	PDEBUG(D_STREAM, "camera stopped");
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	kfree(sd->jpeg_hdr);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	static unsigned char ffd9[] = {0xff, 0xd9};

	/* a frame starts with:
	 *	- 0xff 0xfe
	 *	- 0x08 0x00	- length (little endian ?!)
	 *	- 4 bytes = size of whole frame (BE - including header)
	 *	- 0x00 0x0c
	 *	- 0xff 0xd8
	 *	- ..	JPEG image with escape sequences (ff 00)
	 *		(without ending - ff d9)
	 */
	if (data[0] == 0xff && data[1] == 0xfe) {
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					ffd9, 2);

		/* put the JPEG 411 header */
		gspca_frame_add(gspca_dev, FIRST_PACKET, frame,
			sd->jpeg_hdr, JPEG_HDR_SZ);

		/* beginning of the frame */
#define STKHDRSZ 12
		data += STKHDRSZ;
		len -= STKHDRSZ;
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		setbrightness(gspca_dev);
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->brightness;
	return 0;
}

static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming)
		setcontrast(gspca_dev);
	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->contrast;
	return 0;
}

static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->colors = val;
	if (gspca_dev->streaming)
		setcolors(gspca_dev);
	return 0;
}

static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->colors;
	return 0;
}

static int sd_setfreq(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->lightfreq = val;
	if (gspca_dev->streaming)
		setfreq(gspca_dev);
	return 0;
}

static int sd_getfreq(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->lightfreq;
	return 0;
}

static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu)
{
	switch (menu->id) {
	case V4L2_CID_POWER_LINE_FREQUENCY:
		switch (menu->index) {
		case 1:		/* V4L2_CID_POWER_LINE_FREQUENCY_50HZ */
			strcpy((char *) menu->name, "50 Hz");
			return 0;
		case 2:		/* V4L2_CID_POWER_LINE_FREQUENCY_60HZ */
			strcpy((char *) menu->name, "60 Hz");
			return 0;
		}
		break;
	}
	return -EINVAL;
}

static int sd_set_jcomp(struct gspca_dev *gspca_dev,
			struct v4l2_jpegcompression *jcomp)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (jcomp->quality < QUALITY_MIN)
		sd->quality = QUALITY_MIN;
	else if (jcomp->quality > QUALITY_MAX)
		sd->quality = QUALITY_MAX;
	else
		sd->quality = jcomp->quality;
	if (gspca_dev->streaming)
		jpeg_set_qual(sd->jpeg_hdr, sd->quality);
	return 0;
}

static int sd_get_jcomp(struct gspca_dev *gspca_dev,
			struct v4l2_jpegcompression *jcomp)
{
	struct sd *sd = (struct sd *) gspca_dev;

	memset(jcomp, 0, sizeof *jcomp);
	jcomp->quality = sd->quality;
	jcomp->jpeg_markers = V4L2_JPEG_MARKER_DHT
			| V4L2_JPEG_MARKER_DQT;
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
	.querymenu = sd_querymenu,
	.get_jcomp = sd_get_jcomp,
	.set_jcomp = sd_set_jcomp,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x05e1, 0x0893)},
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
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	int ret;
	ret = usb_register(&sd_driver);
	if (ret < 0)
		return ret;
	info("registered");
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	info("deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
