/*
 * ov534-ov772x gspca driver
 *
 * Copyright (C) 2008 Antonio Ospite <ospite@studenti.unina.it>
 * Copyright (C) 2008 Jim Paris <jim@jtan.com>
 * Copyright (C) 2009 Jean-Francois Moine http://moinejf.free.fr
 *
 * Based on a prototype written by Mark Ferrell <majortrips@gmail.com>
 * USB protocol reverse engineered by Jim Paris <jim@jtan.com>
 * https://jim.sh/svn/jim/devl/playstation/ps3/eye/test/
 *
 * PS3 Eye camera enhanced by Richard Kaswy http://kaswy.free.fr
 * PS3 Eye camera, brightness, contrast, hue, AWB control added
 *	by Max Thrun <bear24rw@gmail.com>
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

#define MODULE_NAME "ov534"

#include "gspca.h"

#define OV534_REG_ADDRESS	0xf1	/* sensor address */
#define OV534_REG_SUBADDR	0xf2
#define OV534_REG_WRITE		0xf3
#define OV534_REG_READ		0xf4
#define OV534_REG_OPERATION	0xf5
#define OV534_REG_STATUS	0xf6

#define OV534_OP_WRITE_3	0x37
#define OV534_OP_WRITE_2	0x33
#define OV534_OP_READ_2		0xf9

#define CTRL_TIMEOUT 500

MODULE_AUTHOR("Antonio Ospite <ospite@studenti.unina.it>");
MODULE_DESCRIPTION("GSPCA/OV534 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	__u32 last_pts;
	u16 last_fid;
	u8 frame_rate;

	u8 brightness;
	u8 contrast;
	u8 gain;
	u8 exposure;
	u8 redblc;
	u8 blueblc;
	u8 hue;
	u8 autogain;
	u8 awb;
	s8 sharpness;
	u8 hflip;
	u8 vflip;

};

/* V4L2 controls supported by the driver */
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setredblc(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getredblc(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setblueblc(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getblueblc(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_sethflip(struct gspca_dev *gspca_dev, __s32 val);
static int sd_gethflip(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setvflip(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getvflip(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val);
static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setawb(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getawb(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
    {							/* 0 */
	{
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define BRIGHTNESS_DEF 20
		.default_value = BRIGHTNESS_DEF,
	},
	.set = sd_setbrightness,
	.get = sd_getbrightness,
    },
    {							/* 1 */
	{
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define CONTRAST_DEF 37
		.default_value = CONTRAST_DEF,
	},
	.set = sd_setcontrast,
	.get = sd_getcontrast,
    },
    {							/* 2 */
	{
	    .id      = V4L2_CID_GAIN,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Main Gain",
	    .minimum = 0,
	    .maximum = 63,
	    .step    = 1,
#define GAIN_DEF 20
	    .default_value = GAIN_DEF,
	},
	.set = sd_setgain,
	.get = sd_getgain,
    },
    {							/* 3 */
	{
	    .id      = V4L2_CID_EXPOSURE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Exposure",
	    .minimum = 0,
	    .maximum = 255,
	    .step    = 1,
#define EXPO_DEF 120
	    .default_value = EXPO_DEF,
	},
	.set = sd_setexposure,
	.get = sd_getexposure,
    },
    {							/* 4 */
	{
	    .id      = V4L2_CID_RED_BALANCE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Red Balance",
	    .minimum = 0,
	    .maximum = 255,
	    .step    = 1,
#define RED_BALANCE_DEF 128
	    .default_value = RED_BALANCE_DEF,
	},
	.set = sd_setredblc,
	.get = sd_getredblc,
    },
    {							/* 5 */
	{
	    .id      = V4L2_CID_BLUE_BALANCE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Blue Balance",
	    .minimum = 0,
	    .maximum = 255,
	    .step    = 1,
#define BLUE_BALANCE_DEF 128
	    .default_value = BLUE_BALANCE_DEF,
	},
	.set = sd_setblueblc,
	.get = sd_getblueblc,
    },
    {							/* 6 */
	{
		.id      = V4L2_CID_HUE,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Hue",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define HUE_DEF 143
		.default_value = HUE_DEF,
	},
	.set = sd_sethue,
	.get = sd_gethue,
    },
    {							/* 7 */
	{
	    .id      = V4L2_CID_AUTOGAIN,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "Autogain",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
#define AUTOGAIN_DEF 0
	    .default_value = AUTOGAIN_DEF,
	},
	.set = sd_setautogain,
	.get = sd_getautogain,
    },
#define AWB_IDX 8
    {							/* 8 */
	{
		.id      = V4L2_CID_AUTO_WHITE_BALANCE,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Auto White Balance",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
#define AWB_DEF 0
		.default_value = AWB_DEF,
	},
	.set = sd_setawb,
	.get = sd_getawb,
    },
    {							/* 9 */
	{
	    .id      = V4L2_CID_SHARPNESS,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Sharpness",
	    .minimum = 0,
	    .maximum = 63,
	    .step    = 1,
#define SHARPNESS_DEF 0
	    .default_value = SHARPNESS_DEF,
	},
	.set = sd_setsharpness,
	.get = sd_getsharpness,
    },
    {							/* 10 */
	{
	    .id      = V4L2_CID_HFLIP,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "HFlip",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
#define HFLIP_DEF 0
	    .default_value = HFLIP_DEF,
	},
	.set = sd_sethflip,
	.get = sd_gethflip,
    },
    {							/* 11 */
	{
	    .id      = V4L2_CID_VFLIP,
	    .type    = V4L2_CTRL_TYPE_BOOLEAN,
	    .name    = "VFlip",
	    .minimum = 0,
	    .maximum = 1,
	    .step    = 1,
#define VFLIP_DEF 0
	    .default_value = VFLIP_DEF,
	},
	.set = sd_setvflip,
	.get = sd_getvflip,
    },
};

static const struct v4l2_pix_format ov772x_mode[] = {
	{320, 240, V4L2_PIX_FMT_YUYV, V4L2_FIELD_NONE,
	 .bytesperline = 320 * 2,
	 .sizeimage = 320 * 240 * 2,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 .priv = 1},
	{640, 480, V4L2_PIX_FMT_YUYV, V4L2_FIELD_NONE,
	 .bytesperline = 640 * 2,
	 .sizeimage = 640 * 480 * 2,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 .priv = 0},
};

static const u8 qvga_rates[] = {125, 100, 75, 60, 50, 40, 30};
static const u8 vga_rates[] = {60, 50, 40, 30, 15};

static const struct framerates ov772x_framerates[] = {
	{ /* 320x240 */
		.rates = qvga_rates,
		.nrates = ARRAY_SIZE(qvga_rates),
	},
	{ /* 640x480 */
		.rates = vga_rates,
		.nrates = ARRAY_SIZE(vga_rates),
	},
};

static const u8 bridge_init[][2] = {
	{ 0xc2, 0x0c },
	{ 0x88, 0xf8 },
	{ 0xc3, 0x69 },
	{ 0x89, 0xff },
	{ 0x76, 0x03 },
	{ 0x92, 0x01 },
	{ 0x93, 0x18 },
	{ 0x94, 0x10 },
	{ 0x95, 0x10 },
	{ 0xe2, 0x00 },
	{ 0xe7, 0x3e },

	{ 0x96, 0x00 },

	{ 0x97, 0x20 },
	{ 0x97, 0x20 },
	{ 0x97, 0x20 },
	{ 0x97, 0x0a },
	{ 0x97, 0x3f },
	{ 0x97, 0x4a },
	{ 0x97, 0x20 },
	{ 0x97, 0x15 },
	{ 0x97, 0x0b },

	{ 0x8e, 0x40 },
	{ 0x1f, 0x81 },
	{ 0x34, 0x05 },
	{ 0xe3, 0x04 },
	{ 0x88, 0x00 },
	{ 0x89, 0x00 },
	{ 0x76, 0x00 },
	{ 0xe7, 0x2e },
	{ 0x31, 0xf9 },
	{ 0x25, 0x42 },
	{ 0x21, 0xf0 },

	{ 0x1c, 0x00 },
	{ 0x1d, 0x40 },
	{ 0x1d, 0x02 }, /* payload size 0x0200 * 4 = 2048 bytes */
	{ 0x1d, 0x00 }, /* payload size */

	{ 0x1d, 0x02 }, /* frame size 0x025800 * 4 = 614400 */
	{ 0x1d, 0x58 }, /* frame size */
	{ 0x1d, 0x00 }, /* frame size */

	{ 0x1c, 0x0a },
	{ 0x1d, 0x08 }, /* turn on UVC header */
	{ 0x1d, 0x0e }, /* .. */

	{ 0x8d, 0x1c },
	{ 0x8e, 0x80 },
	{ 0xe5, 0x04 },

	{ 0xc0, 0x50 },
	{ 0xc1, 0x3c },
	{ 0xc2, 0x0c },
};
static const u8 sensor_init[][2] = {
	{ 0x12, 0x80 },
	{ 0x11, 0x01 },
/*fixme: better have a delay?*/
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },
	{ 0x11, 0x01 },

	{ 0x3d, 0x03 },
	{ 0x17, 0x26 },
	{ 0x18, 0xa0 },
	{ 0x19, 0x07 },
	{ 0x1a, 0xf0 },
	{ 0x32, 0x00 },
	{ 0x29, 0xa0 },
	{ 0x2c, 0xf0 },
	{ 0x65, 0x20 },
	{ 0x11, 0x01 },
	{ 0x42, 0x7f },
	{ 0x63, 0xaa },		/* AWB - was e0 */
	{ 0x64, 0xff },
	{ 0x66, 0x00 },
	{ 0x13, 0xf0 },		/* com8 */
	{ 0x0d, 0x41 },
	{ 0x0f, 0xc5 },
	{ 0x14, 0x11 },

	{ 0x22, 0x7f },
	{ 0x23, 0x03 },
	{ 0x24, 0x40 },
	{ 0x25, 0x30 },
	{ 0x26, 0xa1 },
	{ 0x2a, 0x00 },
	{ 0x2b, 0x00 },
	{ 0x6b, 0xaa },
	{ 0x13, 0xff },		/* AWB */

	{ 0x90, 0x05 },
	{ 0x91, 0x01 },
	{ 0x92, 0x03 },
	{ 0x93, 0x00 },
	{ 0x94, 0x60 },
	{ 0x95, 0x3c },
	{ 0x96, 0x24 },
	{ 0x97, 0x1e },
	{ 0x98, 0x62 },
	{ 0x99, 0x80 },
	{ 0x9a, 0x1e },
	{ 0x9b, 0x08 },
	{ 0x9c, 0x20 },
	{ 0x9e, 0x81 },

	{ 0xa6, 0x04 },
	{ 0x7e, 0x0c },
	{ 0x7f, 0x16 },
	{ 0x80, 0x2a },
	{ 0x81, 0x4e },
	{ 0x82, 0x61 },
	{ 0x83, 0x6f },
	{ 0x84, 0x7b },
	{ 0x85, 0x86 },
	{ 0x86, 0x8e },
	{ 0x87, 0x97 },
	{ 0x88, 0xa4 },
	{ 0x89, 0xaf },
	{ 0x8a, 0xc5 },
	{ 0x8b, 0xd7 },
	{ 0x8c, 0xe8 },
	{ 0x8d, 0x20 },

	{ 0x0c, 0x90 },

	{ 0x2b, 0x00 },
	{ 0x22, 0x7f },
	{ 0x23, 0x03 },
	{ 0x11, 0x01 },
	{ 0x0c, 0xd0 },
	{ 0x64, 0xff },
	{ 0x0d, 0x41 },

	{ 0x14, 0x41 },
	{ 0x0e, 0xcd },
	{ 0xac, 0xbf },
	{ 0x8e, 0x00 },		/* De-noise threshold */
	{ 0x0c, 0xd0 }
};
static const u8 bridge_start_vga[][2] = {
	{0x1c, 0x00},
	{0x1d, 0x40},
	{0x1d, 0x02},
	{0x1d, 0x00},
	{0x1d, 0x02},
	{0x1d, 0x58},
	{0x1d, 0x00},
	{0xc0, 0x50},
	{0xc1, 0x3c},
};
static const u8 sensor_start_vga[][2] = {
	{0x12, 0x00},
	{0x17, 0x26},
	{0x18, 0xa0},
	{0x19, 0x07},
	{0x1a, 0xf0},
	{0x29, 0xa0},
	{0x2c, 0xf0},
	{0x65, 0x20},
};
static const u8 bridge_start_qvga[][2] = {
	{0x1c, 0x00},
	{0x1d, 0x40},
	{0x1d, 0x02},
	{0x1d, 0x00},
	{0x1d, 0x01},
	{0x1d, 0x4b},
	{0x1d, 0x00},
	{0xc0, 0x28},
	{0xc1, 0x1e},
};
static const u8 sensor_start_qvga[][2] = {
	{0x12, 0x40},
	{0x17, 0x3f},
	{0x18, 0x50},
	{0x19, 0x03},
	{0x1a, 0x78},
	{0x29, 0x50},
	{0x2c, 0x78},
	{0x65, 0x2f},
};

static void ov534_reg_write(struct gspca_dev *gspca_dev, u16 reg, u8 val)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	PDEBUG(D_USBO, "reg=0x%04x, val=0%02x", reg, val);
	gspca_dev->usb_buf[0] = val;
	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      0x01,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x00, reg, gspca_dev->usb_buf, 1, CTRL_TIMEOUT);
	if (ret < 0)
		PDEBUG(D_ERR, "write failed");
}

static u8 ov534_reg_read(struct gspca_dev *gspca_dev, u16 reg)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      0x01,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x00, reg, gspca_dev->usb_buf, 1, CTRL_TIMEOUT);
	PDEBUG(D_USBI, "reg=0x%04x, data=0x%02x", reg, gspca_dev->usb_buf[0]);
	if (ret < 0)
		PDEBUG(D_ERR, "read failed");
	return gspca_dev->usb_buf[0];
}

/* Two bits control LED: 0x21 bit 7 and 0x23 bit 7.
 * (direction and output)? */
static void ov534_set_led(struct gspca_dev *gspca_dev, int status)
{
	u8 data;

	PDEBUG(D_CONF, "led status: %d", status);

	data = ov534_reg_read(gspca_dev, 0x21);
	data |= 0x80;
	ov534_reg_write(gspca_dev, 0x21, data);

	data = ov534_reg_read(gspca_dev, 0x23);
	if (status)
		data |= 0x80;
	else
		data &= ~0x80;

	ov534_reg_write(gspca_dev, 0x23, data);

	if (!status) {
		data = ov534_reg_read(gspca_dev, 0x21);
		data &= ~0x80;
		ov534_reg_write(gspca_dev, 0x21, data);
	}
}

static int sccb_check_status(struct gspca_dev *gspca_dev)
{
	u8 data;
	int i;

	for (i = 0; i < 5; i++) {
		data = ov534_reg_read(gspca_dev, OV534_REG_STATUS);

		switch (data) {
		case 0x00:
			return 1;
		case 0x04:
			return 0;
		case 0x03:
			break;
		default:
			PDEBUG(D_ERR, "sccb status 0x%02x, attempt %d/5",
			       data, i + 1);
		}
	}
	return 0;
}

static void sccb_reg_write(struct gspca_dev *gspca_dev, u8 reg, u8 val)
{
	PDEBUG(D_USBO, "reg: 0x%02x, val: 0x%02x", reg, val);
	ov534_reg_write(gspca_dev, OV534_REG_SUBADDR, reg);
	ov534_reg_write(gspca_dev, OV534_REG_WRITE, val);
	ov534_reg_write(gspca_dev, OV534_REG_OPERATION, OV534_OP_WRITE_3);

	if (!sccb_check_status(gspca_dev))
		PDEBUG(D_ERR, "sccb_reg_write failed");
}

static u8 sccb_reg_read(struct gspca_dev *gspca_dev, u16 reg)
{
	ov534_reg_write(gspca_dev, OV534_REG_SUBADDR, reg);
	ov534_reg_write(gspca_dev, OV534_REG_OPERATION, OV534_OP_WRITE_2);
	if (!sccb_check_status(gspca_dev))
		PDEBUG(D_ERR, "sccb_reg_read failed 1");

	ov534_reg_write(gspca_dev, OV534_REG_OPERATION, OV534_OP_READ_2);
	if (!sccb_check_status(gspca_dev))
		PDEBUG(D_ERR, "sccb_reg_read failed 2");

	return ov534_reg_read(gspca_dev, OV534_REG_READ);
}

/* output a bridge sequence (reg - val) */
static void reg_w_array(struct gspca_dev *gspca_dev,
			const u8 (*data)[2], int len)
{
	while (--len >= 0) {
		ov534_reg_write(gspca_dev, (*data)[0], (*data)[1]);
		data++;
	}
}

/* output a sensor sequence (reg - val) */
static void sccb_w_array(struct gspca_dev *gspca_dev,
			const u8 (*data)[2], int len)
{
	while (--len >= 0) {
		if ((*data)[0] != 0xff) {
			sccb_reg_write(gspca_dev, (*data)[0], (*data)[1]);
		} else {
			sccb_reg_read(gspca_dev, (*data)[1]);
			sccb_reg_write(gspca_dev, 0xff, 0x00);
		}
		data++;
	}
}

/* ov772x specific controls */
static void set_frame_rate(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	struct rate_s {
		u8 fps;
		u8 r11;
		u8 r0d;
		u8 re5;
	};
	const struct rate_s *r;
	static const struct rate_s rate_0[] = {	/* 640x480 */
		{60, 0x01, 0xc1, 0x04},
		{50, 0x01, 0x41, 0x02},
		{40, 0x02, 0xc1, 0x04},
		{30, 0x04, 0x81, 0x02},
		{15, 0x03, 0x41, 0x04},
	};
	static const struct rate_s rate_1[] = {	/* 320x240 */
		{125, 0x02, 0x81, 0x02},
		{100, 0x02, 0xc1, 0x04},
		{75, 0x03, 0xc1, 0x04},
		{60, 0x04, 0xc1, 0x04},
		{50, 0x02, 0x41, 0x04},
		{40, 0x03, 0x41, 0x04},
		{30, 0x04, 0x41, 0x04},
	};

	if (gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv == 0) {
		r = rate_0;
		i = ARRAY_SIZE(rate_0);
	} else {
		r = rate_1;
		i = ARRAY_SIZE(rate_1);
	}
	while (--i > 0) {
		if (sd->frame_rate >= r->fps)
			break;
		r++;
	}

	sccb_reg_write(gspca_dev, 0x11, r->r11);
	sccb_reg_write(gspca_dev, 0x0d, r->r0d);
	ov534_reg_write(gspca_dev, 0xe5, r->re5);

	PDEBUG(D_PROBE, "frame_rate: %d", r->fps);
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sccb_reg_write(gspca_dev, 0x9B, sd->brightness);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sccb_reg_write(gspca_dev, 0x9C, sd->contrast);
}

static void setgain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	val = sd->gain;
	switch (val & 0x30) {
	case 0x00:
		val &= 0x0f;
		break;
	case 0x10:
		val &= 0x0f;
		val |= 0x30;
		break;
	case 0x20:
		val &= 0x0f;
		val |= 0x70;
		break;
	default:
/*	case 0x30: */
		val &= 0x0f;
		val |= 0xf0;
		break;
	}
	sccb_reg_write(gspca_dev, 0x00, val);
}

static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	val = sd->exposure;
	sccb_reg_write(gspca_dev, 0x08, val >> 7);
	sccb_reg_write(gspca_dev, 0x10, val << 1);
}

static void setredblc(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sccb_reg_write(gspca_dev, 0x43, sd->redblc);
}

static void setblueblc(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sccb_reg_write(gspca_dev, 0x42, sd->blueblc);
}

static void sethue(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sccb_reg_write(gspca_dev, 0x01, sd->hue);
}

static void setautogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->autogain) {
		sccb_reg_write(gspca_dev, 0x13, 0xf7); /* AGC,AEC,AWB ON */
		sccb_reg_write(gspca_dev, 0x64,
				sccb_reg_read(gspca_dev, 0x64) | 0x03);
	} else {
		sccb_reg_write(gspca_dev, 0x13, 0xf0); /* AGC,AEC,AWB OFF */
		sccb_reg_write(gspca_dev, 0x64,
				sccb_reg_read(gspca_dev, 0x64) & 0xfc);
	}
}

static void setawb(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->awb)
		sccb_reg_write(gspca_dev, 0x63, 0xe0);	/* AWB on */
	else
		sccb_reg_write(gspca_dev, 0x63, 0xaa);	/* AWB off */
}

static void setsharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	val = sd->sharpness;
	sccb_reg_write(gspca_dev, 0x91, val);	/* vga noise */
	sccb_reg_write(gspca_dev, 0x8e, val);	/* qvga noise */
}

static void sethflip(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->hflip == 0)
		sccb_reg_write(gspca_dev, 0x0c,
				sccb_reg_read(gspca_dev, 0x0c) | 0x40);
	else
		sccb_reg_write(gspca_dev, 0x0c,
				sccb_reg_read(gspca_dev, 0x0c) & 0xbf);
}

static void setvflip(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->vflip == 0)
		sccb_reg_write(gspca_dev, 0x0c,
				sccb_reg_read(gspca_dev, 0x0c) | 0x80);
	else
		sccb_reg_write(gspca_dev, 0x0c,
				sccb_reg_read(gspca_dev, 0x0c) & 0x7f);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;

	cam->cam_mode = ov772x_mode;
	cam->nmodes = ARRAY_SIZE(ov772x_mode);
	cam->mode_framerates = ov772x_framerates;

	cam->bulk = 1;
	cam->bulk_size = 16384;
	cam->bulk_nurbs = 2;

	sd->frame_rate = 30;

	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->gain = GAIN_DEF;
	sd->exposure = EXPO_DEF;
	sd->redblc = RED_BALANCE_DEF;
	sd->blueblc = BLUE_BALANCE_DEF;
	sd->hue = HUE_DEF;
#if AUTOGAIN_DEF != 0
	sd->autogain = AUTOGAIN_DEF;
#else
	gspca_dev->ctrl_inac |= (1 << AWB_IDX);
#endif
#if AWB_DEF != 0
	sd->awb = AWB_DEF
#endif
#if SHARPNESS_DEF != 0
	sd->sharpness = SHARPNESS_DEF;
#endif
#if HFLIP_DEF != 0
	sd->hflip = HFLIP_DEF;
#endif
#if VFLIP_DEF != 0
	sd->vflip = VFLIP_DEF;
#endif

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	u16 sensor_id;

	/* reset bridge */
	ov534_reg_write(gspca_dev, 0xe7, 0x3a);
	ov534_reg_write(gspca_dev, 0xe0, 0x08);
	msleep(100);

	/* initialize the sensor address */
	ov534_reg_write(gspca_dev, OV534_REG_ADDRESS, 0x42);

	/* reset sensor */
	sccb_reg_write(gspca_dev, 0x12, 0x80);
	msleep(10);

	/* probe the sensor */
	sccb_reg_read(gspca_dev, 0x0a);
	sensor_id = sccb_reg_read(gspca_dev, 0x0a) << 8;
	sccb_reg_read(gspca_dev, 0x0b);
	sensor_id |= sccb_reg_read(gspca_dev, 0x0b);
	PDEBUG(D_PROBE, "Sensor ID: %04x", sensor_id);

	/* initialize */
	reg_w_array(gspca_dev, bridge_init,
			ARRAY_SIZE(bridge_init));
	ov534_set_led(gspca_dev, 1);
	sccb_w_array(gspca_dev, sensor_init,
			ARRAY_SIZE(sensor_init));
	ov534_reg_write(gspca_dev, 0xe0, 0x09);
	ov534_set_led(gspca_dev, 0);
	set_frame_rate(gspca_dev);

	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	int mode;

	mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	if (mode != 0) {	/* 320x240 */
		reg_w_array(gspca_dev, bridge_start_qvga,
				ARRAY_SIZE(bridge_start_qvga));
		sccb_w_array(gspca_dev, sensor_start_qvga,
				ARRAY_SIZE(sensor_start_qvga));
	} else {		/* 640x480 */
		reg_w_array(gspca_dev, bridge_start_vga,
				ARRAY_SIZE(bridge_start_vga));
		sccb_w_array(gspca_dev, sensor_start_vga,
				ARRAY_SIZE(sensor_start_vga));
	}
	set_frame_rate(gspca_dev);

	setautogain(gspca_dev);
	setawb(gspca_dev);
	setgain(gspca_dev);
	setredblc(gspca_dev);
	setblueblc(gspca_dev);
	sethue(gspca_dev);
	setexposure(gspca_dev);
	setbrightness(gspca_dev);
	setcontrast(gspca_dev);
	setsharpness(gspca_dev);
	setvflip(gspca_dev);
	sethflip(gspca_dev);

	ov534_set_led(gspca_dev, 1);
	ov534_reg_write(gspca_dev, 0xe0, 0x00);
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	ov534_reg_write(gspca_dev, 0xe0, 0x09);
	ov534_set_led(gspca_dev, 0);
}

/* Values for bmHeaderInfo (Video and Still Image Payload Headers, 2.4.3.3) */
#define UVC_STREAM_EOH	(1 << 7)
#define UVC_STREAM_ERR	(1 << 6)
#define UVC_STREAM_STI	(1 << 5)
#define UVC_STREAM_RES	(1 << 4)
#define UVC_STREAM_SCR	(1 << 3)
#define UVC_STREAM_PTS	(1 << 2)
#define UVC_STREAM_EOF	(1 << 1)
#define UVC_STREAM_FID	(1 << 0)

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u32 this_pts;
	u16 this_fid;
	int remaining_len = len;

	do {
		len = min(remaining_len, 2048);

		/* Payloads are prefixed with a UVC-style header.  We
		   consider a frame to start when the FID toggles, or the PTS
		   changes.  A frame ends when EOF is set, and we've received
		   the correct number of bytes. */

		/* Verify UVC header.  Header length is always 12 */
		if (data[0] != 12 || len < 12) {
			PDEBUG(D_PACK, "bad header");
			goto discard;
		}

		/* Check errors */
		if (data[1] & UVC_STREAM_ERR) {
			PDEBUG(D_PACK, "payload error");
			goto discard;
		}

		/* Extract PTS and FID */
		if (!(data[1] & UVC_STREAM_PTS)) {
			PDEBUG(D_PACK, "PTS not present");
			goto discard;
		}
		this_pts = (data[5] << 24) | (data[4] << 16)
						| (data[3] << 8) | data[2];
		this_fid = (data[1] & UVC_STREAM_FID) ? 1 : 0;

		/* If PTS or FID has changed, start a new frame. */
		if (this_pts != sd->last_pts || this_fid != sd->last_fid) {
			if (gspca_dev->last_packet_type == INTER_PACKET)
				gspca_frame_add(gspca_dev, LAST_PACKET,
						NULL, 0);
			sd->last_pts = this_pts;
			sd->last_fid = this_fid;
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					data + 12, len - 12);
		/* If this packet is marked as EOF, end the frame */
		} else if (data[1] & UVC_STREAM_EOF) {
			struct gspca_frame *frame;

			sd->last_pts = 0;
			frame = gspca_get_i_frame(gspca_dev);
			if (frame == NULL)
				goto discard;
			if (frame->data_end - frame->data !=
			    gspca_dev->width * gspca_dev->height * 2) {
				PDEBUG(D_PACK, "short frame");
				goto discard;
			}
			gspca_frame_add(gspca_dev, LAST_PACKET,
					data + 12, len - 12);
		} else {

			/* Add the data from this payload */
			gspca_frame_add(gspca_dev, INTER_PACKET,
					data + 12, len - 12);
		}

		/* Done this payload */
		goto scan_next;

discard:
		/* Discard data until a new frame starts. */
		gspca_dev->last_packet_type = DISCARD_PACKET;

scan_next:
		remaining_len -= len;
		data += len;
	} while (remaining_len > 0);
}

/* controls */
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

static int sd_setredblc(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->redblc = val;
	if (gspca_dev->streaming)
		setredblc(gspca_dev);
	return 0;
}

static int sd_getredblc(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->redblc;
	return 0;
}

static int sd_setblueblc(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->blueblc = val;
	if (gspca_dev->streaming)
		setblueblc(gspca_dev);
	return 0;
}

static int sd_getblueblc(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->blueblc;
	return 0;
}

static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hue = val;
	if (gspca_dev->streaming)
		sethue(gspca_dev);
	return 0;
}

static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->hue;
	return 0;
}

static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = val;

	if (gspca_dev->streaming) {

		/* the auto white balance control works only
		 * when auto gain is set */
		if (val)
			gspca_dev->ctrl_inac &= ~(1 << AWB_IDX);
		else
			gspca_dev->ctrl_inac |= (1 << AWB_IDX);
		setautogain(gspca_dev);
	}
	return 0;
}

static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

static int sd_setawb(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->awb = val;
	if (gspca_dev->streaming)
		setawb(gspca_dev);
	return 0;
}

static int sd_getawb(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->awb;
	return 0;
}

static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->sharpness = val;
	if (gspca_dev->streaming)
		setsharpness(gspca_dev);
	return 0;
}

static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->sharpness;
	return 0;
}

static int sd_sethflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hflip = val;
	if (gspca_dev->streaming)
		sethflip(gspca_dev);
	return 0;
}

static int sd_gethflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->hflip;
	return 0;
}

static int sd_setvflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->vflip = val;
	if (gspca_dev->streaming)
		setvflip(gspca_dev);
	return 0;
}

static int sd_getvflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->vflip;
	return 0;
}

/* get stream parameters (framerate) */
static int sd_get_streamparm(struct gspca_dev *gspca_dev,
			     struct v4l2_streamparm *parm)
{
	struct v4l2_captureparm *cp = &parm->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	struct sd *sd = (struct sd *) gspca_dev;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cp->capability |= V4L2_CAP_TIMEPERFRAME;
	tpf->numerator = 1;
	tpf->denominator = sd->frame_rate;

	return 0;
}

/* set stream parameters (framerate) */
static int sd_set_streamparm(struct gspca_dev *gspca_dev,
			     struct v4l2_streamparm *parm)
{
	struct v4l2_captureparm *cp = &parm->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	struct sd *sd = (struct sd *) gspca_dev;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* Set requested framerate */
	sd->frame_rate = tpf->denominator / tpf->numerator;
	if (gspca_dev->streaming)
		set_frame_rate(gspca_dev);

	/* Return the actual framerate */
	tpf->numerator = 1;
	tpf->denominator = sd->frame_rate;

	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name     = MODULE_NAME,
	.ctrls    = sd_ctrls,
	.nctrls   = ARRAY_SIZE(sd_ctrls),
	.config   = sd_config,
	.init     = sd_init,
	.start    = sd_start,
	.stopN    = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.get_streamparm = sd_get_streamparm,
	.set_streamparm = sd_set_streamparm,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x1415, 0x2000)},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
				THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name       = MODULE_NAME,
	.id_table   = device_table,
	.probe      = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend    = gspca_suspend,
	.resume     = gspca_resume,
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
