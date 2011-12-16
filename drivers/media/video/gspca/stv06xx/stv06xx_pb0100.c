/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * P/N 861037:      Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 - QuickCam Express
 * P/N 861055:      Sensor ST VV6410       ASIC STV0610   - LEGO cam
 * P/N 861075-0040: Sensor HDCS1000        ASIC
 * P/N 961179-0700: Sensor ST VV6410       ASIC STV0602   - Dexxa WebCam USB
 * P/N 861040-0000: Sensor ST VV6410       ASIC STV0610   - QuickCam Web
 */

/*
 * The spec file for the PB-0100 suggests the following for best quality
 * images after the sensor has been reset :
 *
 * PB_ADCGAINL      = R60 = 0x03 (3 dec)      : sets low reference of ADC
						to produce good black level
 * PB_PREADCTRL     = R32 = 0x1400 (5120 dec) : Enables global gain changes
						through R53
 * PB_ADCMINGAIN    = R52 = 0x10 (16 dec)     : Sets the minimum gain for
						auto-exposure
 * PB_ADCGLOBALGAIN = R53 = 0x10 (16 dec)     : Sets the global gain
 * PB_EXPGAIN       = R14 = 0x11 (17 dec)     : Sets the auto-exposure value
 * PB_UPDATEINT     = R23 = 0x02 (2 dec)      : Sets the speed on
						auto-exposure routine
 * PB_CFILLIN       = R5  = 0x0E (14 dec)     : Sets the frame rate
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "stv06xx_pb0100.h"

static const struct ctrl pb0100_ctrl[] = {
#define GAIN_IDX 0
	{
		{
			.id		= V4L2_CID_GAIN,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Gain",
			.minimum	= 0,
			.maximum	= 255,
			.step		= 1,
			.default_value  = 128
		},
		.set = pb0100_set_gain,
		.get = pb0100_get_gain
	},
#define RED_BALANCE_IDX 1
	{
		{
			.id		= V4L2_CID_RED_BALANCE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Red Balance",
			.minimum	= -255,
			.maximum	= 255,
			.step		= 1,
			.default_value  = 0
		},
		.set = pb0100_set_red_balance,
		.get = pb0100_get_red_balance
	},
#define BLUE_BALANCE_IDX 2
	{
		{
			.id		= V4L2_CID_BLUE_BALANCE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Blue Balance",
			.minimum	= -255,
			.maximum	= 255,
			.step		= 1,
			.default_value  = 0
		},
		.set = pb0100_set_blue_balance,
		.get = pb0100_get_blue_balance
	},
#define EXPOSURE_IDX 3
	{
		{
			.id		= V4L2_CID_EXPOSURE,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Exposure",
			.minimum	= 0,
			.maximum	= 511,
			.step		= 1,
			.default_value  = 12
		},
		.set = pb0100_set_exposure,
		.get = pb0100_get_exposure
	},
#define AUTOGAIN_IDX 4
	{
		{
			.id		= V4L2_CID_AUTOGAIN,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "Automatic Gain and Exposure",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value  = 1
		},
		.set = pb0100_set_autogain,
		.get = pb0100_get_autogain
	},
#define AUTOGAIN_TARGET_IDX 5
	{
		{
			.id		= V4L2_CTRL_CLASS_USER + 0x1000,
			.type		= V4L2_CTRL_TYPE_INTEGER,
			.name		= "Automatic Gain Target",
			.minimum	= 0,
			.maximum	= 255,
			.step		= 1,
			.default_value  = 128
		},
		.set = pb0100_set_autogain_target,
		.get = pb0100_get_autogain_target
	},
#define NATURAL_IDX 6
	{
		{
			.id		= V4L2_CTRL_CLASS_USER + 0x1001,
			.type		= V4L2_CTRL_TYPE_BOOLEAN,
			.name		= "Natural Light Source",
			.minimum	= 0,
			.maximum	= 1,
			.step		= 1,
			.default_value  = 1
		},
		.set = pb0100_set_natural,
		.get = pb0100_get_natural
	}
};

static struct v4l2_pix_format pb0100_mode[] = {
/* low res / subsample modes disabled as they are only half res horizontal,
   halving the vertical resolution does not seem to work */
	{
		320,
		240,
		V4L2_PIX_FMT_SGRBG8,
		V4L2_FIELD_NONE,
		.sizeimage = 320 * 240,
		.bytesperline = 320,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = PB0100_CROP_TO_VGA
	},
	{
		352,
		288,
		V4L2_PIX_FMT_SGRBG8,
		V4L2_FIELD_NONE,
		.sizeimage = 352 * 288,
		.bytesperline = 352,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	}
};

static int pb0100_probe(struct sd *sd)
{
	u16 sensor;
	int i, err;
	s32 *sensor_settings;

	err = stv06xx_read_sensor(sd, PB_IDENT, &sensor);

	if (err < 0)
		return -ENODEV;

	if ((sensor >> 8) == 0x64) {
		sensor_settings = kmalloc(
				ARRAY_SIZE(pb0100_ctrl) * sizeof(s32),
				GFP_KERNEL);
		if (!sensor_settings)
			return -ENOMEM;

		pr_info("Photobit pb0100 sensor detected\n");

		sd->gspca_dev.cam.cam_mode = pb0100_mode;
		sd->gspca_dev.cam.nmodes = ARRAY_SIZE(pb0100_mode);
		sd->desc.ctrls = pb0100_ctrl;
		sd->desc.nctrls = ARRAY_SIZE(pb0100_ctrl);
		for (i = 0; i < sd->desc.nctrls; i++)
			sensor_settings[i] = pb0100_ctrl[i].qctrl.default_value;
		sd->sensor_priv = sensor_settings;

		return 0;
	}

	return -ENODEV;
}

static int pb0100_start(struct sd *sd)
{
	int err, packet_size, max_packet_size;
	struct usb_host_interface *alt;
	struct usb_interface *intf;
	struct cam *cam = &sd->gspca_dev.cam;
	s32 *sensor_settings = sd->sensor_priv;
	u32 mode = cam->cam_mode[sd->gspca_dev.curr_mode].priv;

	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt)
		return -ENODEV;
	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);

	/* If we don't have enough bandwidth use a lower framerate */
	max_packet_size = sd->sensor->max_packet_size[sd->gspca_dev.curr_mode];
	if (packet_size < max_packet_size)
		stv06xx_write_sensor(sd, PB_ROWSPEED, BIT(4)|BIT(3)|BIT(1));
	else
		stv06xx_write_sensor(sd, PB_ROWSPEED, BIT(5)|BIT(3)|BIT(1));

	/* Setup sensor window */
	if (mode & PB0100_CROP_TO_VGA) {
		stv06xx_write_sensor(sd, PB_RSTART, 30);
		stv06xx_write_sensor(sd, PB_CSTART, 20);
		stv06xx_write_sensor(sd, PB_RWSIZE, 240 - 1);
		stv06xx_write_sensor(sd, PB_CWSIZE, 320 - 1);
	} else {
		stv06xx_write_sensor(sd, PB_RSTART, 8);
		stv06xx_write_sensor(sd, PB_CSTART, 4);
		stv06xx_write_sensor(sd, PB_RWSIZE, 288 - 1);
		stv06xx_write_sensor(sd, PB_CWSIZE, 352 - 1);
	}

	if (mode & PB0100_SUBSAMPLE) {
		stv06xx_write_bridge(sd, STV_Y_CTRL, 0x02); /* Wrong, FIXME */
		stv06xx_write_bridge(sd, STV_X_CTRL, 0x06);

		stv06xx_write_bridge(sd, STV_SCAN_RATE, 0x10);
	} else {
		stv06xx_write_bridge(sd, STV_Y_CTRL, 0x01);
		stv06xx_write_bridge(sd, STV_X_CTRL, 0x0a);
		/* larger -> slower */
		stv06xx_write_bridge(sd, STV_SCAN_RATE, 0x20);
	}

	/* set_gain also sets red and blue balance */
	pb0100_set_gain(&sd->gspca_dev, sensor_settings[GAIN_IDX]);
	pb0100_set_exposure(&sd->gspca_dev, sensor_settings[EXPOSURE_IDX]);
	pb0100_set_autogain_target(&sd->gspca_dev,
				   sensor_settings[AUTOGAIN_TARGET_IDX]);
	pb0100_set_autogain(&sd->gspca_dev, sensor_settings[AUTOGAIN_IDX]);

	err = stv06xx_write_sensor(sd, PB_CONTROL, BIT(5)|BIT(3)|BIT(1));
	PDEBUG(D_STREAM, "Started stream, status: %d", err);

	return (err < 0) ? err : 0;
}

static int pb0100_stop(struct sd *sd)
{
	int err;

	err = stv06xx_write_sensor(sd, PB_ABORTFRAME, 1);

	if (err < 0)
		goto out;

	/* Set bit 1 to zero */
	err = stv06xx_write_sensor(sd, PB_CONTROL, BIT(5)|BIT(3));

	PDEBUG(D_STREAM, "Halting stream");
out:
	return (err < 0) ? err : 0;
}

static void pb0100_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
	kfree(sd->sensor_priv);
}

/* FIXME: Sort the init commands out and put them into tables,
	  this is only for getting the camera to work */
/* FIXME: No error handling for now,
	  add this once the init has been converted to proper tables */
static int pb0100_init(struct sd *sd)
{
	stv06xx_write_bridge(sd, STV_REG00, 1);
	stv06xx_write_bridge(sd, STV_SCAN_RATE, 0);

	/* Reset sensor */
	stv06xx_write_sensor(sd, PB_RESET, 1);
	stv06xx_write_sensor(sd, PB_RESET, 0);

	/* Disable chip */
	stv06xx_write_sensor(sd, PB_CONTROL, BIT(5)|BIT(3));

	/* Gain stuff...*/
	stv06xx_write_sensor(sd, PB_PREADCTRL, BIT(12)|BIT(10)|BIT(6));
	stv06xx_write_sensor(sd, PB_ADCGLOBALGAIN, 12);

	/* Set up auto-exposure */
	/* ADC VREF_HI new setting for a transition
	  from the Expose1 to the Expose2 setting */
	stv06xx_write_sensor(sd, PB_R28, 12);
	/* gain max for autoexposure */
	stv06xx_write_sensor(sd, PB_ADCMAXGAIN, 180);
	/* gain min for autoexposure  */
	stv06xx_write_sensor(sd, PB_ADCMINGAIN, 12);
	/* Maximum frame integration time (programmed into R8)
	   allowed for auto-exposure routine */
	stv06xx_write_sensor(sd, PB_R54, 3);
	/* Minimum frame integration time (programmed into R8)
	   allowed for auto-exposure routine */
	stv06xx_write_sensor(sd, PB_R55, 0);
	stv06xx_write_sensor(sd, PB_UPDATEINT, 1);
	/* R15  Expose0 (maximum that auto-exposure may use) */
	stv06xx_write_sensor(sd, PB_R15, 800);
	/* R17  Expose2 (minimum that auto-exposure may use) */
	stv06xx_write_sensor(sd, PB_R17, 10);

	stv06xx_write_sensor(sd, PB_EXPGAIN, 0);

	/* 0x14 */
	stv06xx_write_sensor(sd, PB_VOFFSET, 0);
	/* 0x0D */
	stv06xx_write_sensor(sd, PB_ADCGAINH, 11);
	/* Set black level (important!) */
	stv06xx_write_sensor(sd, PB_ADCGAINL, 0);

	/* ??? */
	stv06xx_write_bridge(sd, STV_REG00, 0x11);
	stv06xx_write_bridge(sd, STV_REG03, 0x45);
	stv06xx_write_bridge(sd, STV_REG04, 0x07);

	/* Scan/timing for the sensor */
	stv06xx_write_sensor(sd, PB_ROWSPEED, BIT(4)|BIT(3)|BIT(1));
	stv06xx_write_sensor(sd, PB_CFILLIN, 14);
	stv06xx_write_sensor(sd, PB_VBL, 0);
	stv06xx_write_sensor(sd, PB_FINTTIME, 0);
	stv06xx_write_sensor(sd, PB_RINTTIME, 123);

	stv06xx_write_bridge(sd, STV_REG01, 0xc2);
	stv06xx_write_bridge(sd, STV_REG02, 0xb0);
	return 0;
}

static int pb0100_dump(struct sd *sd)
{
	return 0;
}

static int pb0100_get_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[GAIN_IDX];

	return 0;
}

static int pb0100_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	if (sensor_settings[AUTOGAIN_IDX])
		return -EBUSY;

	sensor_settings[GAIN_IDX] = val;
	err = stv06xx_write_sensor(sd, PB_G1GAIN, val);
	if (!err)
		err = stv06xx_write_sensor(sd, PB_G2GAIN, val);
	PDEBUG(D_V4L2, "Set green gain to %d, status: %d", val, err);

	if (!err)
		err = pb0100_set_red_balance(gspca_dev,
					     sensor_settings[RED_BALANCE_IDX]);
	if (!err)
		err = pb0100_set_blue_balance(gspca_dev,
					    sensor_settings[BLUE_BALANCE_IDX]);

	return err;
}

static int pb0100_get_red_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[RED_BALANCE_IDX];

	return 0;
}

static int pb0100_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	if (sensor_settings[AUTOGAIN_IDX])
		return -EBUSY;

	sensor_settings[RED_BALANCE_IDX] = val;
	val += sensor_settings[GAIN_IDX];
	if (val < 0)
		val = 0;
	else if (val > 255)
		val = 255;

	err = stv06xx_write_sensor(sd, PB_RGAIN, val);
	PDEBUG(D_V4L2, "Set red gain to %d, status: %d", val, err);

	return err;
}

static int pb0100_get_blue_balance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[BLUE_BALANCE_IDX];

	return 0;
}

static int pb0100_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	if (sensor_settings[AUTOGAIN_IDX])
		return -EBUSY;

	sensor_settings[BLUE_BALANCE_IDX] = val;
	val += sensor_settings[GAIN_IDX];
	if (val < 0)
		val = 0;
	else if (val > 255)
		val = 255;

	err = stv06xx_write_sensor(sd, PB_BGAIN, val);
	PDEBUG(D_V4L2, "Set blue gain to %d, status: %d", val, err);

	return err;
}

static int pb0100_get_exposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[EXPOSURE_IDX];

	return 0;
}

static int pb0100_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	if (sensor_settings[AUTOGAIN_IDX])
		return -EBUSY;

	sensor_settings[EXPOSURE_IDX] = val;
	err = stv06xx_write_sensor(sd, PB_RINTTIME, val);
	PDEBUG(D_V4L2, "Set exposure to %d, status: %d", val, err);

	return err;
}

static int pb0100_get_autogain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[AUTOGAIN_IDX];

	return 0;
}

static int pb0100_set_autogain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[AUTOGAIN_IDX] = val;
	if (sensor_settings[AUTOGAIN_IDX]) {
		if (sensor_settings[NATURAL_IDX])
			val = BIT(6)|BIT(4)|BIT(0);
		else
			val = BIT(4)|BIT(0);
	} else
		val = 0;

	err = stv06xx_write_sensor(sd, PB_EXPGAIN, val);
	PDEBUG(D_V4L2, "Set autogain to %d (natural: %d), status: %d",
	       sensor_settings[AUTOGAIN_IDX], sensor_settings[NATURAL_IDX],
	       err);

	return err;
}

static int pb0100_get_autogain_target(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[AUTOGAIN_TARGET_IDX];

	return 0;
}

static int pb0100_set_autogain_target(struct gspca_dev *gspca_dev, __s32 val)
{
	int err, totalpixels, brightpixels, darkpixels;
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[AUTOGAIN_TARGET_IDX] = val;

	/* Number of pixels counted by the sensor when subsampling the pixels.
	 * Slightly larger than the real value to avoid oscillation */
	totalpixels = gspca_dev->width * gspca_dev->height;
	totalpixels = totalpixels/(8*8) + totalpixels/(64*64);

	brightpixels = (totalpixels * val) >> 8;
	darkpixels   = totalpixels - brightpixels;
	err = stv06xx_write_sensor(sd, PB_R21, brightpixels);
	if (!err)
		err = stv06xx_write_sensor(sd, PB_R22, darkpixels);

	PDEBUG(D_V4L2, "Set autogain target to %d, status: %d", val, err);

	return err;
}

static int pb0100_get_natural(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	*val = sensor_settings[NATURAL_IDX];

	return 0;
}

static int pb0100_set_natural(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 *sensor_settings = sd->sensor_priv;

	sensor_settings[NATURAL_IDX] = val;

	return pb0100_set_autogain(gspca_dev, sensor_settings[AUTOGAIN_IDX]);
}
