/*
 * Driver for the s5k4aa sensor
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

#include "m5602_s5k4aa.h"

static
    const
	struct dmi_system_id s5k4aa_vflip_dmi_table[] = {
	{
		.ident = "Fujitsu-Siemens Amilo Xa 2528",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xa 2528")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Xi 2550",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xi 2550")
		}
	}, {
		.ident = "MSI GX700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "GX700"),
			DMI_MATCH(DMI_BIOS_DATE, "07/26/2007")
		}
	}, {
		.ident = "MSI GX700/GX705/EX700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "GX700/GX705/EX700")
		}
	},
	{ }
};

static void s5k4aa_dump_registers(struct sd *sd);

int s5k4aa_probe(struct sd *sd)
{
	u8 prod_id[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	const u8 expected_prod_id[6] = {0x00, 0x10, 0x00, 0x4b, 0x33, 0x75};
	int i, err = 0;

	if (force_sensor) {
		if (force_sensor == S5K4AA_SENSOR) {
			info("Forcing a %s sensor", s5k4aa.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	info("Probing for a s5k4aa sensor");

	/* Preinit the sensor */
	for (i = 0; i < ARRAY_SIZE(preinit_s5k4aa) && !err; i++) {
		u8 data[2] = {0x00, 0x00};

		switch (preinit_s5k4aa[i][0]) {
		case BRIDGE:
			err = m5602_write_bridge(sd,
						 preinit_s5k4aa[i][1],
						 preinit_s5k4aa[i][2]);
			break;

		case SENSOR:
			data[0] = preinit_s5k4aa[i][2];
			err = m5602_write_sensor(sd,
						  preinit_s5k4aa[i][1],
						  data, 1);
			break;

		case SENSOR_LONG:
			data[0] = preinit_s5k4aa[i][2];
			data[1] = preinit_s5k4aa[i][3];
			err = m5602_write_sensor(sd,
						  preinit_s5k4aa[i][1],
						  data, 2);
			break;
		default:
			info("Invalid stream command, exiting init");
			return -EINVAL;
		}
	}

	/* Test some registers, but we don't know their exact meaning yet */
	if (m5602_read_sensor(sd, 0x00, prod_id, 2))
		return -ENODEV;
	if (m5602_read_sensor(sd, 0x02, prod_id+2, 2))
		return -ENODEV;
	if (m5602_read_sensor(sd, 0x04, prod_id+4, 2))
		return -ENODEV;

	if (memcmp(prod_id, expected_prod_id, sizeof(prod_id)))
		return -ENODEV;
	else
		info("Detected a s5k4aa sensor");

sensor_found:
	sd->gspca_dev.cam.cam_mode = s5k4aa.modes;
	sd->gspca_dev.cam.nmodes = s5k4aa.nmodes;
	sd->desc->ctrls = s5k4aa.ctrls;
	sd->desc->nctrls = s5k4aa.nctrls;

	return 0;
}

int s5k4aa_init(struct sd *sd)
{
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(init_s5k4aa) && !err; i++) {
		u8 data[2] = {0x00, 0x00};

		switch (init_s5k4aa[i][0]) {
		case BRIDGE:
			err = m5602_write_bridge(sd,
				init_s5k4aa[i][1],
				init_s5k4aa[i][2]);
			break;

		case SENSOR:
			data[0] = init_s5k4aa[i][2];
			err = m5602_write_sensor(sd,
				init_s5k4aa[i][1], data, 1);
			break;

		case SENSOR_LONG:
			data[0] = init_s5k4aa[i][2];
			data[1] = init_s5k4aa[i][3];
			err = m5602_write_sensor(sd,
				init_s5k4aa[i][1], data, 2);
			break;
		default:
			info("Invalid stream command, exiting init");
			return -EINVAL;
		}
	}

	if (dump_sensor)
		s5k4aa_dump_registers(sd);

	if (!err && dmi_check_system(s5k4aa_vflip_dmi_table)) {
		u8 data = 0x02;
		info("vertical flip quirk active");
		m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
		m5602_read_sensor(sd, S5K4AA_READ_MODE, &data, 1);
		data |= S5K4AA_RM_V_FLIP;
		data &= ~S5K4AA_RM_H_FLIP;
		m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);

		/* Decrement COLSTART to preserve color order (BGGR) */
		m5602_read_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
		data--;
		m5602_write_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);

		/* Increment ROWSTART to preserve color order (BGGR) */
		m5602_read_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
		data++;
		m5602_write_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	}

	return (err < 0) ? err : 0;
}

int s5k4aa_power_down(struct sd *sd)
{
	return 0;
}

int s5k4aa_get_exposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K4AA_EXPOSURE_HI, &data, 1);
	if (err < 0)
		goto out;

	*val = data << 8;
	err = m5602_read_sensor(sd, S5K4AA_EXPOSURE_LO, &data, 1);
	*val |= data;
	PDEBUG(D_V4L2, "Read exposure %d", *val);
out:
	return err;
}

int s5k4aa_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	PDEBUG(D_V4L2, "Set exposure to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;
	data = (val >> 8) & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_EXPOSURE_HI, &data, 1);
	if (err < 0)
		goto out;
	data = val & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_EXPOSURE_LO, &data, 1);
out:
	return err;
}

int s5k4aa_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	*val = (data & S5K4AA_RM_V_FLIP) >> 7;
	PDEBUG(D_V4L2, "Read vertical flip %d", *val);

out:
	return err;
}

int s5k4aa_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	PDEBUG(D_V4L2, "Set vertical flip to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		goto out;
	data = ((data & ~S5K4AA_RM_V_FLIP)
			| ((val & 0x01) << 7));
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		goto out;

	if (val) {
		err = m5602_read_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
		if (err < 0)
			goto out;

		data++;
		err = m5602_write_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	} else {
		err = m5602_read_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
		if (err < 0)
			goto out;

		data--;
		err = m5602_write_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	}
out:
	return err;
}

int s5k4aa_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	*val = (data & S5K4AA_RM_H_FLIP) >> 6;
	PDEBUG(D_V4L2, "Read horizontal flip %d", *val);
out:
	return err;
}

int s5k4aa_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	PDEBUG(D_V4L2, "Set horizontal flip to %d",
	       val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		goto out;

	data = ((data & ~S5K4AA_RM_H_FLIP) | ((val & 0x01) << 6));
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		goto out;

	if (val) {
		err = m5602_read_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
		if (err < 0)
			goto out;
		data++;
		err = m5602_write_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
		if (err < 0)
			goto out;
	} else {
		err = m5602_read_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
		if (err < 0)
			goto out;
		data--;
		err = m5602_write_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
	}
out:
	return err;
}

int s5k4aa_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;

	err = m5602_read_sensor(sd, S5K4AA_GAIN_2, &data, 1);
	*val = data;
	PDEBUG(D_V4L2, "Read gain %d", *val);

out:
	return err;
}

int s5k4aa_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	PDEBUG(D_V4L2, "Set gain to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		goto out;

	data = val & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_GAIN_2, &data, 1);

out:
	return err;
}

static void s5k4aa_dump_registers(struct sd *sd)
{
	int address;
	u8 page, old_page;
	m5602_read_sensor(sd, S5K4AA_PAGE_MAP, &old_page, 1);
	for (page = 0; page < 16; page++) {
		m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &page, 1);
		info("Dumping the s5k4aa register state for page 0x%x", page);
		for (address = 0; address <= 0xff; address++) {
			u8 value = 0;
			m5602_read_sensor(sd, address, &value, 1);
			info("register 0x%x contains 0x%x",
			     address, value);
		}
	}
	info("s5k4aa register state dump complete");

	for (page = 0; page < 16; page++) {
		m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &page, 1);
		info("Probing for which registers that are "
		     "read/write for page 0x%x", page);
		for (address = 0; address <= 0xff; address++) {
			u8 old_value, ctrl_value, test_value = 0xff;

			m5602_read_sensor(sd, address, &old_value, 1);
			m5602_write_sensor(sd, address, &test_value, 1);
			m5602_read_sensor(sd, address, &ctrl_value, 1);

			if (ctrl_value == test_value)
				info("register 0x%x is writeable", address);
			else
				info("register 0x%x is read only", address);

			/* Restore original value */
			m5602_write_sensor(sd, address, &old_value, 1);
		}
	}
	info("Read/write register probing complete");
	m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &old_page, 1);
}
