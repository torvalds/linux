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
static int setbrightness(struct sd *sd, s32 val);
static int setcontrast(struct sd *sd, s32 val);
static int setgain(struct sd *sd, u8 gain);
static int setexposure(struct sd *sd, s16 expo);

static int st6422_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sd *sd = container_of(ctrl->handler, struct sd, ctrl_handler);
	int err = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		err = setbrightness(sd, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		err = setcontrast(sd, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		err = setgain(sd, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		err = setexposure(sd, ctrl->val);
		break;
	}

	/* commit settings */
	if (err >= 0)
		err = stv06xx_write_bridge(sd, 0x143f, 0x01);
	sd->gspca_dev.usb_err = err;
	return err;
}

static const struct v4l2_ctrl_ops st6422_ctrl_ops = {
	.s_ctrl = st6422_s_ctrl,
};

static int st6422_init_controls(struct sd *sd)
{
	struct v4l2_ctrl_handler *hdl = &sd->ctrl_handler;

	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &st6422_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 31, 1, 3);
	v4l2_ctrl_new_std(hdl, &st6422_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 15, 1, 11);
	v4l2_ctrl_new_std(hdl, &st6422_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 1023, 1, 256);
	v4l2_ctrl_new_std(hdl, &st6422_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 64);

	return hdl->error;
}

static int st6422_probe(struct sd *sd)
{
	if (sd->bridge != BRIDGE_ST6422)
		return -ENODEV;

	pr_info("st6422 sensor detected\n");

	sd->gspca_dev.cam.cam_mode = st6422_mode;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(st6422_mode);
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

static int setbrightness(struct sd *sd, s32 val)
{
	/* val goes from 0 -> 31 */
	return stv06xx_write_bridge(sd, 0x1432, val);
}

static int setcontrast(struct sd *sd, s32 val)
{
	/* Val goes from 0 -> 15 */
	return stv06xx_write_bridge(sd, 0x143a, val | 0xf0);
}

static int setgain(struct sd *sd, u8 gain)
{
	int err;

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

static int setexposure(struct sd *sd, s16 expo)
{
	int err;

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

	/* commit settings */
	err = stv06xx_write_bridge(sd, 0x143f, 0x01);
	return (err < 0) ? err : 0;
}

static int st6422_stop(struct sd *sd)
{
	PDEBUG(D_STREAM, "Halting stream");

	return 0;
}
