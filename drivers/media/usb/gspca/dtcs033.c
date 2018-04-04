/*
 * Subdriver for Scopium astro-camera (DTCS033, 0547:7303)
 *
 * Copyright (C) 2014 Robert Butora (robert.butora.fi@gmail.com)
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define MODULE_NAME "dtcs033"
#include "gspca.h"

MODULE_AUTHOR("Robert Butora <robert.butora.fi@gmail.com>");
MODULE_DESCRIPTION("Scopium DTCS033 astro-cam USB Camera Driver");
MODULE_LICENSE("GPL");

struct dtcs033_usb_requests {
	u8 bRequestType;
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
};

/* send a usb request */
static void reg_rw(struct gspca_dev *gspca_dev,
		u8 bRequestType, u8 bRequest,
		u16 wValue, u16 wIndex, u16 wLength)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;

	ret = usb_control_msg(udev,
		usb_rcvctrlpipe(udev, 0),
		bRequest,
		bRequestType,
		wValue, wIndex,
		gspca_dev->usb_buf, wLength, 500);

	if (ret < 0) {
		gspca_dev->usb_err = ret;
		pr_err("usb_control_msg error %d\n", ret);
	}

	return;
}
/* send several usb in/out requests */
static int reg_reqs(struct gspca_dev *gspca_dev,
		    const struct dtcs033_usb_requests *preqs, int n_reqs)
{
	int i = 0;
	const struct dtcs033_usb_requests *preq;

	while ((i < n_reqs) && (gspca_dev->usb_err >= 0)) {

		preq = &preqs[i];

		reg_rw(gspca_dev, preq->bRequestType, preq->bRequest,
			preq->wValue, preq->wIndex, preq->wLength);

		if (gspca_dev->usb_err < 0) {

			gspca_err(gspca_dev, "usb error request no: %d / %d\n",
				  i, n_reqs);
		} else if (preq->bRequestType & USB_DIR_IN) {

			gspca_dbg(gspca_dev, D_STREAM,
				  "USB IN (%d) returned[%d] %3ph %s",
				  i,
				  preq->wLength,
				  gspca_dev->usb_buf,
				  preq->wLength > 3 ? "...\n" : "\n");
		}

		i++;
	}
	return gspca_dev->usb_err;
}

/* -- subdriver interface implementation -- */

#define DT_COLS (640)
static const struct v4l2_pix_format dtcs033_mode[] = {
	/* raw Bayer patterned output */
	{DT_COLS, 480, V4L2_PIX_FMT_GREY, V4L2_FIELD_NONE,
		.bytesperline = DT_COLS,
		.sizeimage = DT_COLS*480,
		.colorspace = V4L2_COLORSPACE_SRGB,
	},
	/* this mode will demosaic the Bayer pattern */
	{DT_COLS, 480, V4L2_PIX_FMT_SRGGB8, V4L2_FIELD_NONE,
		.bytesperline = DT_COLS,
		.sizeimage = DT_COLS*480,
		.colorspace = V4L2_COLORSPACE_SRGB,
	}
};

/* config called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		const struct usb_device_id *id)
{
	gspca_dev->cam.cam_mode = dtcs033_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(dtcs033_mode);

	gspca_dev->cam.bulk = 1;
	gspca_dev->cam.bulk_nurbs = 1;
	gspca_dev->cam.bulk_size = DT_COLS*512;

	return 0;
}

/* init called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

/* start stop the camera */
static int  dtcs033_start(struct gspca_dev *gspca_dev);
static void dtcs033_stopN(struct gspca_dev *gspca_dev);

/* intercept camera image data */
static void dtcs033_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,  /* packet data */
			int len)   /* packet data length */
{
	/* drop incomplete frames */
	if (len != DT_COLS*512) {
		gspca_dev->last_packet_type = DISCARD_PACKET;
		/* gspca.c: discard invalidates the whole frame. */
		return;
	}

	/* forward complete frames */
	gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
	gspca_frame_add(gspca_dev, INTER_PACKET,
		data + 16*DT_COLS,
		len  - 32*DT_COLS); /* skip first & last 16 lines */
	gspca_frame_add(gspca_dev, LAST_PACKET,  NULL, 0);

	return;
}

/* -- controls: exposure and gain -- */

static void dtcs033_setexposure(struct gspca_dev *gspca_dev,
			s32 expo, s32 gain)
{
	/* gain [dB] encoding */
	u16 sGain   = (u16)gain;
	u16 gainVal = 224+(sGain-14)*(768-224)/(33-14);
	u16 wIndex =  0x0100|(0x00FF&gainVal);
	u16 wValue = (0xFF00&gainVal)>>8;

	/* exposure time [msec] encoding */
	u16 sXTime   = (u16)expo;
	u16 xtimeVal = (524*(150-(sXTime-1)))/150;

	const u8 bRequestType =
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	const u8 bRequest = 0x18;

	reg_rw(gspca_dev,
		bRequestType, bRequest, wValue, wIndex, 0);
	if (gspca_dev->usb_err < 0)
		gspca_err(gspca_dev, "usb error in setexposure(gain) sequence\n");

	reg_rw(gspca_dev,
		bRequestType, bRequest, (xtimeVal<<4), 0x6300, 0);
	if (gspca_dev->usb_err < 0)
		gspca_err(gspca_dev, "usb error in setexposure(time) sequence\n");
}

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;/* !! must be the first item */

	/* exposure & gain controls */
	struct {
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
	};
};

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
	container_of(ctrl->handler,
		struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->usb_err = 0;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dtcs033_setexposure(gspca_dev,
				ctrl->val, sd->gain->val);
		break;
	case V4L2_CID_GAIN:
		dtcs033_setexposure(gspca_dev,
				sd->exposure->val, ctrl->val);
		break;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

static int dtcs033_init_controls(struct gspca_dev *gspca_dev)
{
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 2);
	/*                               min max step default */
	sd->exposure = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_EXPOSURE,
				1,  150,  1,  75);/* [msec] */
	sd->gain     = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_GAIN,
				14,  33,  1,  24);/* [dB] */
	if (hdl->error) {
		gspca_err(gspca_dev, "Could not initialize controls: %d\n",
			  hdl->error);
		return hdl->error;
	}

	v4l2_ctrl_cluster(2, &sd->exposure);
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name     = MODULE_NAME,
	.config   = sd_config,
	.init     = sd_init,
	.start    = dtcs033_start,
	.stopN    = dtcs033_stopN,
	.pkt_scan = dtcs033_pkt_scan,
	.init_controls = dtcs033_init_controls,
};

/* -- module initialisation -- */

static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0547, 0x7303)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* device connect */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id,
			&sd_desc, sizeof(struct sd),
			THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name       = MODULE_NAME,
	.id_table   = device_table,
	.probe      = sd_probe,
	.disconnect   = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend      = gspca_suspend,
	.resume       = gspca_resume,
	.reset_resume = gspca_resume,
#endif
};
module_usb_driver(sd_driver);


/* ---------------------------------------------------------
 USB requests to start/stop the camera [USB 2.0 spec Ch.9].

 bRequestType :
 0x40 =  USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
 0xC0 =  USB_DIR_IN  | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
*/
static const struct dtcs033_usb_requests dtcs033_start_reqs[] = {
/* -- bRequest,wValue,wIndex,wLength */
{ 0x40, 0x01, 0x0001, 0x000F, 0x0000 },
{ 0x40, 0x01, 0x0000, 0x000F, 0x0000 },
{ 0x40, 0x01, 0x0001, 0x000F, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x7F00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1001, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0004, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x7F01, 0x0000 },
{ 0x40, 0x18, 0x30E0, 0x0009, 0x0000 },
{ 0x40, 0x18, 0x0500, 0x012C, 0x0000 },
{ 0x40, 0x18, 0x0380, 0x0200, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x035C, 0x0000 },
{ 0x40, 0x18, 0x05C0, 0x0438, 0x0000 },
{ 0x40, 0x18, 0x0440, 0x0500, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0668, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0700, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0800, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0900, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0A00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0B00, 0x0000 },
{ 0x40, 0x18, 0x30E0, 0x6009, 0x0000 },
{ 0x40, 0x18, 0x0500, 0x612C, 0x0000 },
{ 0x40, 0x18, 0x2090, 0x6274, 0x0000 },
{ 0x40, 0x18, 0x05C0, 0x6338, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6400, 0x0000 },
{ 0x40, 0x18, 0x05C0, 0x6538, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6600, 0x0000 },
{ 0x40, 0x18, 0x0680, 0x6744, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6800, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6900, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6A00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6B00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6C00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6D00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6E00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x808C, 0x0000 },
{ 0x40, 0x18, 0x0010, 0x8101, 0x0000 },
{ 0x40, 0x18, 0x30E0, 0x8200, 0x0000 },
{ 0x40, 0x18, 0x0810, 0x832C, 0x0000 },
{ 0x40, 0x18, 0x0680, 0x842B, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x8500, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x8600, 0x0000 },
{ 0x40, 0x18, 0x0280, 0x8715, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x880C, 0x0000 },
{ 0x40, 0x18, 0x0010, 0x8901, 0x0000 },
{ 0x40, 0x18, 0x30E0, 0x8A00, 0x0000 },
{ 0x40, 0x18, 0x0810, 0x8B2C, 0x0000 },
{ 0x40, 0x18, 0x0680, 0x8C2B, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x8D00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x8E00, 0x0000 },
{ 0x40, 0x18, 0x0280, 0x8F15, 0x0000 },
{ 0x40, 0x18, 0x0010, 0xD040, 0x0000 },
{ 0x40, 0x18, 0x0000, 0xD100, 0x0000 },
{ 0x40, 0x18, 0x00B0, 0xD20A, 0x0000 },
{ 0x40, 0x18, 0x0000, 0xD300, 0x0000 },
{ 0x40, 0x18, 0x30E2, 0xD40D, 0x0000 },
{ 0x40, 0x18, 0x0001, 0xD5C0, 0x0000 },
{ 0x40, 0x18, 0x00A0, 0xD60A, 0x0000 },
{ 0x40, 0x18, 0x0000, 0xD700, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x7F00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1501, 0x0000 },
{ 0x40, 0x18, 0x0001, 0x01FF, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0200, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0304, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1101, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1201, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1300, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1400, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1601, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1800, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1900, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1A00, 0x0000 },
{ 0x40, 0x18, 0x2000, 0x1B00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1C00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x2100, 0x0000 },
{ 0x40, 0x18, 0x00C0, 0x228E, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x3001, 0x0000 },
{ 0x40, 0x18, 0x0010, 0x3101, 0x0000 },
{ 0x40, 0x18, 0x0008, 0x3301, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x3400, 0x0000 },
{ 0x40, 0x18, 0x0012, 0x3549, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x3620, 0x0000 },
{ 0x40, 0x18, 0x0001, 0x3700, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x4000, 0x0000 },
{ 0x40, 0x18, 0xFFFF, 0x41FF, 0x0000 },
{ 0x40, 0x18, 0xFFFF, 0x42FF, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x500F, 0x0000 },
{ 0x40, 0x18, 0x2272, 0x5108, 0x0000 },
{ 0x40, 0x18, 0x2272, 0x5208, 0x0000 },
{ 0x40, 0x18, 0xFFFF, 0x53FF, 0x0000 },
{ 0x40, 0x18, 0xFFFF, 0x54FF, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6000, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6102, 0x0000 },
{ 0x40, 0x18, 0x0010, 0x6214, 0x0000 },
{ 0x40, 0x18, 0x0C80, 0x6300, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6401, 0x0000 },
{ 0x40, 0x18, 0x0680, 0x6551, 0x0000 },
{ 0x40, 0x18, 0xFFFF, 0x66FF, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6702, 0x0000 },
{ 0x40, 0x18, 0x0010, 0x6800, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6900, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6A00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6B00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6C00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6D01, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6E00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x6F00, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x7000, 0x0000 },
{ 0x40, 0x18, 0x0001, 0x7118, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x2001, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1101, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1301, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1300, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1501, 0x0000 },
{ 0xC0, 0x11, 0x0000, 0x24C0, 0x0003 },
{ 0x40, 0x18, 0x0000, 0x3000, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x3620, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x1501, 0x0000 },
{ 0x40, 0x18, 0x0010, 0x6300, 0x0000 },
{ 0x40, 0x18, 0x0002, 0x01F0, 0x0000 },
{ 0x40, 0x01, 0x0003, 0x000F, 0x0000 }
};

static const struct dtcs033_usb_requests dtcs033_stop_reqs[] = {
/* -- bRequest,wValue,wIndex,wLength */
{ 0x40, 0x01, 0x0001, 0x000F, 0x0000 },
{ 0x40, 0x01, 0x0000, 0x000F, 0x0000 },
{ 0x40, 0x18, 0x0000, 0x0003, 0x0000 }
};
static int dtcs033_start(struct gspca_dev *gspca_dev)
{
	return reg_reqs(gspca_dev, dtcs033_start_reqs,
		ARRAY_SIZE(dtcs033_start_reqs));
}

static void dtcs033_stopN(struct gspca_dev *gspca_dev)
{
	reg_reqs(gspca_dev, dtcs033_stop_reqs,
		ARRAY_SIZE(dtcs033_stop_reqs));
	return;
}
