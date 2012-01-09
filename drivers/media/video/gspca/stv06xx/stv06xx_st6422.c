/*
 * Support for the sensor part which is integrated (I think) into the
 * st6422 stv06xx alike bridge, as its integrated there are no i2c writes
 * but instead direct bridge writes.
 *
 * Copyright (c) 2009 Hans de Goede <hdegoede@redhat.com>
 *
 * Strongly based on qc-usb-messenger, which is:
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "stv06xx_st6422.h"

/* controls */
enum e_ctrl {
	BRIGHTNESS,
	CONTRAST,
	GAIN,
	EXPOSURE,
	NCTRLS		/* number of controls */
};

/* sensor settings */
struct st6422_settings {
	struct gspca_ctrl ctrls[NCTRLS];
};

static struct v4l2_pix_format st6422_mode[] = {
	/* Note we actually get 124 lines of data, of which we skip the 4st
	   4 as they are garbage */
	{
		162,
		120,
		V4L2_PIX_FMT_SGRBG8,
		V4L2_FIELD_NONE,
		.sizeimage = 162 * 120,
		.bytesperline = 162,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1
	},
	/* Note we actually get 248 lines of data, of which we skip the 4st
	   4 as they are garbage, and we tell the app it only gets the
	   first 240 of the 244 lines it actually gets, so that it ignores
	   the last 4. */
	{
		324,
		240,
		V4L2_PIX_FMT_SGRBG8,
		V4L2_FIELD_NONE,
		.sizeimage = 324 * 244,
		.bytesperline = 324,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
};

/* V4L2 controls supported by the driver */
static void st6422_set_brightness(struct gspca_dev *gspca_dev);
static void st6422_set_contrast(struct gspca_dev *gspca_dev);
static void st6422_set_gain(struct gspca_dev *gspca_dev);
static void st6422_set_exposure(struct gspca_dev *gspca_dev);

static const struct ctrl st6422_ctrl[NCTRLS] = {
[BRIGHTNESS] = {
		{
			.id		= V4L2_CID_BRIGHTNESS,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Brightness",
			.minimum	= 0,
			.maximum	= 31,
			.step		= 1,
			.default_value  = 3
		},
		.set_control = st6422_set_brightness
	},
[CONTRAST] = {
		{
			.id		= V4L2_CID_CONTRAST,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Contrast",
			.minimum	= 0,
			.maximum	= 15,
			.step		= 1,
			.default_value  = 11
		},
		.set_control = st6422_set_contrast
	},
[GAIN] = {
		{
			.id		= V4L2_CID_GAIN,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Gain",
			.minimum	= 0,
			.maximum	= 255,
			.step		= 1,
			.default_value  = 64
		},
		.set_control = st6422_set_gain
	},
[EXPOSURE] = {
		{
			.id		= V4L2_CID_EXPOSURE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Exposure",
			.minimum	= 0,
#define EXPOSURE_MAX 1023
			.maximum	= EXPOSURE_MAX,
			.step		= 1,
			.default_value  = 256
		},
		.set_control = st6422_set_exposure
	},
};

static int st6422_probe(struct sd *sd)
{
	struct st6422_settings *sensor_settings;

	if (sd->bridge != BRIDGE_ST6422)
		return -ENODEV;

	pr_info("st6422 sensor detected\n");

	sensor_settings = kmalloc(sizeof *sensor_settings, GFP_KERNEL);
	if (!sensor_settings)
		return -ENOMEM;

	sd->gspca_dev.cam.cam_mode = st6422_mode;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(st6422_mode);
	sd->gspca_dev.cam.ctrls = sensor_settings->ctrls;
	sd->desc.ctrls = st6422_ctrl;
	sd->desc.nctrls = ARRAY_SIZE(st6422_ctrl);
	sd->sensor_priv = sensor_settings;

	return 0;
}

static int st6422_init(struct sd *sd)
{
	int err = 0, i;

	const u16 st6422_bridge_init[][2] = {
		{ STV_ISO_ENABLE, 0x00 }, /* disable capture */
		{ 0x1436, 0x00 },
		{ 0x1432, 0x03 },	/* 0x00-0x1F brightness */
		{ 0x143a, 0xf9 },	/* 0x00-0x0F contrast */
		{ 0x0509, 0x38 },	/* R */
		{ 0x050a, 0x38 },	/* G */
		{ 0x050b, 0x38 },	/* B */
		{ 0x050c, 0x2a },
		{ 0x050d, 0x01 },


		{ 0x1431, 0x00 },	/* 0x00-0x07 ??? */
		{ 0x1433, 0x34 },	/* 160x120, 0x00-0x01 night filter */
		{ 0x1438, 0x18 },	/* 640x480 */
/* 18 bayes */
/* 10 compressed? */

		{ 0x1439, 0x00 },
/* anti-noise?  0xa2 gives a perfect image */

		{ 0x143b, 0x05 },
		{ 0x143c, 0x00 },	/* 0x00-0x01 - ??? */


/* shutter time 0x0000-0x03FF */
/* low value  give good picures on moving objects (but requires much light) */
/* high value gives good picures in darkness (but tends to be overexposed) */
		{ 0x143e, 0x01 },
		{ 0x143d, 0x00 },

		{ 0x1442, 0xe2 },
/* write: 1x1x xxxx */
/* read:  1x1x xxxx */
/*        bit 5 == button pressed and hold if 0 */
/* write 0xe2,0xea */

/* 0x144a */
/* 0x00 init */
/* bit 7 == button has been pressed, but not handled */

/* interrupt */
/* if(urb->iso_frame_desc[i].status == 0x80) { */
/* if(urb->iso_frame_desc[i].status == 0x88) { */

		{ 0x1500, 0xd0 },
		{ 0x1500, 0xd0 },
		{ 0x1500, 0x50 },	/* 0x00 - 0xFF  0x80 == compr ? */

		{ 0x1501, 0xaf },
/* high val-> light area gets darker */
/* low val -> light area gets lighter */
		{ 0x1502, 0xc2 },
/* high val-> light area gets darker */
/* low val -> light area gets lighter */
		{ 0x1503, 0x45 },
/* high val-> light area gets darker */
/* low val -> light area gets lighter */
		{ 0x1505, 0x02 },
/* 2  : 324x248  80352 bytes */
/* 7  : 248x162  40176 bytes */
/* c+f: 162*124  20088 bytes */

		{ 0x150e, 0x8e },
		{ 0x150f, 0x37 },
		{ 0x15c0, 0x00 },
		{ 0x15c3, 0x08 },	/* 0x04/0x14 ... test pictures ??? */


		{ 0x143f, 0x01 },	/* commit settings */

	};

	for (i = 0; i < ARRAY_SIZE(st6422_bridge_init) && !err; i++) {
		err = stv06xx_write_bridge(sd, st6422_bridge_init[i][0],
					       st6422_bridge_init[i][1]);
	}

	return err;
}

static void st6422_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
	kfree(sd->sensor_priv);
}

static int setbrightness(struct sd *sd)
{
	struct st6422_settings *sensor_settings = sd->sensor_priv;

	/* val goes from 0 -> 31 */
	return stv06xx_write_bridge(sd, 0x1432,
			sensor_settings->ctrls[BRIGHTNESS].val);
}

static int setcontrast(struct sd *sd)
{
	struct st6422_settings *sensor_settings = sd->sensor_priv;

	/* Val goes from 0 -> 15 */
	return stv06xx_write_bridge(sd, 0x143a,
			sensor_settings->ctrls[CONTRAST].val | 0xf0);
}

static int setgain(struct sd *sd)
{
	struct st6422_settings *sensor_settings = sd->sensor_priv;
	u8 gain;
	int err;

	gain = sensor_settings->ctrls[GAIN].val;

	/* Set red, green, blue, gain */
	err = stv06xx_write_bridge(sd, 0x0509, gain);
	if (err < 0)
		return err;

	err = stv06xx_write_bridge(sd, 0x050a, gain);
	if (err < 0)
		return err;

	err = stv06xx_write_bridge(sd, 0x050b, gain);
	if (err < 0)
		return err;

	/* 2 mystery writes */
	err = stv06xx_write_bridge(sd, 0x050c, 0x2a);
	if (err < 0)
		return err;

	return stv06xx_write_bridge(sd, 0x050d, 0x01);
}

static int setexposure(struct sd *sd)
{
	struct st6422_settings *sensor_settings = sd->sensor_priv;
	u16 expo;
	int err;

	expo = sensor_settings->ctrls[EXPOSURE].val;
	err = stv06xx_write_bridge(sd, 0x143d, expo & 0xff);
	if (err < 0)
		return err;

	return stv06xx_write_bridge(sd, 0x143e, expo >> 8);
}

static int st6422_start(struct sd *sd)
{
	int err;
	struct cam *cam = &sd->gspca_dev.cam;

	if (cam->cam_mode[sd->gspca_dev.curr_mode].priv)
		err = stv06xx_write_bridge(sd, 0x1505, 0x0f);
	else
		err = stv06xx_write_bridge(sd, 0x1505, 0x02);
	if (err < 0)
		return err;

	err = setbrightness(sd);
	if (err < 0)
		return err;

	err = setcontrast(sd);
	if (err < 0)
		return err;

	err = setexposure(sd);
	if (err < 0)
		return err;

	err = setgain(sd);
	if (err < 0)
		return err;

	/* commit settings */
	err = stv06xx_write_bridge(sd, 0x143f, 0x01);
	return (err < 0) ? err : 0;
}

static int st6422_stop(struct sd *sd)
{
	PDEBUG(D_STREAM, "Halting stream");

	return 0;
}

static void st6422_set_brightness(struct gspca_dev *gspca_dev)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;

	err = setbrightness(sd);

	/* commit settings */
	if (err >= 0)
		err = stv06xx_write_bridge(sd, 0x143f, 0x01);

	gspca_dev->usb_err = err;
}

static void st6422_set_contrast(struct gspca_dev *gspca_dev)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;

	err = setcontrast(sd);

	/* commit settings */
	if (err >= 0)
		err = stv06xx_write_bridge(sd, 0x143f, 0x01);

	gspca_dev->usb_err = err;
}

static void st6422_set_gain(struct gspca_dev *gspca_dev)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;

	err = setgain(sd);

	/* commit settings */
	if (err >= 0)
		err = stv06xx_write_bridge(sd, 0x143f, 0x01);

	gspca_dev->usb_err = err;
}

static void st6422_set_exposure(struct gspca_dev *gspca_dev)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;

	err = setexposure(sd);

	/* commit settings */
	if (err >= 0)
		err = stv06xx_write_bridge(sd, 0x143f, 0x01);

	gspca_dev->usb_err = err;
}
