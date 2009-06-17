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

static int s5k4aa_get_exposure(struct gspca_dev *gspca_dev, __s32 *val);
static int s5k4aa_set_exposure(struct gspca_dev *gspca_dev, __s32 val);
static int s5k4aa_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
static int s5k4aa_set_vflip(struct gspca_dev *gspca_dev, __s32 val);
static int s5k4aa_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
static int s5k4aa_set_hflip(struct gspca_dev *gspca_dev, __s32 val);
static int s5k4aa_get_gain(struct gspca_dev *gspca_dev, __s32 *val);
static int s5k4aa_set_gain(struct gspca_dev *gspca_dev, __s32 val);
static int s5k4aa_get_noise(struct gspca_dev *gspca_dev, __s32 *val);
static int s5k4aa_set_noise(struct gspca_dev *gspca_dev, __s32 val);
static int s5k4aa_get_brightness(struct gspca_dev *gspca_dev, __s32 *val);
static int s5k4aa_set_brightness(struct gspca_dev *gspca_dev, __s32 val);

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
	}, {
		.ident = "MSI L735",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-1717X")
		}
	}, {
		.ident = "Lenovo Y300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "L3000 Y300"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Y300")
		}
	},
	{ }
};

static struct v4l2_pix_format s5k4aa_modes[] = {
	{
		640,
		480,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			640 * 480,
		.bytesperline = 640,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
	{
		1280,
		1024,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			1280 * 1024,
		.bytesperline = 1280,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	}
};

static const struct ctrl s5k4aa_ctrls[] = {
#define VFLIP_IDX 0
	{
		{
			.id 		= V4L2_CID_VFLIP,
			.type 		= V4L2_CTRL_TYPE_BOOLEAN,
			.name 		= "vertical flip",
			.minimum 	= 0,
			.maximum 	= 1,
			.step 		= 1,
			.default_value 	= 0
		},
		.set = s5k4aa_set_vflip,
		.get = s5k4aa_get_vflip
	},
#define HFLIP_IDX 1
	{
		{
			.id 		= V4L2_CID_HFLIP,
			.type 		= V4L2_CTRL_TYPE_BOOLEAN,
			.name 		= "horizontal flip",
			.minimum 	= 0,
			.maximum 	= 1,
			.step 		= 1,
			.default_value 	= 0
		},
		.set = s5k4aa_set_hflip,
		.get = s5k4aa_get_hflip
	},
#define GAIN_IDX 2
	{
		{
			.id		= V4L2_CID_GAIN,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Gain",
			.minimum	= 0,
			.maximum	= 127,
			.step		= 1,
			.default_value	= S5K4AA_DEFAULT_GAIN,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = s5k4aa_set_gain,
		.get = s5k4aa_get_gain
	},
#define EXPOSURE_IDX 3
	{
		{
			.id		= V4L2_CID_EXPOSURE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Exposure",
			.minimum	= 13,
			.maximum	= 0xfff,
			.step		= 1,
			.default_value	= 0x100,
			.flags		= V4L2_CTRL_FLAG_SLIDER
		},
		.set = s5k4aa_set_exposure,
		.get = s5k4aa_get_exposure
	},
#define NOISE_SUPP_IDX 4
	{
		{
			.id		= V4L2_CID_PRIVATE_BASE,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "Noise suppression (smoothing)",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value	= 1,
		},
			.set = s5k4aa_set_noise,
			.get = s5k4aa_get_noise
	},
#define BRIGHTNESS_IDX 5
	{
		{
			.id		= V4L2_CID_BRIGHTNESS,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Brightness",
			.minimum	= 0,
			.maximum	= 0x1f,
			.step		= 1,
			.default_value	= S5K4AA_DEFAULT_BRIGHTNESS,
		},
			.set = s5k4aa_set_brightness,
			.get = s5k4aa_get_brightness
	},

};

static void s5k4aa_dump_registers(struct sd *sd);

int s5k4aa_probe(struct sd *sd)
{
	u8 prod_id[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	const u8 expected_prod_id[6] = {0x00, 0x10, 0x00, 0x4b, 0x33, 0x75};
	int i, err = 0;
	s32 *sensor_settings;

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
	sensor_settings = kmalloc(
		ARRAY_SIZE(s5k4aa_ctrls) * sizeof(s32), GFP_KERNEL);
	if (!sensor_settings)
		return -ENOMEM;

	sd->gspca_dev.cam.cam_mode = s5k4aa_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(s5k4aa_modes);
	sd->desc->ctrls = s5k4aa_ctrls;
	sd->desc->nctrls = ARRAY_SIZE(s5k4aa_ctrls);

	for (i = 0; i < ARRAY_SIZE(s5k4aa_ctrls); i++)
		sensor_settings[i] = s5k4aa_ctrls[i].qctrl.default_value;
	sd->sensor_priv = sensor_settings;

	return 0;
}

int s5k4aa_start(struct sd *sd)
{
	int i, err = 0;
	u8 data[2];
	struct cam *cam = &sd->gspca_dev.cam;
	s32 *sensor_settings = sd->sensor_priv;

	switch (cam->cam_mode[sd->gspca_dev.curr_mode].width) {
	case 1280:
		PDEBUG(D_V4L2, "Configuring camera for SXGA mode");

		for (i = 0; i < ARRAY_SIZE(SXGA_s5k4aa); i++) {
			switch (SXGA_s5k4aa[i][0]) {
			case BRIDGE:
				err = m5602_write_bridge(sd,
						 SXGA_s5k4aa[i][1],
						 SXGA_s5k4aa[i][2]);
			break;

			case SENSOR:
				data[0] = SXGA_s5k4aa[i][2];
				err = m5602_write_sensor(sd,
						 SXGA_s5k4aa[i][1],
						 data, 1);
			break;

			case SENSOR_LONG:
				data[0] = SXGA_s5k4aa[i][2];
				data[1] = SXGA_s5k4aa[i][3];
				err = m5602_write_sensor(sd,
						  SXGA_s5k4aa[i][1],
						  data, 2);
			break;

			default:
				err("Invalid stream command, exiting init");
				return -EINVAL;
			}
		}
		err = s5k4aa_set_noise(&sd->gspca_dev, 0);
		if (err < 0)
			return err;
		break;

	case 640:
		PDEBUG(D_V4L2, "Configuring camera for VGA mode");

		for (i = 0; i < ARRAY_SIZE(VGA_s5k4aa); i++) {
			switch (VGA_s5k4aa[i][0]) {
			case BRIDGE:
				err = m5602_write_bridge(sd,
						 VGA_s5k4aa[i][1],
						 VGA_s5k4aa[i][2]);
			break;

			case SENSOR:
				data[0] = VGA_s5k4aa[i][2];
				err = m5602_write_sensor(sd,
						 VGA_s5k4aa[i][1],
						 data, 1);
			break;

			case SENSOR_LONG:
				data[0] = VGA_s5k4aa[i][2];
				data[1] = VGA_s5k4aa[i][3];
				err = m5602_write_sensor(sd,
						  VGA_s5k4aa[i][1],
						  data, 2);
			break;

			default:
				err("Invalid stream command, exiting init");
				return -EINVAL;
			}
		}
		err = s5k4aa_set_noise(&sd->gspca_dev, 1);
		if (err < 0)
			return err;
		break;
	}
	if (err < 0)
		return err;

	err = s5k4aa_set_exposure(&sd->gspca_dev,
				   sensor_settings[EXPOSURE_IDX]);
	if (err < 0)
		return err;

	err = s5k4aa_set_gain(&sd->gspca_dev, sensor_settings[GAIN_IDX]);
	if (err < 0)
		return err;

	err = s5k4aa_set_brightness(&sd->gspca_dev,
				     sensor_settings[BRIGHTNESS_IDX]);
	if (err < 0)
		return err;

	err = s5k4aa_set_noise(&sd->gspca_dev, sensor_settings[NOISE_SUPP_IDX]);
	if (err < 0)
		return err;

	err = s5k4aa_set_vflip(&sd->gspca_dev, sensor_settings[VFLIP_IDX]);
	if (err < 0)
		return err;

	return s5k4aa_set_hflip(&sd->gspca_dev, sensor_settings[HFLIP_IDX]);
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

	return err;
}

static int s5k4aa_get_exposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[EXPOSURE_IDX];
	PDEBUG(D_V4L2, "Read exposure %d", *val);

	return 0;
}

static int s5k4aa_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	sensor_settings[EXPOSURE_IDX] = val;
	PDEBUG(D_V4L2, "Set exposure to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;
	data = (val >> 8) & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_EXPOSURE_HI, &data, 1);
	if (err < 0)
		return err;
	data = val & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_EXPOSURE_LO, &data, 1);

	return err;
}

static int s5k4aa_get_vflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[VFLIP_IDX];
	PDEBUG(D_V4L2, "Read vertical flip %d", *val);

	return 0;
}

static int s5k4aa_set_vflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	sensor_settings[VFLIP_IDX] = val;

	PDEBUG(D_V4L2, "Set vertical flip to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	if (dmi_check_system(s5k4aa_vflip_dmi_table))
		val = !val;

	data = ((data & ~S5K4AA_RM_V_FLIP) | ((val & 0x01) << 7));
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	if (err < 0)
		return err;
	data = (data & 0xfe) | !val;
	err = m5602_write_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	return err;
}

static int s5k4aa_get_hflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[HFLIP_IDX];
	PDEBUG(D_V4L2, "Read horizontal flip %d", *val);

	return 0;
}

static int s5k4aa_set_hflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	sensor_settings[HFLIP_IDX] = val;

	PDEBUG(D_V4L2, "Set horizontal flip to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	if (dmi_check_system(s5k4aa_vflip_dmi_table))
		val = !val;

	data = ((data & ~S5K4AA_RM_H_FLIP) | ((val & 0x01) << 6));
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
	if (err < 0)
		return err;
	data = (data & 0xfe) | !val;
	err = m5602_write_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
	return err;
}

static int s5k4aa_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[GAIN_IDX];
	PDEBUG(D_V4L2, "Read gain %d", *val);
	return 0;
}

static int s5k4aa_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	sensor_settings[GAIN_IDX] = val;

	PDEBUG(D_V4L2, "Set gain to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	data = val & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_GAIN, &data, 1);

	return err;
}

static int s5k4aa_get_brightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[BRIGHTNESS_IDX];
	PDEBUG(D_V4L2, "Read brightness %d", *val);
	return 0;
}

static int s5k4aa_set_brightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	sensor_settings[BRIGHTNESS_IDX] = val;

	PDEBUG(D_V4L2, "Set brightness to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	data = val & 0xff;
	return m5602_write_sensor(sd, S5K4AA_BRIGHTNESS, &data, 1);
}

static int s5k4aa_get_noise(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[NOISE_SUPP_IDX];
	PDEBUG(D_V4L2, "Read noise %d", *val);
	return 0;
}

static int s5k4aa_set_noise(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	sensor_settings[NOISE_SUPP_IDX] = val;

	PDEBUG(D_V4L2, "Set noise to %d", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	data = val & 0x01;
	return m5602_write_sensor(sd, S5K4AA_NOISE_SUPP, &data, 1);
}

void s5k4aa_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
	kfree(sd->sensor_priv);
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
