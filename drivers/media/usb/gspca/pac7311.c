/*
 *		Pixart PAC7311 library
 *		Copyright (C) 2005 Thomas Kaiser thomas@kaiser-linux.li
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

/* Some documentation about various registers as determined by trial and error.
 *
 * Register page 1:
 *
 * Address	Description
 * 0x08		Unknown compressor related, must always be 8 except when not
 *		in 640x480 resolution and page 4 reg 2 <= 3 then set it to 9 !
 * 0x1b		Auto white balance related, bit 0 is AWB enable (inverted)
 *		bits 345 seem to toggle per color gains on/off (inverted)
 * 0x78		Global control, bit 6 controls the LED (inverted)
 * 0x80		Compression balance, interesting settings:
 *		0x01 Use this to allow the camera to switch to higher compr.
 *		     on the fly. Needed to stay within bandwidth @ 640x480@30
 *		0x1c From usb captures under Windows for 640x480
 *		0x2a Values >= this switch the camera to a lower compression,
 *		     using the same table for both luminance and chrominance.
 *		     This gives a sharper picture. Usable only at 640x480@ <
 *		     15 fps or 320x240 / 160x120. Note currently the driver
 *		     does not use this as the quality gain is small and the
 *		     generated JPG-s are only understood by v4l-utils >= 0.8.9
 *		0x3f From usb captures under Windows for 320x240
 *		0x69 From usb captures under Windows for 160x120
 *
 * Register page 4:
 *
 * Address	Description
 * 0x02		Clock divider 2-63, fps =~ 60 / val. Must be a multiple of 3 on
 *		the 7302, so one of 3, 6, 9, ..., except when between 6 and 12?
 * 0x0f		Master gain 1-245, low value = high gain
 * 0x10		Another gain 0-15, limited influence (1-2x gain I guess)
 * 0x21		Bitfield: 0-1 unused, 2-3 vflip/hflip, 4-5 unknown, 6-7 unused
 *		Note setting vflip disabled leads to a much lower image quality,
 *		so we always vflip, and tell userspace to flip it back
 * 0x27		Seems to toggle various gains on / off, Setting bit 7 seems to
 *		completely disable the analog amplification block. Set to 0x68
 *		for max gain, 0x14 for minimal gain.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "pac7311"

#include <linux/input.h>
#include "gspca.h"
/* Include pac common sof detection functions */
#include "pac_common.h"

#define PAC7311_GAIN_DEFAULT     122
#define PAC7311_EXPOSURE_DEFAULT   3 /* 20 fps, avoid using high compr. */

MODULE_AUTHOR("Thomas Kaiser thomas@kaiser-linux.li");
MODULE_DESCRIPTION("Pixart PAC7311");
MODULE_LICENSE("GPL");

struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */

	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *hflip;

	u8 sof_read;
	u8 autogain_ignore_frames;

	atomic_t avg_lum;
};

static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_PJPG, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{320, 240, V4L2_PIX_FMT_PJPG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_PJPG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

#define LOAD_PAGE4		254
#define END_OF_SEQUENCE		0

static const __u8 init_7311[] = {
	0xff, 0x01,
	0x78, 0x40,	/* Bit_0=start stream, Bit_6=LED */
	0x78, 0x40,	/* Bit_0=start stream, Bit_6=LED */
	0x78, 0x44,	/* Bit_0=start stream, Bit_6=LED */
	0xff, 0x04,
	0x27, 0x80,
	0x28, 0xca,
	0x29, 0x53,
	0x2a, 0x0e,
	0xff, 0x01,
	0x3e, 0x20,
};

static const __u8 start_7311[] = {
/*	index, len, [value]* */
	0xff, 1,	0x01,		/* page 1 */
	0x02, 43,	0x48, 0x0a, 0x40, 0x08, 0x00, 0x00, 0x08, 0x00,
			0x06, 0xff, 0x11, 0xff, 0x5a, 0x30, 0x90, 0x4c,
			0x00, 0x07, 0x00, 0x0a, 0x10, 0x00, 0xa0, 0x10,
			0x02, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x01, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00,
	0x3e, 42,	0x00, 0x00, 0x78, 0x52, 0x4a, 0x52, 0x78, 0x6e,
			0x48, 0x46, 0x48, 0x6e, 0x5f, 0x49, 0x42, 0x49,
			0x5f, 0x5f, 0x49, 0x42, 0x49, 0x5f, 0x6e, 0x48,
			0x46, 0x48, 0x6e, 0x78, 0x52, 0x4a, 0x52, 0x78,
			0x00, 0x00, 0x09, 0x1b, 0x34, 0x49, 0x5c, 0x9b,
			0xd0, 0xff,
	0x78, 6,	0x44, 0x00, 0xf2, 0x01, 0x01, 0x80,
	0x7f, 18,	0x2a, 0x1c, 0x00, 0xc8, 0x02, 0x58, 0x03, 0x84,
			0x12, 0x00, 0x1a, 0x04, 0x08, 0x0c, 0x10, 0x14,
			0x18, 0x20,
	0x96, 3,	0x01, 0x08, 0x04,
	0xa0, 4,	0x44, 0x44, 0x44, 0x04,
	0xf0, 13,	0x01, 0x00, 0x00, 0x00, 0x22, 0x00, 0x20, 0x00,
			0x3f, 0x00, 0x0a, 0x01, 0x00,
	0xff, 1,	0x04,		/* page 4 */
	0, LOAD_PAGE4,			/* load the page 4 */
	0x11, 1,	0x01,
	0, END_OF_SEQUENCE		/* end of sequence */
};

#define SKIP		0xaa
/* page 4 - the value SKIP says skip the index - see reg_w_page() */
static const __u8 page4_7311[] = {
	SKIP, SKIP, 0x04, 0x54, 0x07, 0x2b, 0x09, 0x0f,
	0x09, 0x00, SKIP, SKIP, 0x07, 0x00, 0x00, 0x62,
	0x08, SKIP, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x03, 0xa0, 0x01, 0xf4, SKIP,
	SKIP, 0x00, 0x08, SKIP, 0x03, SKIP, 0x00, 0x68,
	0xca, 0x10, 0x06, 0x78, 0x00, 0x00, 0x00, 0x00,
	0x23, 0x28, 0x04, 0x11, 0x00, 0x00
};

static void reg_w_buf(struct gspca_dev *gspca_dev,
		  __u8 index,
		  const u8 *buffer, int len)
{
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	memcpy(gspca_dev->usb_buf, buffer, len);
	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0,		/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index, gspca_dev->usb_buf, len,
			500);
	if (ret < 0) {
		pr_err("reg_w_buf() failed index 0x%02x, error %d\n",
		       index, ret);
		gspca_dev->usb_err = ret;
	}
}


static void reg_w(struct gspca_dev *gspca_dev,
		  __u8 index,
		  __u8 value)
{
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	gspca_dev->usb_buf[0] = value;
	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index, gspca_dev->usb_buf, 1,
			500);
	if (ret < 0) {
		pr_err("reg_w() failed index 0x%02x, value 0x%02x, error %d\n",
		       index, value, ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_w_seq(struct gspca_dev *gspca_dev,
		const __u8 *seq, int len)
{
	while (--len >= 0) {
		reg_w(gspca_dev, seq[0], seq[1]);
		seq += 2;
	}
}

/* load the beginning of a page */
static void reg_w_page(struct gspca_dev *gspca_dev,
			const __u8 *page, int len)
{
	int index;
	int ret = 0;

	if (gspca_dev->usb_err < 0)
		return;
	for (index = 0; index < len; index++) {
		if (page[index] == SKIP)		/* skip this index */
			continue;
		gspca_dev->usb_buf[0] = page[index];
		ret = usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0, index, gspca_dev->usb_buf, 1,
				500);
		if (ret < 0) {
			pr_err("reg_w_page() failed index 0x%02x, value 0x%02x, error %d\n",
			       index, page[index], ret);
			gspca_dev->usb_err = ret;
			break;
		}
	}
}

/* output a variable sequence */
static void reg_w_var(struct gspca_dev *gspca_dev,
			const __u8 *seq,
			const __u8 *page4, unsigned int page4_len)
{
	int index, len;

	for (;;) {
		index = *seq++;
		len = *seq++;
		switch (len) {
		case END_OF_SEQUENCE:
			return;
		case LOAD_PAGE4:
			reg_w_page(gspca_dev, page4, page4_len);
			break;
		default:
			if (len > USB_BUF_SZ) {
				PERR("Incorrect variable sequence");
				return;
			}
			while (len > 0) {
				if (len < 8) {
					reg_w_buf(gspca_dev,
						index, seq, len);
					seq += len;
					break;
				}
				reg_w_buf(gspca_dev, index, seq, 8);
				seq += 8;
				index += 8;
				len -= 8;
			}
		}
	}
	/* not reached */
}

/* this function is called at probe time for pac7311 */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct cam *cam = &gspca_dev->cam;

	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);
	cam->input_flags = V4L2_IN_ST_VFLIP;

	return 0;
}

static void setcontrast(struct gspca_dev *gspca_dev, s32 val)
{
	reg_w(gspca_dev, 0xff, 0x04);
	reg_w(gspca_dev, 0x10, val);
	/* load registers to sensor (Bit 0, auto clear) */
	reg_w(gspca_dev, 0x11, 0x01);
}

static void setgain(struct gspca_dev *gspca_dev, s32 val)
{
	reg_w(gspca_dev, 0xff, 0x04);			/* page 4 */
	reg_w(gspca_dev, 0x0e, 0x00);
	reg_w(gspca_dev, 0x0f, gspca_dev->gain->maximum - val + 1);

	/* load registers to sensor (Bit 0, auto clear) */
	reg_w(gspca_dev, 0x11, 0x01);
}

static void setexposure(struct gspca_dev *gspca_dev, s32 val)
{
	reg_w(gspca_dev, 0xff, 0x04);			/* page 4 */
	reg_w(gspca_dev, 0x02, val);

	/* load registers to sensor (Bit 0, auto clear) */
	reg_w(gspca_dev, 0x11, 0x01);

	/*
	 * Page 1 register 8 must always be 0x08 except when not in
	 *  640x480 mode and page 4 reg 2 <= 3 then it must be 9
	 */
	reg_w(gspca_dev, 0xff, 0x01);
	if (gspca_dev->width != 640 && val <= 3)
		reg_w(gspca_dev, 0x08, 0x09);
	else
		reg_w(gspca_dev, 0x08, 0x08);

	/*
	 * Page1 register 80 sets the compression balance, normally we
	 * want / use 0x1c, but for 640x480@30fps we must allow the
	 * camera to use higher compression or we may run out of
	 * bandwidth.
	 */
	if (gspca_dev->width == 640 && val == 2)
		reg_w(gspca_dev, 0x80, 0x01);
	else
		reg_w(gspca_dev, 0x80, 0x1c);

	/* load registers to sensor (Bit 0, auto clear) */
	reg_w(gspca_dev, 0x11, 0x01);
}

static void sethvflip(struct gspca_dev *gspca_dev, s32 hflip, s32 vflip)
{
	__u8 data;

	reg_w(gspca_dev, 0xff, 0x04);			/* page 4 */
	data = (hflip ? 0x04 : 0x00) |
	       (vflip ? 0x08 : 0x00);
	reg_w(gspca_dev, 0x21, data);

	/* load registers to sensor (Bit 0, auto clear) */
	reg_w(gspca_dev, 0x11, 0x01);
}

/* this function is called at probe and resume time for pac7311 */
static int sd_init(struct gspca_dev *gspca_dev)
{
	reg_w_seq(gspca_dev, init_7311, sizeof(init_7311)/2);
	return gspca_dev->usb_err;
}

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	if (ctrl->id == V4L2_CID_AUTOGAIN && ctrl->is_new && ctrl->val) {
		/* when switching to autogain set defaults to make sure
		   we are on a valid point of the autogain gain /
		   exposure knee graph, and give this change time to
		   take effect before doing autogain. */
		gspca_dev->exposure->val    = PAC7311_EXPOSURE_DEFAULT;
		gspca_dev->gain->val        = PAC7311_GAIN_DEFAULT;
		sd->autogain_ignore_frames  = PAC_AUTOGAIN_IGNORE_FRAMES;
	}

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		setcontrast(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_AUTOGAIN:
		if (gspca_dev->exposure->is_new || (ctrl->is_new && ctrl->val))
			setexposure(gspca_dev, gspca_dev->exposure->val);
		if (gspca_dev->gain->is_new || (ctrl->is_new && ctrl->val))
			setgain(gspca_dev, gspca_dev->gain->val);
		break;
	case V4L2_CID_HFLIP:
		sethvflip(gspca_dev, sd->hflip->val, 1);
		break;
	default:
		return -EINVAL;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

/* this function is called at probe time */
static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 5);

	sd->contrast = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
					V4L2_CID_CONTRAST, 0, 15, 1, 7);
	gspca_dev->autogain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
					V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	gspca_dev->exposure = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
					V4L2_CID_EXPOSURE, 2, 63, 1,
					PAC7311_EXPOSURE_DEFAULT);
	gspca_dev->gain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
					V4L2_CID_GAIN, 0, 244, 1,
					PAC7311_GAIN_DEFAULT);
	sd->hflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}

	v4l2_ctrl_auto_cluster(3, &gspca_dev->autogain, 0, false);
	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->sof_read = 0;

	reg_w_var(gspca_dev, start_7311,
		page4_7311, sizeof(page4_7311));
	setcontrast(gspca_dev, v4l2_ctrl_g_ctrl(sd->contrast));
	setgain(gspca_dev, v4l2_ctrl_g_ctrl(gspca_dev->gain));
	setexposure(gspca_dev, v4l2_ctrl_g_ctrl(gspca_dev->exposure));
	sethvflip(gspca_dev, v4l2_ctrl_g_ctrl(sd->hflip), 1);

	/* set correct resolution */
	switch (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv) {
	case 2:					/* 160x120 */
		reg_w(gspca_dev, 0xff, 0x01);
		reg_w(gspca_dev, 0x17, 0x20);
		reg_w(gspca_dev, 0x87, 0x10);
		break;
	case 1:					/* 320x240 */
		reg_w(gspca_dev, 0xff, 0x01);
		reg_w(gspca_dev, 0x17, 0x30);
		reg_w(gspca_dev, 0x87, 0x11);
		break;
	case 0:					/* 640x480 */
		reg_w(gspca_dev, 0xff, 0x01);
		reg_w(gspca_dev, 0x17, 0x00);
		reg_w(gspca_dev, 0x87, 0x12);
		break;
	}

	sd->sof_read = 0;
	sd->autogain_ignore_frames = 0;
	atomic_set(&sd->avg_lum, -1);

	/* start stream */
	reg_w(gspca_dev, 0xff, 0x01);
	reg_w(gspca_dev, 0x78, 0x05);

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 0xff, 0x04);
	reg_w(gspca_dev, 0x27, 0x80);
	reg_w(gspca_dev, 0x28, 0xca);
	reg_w(gspca_dev, 0x29, 0x53);
	reg_w(gspca_dev, 0x2a, 0x0e);
	reg_w(gspca_dev, 0xff, 0x01);
	reg_w(gspca_dev, 0x3e, 0x20);
	reg_w(gspca_dev, 0x78, 0x44); /* Bit_0=start stream, Bit_6=LED */
	reg_w(gspca_dev, 0x78, 0x44); /* Bit_0=start stream, Bit_6=LED */
	reg_w(gspca_dev, 0x78, 0x44); /* Bit_0=start stream, Bit_6=LED */
}

static void do_autogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int avg_lum = atomic_read(&sd->avg_lum);
	int desired_lum, deadzone;

	if (avg_lum == -1)
		return;

	desired_lum = 170;
	deadzone = 20;

	if (sd->autogain_ignore_frames > 0)
		sd->autogain_ignore_frames--;
	else if (gspca_coarse_grained_expo_autogain(gspca_dev, avg_lum,
						    desired_lum, deadzone))
		sd->autogain_ignore_frames = PAC_AUTOGAIN_IGNORE_FRAMES;
}

/* JPEG header, part 1 */
static const unsigned char pac_jpeg_header1[] = {
  0xff, 0xd8,		/* SOI: Start of Image */

  0xff, 0xc0,		/* SOF0: Start of Frame (Baseline DCT) */
  0x00, 0x11,		/* length = 17 bytes (including this length field) */
  0x08			/* Precision: 8 */
  /* 2 bytes is placed here: number of image lines */
  /* 2 bytes is placed here: samples per line */
};

/* JPEG header, continued */
static const unsigned char pac_jpeg_header2[] = {
  0x03,			/* Number of image components: 3 */
  0x01, 0x21, 0x00,	/* ID=1, Subsampling 1x1, Quantization table: 0 */
  0x02, 0x11, 0x01,	/* ID=2, Subsampling 2x1, Quantization table: 1 */
  0x03, 0x11, 0x01,	/* ID=3, Subsampling 2x1, Quantization table: 1 */

  0xff, 0xda,		/* SOS: Start Of Scan */
  0x00, 0x0c,		/* length = 12 bytes (including this length field) */
  0x03,			/* number of components: 3 */
  0x01, 0x00,		/* selector 1, table 0x00 */
  0x02, 0x11,		/* selector 2, table 0x11 */
  0x03, 0x11,		/* selector 3, table 0x11 */
  0x00, 0x3f,		/* Spectral selection: 0 .. 63 */
  0x00			/* Successive approximation: 0 */
};

static void pac_start_frame(struct gspca_dev *gspca_dev,
		__u16 lines, __u16 samples_per_line)
{
	unsigned char tmpbuf[4];

	gspca_frame_add(gspca_dev, FIRST_PACKET,
		pac_jpeg_header1, sizeof(pac_jpeg_header1));

	tmpbuf[0] = lines >> 8;
	tmpbuf[1] = lines & 0xff;
	tmpbuf[2] = samples_per_line >> 8;
	tmpbuf[3] = samples_per_line & 0xff;

	gspca_frame_add(gspca_dev, INTER_PACKET,
		tmpbuf, sizeof(tmpbuf));
	gspca_frame_add(gspca_dev, INTER_PACKET,
		pac_jpeg_header2, sizeof(pac_jpeg_header2));
}

/* this function is run at interrupt level */
static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 *image;
	unsigned char *sof;

	sof = pac_find_sof(gspca_dev, &sd->sof_read, data, len);
	if (sof) {
		int n, lum_offset, footer_length;

		/*
		 * 6 bytes after the FF D9 EOF marker a number of lumination
		 * bytes are send corresponding to different parts of the
		 * image, the 14th and 15th byte after the EOF seem to
		 * correspond to the center of the image.
		 */
		lum_offset = 24 + sizeof pac_sof_marker;
		footer_length = 26;

		/* Finish decoding current frame */
		n = (sof - data) - (footer_length + sizeof pac_sof_marker);
		if (n < 0) {
			gspca_dev->image_len += n;
			n = 0;
		} else {
			gspca_frame_add(gspca_dev, INTER_PACKET, data, n);
		}
		image = gspca_dev->image;
		if (image != NULL
		 && image[gspca_dev->image_len - 2] == 0xff
		 && image[gspca_dev->image_len - 1] == 0xd9)
			gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);

		n = sof - data;
		len -= n;
		data = sof;

		/* Get average lumination */
		if (gspca_dev->last_packet_type == LAST_PACKET &&
				n >= lum_offset)
			atomic_set(&sd->avg_lum, data[-lum_offset] +
						data[-lum_offset + 1]);
		else
			atomic_set(&sd->avg_lum, -1);

		/* Start the new frame with the jpeg header */
		pac_start_frame(gspca_dev,
			gspca_dev->height, gspca_dev->width);
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

#if IS_ENABLED(CONFIG_INPUT)
static int sd_int_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* interrupt packet data */
			int len)		/* interrupt packet length */
{
	int ret = -EINVAL;
	u8 data0, data1;

	if (len == 2) {
		data0 = data[0];
		data1 = data[1];
		if ((data0 == 0x00 && data1 == 0x11) ||
		    (data0 == 0x22 && data1 == 0x33) ||
		    (data0 == 0x44 && data1 == 0x55) ||
		    (data0 == 0x66 && data1 == 0x77) ||
		    (data0 == 0x88 && data1 == 0x99) ||
		    (data0 == 0xaa && data1 == 0xbb) ||
		    (data0 == 0xcc && data1 == 0xdd) ||
		    (data0 == 0xee && data1 == 0xff)) {
			input_report_key(gspca_dev->input_dev, KEY_CAMERA, 1);
			input_sync(gspca_dev->input_dev);
			input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
			input_sync(gspca_dev->input_dev);
			ret = 0;
		}
	}

	return ret;
}
#endif

static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = do_autogain,
#if IS_ENABLED(CONFIG_INPUT)
	.int_pkt_scan = sd_int_pkt_scan,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x093a, 0x2600)},
	{USB_DEVICE(0x093a, 0x2601)},
	{USB_DEVICE(0x093a, 0x2603)},
	{USB_DEVICE(0x093a, 0x2608)},
	{USB_DEVICE(0x093a, 0x260e)},
	{USB_DEVICE(0x093a, 0x260f)},
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
