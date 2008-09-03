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

#define MODULE_NAME "mars"

#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/Mars USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	char qindex;
};

/* V4L2 controls supported by the driver */
static struct ctrl sd_ctrls[] = {
};

static struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 589,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
};

/* MI Register table //elvis */
enum {
	REG_HW_MI_0,
	REG_HW_MI_1,
	REG_HW_MI_2,
	REG_HW_MI_3,
	REG_HW_MI_4,
	REG_HW_MI_5,
	REG_HW_MI_6,
	REG_HW_MI_7,
	REG_HW_MI_9 = 0x09,
	REG_HW_MI_B = 0x0B,
	REG_HW_MI_C,
	REG_HW_MI_D,
	REG_HW_MI_1E = 0x1E,
	REG_HW_MI_20 = 0x20,
	REG_HW_MI_2B = 0x2B,
	REG_HW_MI_2C,
	REG_HW_MI_2D,
	REG_HW_MI_2E,
	REG_HW_MI_35 = 0x35,
	REG_HW_MI_5F = 0x5f,
	REG_HW_MI_60,
	REG_HW_MI_61,
	REG_HW_MI_62,
	REG_HW_MI_63,
	REG_HW_MI_64,
	REG_HW_MI_F1 = 0xf1,
	ATTR_TOTAL_MI_REG = 0xf2
};

/* the bytes to write are in gspca_dev->usb_buf */
static int reg_w(struct gspca_dev *gspca_dev,
		 __u16 index, int len)
{
	int rc;

	rc = usb_control_msg(gspca_dev->dev,
			 usb_sndbulkpipe(gspca_dev->dev, 4),
			 0x12,
			 0xc8,		/* ?? */
			 0,		/* value */
			 index, gspca_dev->usb_buf, len, 500);
	if (rc < 0)
		PDEBUG(D_ERR, "reg write [%02x] error %d", index, rc);
	return rc;
}

static int reg_w_buf(struct gspca_dev *gspca_dev,
			__u16 index, __u8 *buf, int len)
{
	int rc;

	rc = usb_control_msg(gspca_dev->dev,
			 usb_sndbulkpipe(gspca_dev->dev, 4),
			 0x12,
			 0xc8,		/* ?? */
			 0,		/* value */
			 index, buf, len, 500);
	if (rc < 0)
		PDEBUG(D_ERR, "reg write [%02x] error %d", index, rc);
	return rc;
}

static void bulk_w(struct gspca_dev *gspca_dev,
		   __u16 *pch,
		   __u16 Address)
{
	gspca_dev->usb_buf[0] = 0x1f;
	gspca_dev->usb_buf[1] = 0;			/* control byte */
	gspca_dev->usb_buf[2] = Address;
	gspca_dev->usb_buf[3] = *pch >> 8;		/* high byte */
	gspca_dev->usb_buf[4] = *pch;			/* low byte */

	reg_w(gspca_dev, Address, 5);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->epaddr = 0x01;
	cam->cam_mode = vga_mode;
	cam->nmodes = sizeof vga_mode / sizeof vga_mode[0];
	sd->qindex = 1;			/* set the quantization table */
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

static void sd_start(struct gspca_dev *gspca_dev)
{
	int err_code;
	__u8 *data;
	__u16 *MI_buf;
	int h_size, v_size;
	int intpipe;

	PDEBUG(D_STREAM, "camera start, iface %d, alt 8", gspca_dev->iface);
	if (usb_set_interface(gspca_dev->dev, gspca_dev->iface, 8) < 0) {
		PDEBUG(D_ERR|D_STREAM, "Set packet size: set interface error");
		return;
	}

	data = gspca_dev->usb_buf;
	data[0] = 0x01;		/* address */
	data[1] = 0x01;

	err_code = reg_w(gspca_dev, data[0], 2);
	if (err_code < 0)
		return;

	/*
	   Initialize the MR97113 chip register
	 */
	data = kmalloc(16, GFP_KERNEL);
	data[0] = 0x00;		/* address */
	data[1] = 0x0c | 0x01;	/* reg 0 */
	data[2] = 0x01;		/* reg 1 */
	h_size = gspca_dev->width;
	v_size = gspca_dev->height;
	data[3] = h_size / 8;	/* h_size , reg 2 */
	data[4] = v_size / 8;	/* v_size , reg 3 */
	data[5] = 0x30;		/* reg 4, MI, PAS5101 :
				 *	0x30 for 24mhz , 0x28 for 12mhz */
	data[6] = 4;		/* reg 5, H start */
	data[7] = 0xc0;		/* reg 6, gamma 1.5 */
	data[8] = 3;		/* reg 7, V start */
/*	if (h_size == 320 ) */
/*		data[9]= 0x56;	 * reg 8, 24MHz, 2:1 scale down */
/*	else */
	data[9] = 0x52;		/* reg 8, 24MHz, no scale down */
	data[10] = 0x5d;	/* reg 9, I2C device address
				 *	[for PAS5101 (0x40)] [for MI (0x5d)] */

	err_code = reg_w_buf(gspca_dev, data[0], data, 11);
	kfree(data);
	if (err_code < 0)
		return;

	data = gspca_dev->usb_buf;
	data[0] = 0x23;		/* address */
	data[1] = 0x09;		/* reg 35, append frame header */

	err_code = reg_w(gspca_dev, data[0], 2);
	if (err_code < 0)
		return;

	data[0] = 0x3c;		/* address */
/*	if (gspca_dev->width == 1280) */
/*		data[1] = 200;	 * reg 60, pc-cam frame size
				 *	(unit: 4KB) 800KB */
/*	else */
	data[1] = 50;		/* 50 reg 60, pc-cam frame size
				 *	(unit: 4KB) 200KB */
	err_code = reg_w(gspca_dev, data[0], 2);
	if (err_code < 0)
		return;

	if (0) {			/* fixed dark-gain */
		data[1] = 0;		/* reg 94, Y Gain (1.75) */
		data[2] = 0;		/* reg 95, UV Gain (1.75) */
		data[3] = 0x3f;		/* reg 96, Y Gain/UV Gain/disable
					 *	auto dark-gain */
		data[4] = 0;		/* reg 97, set fixed dark level */
		data[5] = 0;		/* reg 98, don't care */
	} else {			/* auto dark-gain */
		data[1] = 0;		/* reg 94, Y Gain (auto) */
		data[2] = 0;		/* reg 95, UV Gain (1.75) */
		data[3] = 0x78;		/* reg 96, Y Gain/UV Gain/disable
					 *	auto dark-gain */
		switch (gspca_dev->width) {
/*		case 1280: */
/*			data[4] = 154;
				 * reg 97, %3 shadow point (unit: 256 pixel) */
/*			data[5] = 51;
				 * reg 98, %1 highlight point
				 *	(uint: 256 pixel) */
/*			break; */
		default:
/*		case 640: */
			data[4] = 36;	/* reg 97, %3 shadow point
					 *	(unit: 256 pixel) */
			data[5] = 12;	/* reg 98, %1 highlight point
					 *	(uint: 256 pixel) */
			break;
		case 320:
			data[4] = 9;	/* reg 97, %3 shadow point
					 *	(unit: 256 pixel) */
			data[5] = 3;	/* reg 98, %1 highlight point
					 *	(uint: 256 pixel) */
			break;
		}
	}
	/* auto dark-gain */
	data[0] = 0x5e;		/* address */

	err_code = reg_w(gspca_dev, data[0], 6);
	if (err_code < 0)
		return;

	data[0] = 0x67;
	data[1] = 0x13;		/* reg 103, first pixel B, disable sharpness */
	err_code = reg_w(gspca_dev, data[0], 2);
	if (err_code < 0)
		return;

	/*
	 * initialize the value of MI sensor...
	 */
	MI_buf = kzalloc(ATTR_TOTAL_MI_REG * sizeof *MI_buf, GFP_KERNEL);
	MI_buf[REG_HW_MI_1] = 0x000a;
	MI_buf[REG_HW_MI_2] = 0x000c;
	MI_buf[REG_HW_MI_3] = 0x0405;
	MI_buf[REG_HW_MI_4] = 0x0507;
	/* mi_Attr_Reg_[REG_HW_MI_5]	 = 0x01ff;//13 */
	MI_buf[REG_HW_MI_5] = 0x0013;	/* 13 */
	MI_buf[REG_HW_MI_6] = 0x001f;	/* vertical blanking */
	/* mi_Attr_Reg_[REG_HW_MI_6]	 = 0x0400;  // vertical blanking */
	MI_buf[REG_HW_MI_7] = 0x0002;
	/* mi_Attr_Reg_[REG_HW_MI_9]	 = 0x015f; */
	/* mi_Attr_Reg_[REG_HW_MI_9]	 = 0x030f; */
	MI_buf[REG_HW_MI_9] = 0x0374;
	MI_buf[REG_HW_MI_B] = 0x0000;
	MI_buf[REG_HW_MI_C] = 0x0000;
	MI_buf[REG_HW_MI_D] = 0x0000;
	MI_buf[REG_HW_MI_1E] = 0x8000;
/* mi_Attr_Reg_[REG_HW_MI_20]	  = 0x1104; */
	MI_buf[REG_HW_MI_20] = 0x1104;	/* 0x111c; */
	MI_buf[REG_HW_MI_2B] = 0x0008;
/* mi_Attr_Reg_[REG_HW_MI_2C]	  = 0x000f; */
	MI_buf[REG_HW_MI_2C] = 0x001f;	/* lita suggest */
	MI_buf[REG_HW_MI_2D] = 0x0008;
	MI_buf[REG_HW_MI_2E] = 0x0008;
	MI_buf[REG_HW_MI_35] = 0x0051;
	MI_buf[REG_HW_MI_5F] = 0x0904;	/* fail to write */
	MI_buf[REG_HW_MI_60] = 0x0000;
	MI_buf[REG_HW_MI_61] = 0x0000;
	MI_buf[REG_HW_MI_62] = 0x0498;
	MI_buf[REG_HW_MI_63] = 0x0000;
	MI_buf[REG_HW_MI_64] = 0x0000;
	MI_buf[REG_HW_MI_F1] = 0x0001;
	/* changing while setting up the different value of dx/dy */

	if (gspca_dev->width != 1280) {
		MI_buf[0x01] = 0x010a;
		MI_buf[0x02] = 0x014c;
		MI_buf[0x03] = 0x01e5;
		MI_buf[0x04] = 0x0287;
	}
	MI_buf[0x20] = 0x1104;

	bulk_w(gspca_dev, MI_buf + 1, 1);
	bulk_w(gspca_dev, MI_buf + 2, 2);
	bulk_w(gspca_dev, MI_buf + 3, 3);
	bulk_w(gspca_dev, MI_buf + 4, 4);
	bulk_w(gspca_dev, MI_buf + 5, 5);
	bulk_w(gspca_dev, MI_buf + 6, 6);
	bulk_w(gspca_dev, MI_buf + 7, 7);
	bulk_w(gspca_dev, MI_buf + 9, 9);
	bulk_w(gspca_dev, MI_buf + 0x0b, 0x0b);
	bulk_w(gspca_dev, MI_buf + 0x0c, 0x0c);
	bulk_w(gspca_dev, MI_buf + 0x0d, 0x0d);
	bulk_w(gspca_dev, MI_buf + 0x1e, 0x1e);
	bulk_w(gspca_dev, MI_buf + 0x20, 0x20);
	bulk_w(gspca_dev, MI_buf + 0x2b, 0x2b);
	bulk_w(gspca_dev, MI_buf + 0x2c, 0x2c);
	bulk_w(gspca_dev, MI_buf + 0x2d, 0x2d);
	bulk_w(gspca_dev, MI_buf + 0x2e, 0x2e);
	bulk_w(gspca_dev, MI_buf + 0x35, 0x35);
	bulk_w(gspca_dev, MI_buf + 0x5f, 0x5f);
	bulk_w(gspca_dev, MI_buf + 0x60, 0x60);
	bulk_w(gspca_dev, MI_buf + 0x61, 0x61);
	bulk_w(gspca_dev, MI_buf + 0x62, 0x62);
	bulk_w(gspca_dev, MI_buf + 0x63, 0x63);
	bulk_w(gspca_dev, MI_buf + 0x64, 0x64);
	bulk_w(gspca_dev, MI_buf + 0xf1, 0xf1);
	kfree(MI_buf);

	intpipe = usb_sndintpipe(gspca_dev->dev, 0);
	err_code = usb_clear_halt(gspca_dev->dev, intpipe);

	data[0] = 0x00;
	data[1] = 0x4d;		/* ISOC transfering enable... */
	reg_w(gspca_dev, data[0], 2);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	int result;

	gspca_dev->usb_buf[0] = 1;
	gspca_dev->usb_buf[1] = 0;
	result = reg_w(gspca_dev, gspca_dev->usb_buf[0], 2);
	if (result < 0)
		PDEBUG(D_ERR, "Camera Stop failed");
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
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
				PDEBUG(D_PACK, "sof offset: %d leng: %d",
					p, len);
				frame = gspca_frame_add(gspca_dev, LAST_PACKET,
							frame, data, 0);

				/* put the JPEG header */
				jpeg_put_header(gspca_dev, frame,
						sd->qindex, 0x21);
				data += 16;
				len -= 16;
				break;
			}
		}
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
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
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
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
