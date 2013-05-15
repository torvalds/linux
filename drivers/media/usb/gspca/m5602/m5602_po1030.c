/*
 * Driver for the po1030 sensor
 *
 * Copyright (c) 2008 Erik Andrén
 * Copyright (c) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (c) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "m5602_po1030.h"

static int po1030_s_ctrl(struct v4l2_ctrl *ctrl);
static void po1030_dump_registers(struct sd *sd);

static struct v4l2_pix_format po1030_modes[] = {
	{
		640,
		480,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage = 640 * 480,
		.bytesperline = 640,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2
	}
};

static const struct v4l2_ctrl_ops po1030_ctrl_ops = {
	.s_ctrl = po1030_s_ctrl,
};

static const struct v4l2_ctrl_config po1030_greenbal_cfg = {
	.ops	= &po1030_ctrl_ops,
	.id	= M5602_V4L2_CID_GREEN_BALANCE,
	.name	= "Green Balance",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= 255,
	.step	= 1,
	.def	= PO1030_GREEN_GAIN_DEFAULT,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

int po1030_probe(struct sd *sd)
{
	u8 dev_id_h = 0, i;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	if (force_sensor) {
		if (force_sensor == PO1030_SENSOR) {
			pr_info("Forcing a %s sensor\n", po1030.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	PDEBUG(D_PROBE, "Probing for a po1030 sensor");

	/* Run the pre-init to actually probe the unit */
	for (i = 0; i < ARRAY_SIZE(preinit_po1030); i++) {
		u8 data = preinit_po1030[i][2];
		if (preinit_po1030[i][0] == SENSOR)
			m5602_write_sensor(sd,
				preinit_po1030[i][1], &data, 1);
		else
			m5602_write_bridge(sd, preinit_po1030[i][1], data);
	}

	if (m5602_read_sensor(sd, PO1030_DEVID_H, &dev_id_h, 1))
		return -ENODEV;

	if (dev_id_h == 0x30) {
		pr_info("Detected a po1030 sensor\n");
		goto sensor_found;
	}
	return -ENODEV;

sensor_found:
	sd->gspca_dev.cam.cam_mode = po1030_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(po1030_modes);

	return 0;
}

int po1030_init(struct sd *sd)
{
	int i, err = 0;

	/* Init the sensor */
	for (i = 0; i < ARRAY_SIZE(init_po1030) && !err; i++) {
		u8 data[2] = {0x00, 0x00};

		switch (init_po1030[i][0]) {
		case BRIDGE:
			err = m5602_write_bridge(sd,
				init_po1030[i][1],
				init_po1030[i][2]);
			break;

		case SENSOR:
			data[0] = init_po1030[i][2];
			err = m5602_write_sensor(sd,
				init_po1030[i][1], data, 1);
			break;

		default:
			pr_info("Invalid stream command, exiting init\n");
			return -EINVAL;
		}
	}
	if (err < 0)
		return err;

	if (dump_sensor)
		po1030_dump_registers(sd);

	return 0;
}

int po1030_init_controls(struct sd *sd)
{
	struct v4l2_ctrl_handler *hdl = &sd->gspca_dev.ctrl_handler;

	sd->gspca_dev.vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 9);

	sd->auto_white_bal = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops,
					       V4L2_CID_AUTO_WHITE_BALANCE,
					       0, 1, 1, 0);
	sd->green_bal = v4l2_ctrl_new_custom(hdl, &po1030_greenbal_cfg, NULL);
	sd->red_bal = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops,
					V4L2_CID_RED_BALANCE, 0, 255, 1,
					PO1030_RED_GAIN_DEFAULT);
	sd->blue_bal = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops,
					V4L2_CID_BLUE_BALANCE, 0, 255, 1,
					PO1030_BLUE_GAIN_DEFAULT);

	sd->autoexpo = v4l2_ctrl_new_std_menu(hdl, &po1030_ctrl_ops,
			  V4L2_CID_EXPOSURE_AUTO, 1, 0, V4L2_EXPOSURE_MANUAL);
	sd->expo = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops, V4L2_CID_EXPOSURE,
			  0, 0x2ff, 1, PO1030_EXPOSURE_DEFAULT);

	sd->gain = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops, V4L2_CID_GAIN, 0,
				     0x4f, 1, PO1030_GLOBAL_GAIN_DEFAULT);

	sd->hflip = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops, V4L2_CID_HFLIP,
				      0, 1, 1, 0);
	sd->vflip = v4l2_ctrl_new_std(hdl, &po1030_ctrl_ops, V4L2_CID_VFLIP,
				      0, 1, 1, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}

	v4l2_ctrl_auto_cluster(4, &sd->auto_white_bal, 0, false);
	v4l2_ctrl_auto_cluster(2, &sd->autoexpo, 0, false);
	v4l2_ctrl_cluster(2, &sd->hflip);

	return 0;
}

int po1030_start(struct sd *sd)
{
	struct cam *cam = &sd->gspca_dev.cam;
	int i, err = 0;
	int width = cam->cam_mode[sd->gspca_dev.curr_mode].width;
	int height = cam->cam_mode[sd->gspca_dev.curr_mode].height;
	int ver_offs = cam->cam_mode[sd->gspca_dev.curr_mode].priv;
	u8 data;

	switch (width) {
	case 320:
		data = PO1030_SUBSAMPLING;
		err = m5602_write_sensor(sd, PO1030_CONTROL3, &data, 1);
		if (err < 0)
			return err;

		data = ((width + 3) >> 8) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWWIDTH_H, &data, 1);
		if (err < 0)
			return err;

		data = (width + 3) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWWIDTH_L, &data, 1);
		if (err < 0)
			return err;

		data = ((height + 1) >> 8) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWHEIGHT_H, &data, 1);
		if (err < 0)
			return err;

		data = (height + 1) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWHEIGHT_L, &data, 1);

		height += 6;
		width -= 1;
		break;

	case 640:
		data = 0;
		err = m5602_write_sensor(sd, PO1030_CONTROL3, &data, 1);
		if (err < 0)
			return err;

		data = ((width + 7) >> 8) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWWIDTH_H, &data, 1);
		if (err < 0)
			return err;

		data = (width + 7) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWWIDTH_L, &data, 1);
		if (err < 0)
			return err;

		data = ((height + 3) >> 8) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWHEIGHT_H, &data, 1);
		if (err < 0)
			return err;

		data = (height + 3) & 0xff;
		err = m5602_write_sensor(sd, PO1030_WINDOWHEIGHT_L, &data, 1);

		height += 12;
		width -= 2;
		break;
	}
	err = m5602_write_bridge(sd, M5602_XB_SENSOR_TYPE, 0x0c);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_LINE_OF_FRAME_H, 0x81);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_PIX_OF_LINE_H, 0x82);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0x01);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA,
				 ((ver_offs >> 8) & 0xff));
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, (ver_offs & 0xff));
	if (err < 0)
		return err;

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, (height >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, (height & 0xff));
	if (err < 0)
		return err;

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, 0);

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, (width >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, (width & 0xff));
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);
	return err;
}

static int po1030_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	PDEBUG(D_CONF, "Set exposure to %d", val & 0xffff);

	i2c_data = ((val & 0xff00) >> 8);
	PDEBUG(D_CONF, "Set exposure to high byte to 0x%x",
	       i2c_data);

	err = m5602_write_sensor(sd, PO1030_INTEGLINES_H,
				  &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = (val & 0xff);
	PDEBUG(D_CONF, "Set exposure to low byte to 0x%x",
	       i2c_data);
	err = m5602_write_sensor(sd, PO1030_INTEGLINES_M,
				  &i2c_data, 1);

	return err;
}

static int po1030_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	i2c_data = val & 0xff;
	PDEBUG(D_CONF, "Set global gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_GLOBALGAIN,
				 &i2c_data, 1);
	return err;
}

static int po1030_set_hvflip(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	PDEBUG(D_CONF, "Set hvflip %d %d", sd->hflip->val, sd->vflip->val);
	err = m5602_read_sensor(sd, PO1030_CONTROL2, &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = (0x3f & i2c_data) | (sd->hflip->val << 7) |
		   (sd->vflip->val << 6);

	err = m5602_write_sensor(sd, PO1030_CONTROL2,
				 &i2c_data, 1);

	return err;
}

static int po1030_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	i2c_data = val & 0xff;
	PDEBUG(D_CONF, "Set red gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_RED_GAIN,
				  &i2c_data, 1);
	return err;
}

static int po1030_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	i2c_data = val & 0xff;
	PDEBUG(D_CONF, "Set blue gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_BLUE_GAIN,
				  &i2c_data, 1);

	return err;
}

static int po1030_set_green_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	i2c_data = val & 0xff;
	PDEBUG(D_CONF, "Set green gain to %d", i2c_data);

	err = m5602_write_sensor(sd, PO1030_GREEN_1_GAIN,
			   &i2c_data, 1);
	if (err < 0)
		return err;

	return m5602_write_sensor(sd, PO1030_GREEN_2_GAIN,
				 &i2c_data, 1);
}

static int po1030_set_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
	if (err < 0)
		return err;

	PDEBUG(D_CONF, "Set auto white balance to %d", val);
	i2c_data = (i2c_data & 0xfe) | (val & 0x01);
	err = m5602_write_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
	return err;
}

static int po1030_set_auto_exposure(struct gspca_dev *gspca_dev,
				    __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
	if (err < 0)
		return err;

	PDEBUG(D_CONF, "Set auto exposure to %d", val);
	val = (val == V4L2_EXPOSURE_AUTO);
	i2c_data = (i2c_data & 0xfd) | ((val & 0x01) << 1);
	return m5602_write_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
}

void po1030_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
}

static int po1030_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *) gspca_dev;
	int err;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		err = po1030_set_auto_white_balance(gspca_dev, ctrl->val);
		if (err || ctrl->val)
			return err;
		err = po1030_set_green_balance(gspca_dev, sd->green_bal->val);
		if (err)
			return err;
		err = po1030_set_red_balance(gspca_dev, sd->red_bal->val);
		if (err)
			return err;
		err = po1030_set_blue_balance(gspca_dev, sd->blue_bal->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		err = po1030_set_auto_exposure(gspca_dev, ctrl->val);
		if (err || ctrl->val == V4L2_EXPOSURE_AUTO)
			return err;
		err = po1030_set_exposure(gspca_dev, sd->expo->val);
		break;
	case V4L2_CID_GAIN:
		err = po1030_set_gain(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		err = po1030_set_hvflip(gspca_dev);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static void po1030_dump_registers(struct sd *sd)
{
	int address;
	u8 value = 0;

	pr_info("Dumping the po1030 sensor core registers\n");
	for (address = 0; address < 0x7f; address++) {
		m5602_read_sensor(sd, address, &value, 1);
		pr_info("register 0x%x contains 0x%x\n", address, value);
	}

	pr_info("po1030 register state dump complete\n");

	pr_info("Probing for which registers that are read/write\n");
	for (address = 0; address < 0xff; address++) {
		u8 old_value, ctrl_value;
		u8 test_value[2] = {0xff, 0xff};

		m5602_read_sensor(sd, address, &old_value, 1);
		m5602_write_sensor(sd, address, test_value, 1);
		m5602_read_sensor(sd, address, &ctrl_value, 1);

		if (ctrl_value == test_value[0])
			pr_info("register 0x%x is writeable\n", address);
		else
			pr_info("register 0x%x is read only\n", address);

		/* Restore original value */
		m5602_write_sensor(sd, address, &old_value, 1);
	}
}
