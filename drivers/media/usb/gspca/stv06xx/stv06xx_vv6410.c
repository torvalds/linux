// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * P/N 861037:      Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 - QuickCam Express
 * P/N 861055:      Sensor ST VV6410       ASIC STV0610   - LEGO cam
 * P/N 861075-0040: Sensor HDCS1000        ASIC
 * P/N 961179-0700: Sensor ST VV6410       ASIC STV0602   - Dexxa WebCam USB
 * P/N 861040-0000: Sensor ST VV6410       ASIC STV0610   - QuickCam Web
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "stv06xx_vv6410.h"

static struct v4l2_pix_format vv6410_mode[] = {
	{
		356,
		292,
		V4L2_PIX_FMT_SGRBG8,
		V4L2_FIELD_NONE,
		.sizeimage = 356 * 292,
		.bytesperline = 356,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	}
};

static int vv6410_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	int err = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (!gspca_dev->streaming)
			return 0;
		err = vv6410_set_hflip(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		if (!gspca_dev->streaming)
			return 0;
		err = vv6410_set_vflip(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		err = vv6410_set_analog_gain(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		err = vv6410_set_exposure(gspca_dev, ctrl->val);
		break;
	}
	return err;
}

static const struct v4l2_ctrl_ops vv6410_ctrl_ops = {
	.s_ctrl = vv6410_s_ctrl,
};

static int vv6410_probe(struct sd *sd)
{
	u16 data;
	int err;

	err = stv06xx_read_sensor(sd, VV6410_DEVICEH, &data);
	if (err < 0)
		return -ENODEV;

	if (data != 0x19)
		return -ENODEV;

	pr_info("vv6410 sensor detected\n");

	sd->gspca_dev.cam.cam_mode = vv6410_mode;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(vv6410_mode);
	return 0;
}

static int vv6410_init_controls(struct sd *sd)
{
	struct v4l2_ctrl_handler *hdl = &sd->gspca_dev.ctrl_handler;

	v4l2_ctrl_handler_init(hdl, 2);
	/* Disable the hardware VFLIP and HFLIP as we currently lack a
	   mechanism to adjust the image offset in such a way that
	   we don't need to renegotiate the announced format */
	/* v4l2_ctrl_new_std(hdl, &vv6410_ctrl_ops, */
	/*		V4L2_CID_HFLIP, 0, 1, 1, 0); */
	/* v4l2_ctrl_new_std(hdl, &vv6410_ctrl_ops, */
	/*		V4L2_CID_VFLIP, 0, 1, 1, 0); */
	v4l2_ctrl_new_std(hdl, &vv6410_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 32768, 1, 20000);
	v4l2_ctrl_new_std(hdl, &vv6410_ctrl_ops,
			V4L2_CID_GAIN, 0, 15, 1, 10);
	return hdl->error;
}

static int vv6410_init(struct sd *sd)
{
	int err = 0, i;

	for (i = 0; i < ARRAY_SIZE(stv_bridge_init); i++)
		stv06xx_write_bridge(sd, stv_bridge_init[i].addr, stv_bridge_init[i].data);

	err = stv06xx_write_sensor_bytes(sd, (u8 *) vv6410_sensor_init,
					 ARRAY_SIZE(vv6410_sensor_init));
	return (err < 0) ? err : 0;
}

static int vv6410_start(struct sd *sd)
{
	int err;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	struct cam *cam = &sd->gspca_dev.cam;
	u32 priv = cam->cam_mode[sd->gspca_dev.curr_mode].priv;

	if (priv & VV6410_SUBSAMPLE) {
		gspca_dbg(gspca_dev, D_CONF, "Enabling subsampling\n");
		stv06xx_write_bridge(sd, STV_Y_CTRL, 0x02);
		stv06xx_write_bridge(sd, STV_X_CTRL, 0x06);

		stv06xx_write_bridge(sd, STV_SCAN_RATE, 0x10);
	} else {
		stv06xx_write_bridge(sd, STV_Y_CTRL, 0x01);
		stv06xx_write_bridge(sd, STV_X_CTRL, 0x0a);
		stv06xx_write_bridge(sd, STV_SCAN_RATE, 0x00);

	}

	/* Turn on LED */
	err = stv06xx_write_bridge(sd, STV_LED_CTRL, LED_ON);
	if (err < 0)
		return err;

	err = stv06xx_write_sensor(sd, VV6410_SETUP0, 0);
	if (err < 0)
		return err;

	gspca_dbg(gspca_dev, D_STREAM, "Starting stream\n");

	return 0;
}

static int vv6410_stop(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int err;

	/* Turn off LED */
	err = stv06xx_write_bridge(sd, STV_LED_CTRL, LED_OFF);
	if (err < 0)
		return err;

	err = stv06xx_write_sensor(sd, VV6410_SETUP0, VV6410_LOW_POWER_MODE);
	if (err < 0)
		return err;

	gspca_dbg(gspca_dev, D_STREAM, "Halting stream\n");

	return 0;
}

static int vv6410_dump(struct sd *sd)
{
	u8 i;
	int err = 0;

	pr_info("Dumping all vv6410 sensor registers\n");
	for (i = 0; i < 0xff && !err; i++) {
		u16 data;
		err = stv06xx_read_sensor(sd, i, &data);
		pr_info("Register 0x%x contained 0x%x\n", i, data);
	}
	return (err < 0) ? err : 0;
}

static int vv6410_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u16 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = stv06xx_read_sensor(sd, VV6410_DATAFORMAT, &i2c_data);
	if (err < 0)
		return err;

	if (val)
		i2c_data |= VV6410_HFLIP;
	else
		i2c_data &= ~VV6410_HFLIP;

	gspca_dbg(gspca_dev, D_CONF, "Set horizontal flip to %d\n", val);
	err = stv06xx_write_sensor(sd, VV6410_DATAFORMAT, i2c_data);

	return (err < 0) ? err : 0;
}

static int vv6410_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u16 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = stv06xx_read_sensor(sd, VV6410_DATAFORMAT, &i2c_data);
	if (err < 0)
		return err;

	if (val)
		i2c_data |= VV6410_VFLIP;
	else
		i2c_data &= ~VV6410_VFLIP;

	gspca_dbg(gspca_dev, D_CONF, "Set vertical flip to %d\n", val);
	err = stv06xx_write_sensor(sd, VV6410_DATAFORMAT, i2c_data);

	return (err < 0) ? err : 0;
}

static int vv6410_set_analog_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Set analog gain to %d\n", val);
	err = stv06xx_write_sensor(sd, VV6410_ANALOGGAIN, 0xf0 | (val & 0xf));

	return (err < 0) ? err : 0;
}

static int vv6410_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned int fine, coarse;

	val = (val * val >> 14) + val / 4;

	fine = val % VV6410_CIF_LINELENGTH;
	coarse = min(512, val / VV6410_CIF_LINELENGTH);

	gspca_dbg(gspca_dev, D_CONF, "Set coarse exposure to %d, fine exposure to %d\n",
		  coarse, fine);

	err = stv06xx_write_sensor(sd, VV6410_FINEH, fine >> 8);
	if (err < 0)
		goto out;

	err = stv06xx_write_sensor(sd, VV6410_FINEL, fine & 0xff);
	if (err < 0)
		goto out;

	err = stv06xx_write_sensor(sd, VV6410_COARSEH, coarse >> 8);
	if (err < 0)
		goto out;

	err = stv06xx_write_sensor(sd, VV6410_COARSEL, coarse & 0xff);

out:
	return err;
}
