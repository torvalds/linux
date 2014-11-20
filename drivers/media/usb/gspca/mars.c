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

#define QUALITY 50

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	struct v4l2_ctrl *brightness;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *sharpness;
	struct v4l2_ctrl *gamma;
	struct { /* illuminator control cluster */
		struct v4l2_ctrl *illum_top;
		struct v4l2_ctrl *illum_bottom;
	};
	u8 jpeg_hdr[JPEG_HDR_SZ];
};

/* V4L2 controls supported by the driver */
static void setbrightness(struct gspca_dev *gspca_dev, s32 val);
static void setcolors(struct gspca_dev *gspca_dev, s32 val);
static void setgamma(struct gspca_dev *gspca_dev, s32 val);
static void setsharpness(struct gspca_dev *gspca_dev, s32 val);

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

static void setbrightness(struct gspca_dev *gspca_dev, s32 val)
{
	gspca_dev->usb_buf[0] = 0x61;
	gspca_dev->usb_buf[1] = val;
	reg_w(gspca_dev, 2);
}

static void setcolors(struct gspca_dev *gspca_dev, s32 val)
{
	gspca_dev->usb_buf[0] = 0x5f;
	gspca_dev->usb_buf[1] = val << 3;
	gspca_dev->usb_buf[2] = ((val >> 2) & 0xf8) | 0x04;
	reg_w(gspca_dev, 3);
}

static void setgamma(struct gspca_dev *gspca_dev, s32 val)
{
	gspca_dev->usb_buf[0] = 0x06;
	gspca_dev->usb_buf[1] = val * 0x40;
	reg_w(gspca_dev, 2);
}

static void setsharpness(struct gspca_dev *gspca_dev, s32 val)
{
	gspca_dev->usb_buf[0] = 0x67;
	gspca_dev->usb_buf[1] = val * 4 + 3;
	reg_w(gspca_dev, 2);
}

static void setilluminators(struct gspca_dev *gspca_dev, bool top, bool bottom)
{
	/* both are off if not streaming */
	gspca_dev->usb_buf[0] = 0x22;
	if (top)
		gspca_dev->usb_buf[1] = 0x76;
	else if (bottom)
		gspca_dev->usb_buf[1] = 0x7a;
	else
		gspca_dev->usb_buf[1] = 0x7e;
	reg_w(gspca_dev, 2);
}

static int mars_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	if (ctrl->id == V4L2_CID_ILLUMINATORS_1) {
		/* only one can be on at a time */
		if (ctrl->is_new && ctrl->val)
			sd->illum_bottom->val = 0;
		if (sd->illum_bottom->is_new && sd->illum_bottom->val)
			sd->illum_top->val = 0;
	}

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		setbrightness(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		setcolors(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_GAMMA:
		setgamma(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_ILLUMINATORS_1:
		setilluminators(gspca_dev, sd->illum_top->val,
					   sd->illum_bottom->val);
		break;
	case V4L2_CID_SHARPNESS:
		setsharpness(gspca_dev, ctrl->val);
		break;
	default:
		return -EINVAL;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops mars_ctrl_ops = {
	.s_ctrl = mars_s_ctrl,
};

/* this function is called at probe time */
static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 6);
	sd->brightness = v4l2_ctrl_new_std(hdl, &mars_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 30, 1, 15);
	sd->saturation = v4l2_ctrl_new_std(hdl, &mars_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 200);
	sd->gamma = v4l2_ctrl_new_std(hdl, &mars_ctrl_ops,
			V4L2_CID_GAMMA, 0, 3, 1, 1);
	sd->sharpness = v4l2_ctrl_new_std(hdl, &mars_ctrl_ops,
			V4L2_CID_SHARPNESS, 0, 2, 1, 1);
	sd->illum_top = v4l2_ctrl_new_std(hdl, &mars_ctrl_ops,
			V4L2_CID_ILLUMINATORS_1, 0, 1, 1, 0);
	sd->illum_top->flags |= V4L2_CTRL_FLAG_UPDATE;
	sd->illum_bottom = v4l2_ctrl_new_std(hdl, &mars_ctrl_ops,
			V4L2_CID_ILLUMINATORS_2, 0, 1, 1, 0);
	sd->illum_bottom->flags |= V4L2_CTRL_FLAG_UPDATE;
	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}
	v4l2_ctrl_cluster(2, &sd->illum_top);
	return 0;
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 *data;
	int i;

	/* create the JPEG header */
	jpeg_define(sd->jpeg_hdr, gspca_dev->pixfmt.height,
			gspca_dev->pixfmt.width,
			0x21);		/* JPEG 422 */
	jpeg_set_qual(sd->jpeg_hdr, QUALITY);

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
	data[3] = gspca_dev->pixfmt.width / 8;	/* h_size , reg 2 */
	data[4] = gspca_dev->pixfmt.height / 8;	/* v_size , reg 3 */
	data[5] = 0x30;		/* reg 4, MI, PAS5101 :
				 *	0x30 for 24mhz , 0x28 for 12mhz */
	data[6] = 0x02;		/* reg 5, H start - was 0x04 */
	data[7] = v4l2_ctrl_g_ctrl(sd->gamma) * 0x40;	/* reg 0x06: gamma */
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
	data[2] = v4l2_ctrl_g_ctrl(sd->saturation) << 3;
	data[3] = ((v4l2_ctrl_g_ctrl(sd->saturation) >> 2) & 0xf8) | 0x04;
	data[4] = v4l2_ctrl_g_ctrl(sd->brightness); /* reg 0x61 = brightness */
	data[5] = 0x00;

	reg_w(gspca_dev, 6);

	data[0] = 0x67;
/*jfm: from win trace*/
	data[1] = v4l2_ctrl_g_ctrl(sd->sharpness) * 4 + 3;
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

	setilluminators(gspca_dev, v4l2_ctrl_g_ctrl(sd->illum_top),
				   v4l2_ctrl_g_ctrl(sd->illum_bottom));

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (v4l2_ctrl_g_ctrl(sd->illum_top) ||
	    v4l2_ctrl_g_ctrl(sd->illum_bottom)) {
		setilluminators(gspca_dev, false, false);
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

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
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
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
