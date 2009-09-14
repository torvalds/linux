/*
 * Mars MR97310A library
 *
 * Copyright (C) 2009 Kyle Guinn <elyk03@gmail.com>
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

#define MODULE_NAME "mr97310a"

#include "gspca.h"

MODULE_AUTHOR("Kyle Guinn <elyk03@gmail.com>");
MODULE_DESCRIPTION("GSPCA/Mars-Semi MR97310A USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;  /* !! must be the first item */
	u8 sof_read;
};

/* V4L2 controls supported by the driver */
static struct ctrl sd_ctrls[] = {
};

static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 4},
	{176, 144, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3},
	{320, 240, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/* the bytes to write are in gspca_dev->usb_buf */
static int reg_w(struct gspca_dev *gspca_dev, int len)
{
	int rc;

	rc = usb_bulk_msg(gspca_dev->dev,
			  usb_sndbulkpipe(gspca_dev->dev, 4),
			  gspca_dev->usb_buf, len, NULL, 500);
	if (rc < 0)
		PDEBUG(D_ERR, "reg write [%02x] error %d",
		       gspca_dev->usb_buf[0], rc);
	return rc;
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
	__u8 *data = gspca_dev->usb_buf;
	int err_code;

	sd->sof_read = 0;

	/* Note:  register descriptions guessed from MR97113A driver */

	data[0] = 0x01;
	data[1] = 0x01;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x00;
	data[1] = 0x0d;
	data[2] = 0x01;
	data[5] = 0x2b;
	data[7] = 0x00;
	data[9] = 0x50;  /* reg 8, no scale down */
	data[10] = 0xc0;

	switch (gspca_dev->width) {
	case 160:
		data[9] |= 0x0c;  /* reg 8, 4:1 scale down */
		/* fall thru */
	case 320:
		data[9] |= 0x04;  /* reg 8, 2:1 scale down */
		/* fall thru */
	case 640:
	default:
		data[3] = 0x50;  /* reg 2, H size */
		data[4] = 0x78;  /* reg 3, V size */
		data[6] = 0x04;  /* reg 5, H start */
		data[8] = 0x03;  /* reg 7, V start */
		break;

	case 176:
		data[9] |= 0x04;  /* reg 8, 2:1 scale down */
		/* fall thru */
	case 352:
		data[3] = 0x2c;  /* reg 2, H size */
		data[4] = 0x48;  /* reg 3, V size */
		data[6] = 0x94;  /* reg 5, H start */
		data[8] = 0x63;  /* reg 7, V start */
		break;
	}

	err_code = reg_w(gspca_dev, 11);
	if (err_code < 0)
		return err_code;

	data[0] = 0x0a;
	data[1] = 0x80;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x14;
	data[1] = 0x0a;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1b;
	data[1] = 0x00;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x15;
	data[1] = 0x16;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x16;
	data[1] = 0x10;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x17;
	data[1] = 0x3a;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x18;
	data[1] = 0x68;
	err_code = reg_w(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1f;
	data[1] = 0x00;
	data[2] = 0x02;
	data[3] = 0x06;
	data[4] = 0x59;
	data[5] = 0x0c;
	data[6] = 0x16;
	data[7] = 0x00;
	data[8] = 0x07;
	data[9] = 0x00;
	data[10] = 0x01;
	err_code = reg_w(gspca_dev, 11);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1f;
	data[1] = 0x04;
	data[2] = 0x11;
	data[3] = 0x01;
	err_code = reg_w(gspca_dev, 4);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1f;
	data[1] = 0x00;
	data[2] = 0x0a;
	data[3] = 0x00;
	data[4] = 0x01;
	data[5] = 0x00;
	data[6] = 0x00;
	data[7] = 0x01;
	data[8] = 0x00;
	data[9] = 0x0a;
	err_code = reg_w(gspca_dev, 10);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1f;
	data[1] = 0x04;
	data[2] = 0x11;
	data[3] = 0x01;
	err_code = reg_w(gspca_dev, 4);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1f;
	data[1] = 0x00;
	data[2] = 0x12;
	data[3] = 0x00;
	data[4] = 0x63;
	data[5] = 0x00;
	data[6] = 0x70;
	data[7] = 0x00;
	data[8] = 0x00;
	err_code = reg_w(gspca_dev, 9);
	if (err_code < 0)
		return err_code;

	data[0] = 0x1f;
	data[1] = 0x04;
	data[2] = 0x11;
	data[3] = 0x01;
	err_code = reg_w(gspca_dev, 4);
	if (err_code < 0)
		return err_code;

	data[0] = 0x00;
	data[1] = 0x4d;  /* ISOC transfering enable... */
	err_code = reg_w(gspca_dev, 2);
	return err_code;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	int result;

	gspca_dev->usb_buf[0] = 1;
	gspca_dev->usb_buf[1] = 0;
	result = reg_w(gspca_dev, 2);
	if (result < 0)
		PDEBUG(D_ERR, "Camera Stop failed");
}

/* Include pac common sof detection functions */
#include "pac_common.h"

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,    /* target */
			__u8 *data,                   /* isoc packet */
			int len)                      /* iso packet length */
{
	unsigned char *sof;

	sof = pac_find_sof(gspca_dev, data, len);
	if (sof) {
		int n;

		/* finish decoding current frame */
		n = sof - data;
		if (n > sizeof pac_sof_marker)
			n -= sizeof pac_sof_marker;
		else
			n = 0;
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					data, n);
		/* Start next frame. */
		gspca_frame_add(gspca_dev, FIRST_PACKET, frame,
			pac_sof_marker, sizeof pac_sof_marker);
		len -= sof - data;
		data = sof;
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
	{USB_DEVICE(0x08ca, 0x0111)},
	{USB_DEVICE(0x093a, 0x010f)},
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
