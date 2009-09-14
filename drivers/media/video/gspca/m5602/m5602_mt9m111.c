/*
 * Driver for the mt9m111 sensor
 *
 * Copyright (C) 2008 Erik Andrén
 * Copyright (C) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (C) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
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

#include "m5602_mt9m111.h"

static int mt9m111_set_vflip(struct gspca_dev *gspca_dev, __s32 val);
static int mt9m111_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
static int mt9m111_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
static int mt9m111_set_hflip(struct gspca_dev *gspca_dev, __s32 val);
static int mt9m111_get_gain(struct gspca_dev *gspca_dev, __s32 *val);
static int mt9m111_set_gain(struct gspca_dev *gspca_dev, __s32 val);
static int mt9m111_set_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 val);
static int mt9m111_get_auto_white_balance(struct gspca_dev *gspca_dev,
					  __s32 *val);
static int mt9m111_get_green_balance(struct gspca_dev *gspca_dev, __s32 *val);
static int mt9m111_set_green_balance(struct gspca_dev *gspca_dev, __s32 val);
static int mt9m111_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val);
static int mt9m111_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val);
static int mt9m111_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val);
static int mt9m111_set_red_balance(struct gspca_dev *gspca_dev, __s32 val);

static struct v4l2_pix_format mt9m111_modes[] = {
	{
		640,
		480,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage = 640 * 480,
		.bytesperline = 640,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	}
};

const static struct ctrl mt9m111_ctrls[] = {
#define VFLIP_IDX 0
	{
		{
			.id		= V4L2_CID_VFLIP,
			.type           = V4L2_CTRL_TYPE_BOOLEAN,
			.name           = "vertical flip",
			.minimum        = 0,
			.maximum        = 1,
			.step           = 1,
			.default_value  = 0
		},
		.set = mt9m111_set_vflip,
		.get = mt9m111_get_vflip
	},
#define HFLIP_IDX 1
	{
		{
			.id             = V4L2_CID_HFLIP,
			.type           = V4L2_CTRL_TYPE_BOOLEAN,
			.name           = "horizontal flip",
			.minimum        = 0,
			.maximum        = 1,
			.step           = 1,
			.default_value  = 0
		},
		.set = mt9m111_set_hflip,
		.get = mt9m111_get_hflip
	},
#define GAIN_IDX 2
	{
		{
			.id             = V4L2_CID_GAIN,
			.type           = V4L2_CTRL_TYPE_INTEGER,
			.name           = "gain",
			.minimum        = 0,
			.maximum        = (INITIAL_MAX_GAIN - 1) * 2 * 2 * 2,
			.step           = 1,
			.default_value  = MT9M111_DEFAULT_GAIN,
			.flags          = V4L2_CTRL_FLAG_SLIDER
		},
		.set = mt9m111_set_gain,
		.get = mt9m111_get_gain
	},
#define AUTO_WHITE_BALANCE_IDX 3
	{
		{
			.id             = V4L2_CID_AUTO_WHITE_BALANCE,
			.type           = V4L2_CTRL_TYPE_BOOLEAN,
			.name           = "auto white balance",
			.minimum        = 0,
			.maximum        = 1,
			.step           = 1,
			.default_value  = 0,
		},
		.set = mt9m111_set_auto_white_balance,
		.get = mt9m111_get_auto_white_balance
	},
#define GREEN_BALANCE_IDX 4
	{
		{
			.id 		= M5602_V4L2_CID_GREEN_BALANCE,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "green balance",
			.minimum 	= 0x00,
			.maximum 	= 0x7ff,
			.step 		= 0x1,
			.default_value 	= MT9M111_GREEN_GAIN_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = mt9m111_set_green_balance,
		.get = mt9m111_get_green_balance
	},
#define BLUE_BALANCE_IDX 5
	{
		{
			.id 		= V4L2_CID_BLUE_BALANCE,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "blue balance",
			.minimum 	= 0x00,
			.maximum 	= 0x7ff,
			.step 		= 0x1,
			.default_value 	= MT9M111_BLUE_GAIN_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = mt9m111_set_blue_balance,
		.get = mt9m111_get_blue_balance
	},
#define RED_BALANCE_IDX 5
	{
		{
			.id 		= V4L2_CID_RED_BALANCE,
			.type 		= V4L2_CTRL_TYPE_INTEGER,
			.name 		= "red balance",
			.minimum 	= 0x00,
			.maximum 	= 0x7ff,
			.step 		= 0x1,
			.default_value 	= MT9M111_RED_GAIN_DEFAULT,
			.flags         	= V4L2_CTRL_FLAG_SLIDER
		},
		.set = mt9m111_set_red_balance,
		.get = mt9m111_get_red_balance
	},
};

static void mt9m111_dump_registers(struct sd *sd);

int mt9m111_probe(struct sd *sd)
{
	u8 data[2] = {0x00, 0x00};
	int i;
	s32 *sensor_settings;

	if (force_sensor) {
		if (force_sensor == MT9M111_SENSOR) {
			info("Forcing a %s sensor", mt9m111.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	info("Probing for a mt9m111 sensor");

	/* Do the preinit */
	for (i = 0; i < ARRAY_SIZE(preinit_mt9m111); i++) {
		if (preinit_mt9m111[i][0] == BRIDGE) {
			m5602_write_bridge(sd,
				preinit_mt9m111[i][1],
				preinit_mt9m111[i][2]);
		} else {
			data[0] = preinit_mt9m111[i][2];
			data[1] = preinit_mt9m111[i][3];
			m5602_write_sensor(sd,
				preinit_mt9m111[i][1], data, 2);
		}
	}

	if (m5602_read_sensor(sd, MT9M111_SC_CHIPVER, data, 2))
		return -ENODEV;

	if ((data[0] == 0x14) && (data[1] == 0x3a)) {
		info("Detected a mt9m111 sensor");
		goto sensor_found;
	}

	return -ENODEV;

sensor_found:
	sensor_settings = kmalloc(ARRAY_SIZE(mt9m111_ctrls) * sizeof(s32),
				  GFP_KERNEL);
	if (!sensor_settings)
		return -ENOMEM;

	sd->gspca_dev.cam.cam_mode = mt9m111_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(mt9m111_modes);
	sd->desc->ctrls = mt9m111_ctrls;
	sd->desc->nctrls = ARRAY_SIZE(mt9m111_ctrls);

	for (i = 0; i < ARRAY_SIZE(mt9m111_ctrls); i++)
		sensor_settings[i] = mt9m111_ctrls[i].qctrl.default_value;
	sd->sensor_priv = sensor_settings;

	return 0;
}

int mt9m111_init(struct sd *sd)
{
	int i, err = 0;
	s32 *sensor_settings = sd->sensor_priv;

	/* Init the sensor */
	for (i = 0; i < ARRAY_SIZE(init_mt9m111) && !err; i++) {
		u8 data[2];

		if (init_mt9m111[i][0] == BRIDGE) {
			err = m5602_write_bridge(sd,
				init_mt9m111[i][1],
				init_mt9m111[i][2]);
		} else {
			data[0] = init_mt9m111[i][2];
			data[1] = init_mt9m111[i][3];
			err = m5602_write_sensor(sd,
				init_mt9m111[i][1], data, 2);
		}
	}

	if (dump_sensor)
		mt9m111_dump_registers(sd);

	err = mt9m111_set_vflip(&sd->gspca_dev, sensor_settings[VFLIP_IDX]);
	if (err < 0)
		return err;

	err = mt9m111_set_hflip(&sd->gspca_dev, sensor_settings[HFLIP_IDX]);
	if (err < 0)
		return err;

	err = mt9m111_set_green_balance(&sd->gspca_dev,
					 sensor_settings[GREEN_BALANCE_IDX]);
	if (err < 0)
		return err;

	err = mt9m111_set_blue_balance(&sd->gspca_dev,
					 sensor_settings[BLUE_BALANCE_IDX]);
	if (err < 0)
		return err;

	err = mt9m111_set_red_balance(&sd->gspca_dev,
					sensor_settings[RED_BALANCE_IDX]);
	if (err < 0)
		return err;

	return mt9m111_set_gain(&sd->gspca_dev, sensor_settings[GAIN_IDX]);
}

int mt9m111_start(struct sd *sd)
{
	int i, err = 0;
	u8 data[2];
	struct cam *cam = &sd->gspca_dev.cam;
	s32 *sensor_settings = sd->sensor_priv;

	int width = cam->cam_mode[sd->gspca_dev.curr_mode].width - 1;
	int height = cam->cam_mode[sd->gspca_dev.curr_mode].height;

	for (i = 0; i < ARRAY_SIZE(start_mt9m111) && !err; i++) {
		if (start_mt9m111[i][0] == BRIDGE) {
			err = m5602_write_bridge(sd,
				start_mt9m111[i][1],
				start_mt9m111[i][2]);
		} else {
			data[0] = start_mt9m111[i][2];
			data[1] = start_mt9m111[i][3];
			err = m5602_write_sensor(sd,
				start_mt9m111[i][1], data, 2);
		}
	}
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
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 2);
	if (err < 0)
		return err;

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA,
				 (width >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, width & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);
	if (err < 0)
		return err;

	switch (width) {
	case 640:
		PDEBUG(D_V4L2, "Configuring camera for VGA mode");
		data[0] = MT9M111_RMB_OVER_SIZED;
		data[1] = MT9M111_RMB_ROW_SKIP_2X |
			  MT9M111_RMB_COLUMN_SKIP_2X |
			  (sensor_settings[VFLIP_IDX] << 0) |
			  (sensor_settings[HFLIP_IDX] << 1);

		err = m5602_write_sensor(sd,
					 MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
		break;

	case 320:
		PDEBUG(D_V4L2, "Configuring camera for QVGA mode");
		data[0] = MT9M111_RMB_OVER_SIZED;
		data[1] = MT9M111_RMB_ROW_SKIP_4X |
				MT9M111_RMB_COLUMN_SKIP_4X |
				(sensor_settings[VFLIP_IDX] << 0) |
				(sensor_settings[HFLIP_IDX] << 1);
		err = m5602_write_sensor(sd,
					 MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
		break;
	}
	return err;
}

void mt9m111_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
	kfree(sd->sensor_priv);
}

static int mt9m111_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[VFLIP_IDX];
	PDEBUG(D_V4L2, "Read vertical flip %d", *val);

	return 0;
}

static int mt9m111_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	PDEBUG(D_V4L2, "Set vertical flip to %d", val);

	sensor_settings[VFLIP_IDX] = val;

	/* The mt9m111 is flipped by default */
	val = !val;

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
	if (err < 0)
		return err;

	data[1] = (data[1] & 0xfe) | val;
	err = m5602_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				   data, 2);
	return err;
}

static int mt9m111_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[HFLIP_IDX];
	PDEBUG(D_V4L2, "Read horizontal flip %d", *val);

	return 0;
}

static int mt9m111_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	PDEBUG(D_V4L2, "Set horizontal flip to %d", val);

	sensor_settings[HFLIP_IDX] = val;

	/* The mt9m111 is flipped by default */
	val = !val;

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
	if (err < 0)
		return err;

	data[1] = (data[1] & 0xfd) | ((val << 1) & 0x02);
	err = m5602_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
					data, 2);
	return err;
}

static int mt9m111_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[GAIN_IDX];
	PDEBUG(D_V4L2, "Read gain %d", *val);

	return 0;
}

static int mt9m111_set_auto_white_balance(struct gspca_dev *gspca_dev,
					  __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	int err;
	u8 data[2];

	err = m5602_read_sensor(sd, MT9M111_CP_OPERATING_MODE_CTL, data, 2);
	if (err < 0)
		return err;

	sensor_settings[AUTO_WHITE_BALANCE_IDX] = val & 0x01;
	data[1] = ((data[1] & 0xfd) | ((val & 0x01) << 1));

	err = m5602_write_sensor(sd, MT9M111_CP_OPERATING_MODE_CTL, data, 2);

	PDEBUG(D_V4L2, "Set auto white balance %d", val);
	return err;
}

static int mt9m111_get_auto_white_balance(struct gspca_dev *gspca_dev,
					  __s32 *val) {
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[AUTO_WHITE_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read auto white balance %d", *val);
	return 0;
}

static int mt9m111_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err, tmp;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[GAIN_IDX] = val;

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	if (val >= INITIAL_MAX_GAIN * 2 * 2 * 2)
		return -EINVAL;

	if ((val >= INITIAL_MAX_GAIN * 2 * 2) &&
	    (val < (INITIAL_MAX_GAIN - 1) * 2 * 2 * 2))
		tmp = (1 << 10) | (val << 9) |
				(val << 8) | (val / 8);
	else if ((val >= INITIAL_MAX_GAIN * 2) &&
		 (val <  INITIAL_MAX_GAIN * 2 * 2))
		tmp = (1 << 9) | (1 << 8) | (val / 4);
	else if ((val >= INITIAL_MAX_GAIN) &&
		 (val < INITIAL_MAX_GAIN * 2))
		tmp = (1 << 8) | (val / 2);
	else
		tmp = val;

	data[1] = (tmp & 0xff);
	data[0] = (tmp & 0xff00) >> 8;
	PDEBUG(D_V4L2, "tmp=%d, data[1]=%d, data[0]=%d", tmp,
	       data[1], data[0]);

	err = m5602_write_sensor(sd, MT9M111_SC_GLOBAL_GAIN,
				   data, 2);

	return err;
}

static int mt9m111_set_green_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[GREEN_BALANCE_IDX] = val;
	data[1] = (val & 0xff);
	data[0] = (val & 0xff00) >> 8;

	PDEBUG(D_V4L2, "Set green balance %d", val);
	err = m5602_write_sensor(sd, MT9M111_SC_GREEN_1_GAIN,
				 data, 2);
	if (err < 0)
		return err;

	return m5602_write_sensor(sd, MT9M111_SC_GREEN_2_GAIN,
				  data, 2);
}

static int mt9m111_get_green_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[GREEN_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read green balance %d", *val);
	return 0;
}

static int mt9m111_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[BLUE_BALANCE_IDX] = val;
	data[1] = (val & 0xff);
	data[0] = (val & 0xff00) >> 8;

	PDEBUG(D_V4L2, "Set blue balance %d", val);

	return m5602_write_sensor(sd, MT9M111_SC_BLUE_GAIN,
				  data, 2);
}

static int mt9m111_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[BLUE_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read blue balance %d", *val);
	return 0;
}

static int mt9m111_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[RED_BALANCE_IDX] = val;
	data[1] = (val & 0xff);
	data[0] = (val & 0xff00) >> 8;

	PDEBUG(D_V4L2, "Set red balance %d", val);

	return m5602_write_sensor(sd, MT9M111_SC_RED_GAIN,
				  data, 2);
}

static int mt9m111_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[RED_BALANCE_IDX];
	PDEBUG(D_V4L2, "Read red balance %d", *val);
	return 0;
}

static void mt9m111_dump_registers(struct sd *sd)
{
	u8 address, value[2] = {0x00, 0x00};

	info("Dumping the mt9m111 register state");

	info("Dumping the mt9m111 sensor core registers");
	value[1] = MT9M111_SENSOR_CORE;
	m5602_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		m5602_read_sensor(sd, address, value, 2);
		info("register 0x%x contains 0x%x%x",
		     address, value[0], value[1]);
	}

	info("Dumping the mt9m111 color pipeline registers");
	value[1] = MT9M111_COLORPIPE;
	m5602_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		m5602_read_sensor(sd, address, value, 2);
		info("register 0x%x contains 0x%x%x",
		     address, value[0], value[1]);
	}

	info("Dumping the mt9m111 camera control registers");
	value[1] = MT9M111_CAMERA_CONTROL;
	m5602_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		m5602_read_sensor(sd, address, value, 2);
		info("register 0x%x contains 0x%x%x",
		     address, value[0], value[1]);
	}

	info("mt9m111 register state dump complete");
}
