/*
 * Syntek DV4000 (STK014) subdriver
 *
 * Copyright (C) Jean-Francois Moine (http://moinejf.free.fr)
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
 *
 */

#define MODULE_NAME "stk014"

#include "gspca.h"
#include "jpeg.h"

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(0, 1, 0)
static const char version[] = "0.1.0";

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("Syntek DV4000 (STK014) USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;
};

/* global parameters */
static int lightfreq = 50;
static int sd_quant = 7;		/* <= 4 KO - 7: good (enough!) */

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
		.default_value = 127,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
#define SD_CONTRAST 1
	{
	    {
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
		.default_value = 127,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
#define SD_COLOR 2
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Saturation",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
		.default_value = 127,
	    },
	    .set = sd_setcolors,
	    .get = sd_getcolors,
	},
};

static struct cam_mode vga_mode[] = {
	{V4L2_PIX_FMT_JPEG, 320, 240},
	{V4L2_PIX_FMT_JPEG, 640, 480},
};

/* -- read a register -- */
static int reg_read(struct gspca_dev *gspca_dev,
			__u16 index, __u8 *buf)
{
	int ret;
	struct usb_device *dev = gspca_dev->dev;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x00,
			index,
			buf, 1,
			500);
	if (ret < 0)
		PDEBUG(D_ERR, "reg_read err %d", ret);
	return ret;
}

/* -- write a register -- */
static int reg_write(struct gspca_dev *gspca_dev,
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
		PDEBUG(D_ERR, "reg_write err %d", ret);
	return ret;
}

/* -- get a value -- */
static int rcv_val(struct gspca_dev *gspca_dev,
			int ads,
			int len)
{
	struct usb_device *dev = gspca_dev->dev;
	int alen, ret;
	unsigned char bulk_buf[4];

	reg_write(gspca_dev, 0x634, (ads >> 16) & 0xff);
	reg_write(gspca_dev, 0x635, (ads >> 8) & 0xff);
	reg_write(gspca_dev, 0x636, ads & 0xff);
	reg_write(gspca_dev, 0x637, 0);
	reg_write(gspca_dev, 0x638, len & 0xff);
	reg_write(gspca_dev, 0x639, len >> 8);
	reg_write(gspca_dev, 0x63a, 0);
	reg_write(gspca_dev, 0x63b, 0);
	reg_write(gspca_dev, 0x630, 5);
	if (len > sizeof bulk_buf)
		return -1;
	ret = usb_bulk_msg(dev,
			usb_rcvbulkpipe(dev, 5),
			bulk_buf,
			len,
			&alen,
			500);	/* timeout in milliseconds */
	return ret;
}

/* -- send a value -- */
static int snd_val(struct gspca_dev *gspca_dev,
			int ads,
			unsigned int val)
{
	struct usb_device *dev = gspca_dev->dev;
	int alen, ret;
	__u8 value, seq;
	unsigned char bulk_buf[4];

	if (ads == 0x003f08) {
		ret = reg_read(gspca_dev, 0x0704, &value);
		if (ret < 0)
			goto ko;
		ret = reg_read(gspca_dev, 0x0705, &seq);
		if (ret < 0)
			goto ko;
		ret = reg_read(gspca_dev, 0x0650, &value);
		if (ret < 0)
			goto ko;
		reg_write(gspca_dev, 0x654, seq);
	} else
		reg_write(gspca_dev, 0x654, (ads >> 16) & 0xff);
	reg_write(gspca_dev, 0x655, (ads >> 8) & 0xff);
	reg_write(gspca_dev, 0x656, ads & 0xff);
	reg_write(gspca_dev, 0x657, 0);
	reg_write(gspca_dev, 0x658, 0x04);	/* size */
	reg_write(gspca_dev, 0x659, 0);
	reg_write(gspca_dev, 0x65a, 0);
	reg_write(gspca_dev, 0x65b, 0);
	reg_write(gspca_dev, 0x650, 5);
	bulk_buf[0] = (val >> 24) & 0xff;
	bulk_buf[1] = (val >> 16) & 0xff;
	bulk_buf[2] = (val >> 8) & 0xff;
	bulk_buf[3] = val & 0xff;
	ret = usb_bulk_msg(dev,
			usb_sndbulkpipe(dev, 6),
			bulk_buf,
			4,
			&alen,
			500);	/* timeout in milliseconds */
	if (ret < 0)
		goto ko;
	if (ads == 0x003f08) {
		seq += 4;
		seq &= 0x3f;
		reg_write(gspca_dev, 0x705, seq);
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

	PDEBUG(D_CONF, "brightness: %d", sd->brightness);
	parval = 0x06000000		/* whiteness */
		+ (sd->brightness << 16);
	set_par(gspca_dev, parval);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int parval;

	PDEBUG(D_CONF, "contrast: %d", sd->contrast);
	parval = 0x07000000		/* contrast */
		+ (sd->contrast << 16);
	set_par(gspca_dev, parval);
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int parval;

	PDEBUG(D_CONF, "saturation: %d",
		sd->colors);
	parval = 0x08000000		/* saturation */
		+ (sd->colors << 16);
	set_par(gspca_dev, parval);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam = &gspca_dev->cam;

	cam->dev_name = (char *) id->driver_info;
	cam->epaddr = 0x02;
	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = sizeof vga_mode / sizeof vga_mode[0];
	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	sd->colors = sd_ctrls[SD_COLOR].qctrl.default_value;
	return 0;
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	__u8 value;
	int ret;

	/* check if the device responds */
	usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);
	ret = reg_read(gspca_dev, 0x0740, &value);
	if (ret < 0)
		return ret;
	if (value != 0xff) {
		PDEBUG(D_ERR|D_STREAM, "init reg: 0x%02x", value);
		return -1;
	}
	return 0;
}

/* -- start the camera -- */
static void sd_start(struct gspca_dev *gspca_dev)
{
	__u8 dum;
	int ret, value;

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
	if (ret < 0)
		goto out;
	ret = reg_read(gspca_dev, 0x0630, &dum);
	if (ret < 0)
		goto out;
	rcv_val(gspca_dev, 0x000020, 4);	/* << (value ff ff ff ff) */
	ret = reg_read(gspca_dev, 0x0650, &dum);
	if (ret < 0)
		goto out;
	snd_val(gspca_dev, 0x000020, 0xffffffff);
	reg_write(gspca_dev, 0x0620, 0);
	reg_write(gspca_dev, 0x0630, 0);
	reg_write(gspca_dev, 0x0640, 0);
	reg_write(gspca_dev, 0x0650, 0);
	reg_write(gspca_dev, 0x0660, 0);
	setbrightness(gspca_dev);		/* whiteness */
	setcontrast(gspca_dev);			/* contrast */
	setcolors(gspca_dev);			/* saturation */
	set_par(gspca_dev, 0x09800000);		/* Red ? */
	set_par(gspca_dev, 0x0a800000);		/* Green ? */
	set_par(gspca_dev, 0x0b800000);		/* Blue ? */
	set_par(gspca_dev, 0x0d030000);		/* Gamma ? */
	set_par(gspca_dev, lightfreq == 60
			? 0x33780000		/* 60 Hz */
			: 0x33640000);		/* 50 Hz */

	/* start the video flow */
	set_par(gspca_dev, 0x01000000);
	set_par(gspca_dev, 0x01000000);
	PDEBUG(D_STREAM, "camera started alt: 0x%02x", gspca_dev->alt);
	return;
out:
	PDEBUG(D_ERR|D_STREAM, "camera start err %d", ret);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 value;

	set_par(gspca_dev, 0x02000000);
	set_par(gspca_dev, 0x02000000);
	usb_set_interface(dev, gspca_dev->iface, 1);
	reg_read(gspca_dev, 0x0630, &value);
	rcv_val(gspca_dev, 0x000020, 4);	/* << (value ff ff ff ff) */
	reg_read(gspca_dev, 0x0650, &value);
	snd_val(gspca_dev, 0x000020, 0xffffffff);
	reg_write(gspca_dev, 0x0620, 0);
	reg_write(gspca_dev, 0x0630, 0);
	reg_write(gspca_dev, 0x0640, 0);
	reg_write(gspca_dev, 0x0650, 0);
	reg_write(gspca_dev, 0x0660, 0);
	PDEBUG(D_STREAM, "camera stopped");
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
}

static void sd_close(struct gspca_dev *gspca_dev)
{
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			unsigned char *data,		/* isoc packet */
			int len)			/* iso packet length */
{
	int l;
	static unsigned char ffd9[] = {0xff, 0xd9};

	/* a frame starts with:
	 *	- 0xff 0xfe
	 *	- 0x08 0x00	// length (little endian ?!)
	 *	- 4 bytes = size of whole frame (big endian - including header)
	 *	- 0x00 0x0c
	 *	- 0xff 0xd8
	 *	- ..	JPEG image with escape sequences (ff 00)
	 */
	if (data[0] == 0xff && data[1] == 0xfe) {
		if (gspca_dev->last_packet_type == INTER_PACKET) {
			PDEBUG(D_ERR|D_FRAM, "sof actual l: %d init l: %d",
				frame->data_end - frame->data,
				frame->v4l2_buf.bytesused);
		}

		/* put the JPEG headaer */
		jpeg_put_header(gspca_dev, frame, sd_quant, 0x22);

		/* beginning of the frame */
#define STKHDRSZ 12
		l = (data[4] << 24)		/* frame size */
			+ (data[5] << 16)
			+ (data[6] << 8)
			+ data[7]
			- STKHDRSZ
			+ (frame->data_end - frame->data)
			+ 2;			/* EOF (ff d9) */
		gspca_frame_add(gspca_dev, INTER_PACKET, frame,
				data + STKHDRSZ, len - STKHDRSZ);
#undef STKHDRSZ
		frame->v4l2_buf.bytesused = l;
		return;
	}
	if (gspca_dev->last_packet_type != INTER_PACKET) {
		if (gspca_dev->last_packet_type == LAST_PACKET) {
			PDEBUG(D_ERR|D_PACK, "mof actual l: %d init l: %d",
				frame->data_end - frame->data,
				frame->v4l2_buf.bytesused);
		}
		return;
	}

	/* intermediate packet */
	l = frame->data_end - frame->data;
	if (len < frame->v4l2_buf.bytesused - 2 - l) {
		gspca_frame_add(gspca_dev, INTER_PACKET, frame,
				data, len);
		return;
	}

	/* last packet */
	if (len > frame->v4l2_buf.bytesused - 2 - l)
		len = frame->v4l2_buf.bytesused - 2 - l;
	gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
	gspca_frame_add(gspca_dev, LAST_PACKET, frame, ffd9, 2);
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

/* sub-driver description */
static struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = sizeof sd_ctrls / sizeof sd_ctrls[0],
	.config = sd_config,
	.open = sd_open,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.close = sd_close,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
#define DVNM(name) .driver_info = (kernel_ulong_t) name
static __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x05e1, 0x0893), DVNM("Syntek DV4000")},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	PDEBUG(D_PROBE, "camera probe");
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd));
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	if (usb_register(&sd_driver) < 0)
		return -1;
	info("v%s registered", version);
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	info("deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);

module_param(lightfreq, int, 0644);
MODULE_PARM_DESC(lightfreq, "Light frequency 50 or 60 Hz");
module_param_named(quant, sd_quant, int, 0644);
MODULE_PARM_DESC(quant, "Quantization index (0..8)");
