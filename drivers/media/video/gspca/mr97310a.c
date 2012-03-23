/*
 * Mars MR97310A library
 *
 * The original mr97310a driver, which supported the Aiptek Pencam VGA+, is
 * Copyright (C) 2009 Kyle Guinn <elyk03@gmail.com>
 *
 * Support for the MR97310A cameras in addition to the Aiptek Pencam VGA+
 * and for the routines for detecting and classifying these various cameras,
 * is Copyright (C) 2009 Theodore Kilgore <kilgota@auburn.edu>
 *
 * Support for the control settings for the CIF cameras is
 * Copyright (C) 2009 Hans de Goede <hdegoede@redhat.com> and
 * Thomas Kaiser <thomas@kaiser-linux.li>
 *
 * Support for the control settings for the VGA cameras is
 * Copyright (C) 2009 Theodore Kilgore <kilgota@auburn.edu>
 *
 * Several previously unsupported cameras are owned and have been tested by
 * Hans de Goede <hdegoede@redhat.com> and
 * Thomas Kaiser <thomas@kaiser-linux.li> and
 * Theodore Kilgore <kilgota@auburn.edu> and
 * Edmond Rodriguez <erodrig_97@yahoo.com> and
 * Aurelien Jacobs <aurel@gnuage.org>
 *
 * The MR97311A support in gspca/mars.c has been helpful in understanding some
 * of the registers in these cameras.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "mr97310a"

#include "gspca.h"

#define CAM_TYPE_CIF			0
#define CAM_TYPE_VGA			1

#define MR97310A_BRIGHTNESS_DEFAULT	0

#define MR97310A_EXPOSURE_MIN		0
#define MR97310A_EXPOSURE_MAX		4095
#define MR97310A_EXPOSURE_DEFAULT	1000

#define MR97310A_GAIN_MIN		0
#define MR97310A_GAIN_MAX		31
#define MR97310A_GAIN_DEFAULT		25

#define MR97310A_CONTRAST_MIN		0
#define MR97310A_CONTRAST_MAX		31
#define MR97310A_CONTRAST_DEFAULT	23

#define MR97310A_CS_GAIN_MIN		0
#define MR97310A_CS_GAIN_MAX		0x7ff
#define MR97310A_CS_GAIN_DEFAULT	0x110

#define MR97310A_MIN_CLOCKDIV_MIN	3
#define MR97310A_MIN_CLOCKDIV_MAX	8
#define MR97310A_MIN_CLOCKDIV_DEFAULT	3

MODULE_AUTHOR("Kyle Guinn <elyk03@gmail.com>,"
	      "Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("GSPCA/Mars-Semi MR97310A USB Camera Driver");
MODULE_LICENSE("GPL");

/* global parameters */
static int force_sensor_type = -1;
module_param(force_sensor_type, int, 0644);
MODULE_PARM_DESC(force_sensor_type, "Force sensor type (-1 (auto), 0 or 1)");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;  /* !! must be the first item */
	u8 sof_read;
	u8 cam_type;	/* 0 is CIF and 1 is VGA */
	u8 sensor_type;	/* We use 0 and 1 here, too. */
	u8 do_lcd_stop;
	u8 adj_colors;

	int brightness;
	u16 exposure;
	u32 gain;
	u8 contrast;
	u8 min_clockdiv;
};

struct sensor_w_data {
	u8 reg;
	u8 flags;
	u8 data[16];
	int len;
};

static void sd_stopN(struct gspca_dev *gspca_dev);
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setmin_clockdiv(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getmin_clockdiv(struct gspca_dev *gspca_dev, __s32 *val);
static void setbrightness(struct gspca_dev *gspca_dev);
static void setexposure(struct gspca_dev *gspca_dev);
static void setgain(struct gspca_dev *gspca_dev);
static void setcontrast(struct gspca_dev *gspca_dev);

/* V4L2 controls supported by the driver */
static const struct ctrl sd_ctrls[] = {
/* Separate brightness control description for Argus QuickClix as it has
 * different limits from the other mr97310a cameras, and separate gain
 * control for Sakar CyberPix camera. */
	{
#define NORM_BRIGHTNESS_IDX 0
		{
			.id = V4L2_CID_BRIGHTNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Brightness",
			.minimum = -254,
			.maximum = 255,
			.step = 1,
			.default_value = MR97310A_BRIGHTNESS_DEFAULT,
			.flags = 0,
		},
		.set = sd_setbrightness,
		.get = sd_getbrightness,
	},
	{
#define ARGUS_QC_BRIGHTNESS_IDX 1
		{
			.id = V4L2_CID_BRIGHTNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Brightness",
			.minimum = 0,
			.maximum = 15,
			.step = 1,
			.default_value = MR97310A_BRIGHTNESS_DEFAULT,
			.flags = 0,
		},
		.set = sd_setbrightness,
		.get = sd_getbrightness,
	},
	{
#define EXPOSURE_IDX 2
		{
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Exposure",
			.minimum = MR97310A_EXPOSURE_MIN,
			.maximum = MR97310A_EXPOSURE_MAX,
			.step = 1,
			.default_value = MR97310A_EXPOSURE_DEFAULT,
			.flags = 0,
		},
		.set = sd_setexposure,
		.get = sd_getexposure,
	},
	{
#define GAIN_IDX 3
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Gain",
			.minimum = MR97310A_GAIN_MIN,
			.maximum = MR97310A_GAIN_MAX,
			.step = 1,
			.default_value = MR97310A_GAIN_DEFAULT,
			.flags = 0,
		},
		.set = sd_setgain,
		.get = sd_getgain,
	},
	{
#define SAKAR_CS_GAIN_IDX 4
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Gain",
			.minimum = MR97310A_CS_GAIN_MIN,
			.maximum = MR97310A_CS_GAIN_MAX,
			.step = 1,
			.default_value = MR97310A_CS_GAIN_DEFAULT,
			.flags = 0,
		},
		.set = sd_setgain,
		.get = sd_getgain,
	},
	{
#define CONTRAST_IDX 5
		{
			.id = V4L2_CID_CONTRAST,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Contrast",
			.minimum = MR97310A_CONTRAST_MIN,
			.maximum = MR97310A_CONTRAST_MAX,
			.step = 1,
			.default_value = MR97310A_CONTRAST_DEFAULT,
			.flags = 0,
		},
		.set = sd_setcontrast,
		.get = sd_getcontrast,
	},
	{
#define MIN_CLOCKDIV_IDX 6
		{
			.id = V4L2_CID_PRIVATE_BASE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Minimum Clock Divider",
			.minimum = MR97310A_MIN_CLOCKDIV_MIN,
			.maximum = MR97310A_MIN_CLOCKDIV_MAX,
			.step = 1,
			.default_value = MR97310A_MIN_CLOCKDIV_DEFAULT,
			.flags = 0,
		},
		.set = sd_setmin_clockdiv,
		.get = sd_getmin_clockdiv,
	},
};

static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 4},
	{176, 144, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3},
	{320, 240, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_MR97310A, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/* the bytes to write are in gspca_dev->usb_buf */
static int mr_write(struct gspca_dev *gspca_dev, int len)
{
	int rc;

	rc = usb_bulk_msg(gspca_dev->dev,
			  usb_sndbulkpipe(gspca_dev->dev, 4),
			  gspca_dev->usb_buf, len, NULL, 500);
	if (rc < 0)
		pr_err("reg write [%02x] error %d\n",
		       gspca_dev->usb_buf[0], rc);
	return rc;
}

/* the bytes are read into gspca_dev->usb_buf */
static int mr_read(struct gspca_dev *gspca_dev, int len)
{
	int rc;

	rc = usb_bulk_msg(gspca_dev->dev,
			  usb_rcvbulkpipe(gspca_dev->dev, 3),
			  gspca_dev->usb_buf, len, NULL, 500);
	if (rc < 0)
		pr_err("reg read [%02x] error %d\n",
		       gspca_dev->usb_buf[0], rc);
	return rc;
}

static int sensor_write_reg(struct gspca_dev *gspca_dev, u8 reg, u8 flags,
	const u8 *data, int len)
{
	gspca_dev->usb_buf[0] = 0x1f;
	gspca_dev->usb_buf[1] = flags;
	gspca_dev->usb_buf[2] = reg;
	memcpy(gspca_dev->usb_buf + 3, data, len);

	return mr_write(gspca_dev, len + 3);
}

static int sensor_write_regs(struct gspca_dev *gspca_dev,
	const struct sensor_w_data *data, int len)
{
	int i, rc;

	for (i = 0; i < len; i++) {
		rc = sensor_write_reg(gspca_dev, data[i].reg, data[i].flags,
					  data[i].data, data[i].len);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int sensor_write1(struct gspca_dev *gspca_dev, u8 reg, u8 data)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 buf, confirm_reg;
	int rc;

	buf = data;
	if (sd->cam_type == CAM_TYPE_CIF) {
		rc = sensor_write_reg(gspca_dev, reg, 0x01, &buf, 1);
		confirm_reg = sd->sensor_type ? 0x13 : 0x11;
	} else {
		rc = sensor_write_reg(gspca_dev, reg, 0x00, &buf, 1);
		confirm_reg = 0x11;
	}
	if (rc < 0)
		return rc;

	buf = 0x01;
	rc = sensor_write_reg(gspca_dev, confirm_reg, 0x00, &buf, 1);
	if (rc < 0)
		return rc;

	return 0;
}

static int cam_get_response16(struct gspca_dev *gspca_dev, u8 reg, int verbose)
{
	int err_code;

	gspca_dev->usb_buf[0] = reg;
	err_code = mr_write(gspca_dev, 1);
	if (err_code < 0)
		return err_code;

	err_code = mr_read(gspca_dev, 16);
	if (err_code < 0)
		return err_code;

	if (verbose)
		PDEBUG(D_PROBE, "Register: %02x reads %02x%02x%02x", reg,
		       gspca_dev->usb_buf[0],
		       gspca_dev->usb_buf[1],
		       gspca_dev->usb_buf[2]);

	return 0;
}

static int zero_the_pointer(struct gspca_dev *gspca_dev)
{
	__u8 *data = gspca_dev->usb_buf;
	int err_code;
	u8 status = 0;
	int tries = 0;

	err_code = cam_get_response16(gspca_dev, 0x21, 0);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0x51;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	err_code = cam_get_response16(gspca_dev, 0x21, 0);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0xba;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	err_code = cam_get_response16(gspca_dev, 0x21, 0);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0x00;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	err_code = cam_get_response16(gspca_dev, 0x21, 0);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0x00;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	while (status != 0x0a && tries < 256) {
		err_code = cam_get_response16(gspca_dev, 0x21, 0);
		status = data[0];
		tries++;
		if (err_code < 0)
			return err_code;
	}
	if (status != 0x0a)
		PDEBUG(D_ERR, "status is %02x", status);

	tries = 0;
	while (tries < 4) {
		data[0] = 0x19;
		data[1] = 0x00;
		err_code = mr_write(gspca_dev, 2);
		if (err_code < 0)
			return err_code;

		err_code = cam_get_response16(gspca_dev, 0x21, 0);
		status = data[0];
		tries++;
		if (err_code < 0)
			return err_code;
	}

	data[0] = 0x19;
	err_code = mr_write(gspca_dev, 1);
	if (err_code < 0)
		return err_code;

	err_code = mr_read(gspca_dev, 16);
	if (err_code < 0)
		return err_code;

	return 0;
}

static int stream_start(struct gspca_dev *gspca_dev)
{
	gspca_dev->usb_buf[0] = 0x01;
	gspca_dev->usb_buf[1] = 0x01;
	return mr_write(gspca_dev, 2);
}

static void stream_stop(struct gspca_dev *gspca_dev)
{
	gspca_dev->usb_buf[0] = 0x01;
	gspca_dev->usb_buf[1] = 0x00;
	if (mr_write(gspca_dev, 2) < 0)
		PDEBUG(D_ERR, "Stream Stop failed");
}

static void lcd_stop(struct gspca_dev *gspca_dev)
{
	gspca_dev->usb_buf[0] = 0x19;
	gspca_dev->usb_buf[1] = 0x54;
	if (mr_write(gspca_dev, 2) < 0)
		PDEBUG(D_ERR, "LCD Stop failed");
}

static int isoc_enable(struct gspca_dev *gspca_dev)
{
	gspca_dev->usb_buf[0] = 0x00;
	gspca_dev->usb_buf[1] = 0x4d;  /* ISOC transferring enable... */
	return mr_write(gspca_dev, 2);
}

/* This function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
	int gain_default = MR97310A_GAIN_DEFAULT;
	int err_code;

	cam = &gspca_dev->cam;
	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);
	sd->do_lcd_stop = 0;

	/* Several of the supported CIF cameras share the same USB ID but
	 * require different initializations and different control settings.
	 * The same is true of the VGA cameras. Therefore, we are forced
	 * to start the initialization process in order to determine which
	 * camera is present. Some of the supported cameras require the
	 * memory pointer to be set to 0 as the very first item of business
	 * or else they will not stream. So we do that immediately.
	 */
	err_code = zero_the_pointer(gspca_dev);
	if (err_code < 0)
		return err_code;

	err_code = stream_start(gspca_dev);
	if (err_code < 0)
		return err_code;

	/* Now, the query for sensor type. */
	err_code = cam_get_response16(gspca_dev, 0x07, 1);
	if (err_code < 0)
		return err_code;

	if (id->idProduct == 0x0110 || id->idProduct == 0x010e) {
		sd->cam_type = CAM_TYPE_CIF;
		cam->nmodes--;
		/*
		 * All but one of the known CIF cameras share the same USB ID,
		 * but two different init routines are in use, and the control
		 * settings are different, too. We need to detect which camera
		 * of the two known varieties is connected!
		 *
		 * A list of known CIF cameras follows. They all report either
		 * 0200 for type 0 or 0300 for type 1.
		 * If you have another to report, please do
		 *
		 * Name		sd->sensor_type		reported by
		 *
		 * Sakar 56379 Spy-shot	0		T. Kilgore
		 * Innovage		0		T. Kilgore
		 * Vivitar Mini		0		H. De Goede
		 * Vivitar Mini		0		E. Rodriguez
		 * Vivitar Mini		1		T. Kilgore
		 * Elta-Media 8212dc	1		T. Kaiser
		 * Philips dig. keych.	1		T. Kilgore
		 * Trust Spyc@m 100	1		A. Jacobs
		 */
		switch (gspca_dev->usb_buf[0]) {
		case 2:
			sd->sensor_type = 0;
			break;
		case 3:
			sd->sensor_type = 1;
			break;
		default:
			pr_err("Unknown CIF Sensor id : %02x\n",
			       gspca_dev->usb_buf[1]);
			return -ENODEV;
		}
		PDEBUG(D_PROBE, "MR97310A CIF camera detected, sensor: %d",
		       sd->sensor_type);
	} else {
		sd->cam_type = CAM_TYPE_VGA;

		/*
		 * Here is a table of the responses to the query for sensor
		 * type, from the known MR97310A VGA cameras. Six different
		 * cameras of which five share the same USB ID.
		 *
		 * Name			gspca_dev->usb_buf[]	sd->sensor_type
		 *				sd->do_lcd_stop
		 * Aiptek Pencam VGA+	0300		0		1
		 * ION digital		0300		0		1
		 * Argus DC-1620	0450		1		0
		 * Argus QuickClix	0420		1		1
		 * Sakar 77379 Digital	0350		0		1
		 * Sakar 1638x CyberPix	0120		0		2
		 *
		 * Based upon these results, we assume default settings
		 * and then correct as necessary, as follows.
		 *
		 */

		sd->sensor_type = 1;
		sd->do_lcd_stop = 0;
		sd->adj_colors = 0;
		if (gspca_dev->usb_buf[0] == 0x01) {
			sd->sensor_type = 2;
		} else if ((gspca_dev->usb_buf[0] != 0x03) &&
					(gspca_dev->usb_buf[0] != 0x04)) {
			pr_err("Unknown VGA Sensor id Byte 0: %02x\n",
			       gspca_dev->usb_buf[0]);
			pr_err("Defaults assumed, may not work\n");
			pr_err("Please report this\n");
		}
		/* Sakar Digital color needs to be adjusted. */
		if ((gspca_dev->usb_buf[0] == 0x03) &&
					(gspca_dev->usb_buf[1] == 0x50))
			sd->adj_colors = 1;
		if (gspca_dev->usb_buf[0] == 0x04) {
			sd->do_lcd_stop = 1;
			switch (gspca_dev->usb_buf[1]) {
			case 0x50:
				sd->sensor_type = 0;
				PDEBUG(D_PROBE, "sensor_type corrected to 0");
				break;
			case 0x20:
				/* Nothing to do here. */
				break;
			default:
				pr_err("Unknown VGA Sensor id Byte 1: %02x\n",
				       gspca_dev->usb_buf[1]);
				pr_err("Defaults assumed, may not work\n");
				pr_err("Please report this\n");
			}
		}
		PDEBUG(D_PROBE, "MR97310A VGA camera detected, sensor: %d",
		       sd->sensor_type);
	}
	/* Stop streaming as we've started it only to probe the sensor type. */
	sd_stopN(gspca_dev);

	if (force_sensor_type != -1) {
		sd->sensor_type = !!force_sensor_type;
		PDEBUG(D_PROBE, "Forcing sensor type to: %d",
		       sd->sensor_type);
	}

	/* Setup controls depending on camera type */
	if (sd->cam_type == CAM_TYPE_CIF) {
		/* No brightness for sensor_type 0 */
		if (sd->sensor_type == 0)
			gspca_dev->ctrl_dis = (1 << NORM_BRIGHTNESS_IDX) |
					      (1 << ARGUS_QC_BRIGHTNESS_IDX) |
					      (1 << CONTRAST_IDX) |
					      (1 << SAKAR_CS_GAIN_IDX);
		else
			gspca_dev->ctrl_dis = (1 << ARGUS_QC_BRIGHTNESS_IDX) |
					      (1 << CONTRAST_IDX) |
					      (1 << SAKAR_CS_GAIN_IDX) |
					      (1 << MIN_CLOCKDIV_IDX);
	} else {
		/* All controls need to be disabled if VGA sensor_type is 0 */
		if (sd->sensor_type == 0)
			gspca_dev->ctrl_dis = (1 << NORM_BRIGHTNESS_IDX) |
					      (1 << ARGUS_QC_BRIGHTNESS_IDX) |
					      (1 << EXPOSURE_IDX) |
					      (1 << GAIN_IDX) |
					      (1 << CONTRAST_IDX) |
					      (1 << SAKAR_CS_GAIN_IDX) |
					      (1 << MIN_CLOCKDIV_IDX);
		else if (sd->sensor_type == 2) {
			gspca_dev->ctrl_dis = (1 << NORM_BRIGHTNESS_IDX) |
					      (1 << ARGUS_QC_BRIGHTNESS_IDX) |
					      (1 << GAIN_IDX) |
					      (1 << MIN_CLOCKDIV_IDX);
			gain_default = MR97310A_CS_GAIN_DEFAULT;
		} else if (sd->do_lcd_stop)
			/* Argus QuickClix has different brightness limits */
			gspca_dev->ctrl_dis = (1 << NORM_BRIGHTNESS_IDX) |
					      (1 << CONTRAST_IDX) |
					      (1 << SAKAR_CS_GAIN_IDX);
		else
			gspca_dev->ctrl_dis = (1 << ARGUS_QC_BRIGHTNESS_IDX) |
					      (1 << CONTRAST_IDX) |
					      (1 << SAKAR_CS_GAIN_IDX);
	}

	sd->brightness = MR97310A_BRIGHTNESS_DEFAULT;
	sd->exposure = MR97310A_EXPOSURE_DEFAULT;
	sd->gain = gain_default;
	sd->contrast = MR97310A_CONTRAST_DEFAULT;
	sd->min_clockdiv = MR97310A_MIN_CLOCKDIV_DEFAULT;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

static int start_cif_cam(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 *data = gspca_dev->usb_buf;
	int err_code;
	static const __u8 startup_string[] = {
		0x00,
		0x0d,
		0x01,
		0x00, /* Hsize/8 for 352 or 320 */
		0x00, /* Vsize/4 for 288 or 240 */
		0x13, /* or 0xbb, depends on sensor */
		0x00, /* Hstart, depends on res. */
		0x00, /* reserved ? */
		0x00, /* Vstart, depends on res. and sensor */
		0x50, /* 0x54 to get 176 or 160 */
		0xc0
	};

	/* Note: Some of the above descriptions guessed from MR97113A driver */

	memcpy(data, startup_string, 11);
	if (sd->sensor_type)
		data[5] = 0xbb;

	switch (gspca_dev->width) {
	case 160:
		data[9] |= 0x04;  /* reg 8, 2:1 scale down from 320 */
		/* fall thru */
	case 320:
	default:
		data[3] = 0x28;			   /* reg 2, H size/8 */
		data[4] = 0x3c;			   /* reg 3, V size/4 */
		data[6] = 0x14;			   /* reg 5, H start  */
		data[8] = 0x1a + sd->sensor_type;  /* reg 7, V start  */
		break;
	case 176:
		data[9] |= 0x04;  /* reg 8, 2:1 scale down from 352 */
		/* fall thru */
	case 352:
		data[3] = 0x2c;			   /* reg 2, H size/8 */
		data[4] = 0x48;			   /* reg 3, V size/4 */
		data[6] = 0x06;			   /* reg 5, H start  */
		data[8] = 0x06 - sd->sensor_type;  /* reg 7, V start  */
		break;
	}
	err_code = mr_write(gspca_dev, 11);
	if (err_code < 0)
		return err_code;

	if (!sd->sensor_type) {
		static const struct sensor_w_data cif_sensor0_init_data[] = {
			{0x02, 0x00, {0x03, 0x5a, 0xb5, 0x01,
				      0x0f, 0x14, 0x0f, 0x10}, 8},
			{0x0c, 0x00, {0x04, 0x01, 0x01, 0x00, 0x1f}, 5},
			{0x12, 0x00, {0x07}, 1},
			{0x1f, 0x00, {0x06}, 1},
			{0x27, 0x00, {0x04}, 1},
			{0x29, 0x00, {0x0c}, 1},
			{0x40, 0x00, {0x40, 0x00, 0x04}, 3},
			{0x50, 0x00, {0x60}, 1},
			{0x60, 0x00, {0x06}, 1},
			{0x6b, 0x00, {0x85, 0x85, 0xc8, 0xc8, 0xc8, 0xc8}, 6},
			{0x72, 0x00, {0x1e, 0x56}, 2},
			{0x75, 0x00, {0x58, 0x40, 0xa2, 0x02, 0x31, 0x02,
				      0x31, 0x80, 0x00}, 9},
			{0x11, 0x00, {0x01}, 1},
			{0, 0, {0}, 0}
		};
		err_code = sensor_write_regs(gspca_dev, cif_sensor0_init_data,
					 ARRAY_SIZE(cif_sensor0_init_data));
	} else {	/* sd->sensor_type = 1 */
		static const struct sensor_w_data cif_sensor1_init_data[] = {
			/* Reg 3,4, 7,8 get set by the controls */
			{0x02, 0x00, {0x10}, 1},
			{0x05, 0x01, {0x22}, 1}, /* 5/6 also seen as 65h/32h */
			{0x06, 0x01, {0x00}, 1},
			{0x09, 0x02, {0x0e}, 1},
			{0x0a, 0x02, {0x05}, 1},
			{0x0b, 0x02, {0x05}, 1},
			{0x0c, 0x02, {0x0f}, 1},
			{0x0d, 0x02, {0x07}, 1},
			{0x0e, 0x02, {0x0c}, 1},
			{0x0f, 0x00, {0x00}, 1},
			{0x10, 0x00, {0x06}, 1},
			{0x11, 0x00, {0x07}, 1},
			{0x12, 0x00, {0x00}, 1},
			{0x13, 0x00, {0x01}, 1},
			{0, 0, {0}, 0}
		};
		/* Without this command the cam won't work with USB-UHCI */
		gspca_dev->usb_buf[0] = 0x0a;
		gspca_dev->usb_buf[1] = 0x00;
		err_code = mr_write(gspca_dev, 2);
		if (err_code < 0)
			return err_code;
		err_code = sensor_write_regs(gspca_dev, cif_sensor1_init_data,
					 ARRAY_SIZE(cif_sensor1_init_data));
	}
	return err_code;
}

static int start_vga_cam(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 *data = gspca_dev->usb_buf;
	int err_code;
	static const __u8 startup_string[] =
		{0x00, 0x0d, 0x01, 0x00, 0x00, 0x2b, 0x00, 0x00,
		 0x00, 0x50, 0xc0};
	/* What some of these mean is explained in start_cif_cam(), above */

	memcpy(data, startup_string, 11);
	if (!sd->sensor_type) {
		data[5]  = 0x00;
		data[10] = 0x91;
	}
	if (sd->sensor_type == 2) {
		data[5]  = 0x00;
		data[10] = 0x18;
	}

	switch (gspca_dev->width) {
	case 160:
		data[9] |= 0x0c;  /* reg 8, 4:1 scale down */
		/* fall thru */
	case 320:
		data[9] |= 0x04;  /* reg 8, 2:1 scale down */
		/* fall thru */
	case 640:
	default:
		data[3] = 0x50;  /* reg 2, H size/8 */
		data[4] = 0x78;  /* reg 3, V size/4 */
		data[6] = 0x04;  /* reg 5, H start */
		data[8] = 0x03;  /* reg 7, V start */
		if (sd->sensor_type == 2) {
			data[6] = 2;
			data[8] = 1;
		}
		if (sd->do_lcd_stop)
			data[8] = 0x04;  /* Bayer tile shifted */
		break;

	case 176:
		data[9] |= 0x04;  /* reg 8, 2:1 scale down */
		/* fall thru */
	case 352:
		data[3] = 0x2c;  /* reg 2, H size */
		data[4] = 0x48;  /* reg 3, V size */
		data[6] = 0x94;  /* reg 5, H start */
		data[8] = 0x63;  /* reg 7, V start */
		if (sd->do_lcd_stop)
			data[8] = 0x64;  /* Bayer tile shifted */
		break;
	}

	err_code = mr_write(gspca_dev, 11);
	if (err_code < 0)
		return err_code;

	if (!sd->sensor_type) {
		static const struct sensor_w_data vga_sensor0_init_data[] = {
			{0x01, 0x00, {0x0c, 0x00, 0x04}, 3},
			{0x14, 0x00, {0x01, 0xe4, 0x02, 0x84}, 4},
			{0x20, 0x00, {0x00, 0x80, 0x00, 0x08}, 4},
			{0x25, 0x00, {0x03, 0xa9, 0x80}, 3},
			{0x30, 0x00, {0x30, 0x18, 0x10, 0x18}, 4},
			{0, 0, {0}, 0}
		};
		err_code = sensor_write_regs(gspca_dev, vga_sensor0_init_data,
					 ARRAY_SIZE(vga_sensor0_init_data));
	} else if (sd->sensor_type == 1) {
		static const struct sensor_w_data color_adj[] = {
			{0x02, 0x00, {0x06, 0x59, 0x0c, 0x16, 0x00,
				/* adjusted blue, green, red gain correct
				   too much blue from the Sakar Digital */
				0x05, 0x01, 0x04}, 8}
		};

		static const struct sensor_w_data color_no_adj[] = {
			{0x02, 0x00, {0x06, 0x59, 0x0c, 0x16, 0x00,
				/* default blue, green, red gain settings */
				0x07, 0x00, 0x01}, 8}
		};

		static const struct sensor_w_data vga_sensor1_init_data[] = {
			{0x11, 0x04, {0x01}, 1},
			{0x0a, 0x00, {0x00, 0x01, 0x00, 0x00, 0x01,
			/* These settings may be better for some cameras */
			/* {0x0a, 0x00, {0x01, 0x06, 0x00, 0x00, 0x01, */
				0x00, 0x0a}, 7},
			{0x11, 0x04, {0x01}, 1},
			{0x12, 0x00, {0x00, 0x63, 0x00, 0x70, 0x00, 0x00}, 6},
			{0x11, 0x04, {0x01}, 1},
			{0, 0, {0}, 0}
		};

		if (sd->adj_colors)
			err_code = sensor_write_regs(gspca_dev, color_adj,
					 ARRAY_SIZE(color_adj));
		else
			err_code = sensor_write_regs(gspca_dev, color_no_adj,
					 ARRAY_SIZE(color_no_adj));

		if (err_code < 0)
			return err_code;

		err_code = sensor_write_regs(gspca_dev, vga_sensor1_init_data,
					 ARRAY_SIZE(vga_sensor1_init_data));
	} else {	/* sensor type == 2 */
		static const struct sensor_w_data vga_sensor2_init_data[] = {

			{0x01, 0x00, {0x48}, 1},
			{0x02, 0x00, {0x22}, 1},
			/* Reg 3 msb and 4 is lsb of the exposure setting*/
			{0x05, 0x00, {0x10}, 1},
			{0x06, 0x00, {0x00}, 1},
			{0x07, 0x00, {0x00}, 1},
			{0x08, 0x00, {0x00}, 1},
			{0x09, 0x00, {0x00}, 1},
			/* The following are used in the gain control
			 * which is BTW completely borked in the OEM driver
			 * The values for each color go from 0 to 0x7ff
			 *{0x0a, 0x00, {0x01}, 1},  green1 gain msb
			 *{0x0b, 0x00, {0x10}, 1},  green1 gain lsb
			 *{0x0c, 0x00, {0x01}, 1},  red gain msb
			 *{0x0d, 0x00, {0x10}, 1},  red gain lsb
			 *{0x0e, 0x00, {0x01}, 1},  blue gain msb
			 *{0x0f, 0x00, {0x10}, 1},  blue gain lsb
			 *{0x10, 0x00, {0x01}, 1}, green2 gain msb
			 *{0x11, 0x00, {0x10}, 1}, green2 gain lsb
			 */
			{0x12, 0x00, {0x00}, 1},
			{0x13, 0x00, {0x04}, 1}, /* weird effect on colors */
			{0x14, 0x00, {0x00}, 1},
			{0x15, 0x00, {0x06}, 1},
			{0x16, 0x00, {0x01}, 1},
			{0x17, 0x00, {0xe2}, 1}, /* vertical alignment */
			{0x18, 0x00, {0x02}, 1},
			{0x19, 0x00, {0x82}, 1}, /* don't mess with */
			{0x1a, 0x00, {0x00}, 1},
			{0x1b, 0x00, {0x20}, 1},
			/* {0x1c, 0x00, {0x17}, 1}, contrast control */
			{0x1d, 0x00, {0x80}, 1}, /* moving causes a mess */
			{0x1e, 0x00, {0x08}, 1}, /* moving jams the camera */
			{0x1f, 0x00, {0x0c}, 1},
			{0x20, 0x00, {0x00}, 1},
			{0, 0, {0}, 0}
		};
		err_code = sensor_write_regs(gspca_dev, vga_sensor2_init_data,
					 ARRAY_SIZE(vga_sensor2_init_data));
	}
	return err_code;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int err_code;

	sd->sof_read = 0;

	/* Some of the VGA cameras require the memory pointer
	 * to be set to 0 again. We have been forced to start the
	 * stream in sd_config() to detect the hardware, and closed it.
	 * Thus, we need here to do a completely fresh and clean start. */
	err_code = zero_the_pointer(gspca_dev);
	if (err_code < 0)
		return err_code;

	err_code = stream_start(gspca_dev);
	if (err_code < 0)
		return err_code;

	if (sd->cam_type == CAM_TYPE_CIF) {
		err_code = start_cif_cam(gspca_dev);
	} else {
		err_code = start_vga_cam(gspca_dev);
	}
	if (err_code < 0)
		return err_code;

	setbrightness(gspca_dev);
	setcontrast(gspca_dev);
	setexposure(gspca_dev);
	setgain(gspca_dev);

	return isoc_enable(gspca_dev);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	stream_stop(gspca_dev);
	/* Not all the cams need this, but even if not, probably a good idea */
	zero_the_pointer(gspca_dev);
	if (sd->do_lcd_stop)
		lcd_stop(gspca_dev);
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;
	u8 sign_reg = 7;  /* This reg and the next one used on CIF cams. */
	u8 value_reg = 8; /* VGA cams seem to use regs 0x0b and 0x0c */
	static const u8 quick_clix_table[] =
	/*	  0  1  2   3  4  5  6  7  8  9  10  11  12  13  14  15 */
		{ 0, 4, 8, 12, 1, 2, 3, 5, 6, 9,  7, 10, 13, 11, 14, 15};
	/*
	 * This control is disabled for CIF type 1 and VGA type 0 cameras.
	 * It does not quite act linearly for the Argus QuickClix camera,
	 * but it does control brightness. The values are 0 - 15 only, and
	 * the table above makes them act consecutively.
	 */
	if ((gspca_dev->ctrl_dis & (1 << NORM_BRIGHTNESS_IDX)) &&
	    (gspca_dev->ctrl_dis & (1 << ARGUS_QC_BRIGHTNESS_IDX)))
		return;

	if (sd->cam_type == CAM_TYPE_VGA) {
		sign_reg += 4;
		value_reg += 4;
	}

	/* Note register 7 is also seen as 0x8x or 0xCx in some dumps */
	if (sd->brightness > 0) {
		sensor_write1(gspca_dev, sign_reg, 0x00);
		val = sd->brightness;
	} else {
		sensor_write1(gspca_dev, sign_reg, 0x01);
		val = (257 - sd->brightness);
	}
	/* Use lookup table for funky Argus QuickClix brightness */
	if (sd->do_lcd_stop)
		val = quick_clix_table[val];

	sensor_write1(gspca_dev, value_reg, val);
}

static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int exposure = MR97310A_EXPOSURE_DEFAULT;
	u8 buf[2];

	if (gspca_dev->ctrl_dis & (1 << EXPOSURE_IDX))
		return;

	if (sd->cam_type == CAM_TYPE_CIF && sd->sensor_type == 1) {
		/* This cam does not like exposure settings < 300,
		   so scale 0 - 4095 to 300 - 4095 */
		exposure = (sd->exposure * 9267) / 10000 + 300;
		sensor_write1(gspca_dev, 3, exposure >> 4);
		sensor_write1(gspca_dev, 4, exposure & 0x0f);
	} else if (sd->sensor_type == 2) {
		exposure = sd->exposure;
		exposure >>= 3;
		sensor_write1(gspca_dev, 3, exposure >> 8);
		sensor_write1(gspca_dev, 4, exposure & 0xff);
	} else {
		/* We have both a clock divider and an exposure register.
		   We first calculate the clock divider, as that determines
		   the maximum exposure and then we calculate the exposure
		   register setting (which goes from 0 - 511).

		   Note our 0 - 4095 exposure is mapped to 0 - 511
		   milliseconds exposure time */
		u8 clockdiv = (60 * sd->exposure + 7999) / 8000;

		/* Limit framerate to not exceed usb bandwidth */
		if (clockdiv < sd->min_clockdiv && gspca_dev->width >= 320)
			clockdiv = sd->min_clockdiv;
		else if (clockdiv < 2)
			clockdiv = 2;

		if (sd->cam_type == CAM_TYPE_VGA && clockdiv < 4)
			clockdiv = 4;

		/* Frame exposure time in ms = 1000 * clockdiv / 60 ->
		exposure = (sd->exposure / 8) * 511 / (1000 * clockdiv / 60) */
		exposure = (60 * 511 * sd->exposure) / (8000 * clockdiv);
		if (exposure > 511)
			exposure = 511;

		/* exposure register value is reversed! */
		exposure = 511 - exposure;

		buf[0] = exposure & 0xff;
		buf[1] = exposure >> 8;
		sensor_write_reg(gspca_dev, 0x0e, 0, buf, 2);
		sensor_write1(gspca_dev, 0x02, clockdiv);
	}
}

static void setgain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 gainreg;

	if ((gspca_dev->ctrl_dis & (1 << GAIN_IDX)) &&
	    (gspca_dev->ctrl_dis & (1 << SAKAR_CS_GAIN_IDX)))
		return;

	if (sd->cam_type == CAM_TYPE_CIF && sd->sensor_type == 1)
		sensor_write1(gspca_dev, 0x0e, sd->gain);
	else if (sd->cam_type == CAM_TYPE_VGA && sd->sensor_type == 2)
		for (gainreg = 0x0a; gainreg < 0x11; gainreg += 2) {
			sensor_write1(gspca_dev, gainreg, sd->gain >> 8);
			sensor_write1(gspca_dev, gainreg + 1, sd->gain & 0xff);
		}
	else
		sensor_write1(gspca_dev, 0x10, sd->gain);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (gspca_dev->ctrl_dis & (1 << CONTRAST_IDX))
		return;

	sensor_write1(gspca_dev, 0x1c, sd->contrast);
}


static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		setbrightness(gspca_dev);
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->brightness;
	return 0;
}

static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->exposure = val;
	if (gspca_dev->streaming)
		setexposure(gspca_dev);
	return 0;
}

static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->exposure;
	return 0;
}

static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gain = val;
	if (gspca_dev->streaming)
		setgain(gspca_dev);
	return 0;
}

static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->gain;
	return 0;
}

static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming)
		setcontrast(gspca_dev);
	return 0;
}


static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->contrast;
	return 0;
}

static int sd_setmin_clockdiv(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->min_clockdiv = val;
	if (gspca_dev->streaming)
		setexposure(gspca_dev);
	return 0;
}

static int sd_getmin_clockdiv(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->min_clockdiv;
	return 0;
}

/* Include pac common sof detection functions */
#include "pac_common.h"

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* isoc packet */
			int len)		/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned char *sof;

	sof = pac_find_sof(&sd->sof_read, data, len);
	if (sof) {
		int n;

		/* finish decoding current frame */
		n = sof - data;
		if (n > sizeof pac_sof_marker)
			n -= sizeof pac_sof_marker;
		else
			n = 0;
		gspca_frame_add(gspca_dev, LAST_PACKET,
					data, n);
		/* Start next frame. */
		gspca_frame_add(gspca_dev, FIRST_PACKET,
			pac_sof_marker, sizeof pac_sof_marker);
		len -= sof - data;
		data = sof;
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x08ca, 0x0110)},	/* Trust Spyc@m 100 */
	{USB_DEVICE(0x08ca, 0x0111)},	/* Aiptek Pencam VGA+ */
	{USB_DEVICE(0x093a, 0x010f)},	/* All other known MR97310A VGA cams */
	{USB_DEVICE(0x093a, 0x010e)},	/* All known MR97310A CIF cams */
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		    const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
			       THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
