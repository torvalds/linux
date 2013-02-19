/* GSPCA subdrivers for Genesys Logic webcams with the GL860 chip
 * Subdriver core
 *
 * 2009/09/24 Olivier Lorin <o.lorin@laposte.net>
 * GSPCA by Jean-Francois Moine <http://moinejf.free.fr>
 * Thanks BUGabundo and Malmostoso for your amazing help!
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "gspca.h"
#include "gl860.h"

MODULE_AUTHOR("Olivier Lorin <o.lorin@laposte.net>");
MODULE_DESCRIPTION("Genesys Logic USB PC Camera Driver");
MODULE_LICENSE("GPL");

/*======================== static function declarations ====================*/

static void (*dev_init_settings)(struct gspca_dev *gspca_dev);

static int  sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id);
static int  sd_init(struct gspca_dev *gspca_dev);
static int  sd_isoc_init(struct gspca_dev *gspca_dev);
static int  sd_start(struct gspca_dev *gspca_dev);
static void sd_stop0(struct gspca_dev *gspca_dev);
static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data, int len);
static void sd_callback(struct gspca_dev *gspca_dev);

static int gl860_guess_sensor(struct gspca_dev *gspca_dev,
				u16 vendor_id, u16 product_id);

/*============================ driver options ==============================*/

static s32 AC50Hz = 0xff;
module_param(AC50Hz, int, 0644);
MODULE_PARM_DESC(AC50Hz, " Does AC power frequency is 50Hz? (0/1)");

static char sensor[7];
module_param_string(sensor, sensor, sizeof(sensor), 0644);
MODULE_PARM_DESC(sensor,
		" Driver sensor ('MI1320'/'MI2020'/'OV9655'/'OV2640')");

/*============================ webcam controls =============================*/

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *) gspca_dev;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		sd->vcur.brightness = ctrl->val;
		break;
	case V4L2_CID_CONTRAST:
		sd->vcur.contrast = ctrl->val;
		break;
	case V4L2_CID_SATURATION:
		sd->vcur.saturation = ctrl->val;
		break;
	case V4L2_CID_HUE:
		sd->vcur.hue = ctrl->val;
		break;
	case V4L2_CID_GAMMA:
		sd->vcur.gamma = ctrl->val;
		break;
	case V4L2_CID_HFLIP:
		sd->vcur.mirror = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		sd->vcur.flip = ctrl->val;
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		sd->vcur.AC50Hz = ctrl->val;
		break;
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		sd->vcur.whitebal = ctrl->val;
		break;
	case V4L2_CID_SHARPNESS:
		sd->vcur.sharpness = ctrl->val;
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		sd->vcur.backlight = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	if (gspca_dev->streaming)
		sd->waitSet = 1;

	return 0;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 11);

	if (sd->vmax.brightness)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_BRIGHTNESS,
				  0, sd->vmax.brightness, 1,
				  sd->vcur.brightness);

	if (sd->vmax.contrast)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_CONTRAST,
				  0, sd->vmax.contrast, 1,
				  sd->vcur.contrast);

	if (sd->vmax.saturation)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_SATURATION,
				  0, sd->vmax.saturation, 1,
				  sd->vcur.saturation);

	if (sd->vmax.hue)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_HUE,
				  0, sd->vmax.hue, 1, sd->vcur.hue);

	if (sd->vmax.gamma)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_GAMMA,
				  0, sd->vmax.gamma, 1, sd->vcur.gamma);

	if (sd->vmax.mirror)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_HFLIP,
				  0, sd->vmax.mirror, 1, sd->vcur.mirror);

	if (sd->vmax.flip)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_VFLIP,
				  0, sd->vmax.flip, 1, sd->vcur.flip);

	if (sd->vmax.AC50Hz)
		v4l2_ctrl_new_std_menu(hdl, &sd_ctrl_ops,
				  V4L2_CID_POWER_LINE_FREQUENCY,
				  sd->vmax.AC50Hz, 0, sd->vcur.AC50Hz);

	if (sd->vmax.whitebal)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				  V4L2_CID_WHITE_BALANCE_TEMPERATURE,
				  0, sd->vmax.whitebal, 1, sd->vcur.whitebal);

	if (sd->vmax.sharpness)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops, V4L2_CID_SHARPNESS,
				  0, sd->vmax.sharpness, 1,
				  sd->vcur.sharpness);

	if (sd->vmax.backlight)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				  V4L2_CID_BACKLIGHT_COMPENSATION,
				  0, sd->vmax.backlight, 1,
				  sd->vcur.backlight);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}

	return 0;
}

/*==================== sud-driver structure initialisation =================*/

static const struct sd_desc sd_desc_mi1320 = {
	.name        = MODULE_NAME,
	.config      = sd_config,
	.init        = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

static const struct sd_desc sd_desc_mi2020 = {
	.name        = MODULE_NAME,
	.config      = sd_config,
	.init        = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

static const struct sd_desc sd_desc_ov2640 = {
	.name        = MODULE_NAME,
	.config      = sd_config,
	.init        = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

static const struct sd_desc sd_desc_ov9655 = {
	.name        = MODULE_NAME,
	.config      = sd_config,
	.init        = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

/*=========================== sub-driver image sizes =======================*/

static struct v4l2_pix_format mi2020_mode[] = {
	{ 640,  480, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
	{ 800,  598, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 800,
		.sizeimage = 800 * 598,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1
	},
	{1280, 1024, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 1024,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2
	},
	{1600, 1198, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 1600,
		.sizeimage = 1600 * 1198,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3
	},
};

static struct v4l2_pix_format ov2640_mode[] = {
	{ 640,  480, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
	{ 800,  600, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 800,
		.sizeimage = 800 * 600,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1
	},
	{1280,  960, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2
	},
	{1600, 1200, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 1600,
		.sizeimage = 1600 * 1200,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3
	},
};

static struct v4l2_pix_format mi1320_mode[] = {
	{ 640,  480, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
	{ 800,  600, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 800,
		.sizeimage = 800 * 600,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1
	},
	{1280,  960, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2
	},
};

static struct v4l2_pix_format ov9655_mode[] = {
	{ 640,  480, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
	{1280,  960, V4L2_PIX_FMT_SGBRG8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1
	},
};

/*========================= sud-driver functions ===========================*/

/* This function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
	u16 vendor_id, product_id;

	/* Get USB VendorID and ProductID */
	vendor_id  = id->idVendor;
	product_id = id->idProduct;

	sd->nbRightUp = 1;
	sd->nbIm = -1;

	sd->sensor = 0xff;
	if (strcmp(sensor, "MI1320") == 0)
		sd->sensor = ID_MI1320;
	else if (strcmp(sensor, "OV2640") == 0)
		sd->sensor = ID_OV2640;
	else if (strcmp(sensor, "OV9655") == 0)
		sd->sensor = ID_OV9655;
	else if (strcmp(sensor, "MI2020") == 0)
		sd->sensor = ID_MI2020;

	/* Get sensor and set the suitable init/start/../stop functions */
	if (gl860_guess_sensor(gspca_dev, vendor_id, product_id) == -1)
		return -1;

	cam = &gspca_dev->cam;

	switch (sd->sensor) {
	case ID_MI1320:
		gspca_dev->sd_desc = &sd_desc_mi1320;
		cam->cam_mode = mi1320_mode;
		cam->nmodes = ARRAY_SIZE(mi1320_mode);
		dev_init_settings   = mi1320_init_settings;
		break;

	case ID_MI2020:
		gspca_dev->sd_desc = &sd_desc_mi2020;
		cam->cam_mode = mi2020_mode;
		cam->nmodes = ARRAY_SIZE(mi2020_mode);
		dev_init_settings   = mi2020_init_settings;
		break;

	case ID_OV2640:
		gspca_dev->sd_desc = &sd_desc_ov2640;
		cam->cam_mode = ov2640_mode;
		cam->nmodes = ARRAY_SIZE(ov2640_mode);
		dev_init_settings   = ov2640_init_settings;
		break;

	case ID_OV9655:
		gspca_dev->sd_desc = &sd_desc_ov9655;
		cam->cam_mode = ov9655_mode;
		cam->nmodes = ARRAY_SIZE(ov9655_mode);
		dev_init_settings   = ov9655_init_settings;
		break;
	}

	dev_init_settings(gspca_dev);
	if (AC50Hz != 0xff)
		((struct sd *) gspca_dev)->vcur.AC50Hz = AC50Hz;

	return 0;
}

/* This function is called at probe time after sd_config */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return sd->dev_init_at_startup(gspca_dev);
}

/* This function is called before to choose the alt setting */
static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return sd->dev_configure_alt(gspca_dev);
}

/* This function is called to start the webcam */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	return sd->dev_init_pre_alt(gspca_dev);
}

/* This function is called to stop the webcam */
static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!sd->gspca_dev.present)
		return;

	return sd->dev_post_unset_alt(gspca_dev);
}

/* This function is called when an image is being received */
static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	static s32 nSkipped;

	s32 mode = (s32) gspca_dev->curr_mode;
	s32 nToSkip =
		sd->swapRB * (gspca_dev->cam.cam_mode[mode].bytesperline + 1);

	/* Test only against 0202h, so endianess does not matter */
	switch (*(s16 *) data) {
	case 0x0202:		/* End of frame, start a new one */
		gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
		nSkipped = 0;
		if (sd->nbIm >= 0 && sd->nbIm < 10)
			sd->nbIm++;
		gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
		break;

	default:
		data += 2;
		len  -= 2;
		if (nSkipped + len <= nToSkip)
			nSkipped += len;
		else {
			if (nSkipped < nToSkip && nSkipped + len > nToSkip) {
				data += nToSkip - nSkipped;
				len  -= nToSkip - nSkipped;
				nSkipped = nToSkip + 1;
			}
			gspca_frame_add(gspca_dev,
				INTER_PACKET, data, len);
		}
		break;
	}
}

/* This function is called when an image has been read */
/* This function is used to monitor webcam orientation */
static void sd_callback(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!_OV9655_) {
		u8 state;
		u8 upsideDown;

		/* Probe sensor orientation */
		ctrl_in(gspca_dev, 0xc0, 2, 0x0000, 0x0000, 1, (void *)&state);

		/* C8/40 means upside-down (looking backwards) */
		/* D8/50 means right-up (looking onwards) */
		upsideDown = (state == 0xc8 || state == 0x40);

		if (upsideDown && sd->nbRightUp > -4) {
			if (sd->nbRightUp > 0)
				sd->nbRightUp = 0;
			if (sd->nbRightUp == -3) {
				sd->mirrorMask = 1;
				sd->waitSet = 1;
			}
			sd->nbRightUp--;
		}
		if (!upsideDown && sd->nbRightUp < 4) {
			if (sd->nbRightUp  < 0)
				sd->nbRightUp = 0;
			if (sd->nbRightUp == 3) {
				sd->mirrorMask = 0;
				sd->waitSet = 1;
			}
			sd->nbRightUp++;
		}
	}

	if (sd->waitSet)
		sd->dev_camera_settings(gspca_dev);
}

/*=================== USB driver structure initialisation ==================*/

static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x05e3, 0x0503)},
	{USB_DEVICE(0x05e3, 0xf191)},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

static int sd_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id,
			&sd_desc_mi1320, sizeof(struct sd), THIS_MODULE);
}

static void sd_disconnect(struct usb_interface *intf)
{
	gspca_disconnect(intf);
}

static struct usb_driver sd_driver = {
	.name       = MODULE_NAME,
	.id_table   = device_table,
	.probe      = sd_probe,
	.disconnect = sd_disconnect,
#ifdef CONFIG_PM
	.suspend    = gspca_suspend,
	.resume     = gspca_resume,
	.reset_resume = gspca_resume,
#endif
};

/*====================== Init and Exit module functions ====================*/

module_usb_driver(sd_driver);

/*==========================================================================*/

int gl860_RTx(struct gspca_dev *gspca_dev,
		unsigned char pref, u32 req, u16 val, u16 index,
		s32 len, void *pdata)
{
	struct usb_device *udev = gspca_dev->dev;
	s32 r = 0;

	if (pref == 0x40) { /* Send */
		if (len > 0) {
			memcpy(gspca_dev->usb_buf, pdata, len);
			r = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
					req, pref, val, index,
					gspca_dev->usb_buf,
					len, 400 + 200 * (len > 1));
		} else {
			r = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
					req, pref, val, index, NULL, len, 400);
		}
	} else { /* Receive */
		if (len > 0) {
			r = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
					req, pref, val, index,
					gspca_dev->usb_buf,
					len, 400 + 200 * (len > 1));
			memcpy(pdata, gspca_dev->usb_buf, len);
		} else {
			r = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
					req, pref, val, index, NULL, len, 400);
		}
	}

	if (r < 0)
		pr_err("ctrl transfer failed %4d [p%02x r%d v%04x i%04x len%d]\n",
		       r, pref, req, val, index, len);
	else if (len > 1 && r < len)
		PDEBUG(D_ERR, "short ctrl transfer %d/%d", r, len);

	msleep(1);

	return r;
}

int fetch_validx(struct gspca_dev *gspca_dev, struct validx *tbl, int len)
{
	int n;

	for (n = 0; n < len; n++) {
		if (tbl[n].idx != 0xffff)
			ctrl_out(gspca_dev, 0x40, 1, tbl[n].val,
					tbl[n].idx, 0, NULL);
		else if (tbl[n].val == 0xffff)
			break;
		else
			msleep(tbl[n].val);
	}
	return n;
}

int keep_on_fetching_validx(struct gspca_dev *gspca_dev, struct validx *tbl,
				int len, int n)
{
	while (++n < len) {
		if (tbl[n].idx != 0xffff)
			ctrl_out(gspca_dev, 0x40, 1, tbl[n].val, tbl[n].idx,
					0, NULL);
		else if (tbl[n].val == 0xffff)
			break;
		else
			msleep(tbl[n].val);
	}
	return n;
}

void fetch_idxdata(struct gspca_dev *gspca_dev, struct idxdata *tbl, int len)
{
	int n;

	for (n = 0; n < len; n++) {
		if (memcmp(tbl[n].data, "\xff\xff\xff", 3) != 0)
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, tbl[n].idx,
					3, tbl[n].data);
		else
			msleep(tbl[n].idx);
	}
}

static int gl860_guess_sensor(struct gspca_dev *gspca_dev,
				u16 vendor_id, u16 product_id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 probe, nb26, nb96, nOV, ntry;

	if (product_id == 0xf191)
		sd->sensor = ID_MI1320;

	if (sd->sensor == 0xff) {
		ctrl_in(gspca_dev, 0xc0, 2, 0x0000, 0x0004, 1, &probe);
		ctrl_in(gspca_dev, 0xc0, 2, 0x0000, 0x0004, 1, &probe);

		ctrl_out(gspca_dev, 0x40, 1, 0x0000, 0x0000, 0, NULL);
		msleep(3);
		ctrl_out(gspca_dev, 0x40, 1, 0x0010, 0x0010, 0, NULL);
		msleep(3);
		ctrl_out(gspca_dev, 0x40, 1, 0x0008, 0x00c0, 0, NULL);
		msleep(3);
		ctrl_out(gspca_dev, 0x40, 1, 0x0001, 0x00c1, 0, NULL);
		msleep(3);
		ctrl_out(gspca_dev, 0x40, 1, 0x0001, 0x00c2, 0, NULL);
		msleep(3);
		ctrl_out(gspca_dev, 0x40, 1, 0x0020, 0x0006, 0, NULL);
		msleep(3);
		ctrl_out(gspca_dev, 0x40, 1, 0x006a, 0x000d, 0, NULL);
		msleep(56);

		PDEBUG(D_PROBE, "probing for sensor MI2020 or OVXXXX");
		nOV = 0;
		for (ntry = 0; ntry < 4; ntry++) {
			ctrl_out(gspca_dev, 0x40, 1, 0x0040, 0x0000, 0, NULL);
			msleep(3);
			ctrl_out(gspca_dev, 0x40, 1, 0x0063, 0x0006, 0, NULL);
			msleep(3);
			ctrl_out(gspca_dev, 0x40, 1, 0x7a00, 0x8030, 0, NULL);
			msleep(10);
			ctrl_in(gspca_dev, 0xc0, 2, 0x7a00, 0x8030, 1, &probe);
			PDEBUG(D_PROBE, "probe=0x%02x", probe);
			if (probe == 0xff)
				nOV++;
		}

		if (nOV) {
			PDEBUG(D_PROBE, "0xff -> OVXXXX");
			PDEBUG(D_PROBE, "probing for sensor OV2640 or OV9655");

			nb26 = nb96 = 0;
			for (ntry = 0; ntry < 4; ntry++) {
				ctrl_out(gspca_dev, 0x40, 1, 0x0040, 0x0000,
						0, NULL);
				msleep(3);
				ctrl_out(gspca_dev, 0x40, 1, 0x6000, 0x800a,
						0, NULL);
				msleep(10);

				/* Wait for 26(OV2640) or 96(OV9655) */
				ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x800a,
						1, &probe);

				if (probe == 0x26 || probe == 0x40) {
					PDEBUG(D_PROBE,
						"probe=0x%02x -> OV2640",
						probe);
					sd->sensor = ID_OV2640;
					nb26 += 4;
					break;
				}
				if (probe == 0x96 || probe == 0x55) {
					PDEBUG(D_PROBE,
						"probe=0x%02x -> OV9655",
						probe);
					sd->sensor = ID_OV9655;
					nb96 += 4;
					break;
				}
				PDEBUG(D_PROBE, "probe=0x%02x", probe);
				if (probe == 0x00)
					nb26++;
				if (probe == 0xff)
					nb96++;
				msleep(3);
			}
			if (nb26 < 4 && nb96 < 4)
				return -1;
		} else {
			PDEBUG(D_PROBE, "Not any 0xff -> MI2020");
			sd->sensor = ID_MI2020;
		}
	}

	if (_MI1320_) {
		PDEBUG(D_PROBE, "05e3:f191 sensor MI1320 (1.3M)");
	} else if (_MI2020_) {
		PDEBUG(D_PROBE, "05e3:0503 sensor MI2020 (2.0M)");
	} else if (_OV9655_) {
		PDEBUG(D_PROBE, "05e3:0503 sensor OV9655 (1.3M)");
	} else if (_OV2640_) {
		PDEBUG(D_PROBE, "05e3:0503 sensor OV2640 (2.0M)");
	} else {
		PDEBUG(D_PROBE, "***** Unknown sensor *****");
		return -1;
	}

	return 0;
}
