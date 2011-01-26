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

/* Functions to get and set a control value */
#define SD_SETGET(thename) \
static int sd_set_##thename(struct gspca_dev *gspca_dev, s32 val)\
{\
	struct sd *sd = (struct sd *) gspca_dev;\
\
	sd->vcur.thename = val;\
	if (gspca_dev->streaming)\
		sd->waitSet = 1;\
	return 0;\
} \
static int sd_get_##thename(struct gspca_dev *gspca_dev, s32 *val)\
{\
	struct sd *sd = (struct sd *) gspca_dev;\
\
	*val = sd->vcur.thename;\
	return 0;\
}

SD_SETGET(mirror)
SD_SETGET(flip)
SD_SETGET(AC50Hz)
SD_SETGET(backlight)
SD_SETGET(brightness)
SD_SETGET(gamma)
SD_SETGET(hue)
SD_SETGET(saturation)
SD_SETGET(sharpness)
SD_SETGET(whitebal)
SD_SETGET(contrast)

#define GL860_NCTRLS 11

/* control table */
static struct ctrl sd_ctrls_mi1320[GL860_NCTRLS];
static struct ctrl sd_ctrls_mi2020[GL860_NCTRLS];
static struct ctrl sd_ctrls_ov2640[GL860_NCTRLS];
static struct ctrl sd_ctrls_ov9655[GL860_NCTRLS];

#define SET_MY_CTRL(theid, \
	thetype, thelabel, thename) \
	if (sd->vmax.thename != 0) {\
		sd_ctrls[nCtrls].qctrl.id   = theid;\
		sd_ctrls[nCtrls].qctrl.type = thetype;\
		strcpy(sd_ctrls[nCtrls].qctrl.name, thelabel);\
		sd_ctrls[nCtrls].qctrl.minimum = 0;\
		sd_ctrls[nCtrls].qctrl.maximum = sd->vmax.thename;\
		sd_ctrls[nCtrls].qctrl.default_value = sd->vcur.thename;\
		sd_ctrls[nCtrls].qctrl.step = \
			(sd->vmax.thename < 16) ? 1 : sd->vmax.thename/16;\
		sd_ctrls[nCtrls].set = sd_set_##thename;\
		sd_ctrls[nCtrls].get = sd_get_##thename;\
		nCtrls++;\
	}

static int gl860_build_control_table(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct ctrl *sd_ctrls;
	int nCtrls = 0;

	if (_MI1320_)
		sd_ctrls = sd_ctrls_mi1320;
	else if (_MI2020_)
		sd_ctrls = sd_ctrls_mi2020;
	else if (_OV2640_)
		sd_ctrls = sd_ctrls_ov2640;
	else if (_OV9655_)
		sd_ctrls = sd_ctrls_ov9655;
	else
		return 0;

	memset(sd_ctrls, 0, GL860_NCTRLS * sizeof(struct ctrl));

	SET_MY_CTRL(V4L2_CID_BRIGHTNESS,
		V4L2_CTRL_TYPE_INTEGER, "Brightness", brightness)
	SET_MY_CTRL(V4L2_CID_SHARPNESS,
		V4L2_CTRL_TYPE_INTEGER, "Sharpness", sharpness)
	SET_MY_CTRL(V4L2_CID_CONTRAST,
		V4L2_CTRL_TYPE_INTEGER, "Contrast", contrast)
	SET_MY_CTRL(V4L2_CID_GAMMA,
		V4L2_CTRL_TYPE_INTEGER, "Gamma", gamma)
	SET_MY_CTRL(V4L2_CID_HUE,
		V4L2_CTRL_TYPE_INTEGER, "Palette", hue)
	SET_MY_CTRL(V4L2_CID_SATURATION,
		V4L2_CTRL_TYPE_INTEGER, "Saturation", saturation)
	SET_MY_CTRL(V4L2_CID_WHITE_BALANCE_TEMPERATURE,
		V4L2_CTRL_TYPE_INTEGER, "White Bal.", whitebal)
	SET_MY_CTRL(V4L2_CID_BACKLIGHT_COMPENSATION,
		V4L2_CTRL_TYPE_INTEGER, "Backlight" , backlight)

	SET_MY_CTRL(V4L2_CID_HFLIP,
		V4L2_CTRL_TYPE_BOOLEAN, "Mirror", mirror)
	SET_MY_CTRL(V4L2_CID_VFLIP,
		V4L2_CTRL_TYPE_BOOLEAN, "Flip", flip)
	SET_MY_CTRL(V4L2_CID_POWER_LINE_FREQUENCY,
		V4L2_CTRL_TYPE_BOOLEAN, "AC power 50Hz", AC50Hz)

	return nCtrls;
}

/*==================== sud-driver structure initialisation =================*/

static const struct sd_desc sd_desc_mi1320 = {
	.name        = MODULE_NAME,
	.ctrls       = sd_ctrls_mi1320,
	.nctrls      = GL860_NCTRLS,
	.config      = sd_config,
	.init        = sd_init,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

static const struct sd_desc sd_desc_mi2020 = {
	.name        = MODULE_NAME,
	.ctrls       = sd_ctrls_mi2020,
	.nctrls      = GL860_NCTRLS,
	.config      = sd_config,
	.init        = sd_init,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

static const struct sd_desc sd_desc_ov2640 = {
	.name        = MODULE_NAME,
	.ctrls       = sd_ctrls_ov2640,
	.nctrls      = GL860_NCTRLS,
	.config      = sd_config,
	.init        = sd_init,
	.isoc_init   = sd_isoc_init,
	.start       = sd_start,
	.stop0       = sd_stop0,
	.pkt_scan    = sd_pkt_scan,
	.dq_callback = sd_callback,
};

static const struct sd_desc sd_desc_ov9655 = {
	.name        = MODULE_NAME,
	.ctrls       = sd_ctrls_ov9655,
	.nctrls      = GL860_NCTRLS,
	.config      = sd_config,
	.init        = sd_init,
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
	gspca_dev->nbalt = 4;

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
	gl860_build_control_table(gspca_dev);

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
	struct gspca_dev *gspca_dev;
	s32 ret;

	ret = gspca_dev_probe(intf, id,
			&sd_desc_mi1320, sizeof(struct sd), THIS_MODULE);

	if (ret >= 0) {
		gspca_dev = usb_get_intfdata(intf);

		PDEBUG(D_PROBE,
			"Camera is now controlling video device %s",
			video_device_node_name(&gspca_dev->vdev));
	}

	return ret;
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
#endif
};

/*====================== Init and Exit module functions ====================*/

static int __init sd_mod_init(void)
{
	PDEBUG(D_PROBE, "driver startup - version %s", DRIVER_VERSION);

	if (usb_register(&sd_driver) < 0)
		return -1;
	return 0;
}

static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);

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
		err("ctrl transfer failed %4d "
			"[p%02x r%d v%04x i%04x len%d]",
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
