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

static int po1030_get_exposure(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_exposure(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_get_gain(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_gain(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_red_balance(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_get_green_balance(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_green_balance(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_hflip(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
static int po1030_set_vflip(struct gspca_dev *gspca_dev, __s32 val);
static int po1030_set_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 val);
static int po1030_get_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 *val);
static int po1030_set_auto_exposure(struct gspca_dev *gspca_dev,
					 __s32 val);
static int po1030_get_auto_exposure(struct gspca_dev *gspca_dev,
					 __s32 *val);

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

static const struct ctrl po1030_ctrls[] = {
#define GAIN_IDX 0
	{
		{
			.id		= V4L2_CID_GAIN,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "gain",
			.minimum	= 0x00,
			.maximum	= 0x4f,
			.step		= 0x1,
			.default_value	= PO1030_GLOBAL_GAIN_DEFAULT,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_gain,
		.get = po1030_get_gain
	},
#define EXPOSURE_IDX 1
	{
		{
			.id		= V4L2_CID_EXPOSURE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "exposure",
			.minimum	= 0x00,
			.maximum	= 0x02ff,
			.step		= 0x1,
			.default_value	= PO1030_EXPOSURE_DEFAULT,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_exposure,
		.get = po1030_get_exposure
	},
#define RED_BALANCE_IDX 2
	{
		{
			.id		= V4L2_CID_RED_BALANCE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "red balance",
			.minimum	= 0x00,
			.maximum	= 0xff,
			.step		= 0x1,
			.default_value	= PO1030_RED_GAIN_DEFAULT,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_red_balance,
		.get = po1030_get_red_balance
	},
#define BLUE_BALANCE_IDX 3
	{
		{
			.id		= V4L2_CID_BLUE_BALANCE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "blue balance",
			.minimum	= 0x00,
			.maximum	= 0xff,
			.step		= 0x1,
			.default_value	= PO1030_BLUE_GAIN_DEFAULT,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_blue_balance,
		.get = po1030_get_blue_balance
	},
#define HFLIP_IDX 4
	{
		{
			.id		= V4L2_CID_HFLIP,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "horizontal flip",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value	= 0,
		},
		.set = po1030_set_hflip,
		.get = po1030_get_hflip
	},
#define VFLIP_IDX 5
	{
		{
			.id		= V4L2_CID_VFLIP,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "vertical flip",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value	= 0,
		},
		.set = po1030_set_vflip,
		.get = po1030_get_vflip
	},
#define AUTO_WHITE_BALANCE_IDX 6
	{
		{
			.id		= V4L2_CID_AUTO_WHITE_BALANCE,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "auto white balance",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value	= 0,
		},
		.set = po1030_set_auto_white_balance,
		.get = po1030_get_auto_white_balance
	},
#define AUTO_EXPOSURE_IDX 7
	{
		{
			.id		= V4L2_CID_EXPOSURE_AUTO,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "auto exposure",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value	= 0,
		},
		.set = po1030_set_auto_exposure,
		.get = po1030_get_auto_exposure
	},
#define GREEN_BALANCE_IDX 8
	{
		{
			.id		= M5602_V4L2_CID_GREEN_BALANCE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "green balance",
			.minimum	= 0x00,
			.maximum	= 0xff,
			.step		= 0x1,
			.default_value	= PO1030_GREEN_GAIN_DEFAULT,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = po1030_set_green_balance,
		.get = po1030_get_green_balance
	},
};

static void po1030_dump_registers(struct sd *sd);

int po1030_probe(struct sd *sd)
{
	u8 dev_id_h = 0, i;
	s32 *sensor_settings;

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
	sensor_settings = kmalloc(
		ARRAY_SIZE(po1030_ctrls) * sizeof(s32), GFP_KERNEL);
	if (!sensor_settings)
		return -ENOMEM;

	sd->gspca_dev.cam.cam_mode = po1030_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(po1030_modes);
	sd->desc->ctrls = po1030_ctrls;
	sd->desc->nctrls = ARRAY_SIZE(po1030_ctrls);

	for (i = 0; i < ARRAY_SIZE(po1030_ctrls); i++)
		sensor_settings[i] = po1030_ctrls[i].qctrl.default_value;
	sd->sensor_priv = sensor_settings;

	return 0;
}

int po1030_init(struct sd *sd)
{
	s32 *sensor_settings = sd->sensor_priv;
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

	err = po1030_set_exposure(&sd->gspca_dev,
				   sensor_settings[EXPOSURE_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_gain(&sd->gspca_dev, sensor_settings[GAIN_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_hflip(&sd->gspca_dev, sensor_settings[HFLIP_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_vflip(&sd->gspca_dev, sensor_settings[VFLIP_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_red_balance(&sd->gspca_dev,
				      sensor_settings[RED_BALANCE_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_blue_balance(&sd->gspca_dev,
				      sensor_settings[BLUE_BALANCE_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_green_balance(&sd->gspca_dev,
				       sensor_settings[GREEN_BALANCE_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_auto_white_balance(&sd->gspca_dev,
				sensor_settings[AUTO_WHITE_BALANCE_IDX]);
	if (err < 0)
		return err;

	err = po1030_set_auto_exposure(&sd->gspca_dev,
				sensor_settings[AUTO_EXPOSURE_IDX]);
	return err;
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

static int po1030_get_exposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[EXPOSURE_IDX];
	PDEBUG(D_V4L2, "Exposure read as %d", *val);
	return 0;
}

static int po1030_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[EXPOSURE_IDX] = val;
	PDEBUG(D_V4L2, "Set exposure to %d", val & 0xffff);

	i2c_data = ((val & 0xff00) >> 8);
	PDEBUG(D_V4L2, "Set exposure to high byte to 0x%x",
	       i2c_data);

	err = m5602_write_sensor(sd, PO1030_INTEGLINES_H,
				  &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = (val & 0xff);
	PDEBUG(D_V4L2, "Set exposure to low byte to 0x%x",
	       i2c_data);
	err = m5602_write_sensor(sd, PO1030_INTEGLINES_M,
				  &i2c_data, 1);

	return err;
}

static int po1030_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[GAIN_IDX];
	PDEBUG(D_V4L2, "Read global gain %d", *val);
	return 0;
}

static int po1030_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[GAIN_IDX] = val;

	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set global gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_GLOBALGAIN,
				 &i2c_data, 1);
	return err;
}

static int po1030_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[HFLIP_IDX];
	PDEBUG(D_V4L2, "Read hflip %d", *val);

	return 0;
}

static int po1030_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[HFLIP_IDX] = val;

	PDEBUG(D_V4L2, "Set hflip %d", val);
	err = m5602_read_sensor(sd, PO1030_CONTROL2, &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = (0x7f & i2c_data) | ((val & 0x01) << 7);

	err = m5602_write_sensor(sd, PO1030_CONTROL2,
				 &i2c_data, 1);

	return err;
}

static int po1030_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[VFLIP_IDX];
	PDEBUG(D_V4L2, "Read vflip %d", *val);

	return 0;
}

static int po1030_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[VFLIP_IDX] = val;

	PDEBUG(D_V4L2, "Set vflip %d", val);
	err = m5602_read_sensor(sd, PO1030_CONTROL2, &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = (i2c_data & 0xbf) | ((val & 0x01) << 6);

	err = m5602_write_sensor(sd, PO1030_CONTROL2,
				 &i2c_data, 1);

	return err;
}

static int po1030_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[RED_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read red gain %d", *val);
	return 0;
}

static int po1030_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[RED_BALANCE_IDX] = val;

	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set red gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_RED_GAIN,
				  &i2c_data, 1);
	return err;
}

static int po1030_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[BLUE_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read blue gain %d", *val);

	return 0;
}

static int po1030_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[BLUE_BALANCE_IDX] = val;

	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set blue gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_BLUE_GAIN,
				  &i2c_data, 1);

	return err;
}

static int po1030_get_green_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[GREEN_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read green gain %d", *val);

	return 0;
}

static int po1030_set_green_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[GREEN_BALANCE_IDX] = val;
	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set green gain to %d", i2c_data);

	err = m5602_write_sensor(sd, PO1030_GREEN_1_GAIN,
			   &i2c_data, 1);
	if (err < 0)
		return err;

	return m5602_write_sensor(sd, PO1030_GREEN_2_GAIN,
				 &i2c_data, 1);
}

static int po1030_get_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[AUTO_WHITE_BALANCE_IDX];
	PDEBUG(D_V4L2, "Auto white balancing is %d", *val);

	return 0;
}

static int po1030_set_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[AUTO_WHITE_BALANCE_IDX] = val;

	err = m5602_read_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
	if (err < 0)
		return err;

	PDEBUG(D_V4L2, "Set auto white balance to %d", val);
	i2c_data = (i2c_data & 0xfe) | (val & 0x01);
	err = m5602_write_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
	return err;
}

static int po1030_get_auto_exposure(struct gspca_dev *gspca_dev,
				    __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[AUTO_EXPOSURE_IDX];
	PDEBUG(D_V4L2, "Auto exposure is %d", *val);
	return 0;
}

static int po1030_set_auto_exposure(struct gspca_dev *gspca_dev,
				    __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 i2c_data;
	int err;

	sensor_settings[AUTO_EXPOSURE_IDX] = val;
	err = m5602_read_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
	if (err < 0)
		return err;

	PDEBUG(D_V4L2, "Set auto exposure to %d", val);
	i2c_data = (i2c_data & 0xfd) | ((val & 0x01) << 1);
	return m5602_write_sensor(sd, PO1030_AUTOCTRL1, &i2c_data, 1);
}

void po1030_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
	kfree(sd->sensor_priv);
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
