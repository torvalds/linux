/*
 * GSPCA Endpoints (formerly known as AOX) se401 USB Camera sub Driver
 *
 * Copyright (C) 2011 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on the v4l1 se401 driver which is:
 *
 * Copyright (c) 2000 Jeroen B. Vreeken (pe1rxq@amsat.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "se401"

#define BULK_SIZE 4096
#define PACKET_SIZE 1024
#define READ_REQ_SIZE 64
#define MAX_MODES ((READ_REQ_SIZE - 6) / 4)
/* The se401 compression algorithm uses a fixed quant factor, which
   can be configured by setting the high nibble of the SE401_OPERATINGMODE
   feature. This needs to exactly match what is in libv4l! */
#define SE401_QUANT_FACT 8

#include <linux/input.h>
#include <linux/slab.h>
#include "gspca.h"
#include "se401.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Endpoints se401");
MODULE_LICENSE("GPL");

/* exposure change state machine states */
enum {
	EXPO_CHANGED,
	EXPO_DROP_FRAME,
	EXPO_NO_CHANGE,
};

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	struct { /* exposure/freq control cluster */
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *freq;
	};
	bool has_brightness;
	struct v4l2_pix_format fmts[MAX_MODES];
	int pixels_read;
	int packet_read;
	u8 packet[PACKET_SIZE];
	u8 restart_stream;
	u8 button_state;
	u8 resetlevel;
	u8 resetlevel_frame_count;
	int resetlevel_adjust_dir;
	int expo_change_state;
};


static void se401_write_req(struct gspca_dev *gspca_dev, u16 req, u16 value,
			    int silent)
{
	int err;

	if (gspca_dev->usb_err < 0)
		return;

	err = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0), req,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      value, 0, NULL, 0, 1000);
	if (err < 0) {
		if (!silent)
			pr_err("write req failed req %#04x val %#04x error %d\n",
			       req, value, err);
		gspca_dev->usb_err = err;
	}
}

static void se401_read_req(struct gspca_dev *gspca_dev, u16 req, int silent)
{
	int err;

	if (gspca_dev->usb_err < 0)
		return;

	if (USB_BUF_SZ < READ_REQ_SIZE) {
		pr_err("USB_BUF_SZ too small!!\n");
		gspca_dev->usb_err = -ENOBUFS;
		return;
	}

	err = usb_control_msg(gspca_dev->dev,
			      usb_rcvctrlpipe(gspca_dev->dev, 0), req,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, 0, gspca_dev->usb_buf, READ_REQ_SIZE, 1000);
	if (err < 0) {
		if (!silent)
			pr_err("read req failed req %#04x error %d\n",
			       req, err);
		gspca_dev->usb_err = err;
		/*
		 * Make sure the buffer is zeroed to avoid uninitialized
		 * values.
		 */
		memset(gspca_dev->usb_buf, 0, READ_REQ_SIZE);
	}
}

static void se401_set_feature(struct gspca_dev *gspca_dev,
			      u16 selector, u16 param)
{
	int err;

	if (gspca_dev->usb_err < 0)
		return;

	err = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0),
			      SE401_REQ_SET_EXT_FEATURE,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      param, selector, NULL, 0, 1000);
	if (err < 0) {
		pr_err("set feature failed sel %#04x param %#04x error %d\n",
		       selector, param, err);
		gspca_dev->usb_err = err;
	}
}

static int se401_get_feature(struct gspca_dev *gspca_dev, u16 selector)
{
	int err;

	if (gspca_dev->usb_err < 0)
		return gspca_dev->usb_err;

	if (USB_BUF_SZ < 2) {
		pr_err("USB_BUF_SZ too small!!\n");
		gspca_dev->usb_err = -ENOBUFS;
		return gspca_dev->usb_err;
	}

	err = usb_control_msg(gspca_dev->dev,
			      usb_rcvctrlpipe(gspca_dev->dev, 0),
			      SE401_REQ_GET_EXT_FEATURE,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, selector, gspca_dev->usb_buf, 2, 1000);
	if (err < 0) {
		pr_err("get feature failed sel %#04x error %d\n",
		       selector, err);
		gspca_dev->usb_err = err;
		return err;
	}
	return gspca_dev->usb_buf[0] | (gspca_dev->usb_buf[1] << 8);
}

static void setbrightness(struct gspca_dev *gspca_dev, s32 val)
{
	/* HDG: this does not seem to do anything on my cam */
	se401_write_req(gspca_dev, SE401_REQ_SET_BRT, val, 0);
}

static void setgain(struct gspca_dev *gspca_dev, s32 val)
{
	u16 gain = 63 - val;

	/* red color gain */
	se401_set_feature(gspca_dev, HV7131_REG_ARCG, gain);
	/* green color gain */
	se401_set_feature(gspca_dev, HV7131_REG_AGCG, gain);
	/* blue color gain */
	se401_set_feature(gspca_dev, HV7131_REG_ABCG, gain);
}

static void setexposure(struct gspca_dev *gspca_dev, s32 val, s32 freq)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int integration = val << 6;
	u8 expose_h, expose_m, expose_l;

	/* Do this before the set_feature calls, for proper timing wrt
	   the interrupt driven pkt_scan. Note we may still race but that
	   is not a big issue, the expo change state machine is merely for
	   avoiding underexposed frames getting send out, if one sneaks
	   through so be it */
	sd->expo_change_state = EXPO_CHANGED;

	if (freq == V4L2_CID_POWER_LINE_FREQUENCY_50HZ)
		integration = integration - integration % 106667;
	if (freq == V4L2_CID_POWER_LINE_FREQUENCY_60HZ)
		integration = integration - integration % 88889;

	expose_h = (integration >> 16);
	expose_m = (integration >> 8);
	expose_l = integration;

	/* integration time low */
	se401_set_feature(gspca_dev, HV7131_REG_TITL, expose_l);
	/* integration time mid */
	se401_set_feature(gspca_dev, HV7131_REG_TITM, expose_m);
	/* integration time high */
	se401_set_feature(gspca_dev, HV7131_REG_TITU, expose_h);
}

static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *)gspca_dev;
	struct cam *cam = &gspca_dev->cam;
	u8 *cd = gspca_dev->usb_buf;
	int i, j, n;
	int widths[MAX_MODES], heights[MAX_MODES];

	/* Read the camera descriptor */
	se401_read_req(gspca_dev, SE401_REQ_GET_CAMERA_DESCRIPTOR, 1);
	if (gspca_dev->usb_err) {
		/* Sometimes after being idle for a while the se401 won't
		   respond and needs a good kicking  */
		usb_reset_device(gspca_dev->dev);
		gspca_dev->usb_err = 0;
		se401_read_req(gspca_dev, SE401_REQ_GET_CAMERA_DESCRIPTOR, 0);
	}

	/* Some cameras start with their LED on */
	se401_write_req(gspca_dev, SE401_REQ_LED_CONTROL, 0, 0);
	if (gspca_dev->usb_err)
		return gspca_dev->usb_err;

	if (cd[1] != 0x41) {
		pr_err("Wrong descriptor type\n");
		return -ENODEV;
	}

	if (!(cd[2] & SE401_FORMAT_BAYER)) {
		pr_err("Bayer format not supported!\n");
		return -ENODEV;
	}

	if (cd[3])
		pr_info("ExtraFeatures: %d\n", cd[3]);

	n = cd[4] | (cd[5] << 8);
	if (n > MAX_MODES) {
		pr_err("Too many frame sizes\n");
		return -ENODEV;
	}

	for (i = 0; i < n ; i++) {
		widths[i] = cd[6 + i * 4 + 0] | (cd[6 + i * 4 + 1] << 8);
		heights[i] = cd[6 + i * 4 + 2] | (cd[6 + i * 4 + 3] << 8);
	}

	for (i = 0; i < n ; i++) {
		sd->fmts[i].width = widths[i];
		sd->fmts[i].height = heights[i];
		sd->fmts[i].field = V4L2_FIELD_NONE;
		sd->fmts[i].colorspace = V4L2_COLORSPACE_SRGB;
		sd->fmts[i].priv = 1;

		/* janggu compression only works for 1/4th or 1/16th res */
		for (j = 0; j < n; j++) {
			if (widths[j] / 2 == widths[i] &&
			    heights[j] / 2 == heights[i]) {
				sd->fmts[i].priv = 2;
				break;
			}
		}
		/* 1/16th if available too is better then 1/4th, because
		   we then use a larger area of the sensor */
		for (j = 0; j < n; j++) {
			if (widths[j] / 4 == widths[i] &&
			    heights[j] / 4 == heights[i]) {
				sd->fmts[i].priv = 4;
				break;
			}
		}

		if (sd->fmts[i].priv == 1) {
			/* Not a 1/4th or 1/16th res, use bayer */
			sd->fmts[i].pixelformat = V4L2_PIX_FMT_SBGGR8;
			sd->fmts[i].bytesperline = widths[i];
			sd->fmts[i].sizeimage = widths[i] * heights[i];
			pr_info("Frame size: %dx%d bayer\n",
				widths[i], heights[i]);
		} else {
			/* Found a match use janggu compression */
			sd->fmts[i].pixelformat = V4L2_PIX_FMT_SE401;
			sd->fmts[i].bytesperline = 0;
			sd->fmts[i].sizeimage = widths[i] * heights[i] * 3;
			pr_info("Frame size: %dx%d 1/%dth janggu\n",
				widths[i], heights[i],
				sd->fmts[i].priv * sd->fmts[i].priv);
		}
	}

	cam->cam_mode = sd->fmts;
	cam->nmodes = n;
	cam->bulk = 1;
	cam->bulk_size = BULK_SIZE;
	cam->bulk_nurbs = 4;
	sd->resetlevel = 0x2d; /* Set initial resetlevel */

	/* See if the camera supports brightness */
	se401_read_req(gspca_dev, SE401_REQ_GET_BRT, 1);
	sd->has_brightness = !!gspca_dev->usb_err;
	gspca_dev->usb_err = 0;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

/* function called at start time before URB creation */
static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	gspca_dev->alt = 1;	/* Ignore the bogus isoc alt settings */

	return gspca_dev->usb_err;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *)gspca_dev;
	int mult = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	int mode = 0;

	se401_write_req(gspca_dev, SE401_REQ_CAMERA_POWER, 1, 1);
	if (gspca_dev->usb_err) {
		/* Sometimes after being idle for a while the se401 won't
		   respond and needs a good kicking  */
		usb_reset_device(gspca_dev->dev);
		gspca_dev->usb_err = 0;
		se401_write_req(gspca_dev, SE401_REQ_CAMERA_POWER, 1, 0);
	}
	se401_write_req(gspca_dev, SE401_REQ_LED_CONTROL, 1, 0);

	se401_set_feature(gspca_dev, HV7131_REG_MODE_B, 0x05);

	/* set size + mode */
	se401_write_req(gspca_dev, SE401_REQ_SET_WIDTH,
			gspca_dev->pixfmt.width * mult, 0);
	se401_write_req(gspca_dev, SE401_REQ_SET_HEIGHT,
			gspca_dev->pixfmt.height * mult, 0);
	/*
	 * HDG: disabled this as it does not seem to do anything
	 * se401_write_req(gspca_dev, SE401_REQ_SET_OUTPUT_MODE,
	 *		   SE401_FORMAT_BAYER, 0);
	 */

	switch (mult) {
	case 1: /* Raw bayer */
		mode = 0x03; break;
	case 2: /* 1/4th janggu */
		mode = SE401_QUANT_FACT << 4; break;
	case 4: /* 1/16th janggu */
		mode = (SE401_QUANT_FACT << 4) | 0x02; break;
	}
	se401_set_feature(gspca_dev, SE401_OPERATINGMODE, mode);

	se401_set_feature(gspca_dev, HV7131_REG_ARLV, sd->resetlevel);

	sd->packet_read = 0;
	sd->pixels_read = 0;
	sd->restart_stream = 0;
	sd->resetlevel_frame_count = 0;
	sd->resetlevel_adjust_dir = 0;
	sd->expo_change_state = EXPO_NO_CHANGE;

	se401_write_req(gspca_dev, SE401_REQ_START_CONTINUOUS_CAPTURE, 0, 0);

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	se401_write_req(gspca_dev, SE401_REQ_STOP_CONTINUOUS_CAPTURE, 0, 0);
	se401_write_req(gspca_dev, SE401_REQ_LED_CONTROL, 0, 0);
	se401_write_req(gspca_dev, SE401_REQ_CAMERA_POWER, 0, 0);
}

static void sd_dq_callback(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *)gspca_dev;
	unsigned int ahrc, alrc;
	int oldreset, adjust_dir;

	/* Restart the stream if requested do so by pkt_scan */
	if (sd->restart_stream) {
		sd_stopN(gspca_dev);
		sd_start(gspca_dev);
		sd->restart_stream = 0;
	}

	/* Automatically adjust sensor reset level
	   Hyundai have some really nice docs about this and other sensor
	   related stuff on their homepage: www.hei.co.kr */
	sd->resetlevel_frame_count++;
	if (sd->resetlevel_frame_count < 20)
		return;

	/* For some reason this normally read-only register doesn't get reset
	   to zero after reading them just once... */
	se401_get_feature(gspca_dev, HV7131_REG_HIREFNOH);
	se401_get_feature(gspca_dev, HV7131_REG_HIREFNOL);
	se401_get_feature(gspca_dev, HV7131_REG_LOREFNOH);
	se401_get_feature(gspca_dev, HV7131_REG_LOREFNOL);
	ahrc = 256*se401_get_feature(gspca_dev, HV7131_REG_HIREFNOH) +
	    se401_get_feature(gspca_dev, HV7131_REG_HIREFNOL);
	alrc = 256*se401_get_feature(gspca_dev, HV7131_REG_LOREFNOH) +
	    se401_get_feature(gspca_dev, HV7131_REG_LOREFNOL);

	/* Not an exact science, but it seems to work pretty well... */
	oldreset = sd->resetlevel;
	if (alrc > 10) {
		while (alrc >= 10 && sd->resetlevel < 63) {
			sd->resetlevel++;
			alrc /= 2;
		}
	} else if (ahrc > 20) {
		while (ahrc >= 20 && sd->resetlevel > 0) {
			sd->resetlevel--;
			ahrc /= 2;
		}
	}
	/* Detect ping-pong-ing and halve adjustment to avoid overshoot */
	if (sd->resetlevel > oldreset)
		adjust_dir = 1;
	else
		adjust_dir = -1;
	if (sd->resetlevel_adjust_dir &&
	    sd->resetlevel_adjust_dir != adjust_dir)
		sd->resetlevel = oldreset + (sd->resetlevel - oldreset) / 2;

	if (sd->resetlevel != oldreset) {
		sd->resetlevel_adjust_dir = adjust_dir;
		se401_set_feature(gspca_dev, HV7131_REG_ARLV, sd->resetlevel);
	}

	sd->resetlevel_frame_count = 0;
}

static void sd_complete_frame(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	struct sd *sd = (struct sd *)gspca_dev;

	switch (sd->expo_change_state) {
	case EXPO_CHANGED:
		/* The exposure was changed while this frame
		   was being send, so this frame is ok */
		sd->expo_change_state = EXPO_DROP_FRAME;
		break;
	case EXPO_DROP_FRAME:
		/* The exposure was changed while this frame
		   was being captured, drop it! */
		gspca_dev->last_packet_type = DISCARD_PACKET;
		sd->expo_change_state = EXPO_NO_CHANGE;
		break;
	case EXPO_NO_CHANGE:
		break;
	}
	gspca_frame_add(gspca_dev, LAST_PACKET, data, len);
}

static void sd_pkt_scan_janggu(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	struct sd *sd = (struct sd *)gspca_dev;
	int imagesize = gspca_dev->pixfmt.width * gspca_dev->pixfmt.height;
	int i, plen, bits, pixels, info, count;

	if (sd->restart_stream)
		return;

	/* Sometimes a 1024 bytes garbage bulk packet is send between frames */
	if (gspca_dev->last_packet_type == LAST_PACKET && len == 1024) {
		gspca_dev->last_packet_type = DISCARD_PACKET;
		return;
	}

	i = 0;
	while (i < len) {
		/* Read header if not already be present from prev bulk pkt */
		if (sd->packet_read < 4) {
			count = 4 - sd->packet_read;
			if (count > len - i)
				count = len - i;
			memcpy(&sd->packet[sd->packet_read], &data[i], count);
			sd->packet_read += count;
			i += count;
			if (sd->packet_read < 4)
				break;
		}
		bits   = sd->packet[3] + (sd->packet[2] << 8);
		pixels = sd->packet[1] + ((sd->packet[0] & 0x3f) << 8);
		info   = (sd->packet[0] & 0xc0) >> 6;
		plen   = ((bits + 47) >> 4) << 1;
		/* Sanity checks */
		if (plen > 1024) {
			pr_err("invalid packet len %d restarting stream\n",
			       plen);
			goto error;
		}
		if (info == 3) {
			pr_err("unknown frame info value restarting stream\n");
			goto error;
		}

		/* Read (remainder of) packet contents */
		count = plen - sd->packet_read;
		if (count > len - i)
			count = len - i;
		memcpy(&sd->packet[sd->packet_read], &data[i], count);
		sd->packet_read += count;
		i += count;
		if (sd->packet_read < plen)
			break;

		sd->pixels_read += pixels;
		sd->packet_read = 0;

		switch (info) {
		case 0: /* Frame data */
			gspca_frame_add(gspca_dev, INTER_PACKET, sd->packet,
					plen);
			break;
		case 1: /* EOF */
			if (sd->pixels_read != imagesize) {
				pr_err("frame size %d expected %d\n",
				       sd->pixels_read, imagesize);
				goto error;
			}
			sd_complete_frame(gspca_dev, sd->packet, plen);
			return; /* Discard the rest of the bulk packet !! */
		case 2: /* SOF */
			gspca_frame_add(gspca_dev, FIRST_PACKET, sd->packet,
					plen);
			sd->pixels_read = pixels;
			break;
		}
	}
	return;

error:
	sd->restart_stream = 1;
	/* Give userspace a 0 bytes frame, so our dq callback gets
	   called and it can restart the stream */
	gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
	gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
}

static void sd_pkt_scan_bayer(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	struct cam *cam = &gspca_dev->cam;
	int imagesize = cam->cam_mode[gspca_dev->curr_mode].sizeimage;

	if (gspca_dev->image_len == 0) {
		gspca_frame_add(gspca_dev, FIRST_PACKET, data, len);
		return;
	}

	if (gspca_dev->image_len + len >= imagesize) {
		sd_complete_frame(gspca_dev, data, len);
		return;
	}

	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	int mult = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;

	if (len == 0)
		return;

	if (mult == 1) /* mult == 1 means raw bayer */
		sd_pkt_scan_bayer(gspca_dev, data, len);
	else
		sd_pkt_scan_janggu(gspca_dev, data, len);
}

#if IS_ENABLED(CONFIG_INPUT)
static int sd_int_pkt_scan(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	struct sd *sd = (struct sd *)gspca_dev;
	u8 state;

	if (len != 2)
		return -EINVAL;

	switch (data[0]) {
	case 0:
	case 1:
		state = data[0];
		break;
	default:
		return -EINVAL;
	}
	if (sd->button_state != state) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, state);
		input_sync(gspca_dev->input_dev);
		sd->button_state = state;
	}

	return 0;
}
#endif

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		setbrightness(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		setgain(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		setexposure(gspca_dev, ctrl->val, sd->freq->val);
		break;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *)gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	if (sd->has_brightness)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 15);
	/* max is really 63 but > 50 is not pretty */
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_GAIN, 0, 50, 1, 25);
	sd->exposure = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 32767, 1, 15000);
	sd->freq = v4l2_ctrl_new_std_menu(hdl, &sd_ctrl_ops,
			V4L2_CID_POWER_LINE_FREQUENCY,
			V4L2_CID_POWER_LINE_FREQUENCY_60HZ, 0, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}
	v4l2_ctrl_cluster(2, &sd->exposure);
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init = sd_isoc_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.dq_callback = sd_dq_callback,
	.pkt_scan = sd_pkt_scan,
#if IS_ENABLED(CONFIG_INPUT)
	.int_pkt_scan = sd_int_pkt_scan,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x03e8, 0x0004)}, /* Endpoints/Aox SE401 */
	{USB_DEVICE(0x0471, 0x030b)}, /* Philips PCVC665K */
	{USB_DEVICE(0x047d, 0x5001)}, /* Kensington 67014 */
	{USB_DEVICE(0x047d, 0x5002)}, /* Kensington 6701(5/7) */
	{USB_DEVICE(0x047d, 0x5003)}, /* Kensington 67016 */
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

static int sd_pre_reset(struct usb_interface *intf)
{
	return 0;
}

static int sd_post_reset(struct usb_interface *intf)
{
	return 0;
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
	.pre_reset = sd_pre_reset,
	.post_reset = sd_post_reset,
};

module_usb_driver(sd_driver);
