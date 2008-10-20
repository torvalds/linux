/*
 * Driver for the ov9650 sensor
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

#include "m5602_ov9650.h"

int ov9650_read_sensor(struct sd *sd, const u8 address,
		      u8 *i2c_data, const u8 len)
{
	int err, i;

	/* The ov9650 registers have a max depth of one byte */
	if (len > 1 || !len)
		return -EINVAL;

	do {
		err = m5602_read_bridge(sd, M5602_XB_I2C_STATUS, i2c_data);
	} while ((*i2c_data & I2C_BUSY) && !err);

	m5602_write_bridge(sd, M5602_XB_I2C_DEV_ADDR,
			   ov9650.i2c_slave_id);
	m5602_write_bridge(sd, M5602_XB_I2C_REG_ADDR, address);
	m5602_write_bridge(sd, M5602_XB_I2C_CTRL, 0x10 + len);
	m5602_write_bridge(sd, M5602_XB_I2C_CTRL, 0x08);

	for (i = 0; i < len; i++) {
		err = m5602_read_bridge(sd, M5602_XB_I2C_DATA, &(i2c_data[i]));

		PDEBUG(D_CONF, "Reading sensor register "
				"0x%x containing 0x%x ", address, *i2c_data);
	}
	return (err < 0) ? err : 0;
}

int ov9650_write_sensor(struct sd *sd, const u8 address,
			u8 *i2c_data, const u8 len)
{
	int err, i;
	u8 *p;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	/* The ov9650 only supports one byte writes */
	if (len > 1 || !len)
		return -EINVAL;

	memcpy(buf, sensor_urb_skeleton,
	       sizeof(sensor_urb_skeleton));

	buf[11] = sd->sensor->i2c_slave_id;
	buf[15] = address;

	/* Special case larger sensor writes */
	p = buf + 16;

	/* Copy a four byte write sequence for each byte to be written to */
	for (i = 0; i < len; i++) {
		memcpy(p, sensor_urb_skeleton + 16, 4);
		p[3] = i2c_data[i];
		p += 4;
		PDEBUG(D_CONF, "Writing sensor register 0x%x with 0x%x",
		       address, i2c_data[i]);
	}

	/* Copy the tailer */
	memcpy(p, sensor_urb_skeleton + 20, 4);

	/* Set the total length */
	p[3] = 0x10 + len;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      0x04, 0x40, 0x19,
			      0x0000, buf,
			      20 + len * 4, M5602_URB_MSG_TIMEOUT);

	return (err < 0) ? err : 0;
}

int ov9650_probe(struct sd *sd)
{
	u8 prod_id = 0, ver_id = 0, i;

	if (force_sensor) {
		if (force_sensor == OV9650_SENSOR) {
			info("Forcing an %s sensor", ov9650.name);
			goto sensor_found;
		}
		/* If we want to force another sensor,
		   don't try to probe this one */
		return -ENODEV;
	}

	info("Probing for an ov9650 sensor");

	/* Run the pre-init to actually probe the unit */
	for (i = 0; i < ARRAY_SIZE(preinit_ov9650); i++) {
		u8 data = preinit_ov9650[i][2];
		if (preinit_ov9650[i][0] == SENSOR)
			ov9650_write_sensor(sd,
					    preinit_ov9650[i][1], &data, 1);
		else
			m5602_write_bridge(sd, preinit_ov9650[i][1], data);
	}

	if (ov9650_read_sensor(sd, OV9650_PID, &prod_id, 1))
		return -ENODEV;

	if (ov9650_read_sensor(sd, OV9650_VER, &ver_id, 1))
		return -ENODEV;

	if ((prod_id == 0x96) && (ver_id == 0x52)) {
		info("Detected an ov9650 sensor");
		goto sensor_found;
	}

	return -ENODEV;

sensor_found:
	sd->gspca_dev.cam.cam_mode = ov9650.modes;
	sd->gspca_dev.cam.nmodes = ov9650.nmodes;
	sd->desc->ctrls = ov9650.ctrls;
	sd->desc->nctrls = ov9650.nctrls;
	return 0;
}

int ov9650_init(struct sd *sd)
{
	int i, err = 0;
	u8 data;

	if (dump_sensor)
		ov9650_dump_registers(sd);

	for (i = 0; i < ARRAY_SIZE(init_ov9650) && !err; i++) {
		data = init_ov9650[i][2];
		if (init_ov9650[i][0] == SENSOR)
			err = ov9650_write_sensor(sd, init_ov9650[i][1],
						  &data, 1);
		else
			err = m5602_write_bridge(sd, init_ov9650[i][1], data);
	}

	if (!err && dmi_check_system(ov9650_flip_dmi_table)) {
		info("vflip quirk active");
		data = 0x30;
		err = ov9650_write_sensor(sd, OV9650_MVFP, &data, 1);
	}

	return (err < 0) ? err : 0;
}

int ov9650_power_down(struct sd *sd)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(power_down_ov9650); i++) {
		u8 data = power_down_ov9650[i][2];
		if (power_down_ov9650[i][0] == SENSOR)
			ov9650_write_sensor(sd,
					    power_down_ov9650[i][1], &data, 1);
		else
			m5602_write_bridge(sd, power_down_ov9650[i][1], data);
	}

	return 0;
}

int ov9650_get_exposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	err = ov9650_read_sensor(sd, OV9650_COM1, &i2c_data, 1);
	if (err < 0)
		goto out;
	*val = i2c_data & 0x03;

	err = ov9650_read_sensor(sd, OV9650_AECH, &i2c_data, 1);
	if (err < 0)
		goto out;
	*val |= (i2c_data << 2);

	err = ov9650_read_sensor(sd, OV9650_AECHM, &i2c_data, 1);
	if (err < 0)
		goto out;
	*val |= (i2c_data & 0x3f) << 10;

	PDEBUG(D_V4L2, "Read exposure %d", *val);
out:
	return (err < 0) ? err : 0;
}

int ov9650_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	PDEBUG(D_V4L2, "Set exposure to %d",
	       val & 0xffff);

	/* The 6 MSBs */
	i2c_data = (val >> 10) & 0x3f;
	err = ov9650_write_sensor(sd, OV9650_AECHM,
				  &i2c_data, 1);
	if (err < 0)
		goto out;

	/* The 8 middle bits */
	i2c_data = (val >> 2) & 0xff;
	err = ov9650_write_sensor(sd, OV9650_AECH,
				  &i2c_data, 1);
	if (err < 0)
		goto out;

	/* The 2 LSBs */
	i2c_data = val & 0x03;
	err = ov9650_write_sensor(sd, OV9650_COM1, &i2c_data, 1);

out:
	return (err < 0) ? err : 0;
}

int ov9650_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	ov9650_read_sensor(sd, OV9650_VREF, &i2c_data, 1);
	*val = (i2c_data & 0x03) << 8;

	err = ov9650_read_sensor(sd, OV9650_GAIN, &i2c_data, 1);
	*val |= i2c_data;
	PDEBUG(D_V4L2, "Read gain %d", *val);
	return (err < 0) ? err : 0;
}

int ov9650_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	/* The 2 MSB */
	/* Read the OV9650_VREF register first to avoid
	   corrupting the VREF high and low bits */
	ov9650_read_sensor(sd, OV9650_VREF, &i2c_data, 1);
	/* Mask away all uninteresting bits */
	i2c_data = ((val & 0x0300) >> 2) |
			(i2c_data & 0x3F);
	err = ov9650_write_sensor(sd, OV9650_VREF, &i2c_data, 1);

	/* The 8 LSBs */
	i2c_data = val & 0xff;
	err = ov9650_write_sensor(sd, OV9650_GAIN, &i2c_data, 1);
	return (err < 0) ? err : 0;
}

int ov9650_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_RED, &i2c_data, 1);
	*val = i2c_data;

	PDEBUG(D_V4L2, "Read red gain %d", *val);

	return (err < 0) ? err : 0;
}

int ov9650_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set red gain to %d",
			     val & 0xff);

	i2c_data = val & 0xff;
	err = ov9650_write_sensor(sd, OV9650_RED, &i2c_data, 1);

	return (err < 0) ? err : 0;
}

int ov9650_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_BLUE, &i2c_data, 1);
	*val = i2c_data;

	PDEBUG(D_V4L2, "Read blue gain %d", *val);

	return (err < 0) ? err : 0;
}

int ov9650_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set blue gain to %d",
	       val & 0xff);

	i2c_data = val & 0xff;
	err = ov9650_write_sensor(sd, OV9650_BLUE, &i2c_data, 1);

	return (err < 0) ? err : 0;
}

int ov9650_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_MVFP, &i2c_data, 1);
	if (dmi_check_system(ov9650_flip_dmi_table))
		*val = ((i2c_data & OV9650_HFLIP) >> 5) ? 0 : 1;
	else
		*val = (i2c_data & OV9650_HFLIP) >> 5;
	PDEBUG(D_V4L2, "Read horizontal flip %d", *val);

	return (err < 0) ? err : 0;
}

int ov9650_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set horizontal flip to %d", val);
	err = ov9650_read_sensor(sd, OV9650_MVFP, &i2c_data, 1);
	if (err < 0)
		goto out;

	if (dmi_check_system(ov9650_flip_dmi_table))
		i2c_data = ((i2c_data & 0xdf) |
				(((val ? 0 : 1) & 0x01) << 5));
	else
		i2c_data = ((i2c_data & 0xdf) |
				((val & 0x01) << 5));

	err = ov9650_write_sensor(sd, OV9650_MVFP, &i2c_data, 1);
out:
	return (err < 0) ? err : 0;
}

int ov9650_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_MVFP, &i2c_data, 1);
	if (dmi_check_system(ov9650_flip_dmi_table))
		*val = ((i2c_data & 0x10) >> 4) ? 0 : 1;
	else
		*val = (i2c_data & 0x10) >> 4;
	PDEBUG(D_V4L2, "Read vertical flip %d", *val);

	return (err < 0) ? err : 0;
}

int ov9650_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set vertical flip to %d", val);
	err = ov9650_read_sensor(sd, OV9650_MVFP, &i2c_data, 1);
	if (err < 0)
		goto out;

	if (dmi_check_system(ov9650_flip_dmi_table))
		i2c_data = ((i2c_data & 0xef) |
				(((val ? 0 : 1) & 0x01) << 4));
	else
		i2c_data = ((i2c_data & 0xef) |
				((val & 0x01) << 4));

	err = ov9650_write_sensor(sd, OV9650_MVFP, &i2c_data, 1);
out:
	return (err < 0) ? err : 0;
}

int ov9650_get_brightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_VREF, &i2c_data, 1);
	if (err < 0)
		goto out;
	*val = (i2c_data & 0x03) << 8;

	err = ov9650_read_sensor(sd, OV9650_GAIN, &i2c_data, 1);
	*val |= i2c_data;
	PDEBUG(D_V4L2, "Read gain %d", *val);
out:
	return (err < 0) ? err : 0;
}

int ov9650_set_brightness(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set gain to %d", val & 0x3ff);

	/* Read the OV9650_VREF register first to avoid
		corrupting the VREF high and low bits */
	err = ov9650_read_sensor(sd, OV9650_VREF, &i2c_data, 1);
	if (err < 0)
		goto out;

	/* Mask away all uninteresting bits */
	i2c_data = ((val & 0x0300) >> 2) | (i2c_data & 0x3F);
	err = ov9650_write_sensor(sd, OV9650_VREF, &i2c_data, 1);
	if (err < 0)
		goto out;

	/* The 8 LSBs */
	i2c_data = val & 0xff;
	err = ov9650_write_sensor(sd, OV9650_GAIN, &i2c_data, 1);

out:
	return (err < 0) ? err : 0;
}

int ov9650_get_auto_white_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	*val = (i2c_data & OV9650_AWB_EN) >> 1;
	PDEBUG(D_V4L2, "Read auto white balance %d", *val);

	return (err < 0) ? err : 0;
}

int ov9650_set_auto_white_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set auto white balance to %d", val);
	err = ov9650_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	if (err < 0)
		goto out;

	i2c_data = ((i2c_data & 0xfd) | ((val & 0x01) << 1));
	err = ov9650_write_sensor(sd, OV9650_COM8, &i2c_data, 1);
out:
	return (err < 0) ? err : 0;
}

int ov9650_get_auto_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	err = ov9650_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	*val = (i2c_data & OV9650_AGC_EN) >> 2;
	PDEBUG(D_V4L2, "Read auto gain control %d", *val);

	return (err < 0) ? err : 0;
}

int ov9650_set_auto_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_V4L2, "Set auto gain control to %d", val);
	err = ov9650_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	if (err < 0)
		goto out;

	i2c_data = ((i2c_data & 0xfb) | ((val & 0x01) << 2));
	err = ov9650_write_sensor(sd, OV9650_COM8, &i2c_data, 1);
out:
	return (err < 0) ? err : 0;
}

void ov9650_dump_registers(struct sd *sd)
{
	int address;
	info("Dumping the ov9650 register state");
	for (address = 0; address < 0xa9; address++) {
		u8 value;
		ov9650_read_sensor(sd, address, &value, 1);
		info("register 0x%x contains 0x%x",
		     address, value);
	}

	info("ov9650 register state dump complete");

	info("Probing for which registers that are read/write");
	for (address = 0; address < 0xff; address++) {
		u8 old_value, ctrl_value;
		u8 test_value[2] = {0xff, 0xff};

		ov9650_read_sensor(sd, address, &old_value, 1);
		ov9650_write_sensor(sd, address, test_value, 1);
		ov9650_read_sensor(sd, address, &ctrl_value, 1);

		if (ctrl_value == test_value[0])
			info("register 0x%x is writeable", address);
		else
			info("register 0x%x is read only", address);

		/* Restore original value */
		ov9650_write_sensor(sd, address, &old_value, 1);
	}
}
