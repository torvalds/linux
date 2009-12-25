/*
 * Mars MR97310A library
 *
 * Copyright (C) 2009 Kyle Guinn <elyk03@gmail.com>
 *
 * Support for the MR97310A cameras in addition to the Aiptek Pencam VGA+
 * and for the routines for detecting and classifying these various cameras,
 *
 * Copyright (C) 2009 Theodore Kilgore <kilgota@auburn.edu>
 *
 * Acknowledgements:
 *
 * The MR97311A support in gspca/mars.c has been helpful in understanding some
 * of the registers in these cameras.
 *
 * Hans de Goede <hdgoede@redhat.com> and
 * Thomas Kaiser <thomas@kaiser-linux.li>
 * have assisted with their experience. Each of them has also helped by
 * testing a previously unsupported camera.
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

#define MODULE_NAME "mr97310a"

#include "gspca.h"

#define CAM_TYPE_CIF			0
#define CAM_TYPE_VGA			1

#define MR97310A_BRIGHTNESS_MIN		-254
#define MR97310A_BRIGHTNESS_MAX		255
#define MR97310A_BRIGHTNESS_DEFAULT	0

#define MR97310A_EXPOSURE_MIN		300
#define MR97310A_EXPOSURE_MAX		4095
#define MR97310A_EXPOSURE_DEFAULT	1000

#define MR97310A_GAIN_MIN		0
#define MR97310A_GAIN_MAX		31
#define MR97310A_GAIN_DEFAULT		25

MODULE_AUTHOR("Kyle Guinn <elyk03@gmail.com>,"
	      "Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("GSPCA/Mars-Semi MR97310A USB Camera Driver");
MODULE_LICENSE("GPL");

/* global parameters */
int force_sensor_type = -1;
module_param(force_sensor_type, int, 0644);
MODULE_PARM_DESC(force_sensor_type, "Force sensor type (-1 (auto), 0 or 1)");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;  /* !! must be the first item */
	u8 sof_read;
	u8 cam_type;	/* 0 is CIF and 1 is VGA */
	u8 sensor_type;	/* We use 0 and 1 here, too. */
	u8 do_lcd_stop;

	int brightness;
	u16 exposure;
	u8 gain;
};

struct sensor_w_data {
	u8 reg;
	u8 flags;
	u8 data[16];
	int len;
};

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val);
static void setbrightness(struct gspca_dev *gspca_dev);
static void setexposure(struct gspca_dev *gspca_dev);
static void setgain(struct gspca_dev *gspca_dev);

/* V4L2 controls supported by the driver */
static struct ctrl sd_ctrls[] = {
	{
#define BRIGHTNESS_IDX 0
		{
			.id = V4L2_CID_BRIGHTNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Brightness",
			.minimum = MR97310A_BRIGHTNESS_MIN,
			.maximum = MR97310A_BRIGHTNESS_MAX,
			.step = 1,
			.default_value = MR97310A_BRIGHTNESS_DEFAULT,
			.flags = 0,
		},
		.set = sd_setbrightness,
		.get = sd_getbrightness,
	},
	{
#define EXPOSURE_IDX 1
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
#define GAIN_IDX 2
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
		PDEBUG(D_ERR, "reg write [%02x] error %d",
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
		PDEBUG(D_ERR, "reg read [%02x] error %d",
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
	rc = sensor_write_reg(gspca_dev, reg, 0x01, &buf, 1);
	if (rc < 0)
		return rc;

	buf = 0x01;
	confirm_reg = sd->sensor_type ? 0x13 : 0x11;
	rc = sensor_write_reg(gspca_dev, confirm_reg, 0x00, &buf, 1);
	if (rc < 0)
		return rc;

	return 0;
}

static int cam_get_response16(struct gspca_dev *gspca_dev)
{
	__u8 *data = gspca_dev->usb_buf;
	int err_code;

	data[0] = 0x21;
	err_code = mr_write(gspca_dev, 1);
	if (err_code < 0)
		return err_code;

	err_code = mr_read(gspca_dev, 16);
	return err_code;
}

static int zero_the_pointer(struct gspca_dev *gspca_dev)
{
	__u8 *data = gspca_dev->usb_buf;
	int err_code;
	u8 status = 0;
	int tries = 0;

	err_code = cam_get_response16(gspca_dev);
	if (err_code < 0)
		return err_code;

	err_code = mr_write(gspca_dev, 1);
	data[0] = 0x19;
	data[1] = 0x51;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	err_code = cam_get_response16(gspca_dev);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0xba;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	err_code = cam_get_response16(gspca_dev);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0x00;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	err_code = cam_get_response16(gspca_dev);
	if (err_code < 0)
		return err_code;

	data[0] = 0x19;
	data[1] = 0x00;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	while (status != 0x0a && tries < 256) {
		err_code = cam_get_response16(gspca_dev);
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

		err_code = cam_get_response16(gspca_dev);
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

static u8 get_sensor_id(struct gspca_dev *gspca_dev)
{
	int err_code;

	gspca_dev->usb_buf[0] = 0x1e;
	err_code = mr_write(gspca_dev, 1);
	if (err_code < 0)
		return err_code;

	err_code = mr_read(gspca_dev, 16);
	if (err_code < 0)
		return err_code;

	PDEBUG(D_PROBE, "Byte zero reported is %01x", gspca_dev->usb_buf[0]);

	return gspca_dev->usb_buf[0];
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
	__u8 *data = gspca_dev->usb_buf;
	int err_code;

	cam = &gspca_dev->cam;
	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);

	if (id->idProduct == 0x010e) {
		sd->cam_type = CAM_TYPE_CIF;
		cam->nmodes--;

		data[0] = 0x01;
		data[1] = 0x01;
		err_code = mr_write(gspca_dev, 2);
		if (err_code < 0)
			return err_code;

		msleep(200);
		data[0] = get_sensor_id(gspca_dev);
		/*
		 * Known CIF cameras. If you have another to report, please do
		 *
		 * Name			byte just read		sd->sensor_type
		 *					reported by
		 * Sakar Spy-shot	0x28		T. Kilgore	0
		 * Innovage		0xf5 (unstable)	T. Kilgore	0
		 * Vivitar Mini		0x53		H. De Goede	0
		 * Vivitar Mini		0x04 / 0x24	E. Rodriguez	0
		 * Vivitar Mini		0x08		T. Kilgore	1
		 * Elta-Media 8212dc	0x23		T. Kaiser	1
		 * Philips dig. keych.	0x37		T. Kilgore	1
		 */
		if ((data[0] & 0x78) == 8 ||
		    ((data[0] & 0x2) == 0x2 && data[0] != 0x53))
			sd->sensor_type = 1;
		else
			sd->sensor_type = 0;

		PDEBUG(D_PROBE, "MR97310A CIF camera detected, sensor: %d",
		       sd->sensor_type);

		if (force_sensor_type != -1) {
			sd->sensor_type = !! force_sensor_type;
			PDEBUG(D_PROBE, "Forcing sensor type to: %d",
			       sd->sensor_type);
		}

		if (sd->sensor_type == 0)
			gspca_dev->ctrl_dis = (1 << BRIGHTNESS_IDX);
	} else {
		sd->cam_type = CAM_TYPE_VGA;
		PDEBUG(D_PROBE, "MR97310A VGA camera detected");
		gspca_dev->ctrl_dis = (1 << BRIGHTNESS_IDX) |
				      (1 << EXPOSURE_IDX) | (1 << GAIN_IDX);
	}

	sd->brightness = MR97310A_BRIGHTNESS_DEFAULT;
	sd->exposure = MR97310A_EXPOSURE_DEFAULT;
	sd->gain = MR97310A_GAIN_DEFAULT;

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
	const __u8 startup_string[] = {
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
	data[0] = 0x01;
	data[1] = 0x01;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

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
		const struct sensor_w_data cif_sensor0_init_data[] = {
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
		const struct sensor_w_data cif_sensor1_init_data[] = {
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
	if (err_code < 0)
		return err_code;

	setbrightness(gspca_dev);
	setexposure(gspca_dev);
	setgain(gspca_dev);

	msleep(200);

	data[0] = 0x00;
	data[1] = 0x4d;  /* ISOC transfering enable... */
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	return 0;
}

static int start_vga_cam(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 *data = gspca_dev->usb_buf;
	int err_code;
	const __u8 startup_string[] = {0x00, 0x0d, 0x01, 0x00, 0x00, 0x2b,
				       0x00, 0x00, 0x00, 0x50, 0xc0};

	/* What some of these mean is explained in start_cif_cam(), above */
	sd->sof_read = 0;

	/*
	 * We have to know which camera we have, because the register writes
	 * depend upon the camera. This test, run before we actually enter
	 * the initialization routine, distinguishes most of the cameras, If
	 * needed, another routine is done later, too.
	 */
	memset(data, 0, 16);
	data[0] = 0x20;
	err_code = mr_write(gspca_dev, 1);
	if (err_code < 0)
		return err_code;

	err_code = mr_read(gspca_dev, 16);
	if (err_code < 0)
		return err_code;

	PDEBUG(D_PROBE, "Byte reported is %02x", data[0]);

	msleep(200);
	/*
	 * Known VGA cameras. If you have another to report, please do
	 *
	 * Name			byte just read		sd->sensor_type
	 *				sd->do_lcd_stop
	 * Aiptek Pencam VGA+	0x31		0	1
	 * ION digital		0x31		0	1
	 * Argus DC-1620	0x30		1	0
	 * Argus QuickClix	0x30		1	1 (not caught here)
	 */
	sd->sensor_type = data[0] & 1;
	sd->do_lcd_stop = (~data[0]) & 1;



	/* Streaming setup begins here. */


	data[0] = 0x01;
	data[1] = 0x01;
	err_code = mr_write(gspca_dev, 2);
	if (err_code < 0)
		return err_code;

	/*
	 * A second test can now resolve any remaining ambiguity in the
	 * identification of the camera type,
	 */
	if (!sd->sensor_type) {
		data[0] = get_sensor_id(gspca_dev);
		if (data[0] == 0x7f) {
			sd->sensor_type = 1;
			PDEBUG(D_PROBE, "sensor_type corrected to 1");
		}
		msleep(200);
	}

	if (force_sensor_type != -1) {
		sd->sensor_type = !! force_sensor_type;
		PDEBUG(D_PROBE, "Forcing sensor type to: %d",
		       sd->sensor_type);
	}

	/*
	 * Known VGA cameras.
	 * This test is only run if the previous test returned 0x30, but
	 * here is the information for all others, too, just for reference.
	 *
	 * Name			byte just read		sd->sensor_type
	 *
	 * Aiptek Pencam VGA+	0xfb	(this test not run)	1
	 * ION digital		0xbd	(this test not run)	1
	 * Argus DC-1620	0xe5	(no change)		0
	 * Argus QuickClix	0x7f	(reclassified)		1
	 */
	memcpy(data, startup_string, 11);
	if (!sd->sensor_type) {
		data[5]  = 0x00;
		data[10] = 0x91;
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
		/* The only known sensor_type 0 cam is the Argus DC-1620 */
		const struct sensor_w_data vga_sensor0_init_data[] = {
			{0x01, 0x00, {0x0c, 0x00, 0x04}, 3},
			{0x14, 0x00, {0x01, 0xe4, 0x02, 0x84}, 4},
			{0x20, 0x00, {0x00, 0x80, 0x00, 0x08}, 4},
			{0x25, 0x00, {0x03, 0xa9, 0x80}, 3},
			{0x30, 0x00, {0x30, 0x18, 0x10, 0x18}, 4},
			{0, 0, {0}, 0}
		};
		err_code = sensor_write_regs(gspca_dev, vga_sensor0_init_data,
					 ARRAY_SIZE(vga_sensor0_init_data));
	} else {	/* sd->sensor_type = 1 */
		const struct sensor_w_data vga_sensor1_init_data[] = {
			{0x02, 0x00, {0x06, 0x59, 0x0c, 0x16, 0x00,
				0x07, 0x00, 0x01}, 8},
			{0x11, 0x04, {0x01}, 1},
			/*{0x0a, 0x00, {0x00, 0x01, 0x00, 0x00, 0x01, */
			{0x0a, 0x00, {0x01, 0x06, 0x00, 0x00, 0x01,
				0x00, 0x0a}, 7},
			{0x11, 0x04, {0x01}, 1},
			{0x12, 0x00, {0x00, 0x63, 0x00, 0x70, 0x00, 0x00}, 6},
			{0x11, 0x04, {0x01}, 1},
			{0, 0, {0}, 0}
		};
		err_code = sensor_write_regs(gspca_dev, vga_sensor1_init_data,
					 ARRAY_SIZE(vga_sensor1_init_data));
	}
	if (err_code < 0)
		return err_code;

	msleep(200);
	data[0] = 0x00;
	data[1] = 0x4d;  /* ISOC transfering enable... */
	err_code = mr_write(gspca_dev, 2);

	return err_code;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int err_code;
	struct cam *cam;

	cam = &gspca_dev->cam;
	sd->sof_read = 0;
	/*
	 * Some of the supported cameras require the memory pointer to be
	 * set to 0, or else they will not stream.
	 */
	zero_the_pointer(gspca_dev);
	msleep(200);
	if (sd->cam_type == CAM_TYPE_CIF) {
		err_code = start_cif_cam(gspca_dev);
	} else {
		err_code = start_vga_cam(gspca_dev);
	}
	return err_code;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int result;

	gspca_dev->usb_buf[0] = 1;
	gspca_dev->usb_buf[1] = 0;
	result = mr_write(gspca_dev, 2);
	if (result < 0)
		PDEBUG(D_ERR, "Camera Stop failed");

	/* Not all the cams need this, but even if not, probably a good idea */
	zero_the_pointer(gspca_dev);
	if (sd->do_lcd_stop) {
		gspca_dev->usb_buf[0] = 0x19;
		gspca_dev->usb_buf[1] = 0x54;
		result = mr_write(gspca_dev, 2);
		if (result < 0)
			PDEBUG(D_ERR, "Camera Stop failed");
	}
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	if (gspca_dev->ctrl_dis & (1 << BRIGHTNESS_IDX))
		return;

	/* Note register 7 is also seen as 0x8x or 0xCx in dumps */
	if (sd->brightness > 0) {
		sensor_write1(gspca_dev, 7, 0x00);
		val = sd->brightness;
	} else {
		sensor_write1(gspca_dev, 7, 0x01);
		val = 257 - sd->brightness;
	}
	sensor_write1(gspca_dev, 8, val);
}

static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	if (gspca_dev->ctrl_dis & (1 << EXPOSURE_IDX))
		return;

	if (sd->sensor_type) {
		val = sd->exposure >> 4;
		sensor_write1(gspca_dev, 3, val);
		val = sd->exposure & 0xf;
		sensor_write1(gspca_dev, 4, val);
	} else {
		u8 clockdiv;
		int exposure;

		/* We have both a clock divider and an exposure register.
		   We first calculate the clock divider, as that determines
		   the maximum exposure and then we calculayte the exposure
		   register setting (which goes from 0 - 511).

		   Note our 0 - 4095 exposure is mapped to 0 - 511
		   milliseconds exposure time */
		clockdiv = (60 * sd->exposure + 7999) / 8000;

		/* Limit framerate to not exceed usb bandwidth */
		if (clockdiv < 3 && gspca_dev->width >= 320)
			clockdiv = 3;
		else if (clockdiv < 2)
			clockdiv = 2;

		/* Frame exposure time in ms = 1000 * clockdiv / 60 ->
		exposure = (sd->exposure / 8) * 511 / (1000 * clockdiv / 60) */
		exposure = (60 * 511 * sd->exposure) / (8000 * clockdiv);
		if (exposure > 511)
			exposure = 511;

		/* exposure register value is reversed! */
		exposure = 511 - exposure;

		sensor_write1(gspca_dev, 0x02, clockdiv);
		sensor_write1(gspca_dev, 0x0e, exposure & 0xff);
		sensor_write1(gspca_dev, 0x0f, exposure >> 8);
	}
}

static void setgain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (gspca_dev->ctrl_dis & (1 << GAIN_IDX))
		return;

	if (sd->sensor_type) {
		sensor_write1(gspca_dev, 0x0e, sd->gain);
	} else {
		sensor_write1(gspca_dev, 0x10, sd->gain);
	}
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

/* Include pac common sof detection functions */
#include "pac_common.h"

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,    /* target */
			__u8 *data,                   /* isoc packet */
			int len)                      /* iso packet length */
{
	unsigned char *sof;

	sof = pac_find_sof(gspca_dev, data, len);
	if (sof) {
		int n;

		/* finish decoding current frame */
		n = sof - data;
		if (n > sizeof pac_sof_marker)
			n -= sizeof pac_sof_marker;
		else
			n = 0;
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					data, n);
		/* Start next frame. */
		gspca_frame_add(gspca_dev, FIRST_PACKET, frame,
			pac_sof_marker, sizeof pac_sof_marker);
		len -= sof - data;
		data = sof;
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
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
static const __devinitdata struct usb_device_id device_table[] = {
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

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	int ret;

	ret = usb_register(&sd_driver);
	if (ret < 0)
		return ret;
	PDEBUG(D_PROBE, "registered");
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
