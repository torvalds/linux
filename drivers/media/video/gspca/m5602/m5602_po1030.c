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

#include "m5602_po1030.h"

static void po1030_dump_registers(struct sd *sd);

int po1030_probe(struct sd *sd)
{
	u8 prod_id = 0, ver_id = 0, i;

	if (force_sensor) {
		if (force_sensor == PO1030_SENSOR) {
			info("Forcing a %s sensor", po1030.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	info("Probing for a po1030 sensor");

	/* Run the pre-init to actually probe the unit */
	for (i = 0; i < ARRAY_SIZE(preinit_po1030); i++) {
		u8 data = preinit_po1030[i][2];
		if (preinit_po1030[i][0] == SENSOR)
			m5602_write_sensor(sd,
				preinit_po1030[i][1], &data, 1);
		else
			m5602_write_bridge(sd, preinit_po1030[i][1], data);
	}

	if (m5602_read_sensor(sd, 0x3, &prod_id, 1))
		return -ENODEV;

	if (m5602_read_sensor(sd, 0x4, &ver_id, 1))
		return -ENODEV;

	if ((prod_id == 0x02) && (ver_id == 0xef)) {
		info("Detected a po1030 sensor");
		goto sensor_found;
	}
	return -ENODEV;

sensor_found:
	sd->gspca_dev.cam.cam_mode = po1030.modes;
	sd->gspca_dev.cam.nmodes = po1030.nmodes;
	sd->desc->ctrls = po1030.ctrls;
	sd->desc->nctrls = po1030.nctrls;
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
			info("Invalid stream command, exiting init");
			return -EINVAL;
		}
	}

	if (dump_sensor)
		po1030_dump_registers(sd);

	return err;
}

int po1030_get_exposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_REG_INTEGLINES_H,
				 &i2c_data, 1);
	if (err < 0)
		goto out;
	*val = (i2c_data << 8);

	err = m5602_read_sensor(sd, PO1030_REG_INTEGLINES_M,
				 &i2c_data, 1);
	*val |= i2c_data;

	PDEBUG(D_V4L2, "Exposure read as %d", *val);
out:
	return err;
}

int po1030_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	PDEBUG(D_V4L2, "Set exposure to %d", val & 0xffff);

	i2c_data = ((val & 0xff00) >> 8);
	PDEBUG(D_V4L2, "Set exposure to high byte to 0x%x",
	       i2c_data);

	err = m5602_write_sensor(sd, PO1030_REG_INTEGLINES_H,
				  &i2c_data, 1);
	if (err < 0)
		goto out;

	i2c_data = (val & 0xff);
	PDEBUG(D_V4L2, "Set exposure to low byte to 0x%x",
	       i2c_data);
	err = m5602_write_sensor(sd, PO1030_REG_INTEGLINES_M,
				  &i2c_data, 1);

out:
	return err;
}

int po1030_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_REG_GLOBALGAIN,
				 &i2c_data, 1);
	*val = i2c_data;
	PDEBUG(D_V4L2, "Read global gain %d", *val);

	return err;
}

int po1030_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_REG_CONTROL2,
				 &i2c_data, 1);

	*val = (i2c_data >> 7) & 0x01 ;

	PDEBUG(D_V4L2, "Read hflip %d", *val);

	return err;
}

int po1030_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	PDEBUG(D_V4L2, "Set hflip %d", val);
	err = m5602_read_sensor(sd, PO1030_REG_CONTROL2, &i2c_data, 1);
	if (err < 0)
		goto out;

	i2c_data = (0x7f & i2c_data) | ((val & 0x01) << 7);

	err = m5602_write_sensor(sd, PO1030_REG_CONTROL2,
				 &i2c_data, 1);

out:
	return err;
}

int po1030_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_REG_GLOBALGAIN,
				 &i2c_data, 1);

	*val = (i2c_data >> 6) & 0x01;

	PDEBUG(D_V4L2, "Read vflip %d", *val);

	return err;
}

int po1030_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	PDEBUG(D_V4L2, "Set vflip %d", val);
	err = m5602_read_sensor(sd, PO1030_REG_CONTROL2, &i2c_data, 1);
	if (err < 0)
		goto out;

	i2c_data = (i2c_data & 0xbf) | ((val & 0x01) << 6);

	err = m5602_write_sensor(sd, PO1030_REG_CONTROL2,
				 &i2c_data, 1);

out:
	return err;
}

int po1030_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set global gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_REG_GLOBALGAIN,
				  &i2c_data, 1);
	return err;
}

int po1030_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_REG_RED_GAIN,
				 &i2c_data, 1);
	*val = i2c_data;
	PDEBUG(D_V4L2, "Read red gain %d", *val);
	return err;
}

int po1030_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set red gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_REG_RED_GAIN,
				  &i2c_data, 1);
	return err;
}

int po1030_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = m5602_read_sensor(sd, PO1030_REG_BLUE_GAIN,
				 &i2c_data, 1);
	*val = i2c_data;
	PDEBUG(D_V4L2, "Read blue gain %d", *val);

	return err;
}

int po1030_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;
	i2c_data = val & 0xff;
	PDEBUG(D_V4L2, "Set blue gain to %d", i2c_data);
	err = m5602_write_sensor(sd, PO1030_REG_BLUE_GAIN,
				  &i2c_data, 1);

	return err;
}

int po1030_power_down(struct sd *sd)
{
	return 0;
}

static void po1030_dump_registers(struct sd *sd)
{
	int address;
	u8 value = 0;

	info("Dumping the po1030 sensor core registers");
	for (address = 0; address < 0x7f; address++) {
		m5602_read_sensor(sd, address, &value, 1);
		info("register 0x%x contains 0x%x",
		     address, value);
	}

	info("po1030 register state dump complete");

	info("Probing for which registers that are read/write");
	for (address = 0; address < 0xff; address++) {
		u8 old_value, ctrl_value;
		u8 test_value[2] = {0xff, 0xff};

		m5602_read_sensor(sd, address, &old_value, 1);
		m5602_write_sensor(sd, address, test_value, 1);
		m5602_read_sensor(sd, address, &ctrl_value, 1);

		if (ctrl_value == test_value[0])
			info("register 0x%x is writeable", address);
		else
			info("register 0x%x is read only", address);

		/* Restore original value */
		m5602_write_sensor(sd, address, &old_value, 1);
	}
}
