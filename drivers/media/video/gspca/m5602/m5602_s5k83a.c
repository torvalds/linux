/*
 * Driver for the s5k83a sensor
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

#include "m5602_s5k83a.h"

static void s5k83a_dump_registers(struct sd *sd);

int s5k83a_probe(struct sd *sd)
{
	u8 prod_id = 0, ver_id = 0;
	int i, err = 0;

	if (force_sensor) {
		if (force_sensor == S5K83A_SENSOR) {
			info("Forcing a %s sensor", s5k83a.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	info("Probing for a s5k83a sensor");

	/* Preinit the sensor */
	for (i = 0; i < ARRAY_SIZE(preinit_s5k83a) && !err; i++) {
		u8 data[2] = {preinit_s5k83a[i][2], preinit_s5k83a[i][3]};
		if (preinit_s5k83a[i][0] == SENSOR)
			err = m5602_write_sensor(sd, preinit_s5k83a[i][1],
				data, 2);
		else
			err = m5602_write_bridge(sd, preinit_s5k83a[i][1],
				data[0]);
	}

	/* We don't know what register (if any) that contain the product id
	 * Just pick the first addresses that seem to produce the same results
	 * on multiple machines */
	if (m5602_read_sensor(sd, 0x00, &prod_id, 1))
		return -ENODEV;

	if (m5602_read_sensor(sd, 0x01, &ver_id, 1))
		return -ENODEV;

	if ((prod_id == 0xff) || (ver_id == 0xff))
		return -ENODEV;
	else
		info("Detected a s5k83a sensor");

sensor_found:
	sd->gspca_dev.cam.cam_mode = s5k83a.modes;
	sd->gspca_dev.cam.nmodes = s5k83a.nmodes;
	sd->desc->ctrls = s5k83a.ctrls;
	sd->desc->nctrls = s5k83a.nctrls;
	return 0;
}

int s5k83a_init(struct sd *sd)
{
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(init_s5k83a) && !err; i++) {
		u8 data[2] = {0x00, 0x00};

		switch (init_s5k83a[i][0]) {
		case BRIDGE:
			err = m5602_write_bridge(sd,
					init_s5k83a[i][1],
					init_s5k83a[i][2]);
			break;

		case SENSOR:
			data[0] = init_s5k83a[i][2];
			err = m5602_write_sensor(sd,
				init_s5k83a[i][1], data, 1);
			break;

		case SENSOR_LONG:
			data[0] = init_s5k83a[i][2];
			data[1] = init_s5k83a[i][3];
			err = m5602_write_sensor(sd,
				init_s5k83a[i][1], data, 2);
			break;
		default:
			info("Invalid stream command, exiting init");
			return -EINVAL;
		}
	}

	if (dump_sensor)
		s5k83a_dump_registers(sd);

	return (err < 0) ? err : 0;
}

int s5k83a_power_down(struct sd *sd)
{
	return 0;
}

static void s5k83a_dump_registers(struct sd *sd)
{
	int address;
	u8 page, old_page;
	m5602_read_sensor(sd, S5K83A_PAGE_MAP, &old_page, 1);

	for (page = 0; page < 16; page++) {
		m5602_write_sensor(sd, S5K83A_PAGE_MAP, &page, 1);
		info("Dumping the s5k83a register state for page 0x%x", page);
		for (address = 0; address <= 0xff; address++) {
			u8 val = 0;
			m5602_read_sensor(sd, address, &val, 1);
			info("register 0x%x contains 0x%x",
			     address, val);
		}
	}
	info("s5k83a register state dump complete");

	for (page = 0; page < 16; page++) {
		m5602_write_sensor(sd, S5K83A_PAGE_MAP, &page, 1);
		info("Probing for which registers that are read/write "
		      "for page 0x%x", page);
		for (address = 0; address <= 0xff; address++) {
			u8 old_val, ctrl_val, test_val = 0xff;

			m5602_read_sensor(sd, address, &old_val, 1);
			m5602_write_sensor(sd, address, &test_val, 1);
			m5602_read_sensor(sd, address, &ctrl_val, 1);

			if (ctrl_val == test_val)
				info("register 0x%x is writeable", address);
			else
				info("register 0x%x is read only", address);

			/* Restore original val */
			m5602_write_sensor(sd, address, &old_val, 1);
		}
	}
	info("Read/write register probing complete");
	m5602_write_sensor(sd, S5K83A_PAGE_MAP, &old_page, 1);
}

int s5k83a_get_brightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	err = m5602_read_sensor(sd, S5K83A_BRIGHTNESS, data, 2);
	if (err < 0)
		goto out;

	data[1] = data[1] << 1;
	*val = data[1];

out:
	return err;
}

int s5k83a_set_brightness(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = 0x00;
	data[1] = 0x20;
	err = m5602_write_sensor(sd, 0x14, data, 2);
	if (err < 0)
		goto out;

	data[0] = 0x01;
	data[1] = 0x00;
	err = m5602_write_sensor(sd, 0x0d, data, 2);
	if (err < 0)
		goto out;

	/* FIXME: This is not sane, we need to figure out the composition
		  of these registers */
	data[0] = val >> 3; /* brightness, high 5 bits */
	data[1] = val >> 1; /* brightness, high 7 bits */
	err = m5602_write_sensor(sd, S5K83A_BRIGHTNESS, data, 2);

out:
	return err;
}

int s5k83a_get_whiteness(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = m5602_read_sensor(sd, S5K83A_WHITENESS, &data, 1);
	if (err < 0)
		goto out;

	*val = data;

out:
	return err;
}

int s5k83a_set_whiteness(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[1];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = val;
	err = m5602_write_sensor(sd, S5K83A_WHITENESS, data, 1);

	return err;
}

int s5k83a_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	err = m5602_read_sensor(sd, S5K83A_GAIN, data, 2);
	if (err < 0)
		goto out;

	data[1] = data[1] & 0x3f;
	if (data[1] > S5K83A_MAXIMUM_GAIN)
		data[1] = S5K83A_MAXIMUM_GAIN;

	*val = data[1];

out:
	return err;
}

int s5k83a_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = 0;
	data[1] = val;
	err = m5602_write_sensor(sd, S5K83A_GAIN, data, 2);
	return err;
}

int s5k83a_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[1];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = 0x05;
	err = m5602_write_sensor(sd, S5K83A_PAGE_MAP, data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K83A_FLIP, data, 1);
	*val = (data[0] | 0x40) ? 1 : 0;

out:
	return err;
}

int s5k83a_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[1];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = 0x05;
	err = m5602_write_sensor(sd, S5K83A_PAGE_MAP, data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K83A_FLIP, data, 1);
	if (err < 0)
		goto out;

	/* set or zero six bit, seven is hflip */
	data[0] = (val) ? (data[0] & 0x80) | 0x40 | S5K83A_FLIP_MASK
			: (data[0] & 0x80) | S5K83A_FLIP_MASK;
	err = m5602_write_sensor(sd, S5K83A_FLIP, data, 1);
	if (err < 0)
		goto out;

	data[0] = (val) ? 0x0b : 0x0a;
	err = m5602_write_sensor(sd, S5K83A_VFLIP_TUNE, data, 1);

out:
	return err;
}

int s5k83a_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 data[1];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = 0x05;
	err = m5602_write_sensor(sd, S5K83A_PAGE_MAP, data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K83A_FLIP, data, 1);
	*val = (data[0] | 0x80) ? 1 : 0;

out:
	return err;
}

int s5k83a_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[1];
	struct sd *sd = (struct sd *) gspca_dev;

	data[0] = 0x05;
	err = m5602_write_sensor(sd, S5K83A_PAGE_MAP, data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K83A_FLIP, data, 1);
	if (err < 0)
		goto out;

	/* set or zero seven bit, six is vflip */
	data[0] = (val) ? (data[0] & 0x40) | 0x80 | S5K83A_FLIP_MASK
			: (data[0] & 0x40) | S5K83A_FLIP_MASK;
	err = m5602_write_sensor(sd, S5K83A_FLIP, data, 1);
	if (err < 0)
		goto out;

	data[0] = (val) ? 0x0a : 0x0b;
	err = m5602_write_sensor(sd, S5K83A_HFLIP_TUNE, data, 1);
out:
	return err;
}
