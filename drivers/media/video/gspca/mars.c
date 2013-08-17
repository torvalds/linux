/*
 *		Mars-Semi MR97311A library
 *		Copyright (C) 2005 <bradlch@hotmail.com>
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "mars"

#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/Mars USB Camera Driver");
MODULE_LICENSE("GPL");

/* controls */
enum e_ctrl {
	BRIGHTNESS,
	COLORS,
	GAMMA,
	SHARPNESS,
	ILLUM_TOP,
	ILLUM_BOT,
	NCTRLS		/* number of controls */
};

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	struct gspca_ctrl ctrls[NCTRLS];

	u8 quality;
#define QUALITY_MIN 40
#define QUALITY_MAX 70
#define QUALITY_DEF 50

	u8 jpeg_hdr[JPEG_HDR_SZ];
};

/* V4L2 controls supported by the driver */
static void setbrightness(struct gspca_dev *gspca_dev);
static void setcolors(struct gspca_dev *gspca_dev);
static void setgamma(struct gspca_dev *gspca_dev);
static void setsharpness(struct gspca_dev *gspca_dev);
static int sd_setilluminator1(struct gspca_dev *gspca_dev, __s32 val);
static int sd_setilluminator2(struct gspca_dev *gspca_dev, __s32 val);

static const struct ctrl sd_ctrls[NCTRLS] = {
[BRIGHTNESS] = {
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 30,
		.step    = 1,
		.default_value = 15,
	    },
	    .set_control = setbrightness
	},
[COLORS] = {
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Color",
		.minimum = 1,
		.maximum = 255,
		.step    = 1,
		.default_value = 200,
	    },
	    .set_control = setcolors
	},
[GAMMA] = {
	    {
		.id      = V4L2_CID_GAMMA,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Gamma",
		.minimum = 0,
		.maximum = 3,
		.step    = 1,
		.default_value = 1,
	    },
	    .set_control = setgamma
	},
[SHARPNESS] = {
	    {
		.id	 = V4L2_CID_SHARPNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Sharpness",
		.minimum = 0,
		.maximum = 2,
		.step    = 1,
		.default_value = 1,
	    },
	    .set_control = setsharpness
	},
[ILLUM_TOP] = {
	    {
		.id	 = V4L2_CID_ILLUMINATORS_1,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Top illuminator",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_UPDATE,
	    },
	    .set = sd_setilluminator1
	},
[ILLUM_BOT] = {
	    {
		.id	 = V4L2_CID_ILLUMINATORS_2,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Bottom illuminator",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_UPDATE,
	    },
	    .set = sd_setilluminator2
	},
};

static const struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
};

static const __u8 mi_data[0x20] = {
/*	 01    02   03     04    05    06    07    08 */
	0x48, 0x22, 0x01, 0x47, 0x10, 0x00, 0x00, 0x00,
/*	 09    0a   0b     0c    0d    0e    0f    10 */
	0x00, 0x01, 0x30, 0x01, 0x30, 0x01, 0x30, 0x01,
/*	 11    12   13     14    15    16    17    18 */
	0x30, 0x00, 0x04, 0x00, 0x06, 0x01, 0xe2, 0x02,
/*	 19    1a   1b     1c    1d    1e    1f    20 */
	0x82, 0x00, 0x20, 0x17, 0x80, 0x08, 0x0c, 0x00
};

/* write <len> bytes from gspca_dev->usb_buf */
static void reg_w(struct gspca_dev *gspca_dev,
		 int len)
{
	int alen, ret;

	if (gspca_dev->usb_err < 0)
		return;

	ret = usb_bulk_msg(gspca_dev->dev,
			usb_sndbulkpipe(gspca_dev->dev, 4),
			gspca_dev->usb_buf,
			len,
			&alen,
			500);	/* timeout in milliseconds */
	if (ret < 0) {
		pr_err("reg write [%02x] error %d\n",
		       gspca_dev->usb_buf[0], ret);
		gspca_dev->usb_err = ret;
	}
}

static void mi_w(struct gspca_dev *gspca_dev,
		 u8 addr,
		 u8 value)
{
	gspca_dev->usb_buf[0] = 0x1f;
	gspca_dev->usb_buf[1] = 0;			/* control byte */
	gspca_dev->usb_buf[2] = addr;
	gspca_dev->usb_buf[3] = value;

	reg_w(gspca_dev, 4);
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->usb_buf[0] = 0x61;
	gspca_dev->usb_buf[1] = sd->ctrls[BRIGHTNESS].val;
	reg_w(gspca_dev, 2);
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s16 val;

	val = sd->ctrls[COLORS].val;
	gspca_dev->usb_buf[0] = 0x5f;
	gspca_dev->usb_buf[1] = val << 3;
	gspca_dev->usb_buf[2] = ((val >> 2) & 0xf8) | 0x04;
	reg_w(gspca_dev, 3);
}

static void setgamma(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->usb_buf[0] = 0x06;
	gspca_dev->usb_buf[1] = sd->ctrls[GAMMA].val * 0x40;
	reg_w(gspca_dev, 2);
}

static void setsharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->usb_buf[0] = 0x67;
	gspca_dev->usb_buf[1] = sd->ctrls[SHARPNESS].val * 4 + 3;
	reg_w(gspca_dev, 2);
}

static void setilluminators(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->usb_buf[0] = 0x22;
	if (sd->ctrls[ILLUM_TOP].val)
		gspca_dev->usb_buf[1] = 0x76;
	else if (sd->ctrls[ILLUM_BOT].val)
		gspca_dev->usb_buf[1] = 0x7a;
	else
		gspca_dev->usb_buf[1] = 0x7e;
	reg_w(gspca_dev, 2);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);
	cam->ctrls = sd->ctrls;
	sd->quality = QUALITY_DEF;
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	gspca_dev->ctrl_inac = (1 << ILLUM_TOP) | (1 << ILLUM_BOT);
	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 *data;
	int i;

	/* create the JPEG header */
	jpeg_define(sd->jpeg_hdr, gspca_dev->height, gspca_dev->width,
			0x21);		/* JPEG 422 */
	jpeg_set_qual(sd->jpeg_hdr, sd->quality);

	data = gspca_dev->usb_buf;

	data[0] = 0x01;		/* address */
	data[1] = 0x01;
	reg_w(gspca_dev, 2);

	/*
	   Initialize the MR97113 chip register
	 */
	data[0] = 0x00;		/* address */
	data[1] = 0x0c | 0x01;	/* reg 0 */
	data[2] = 0x01;		/* reg 1 */
	data[3] = gspca_dev->width / 8;		/* h_size , reg 2 */
	data[4] = gspca_dev->height / 8;	/* v_size , reg 3 */
	data[5] = 0x30;		/* reg 4, MI, PAS5101 :
				 *	0x30 for 24mhz , 0x28 for 12mhz */
	data[6] = 0x02;		/* reg 5, H start - was 0x04 */
	data[7] = sd->ctrls[GAMMA].val * 0x40;	/* reg 0x06: gamma */
	data[8] = 0x01;		/* reg 7, V start - was 0x03 */
/*	if (h_size == 320 ) */
/*		data[9]= 0x56;	 * reg 8, 24MHz, 2:1 scale down */
/*	else */
	data[9] = 0x52;		/* reg 8, 24MHz, no scale down */
/*jfm: from win trace*/
	data[10] = 0x18;

	reg_w(gspca_dev, 11);

	data[0] = 0x23;		/* address */
	data[1] = 0x09;		/* reg 35, append frame header */

	reg_w(gspca_dev, 2);

	data[0] = 0x3c;		/* address */
/*	if (gspca_dev->width == 1280) */
/*		data[1] = 200;	 * reg 60, pc-cam frame size
				 *	(unit: 4KB) 800KB */
/*	else */
	data[1] = 50;		/* 50 reg 60, pc-cam frame size
				 *	(unit: 4KB) 200KB */
	reg_w(gspca_dev, 2);

	/* auto dark-gain */
	data[0] = 0x5e;		/* address */
	data[1] = 0;		/* reg 94, Y Gain (auto) */
/*jfm: from win trace*/
				/* reg 0x5f/0x60 (LE) = saturation */
				/* h (60): xxxx x100
				 * l (5f): xxxx x000 */
	data[2] = sd->ctrls[COLORS].val << 3;
	data[3] = ((sd->ctrls[COLORS].val >> 2) & 0xf8) | 0x04;
	data[4] = sd->ctrls[BRIGHTNESS].val; /* reg 0x61 = brightness */
	data[5] = 0x00;

	reg_w(gspca_dev, 6);

	data[0] = 0x67;
/*jfm: from win trace*/
	data[1] = sd->ctrls[SHARPNESS].val * 4 + 3;
	data[2] = 0x14;
	reg_w(gspca_dev, 3);

	data[0] = 0x69;
	data[1] = 0x2f;
	data[2] = 0x28;
	data[3] = 0x42;
	reg_w(gspca_dev, 4);

	data[0] = 0x63;
	data[1] = 0x07;
	reg_w(gspca_dev, 2);
/*jfm: win trace - many writes here to reg 0x64*/

	/* initialize the MI sensor */
	for (i = 0; i < sizeof mi_data; i++)
		mi_w(gspca_dev, i + 1, mi_data[i]);

	data[0] = 0x00;
	data[1] = 0x4d;		/* ISOC transferring enable... */
	reg_w(gspca_dev, 2);

	gspca_dev->ctrl_inac = 0; /* activate the illuminator controls */
	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->ctrl_inac = (1 << ILLUM_TOP) | (1 << ILLUM_BOT);
	if (sd->ctrls[ILLUM_TOP].val || sd->ctrls[ILLUM_BOT].val) {
		sd->ctrls[ILLUM_TOP].val = 0;
		sd->ctrls[ILLUM_BOT].val = 0;
		setilluminators(gspca_dev);
		msleep(20);
	}

	gspca_dev->usb_buf[0] = 1;
	gspca_dev->usb_buf[1] = 0;
	reg_w(gspca_dev, 2);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int p;

	if (len < 6) {
/*		gspca_dev->last_packet_type = DISCARD_PACKET; */
		return;
	}
	for (p = 0; p < len - 6; p++) {
		if (data[0 + p] == 0xff
		    && data[1 + p] == 0xff
		    && data[2 + p] == 0x00
		    && data[3 + p] == 0xff
		    && data[4 + p] == 0x96) {
			if (data[5 + p] == 0x64
			    || data[5 + p] == 0x65
			    || data[5 + p] == 0x66
			    || data[5 + p] == 0x67) {
				PDEBUG(D_PACK, "sof offset: %d len: %d",
					p, len);
				gspca_frame_add(gspca_dev, LAST_PACKET,
						data, p);

				/* put the JPEG header */
				gspca_frame_add(gspca_dev, FIRST_PACKET,
					sd->jpeg_hdr, JPEG_HDR_SZ);
				data += p + 16;
				len -= p + 16;
				break;
			}
		}
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static int sd_setilluminator1(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* only one illuminator may be on */
	sd->ctrls[ILLUM_TOP].val = val;
	if (val)
		sd->ctrls[ILLUM_BOT].val = 0;
	setilluminators(gspca_dev);
	return gspca_dev->usb_err;
}

static int sd_setilluminator2(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* only one illuminator may be on */
	sd->ctrls[ILLUM_BOT].val = val;
	if (val)
		sd->ctrls[ILLUM_TOP].val = 0;
	setilluminators(gspca_dev);
	return gspca_dev->usb_err;
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
	.nctrls = NCTRLS,
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.get_jcomp = sd_get_jcomp,
	.set_jcomp = sd_set_jcomp,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x093a, 0x050f)},
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

module_usb_driver(sd_driver);
