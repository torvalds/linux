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
	}, {
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
	}, {
		{
			.id             = V4L2_CID_GAIN,
			.type           = V4L2_CTRL_TYPE_INTEGER,
			.name           = "gain",
			.minimum        = 0,
			.maximum        = (INITIAL_MAX_GAIN - 1) * 2 * 2 * 2,
			.step           = 1,
			.default_value  = DEFAULT_GAIN,
			.flags          = V4L2_CTRL_FLAG_SLIDER
		},
		.set = mt9m111_set_gain,
		.get = mt9m111_get_gain
	}
};


static void mt9m111_dump_registers(struct sd *sd);

int mt9m111_probe(struct sd *sd)
{
	u8 data[2] = {0x00, 0x00};
	int i;

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
	sd->gspca_dev.cam.cam_mode = mt9m111_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(mt9m111_modes);
	sd->desc->ctrls = mt9m111_ctrls;
	sd->desc->nctrls = ARRAY_SIZE(mt9m111_ctrls);
	return 0;
}

int mt9m111_init(struct sd *sd)
{
	int i, err = 0;

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

	return (err < 0) ? err : 0;
}

int mt9m111_power_down(struct sd *sd)
{
	return 0;
}

int mt9m111_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	err = m5602_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				  data, 2);
	*val = data[0] & MT9M111_RMB_MIRROR_ROWS;
	PDEBUG(D_V4L2, "Read vertical flip %d", *val);

	return err;
}

int mt9m111_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set vertical flip to %d", val);

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
	if (err < 0)
		return err;

	data[0] = (data[0] & 0xfe) | val;
	err = m5602_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				   data, 2);
	return err;
}

int mt9m111_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	err = m5602_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
				  data, 2);
	*val = data[0] & MT9M111_RMB_MIRROR_COLS;
	PDEBUG(D_V4L2, "Read horizontal flip %d", *val);

	return err;
}

int mt9m111_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set horizontal flip to %d", val);

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B, data, 2);
	if (err < 0)
		return err;

	data[0] = (data[0] & 0xfd) | ((val << 1) & 0x02);
	err = m5602_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
					data, 2);
	return err;
}

int mt9m111_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err, tmp;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	err = m5602_read_sensor(sd, MT9M111_SC_GLOBAL_GAIN, data, 2);
	tmp = ((data[1] << 8) | data[0]);

	*val = ((tmp & (1 << 10)) * 2) |
	      ((tmp & (1 <<  9)) * 2) |
	      ((tmp & (1 <<  8)) * 2) |
	       (tmp & 0x7f);

	PDEBUG(D_V4L2, "Read gain %d", *val);

	return err;
}

int mt9m111_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err, tmp;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

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

	data[1] = (tmp & 0xff00) >> 8;
	data[0] = (tmp & 0xff);
	PDEBUG(D_V4L2, "tmp=%d, data[1]=%d, data[0]=%d", tmp,
	       data[1], data[0]);

	err = m5602_write_sensor(sd, MT9M111_SC_GLOBAL_GAIN,
				   data, 2);

	return err;
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
