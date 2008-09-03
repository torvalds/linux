/*
 * Etoms Et61x151 GPL Linux driver by Michel Xhaard (09/09/2004)
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
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

#define MODULE_NAME "etoms"

#include "gspca.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("Etoms USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;
	unsigned char autogain;

	char sensor;
#define SENSOR_PAS106 0
#define SENSOR_TAS5130CXX 1
	signed char ag_cnt;
#define AG_CNT_START 13
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
	{
	 {
	  .id = V4L2_CID_BRIGHTNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Brightness",
	  .minimum = 1,
	  .maximum = 127,
	  .step = 1,
#define BRIGHTNESS_DEF 63
	  .default_value = BRIGHTNESS_DEF,
	  },
	 .set = sd_setbrightness,
	 .get = sd_getbrightness,
	 },
	{
	 {
	  .id = V4L2_CID_CONTRAST,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Contrast",
	  .minimum = 0,
	  .maximum = 255,
	  .step = 1,
#define CONTRAST_DEF 127
	  .default_value = CONTRAST_DEF,
	  },
	 .set = sd_setcontrast,
	 .get = sd_getcontrast,
	 },
	{
	 {
	  .id = V4L2_CID_SATURATION,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Color",
	  .minimum = 0,
	  .maximum = 15,
	  .step = 1,
#define COLOR_DEF 7
	  .default_value = COLOR_DEF,
	  },
	 .set = sd_setcolors,
	 .get = sd_getcolors,
	 },
	{
	 {
	  .id = V4L2_CID_AUTOGAIN,
	  .type = V4L2_CTRL_TYPE_BOOLEAN,
	  .name = "Auto Gain",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
#define AUTOGAIN_DEF 1
	  .default_value = AUTOGAIN_DEF,
	  },
	 .set = sd_setautogain,
	 .get = sd_getautogain,
	 },
};

static struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
/*	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0}, */
};

static struct v4l2_pix_format sif_mode[] = {
	{176, 144, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

#define ETOMS_ALT_SIZE_1000   12

#define ET_GPIO_DIR_CTRL 0x04	/* Control IO bit[0..5] (0 in  1 out) */
#define ET_GPIO_OUT 0x05	/* Only IO data */
#define ET_GPIO_IN 0x06		/* Read Only IO data */
#define ET_RESET_ALL 0x03
#define ET_ClCK 0x01
#define ET_CTRL 0x02		/* enable i2c OutClck Powerdown mode */

#define ET_COMP 0x12		/* Compression register */
#define ET_MAXQt 0x13
#define ET_MINQt 0x14
#define ET_COMP_VAL0 0x02
#define ET_COMP_VAL1 0x03

#define ET_REG1d 0x1d
#define ET_REG1e 0x1e
#define ET_REG1f 0x1f
#define ET_REG20 0x20
#define ET_REG21 0x21
#define ET_REG22 0x22
#define ET_REG23 0x23
#define ET_REG24 0x24
#define ET_REG25 0x25
/* base registers for luma calculation */
#define ET_LUMA_CENTER 0x39

#define ET_G_RED 0x4d
#define ET_G_GREEN1 0x4e
#define ET_G_BLUE 0x4f
#define ET_G_GREEN2 0x50
#define ET_G_GR_H 0x51
#define ET_G_GB_H 0x52

#define ET_O_RED 0x34
#define ET_O_GREEN1 0x35
#define ET_O_BLUE 0x36
#define ET_O_GREEN2 0x37

#define ET_SYNCHRO 0x68
#define ET_STARTX 0x69
#define ET_STARTY 0x6a
#define ET_WIDTH_LOW 0x6b
#define ET_HEIGTH_LOW 0x6c
#define ET_W_H_HEIGTH 0x6d

#define ET_REG6e 0x6e		/* OBW */
#define ET_REG6f 0x6f		/* OBW */
#define ET_REG70 0x70		/* OBW_AWB */
#define ET_REG71 0x71		/* OBW_AWB */
#define ET_REG72 0x72		/* OBW_AWB */
#define ET_REG73 0x73		/* Clkdelay ns */
#define ET_REG74 0x74		/* test pattern */
#define ET_REG75 0x75		/* test pattern */

#define ET_I2C_CLK 0x8c
#define ET_PXL_CLK 0x60

#define ET_I2C_BASE 0x89
#define ET_I2C_COUNT 0x8a
#define ET_I2C_PREFETCH 0x8b
#define ET_I2C_REG 0x88
#define ET_I2C_DATA7 0x87
#define ET_I2C_DATA6 0x86
#define ET_I2C_DATA5 0x85
#define ET_I2C_DATA4 0x84
#define ET_I2C_DATA3 0x83
#define ET_I2C_DATA2 0x82
#define ET_I2C_DATA1 0x81
#define ET_I2C_DATA0 0x80

#define PAS106_REG2 0x02	/* pxlClk = systemClk/(reg2) */
#define PAS106_REG3 0x03	/* line/frame H [11..4] */
#define PAS106_REG4 0x04	/* line/frame L [3..0] */
#define PAS106_REG5 0x05	/* exposure time line offset(default 5) */
#define PAS106_REG6 0x06	/* exposure time pixel offset(default 6) */
#define PAS106_REG7 0x07	/* signbit Dac (default 0) */
#define PAS106_REG9 0x09
#define PAS106_REG0e 0x0e	/* global gain [4..0](default 0x0e) */
#define PAS106_REG13 0x13	/* end i2c write */

static const __u8 GainRGBG[] = { 0x80, 0x80, 0x80, 0x80, 0x00, 0x00 };

static const __u8 I2c2[] = { 0x08, 0x08, 0x08, 0x08, 0x0d };

static const __u8 I2c3[] = { 0x12, 0x05 };

static const __u8 I2c4[] = { 0x41, 0x08 };

/* read 'len' bytes to gspca_dev->usb_buf */
static void reg_r(struct gspca_dev *gspca_dev,
		  __u16 index,
		  __u16 len)
{
	struct usb_device *dev = gspca_dev->dev;

#ifdef GSPCA_DEBUG
	if (len > sizeof gspca_dev->usb_buf) {
		err("reg_r: buffer overflow");
		return;
	}
#endif
	usb_control_msg(dev,
			usb_rcvctrlpipe(dev, 0),
			0,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0,
			index, gspca_dev->usb_buf, len, 500);
	PDEBUG(D_USBI, "reg read [%02x] -> %02x ..",
			index, gspca_dev->usb_buf[0]);
}

static void reg_w_val(struct gspca_dev *gspca_dev,
			__u16 index,
			__u8 val)
{
	struct usb_device *dev = gspca_dev->dev;

	gspca_dev->usb_buf[0] = val;
	usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			0,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0,
			index, gspca_dev->usb_buf, 1, 500);
}

static void reg_w(struct gspca_dev *gspca_dev,
		  __u16 index,
		  const __u8 *buffer,
		  __u16 len)
{
	struct usb_device *dev = gspca_dev->dev;

#ifdef GSPCA_DEBUG
	if (len > sizeof gspca_dev->usb_buf) {
		err("reg_w: buffer overflow");
		return;
	}
	PDEBUG(D_USBO, "reg write [%02x] = %02x..", index, *buffer);
#endif
	memcpy(gspca_dev->usb_buf, buffer, len);
	usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			0,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0, index, gspca_dev->usb_buf, len, 500);
}

static int i2c_w(struct gspca_dev *gspca_dev,
		 __u8 reg,
		 const __u8 *buffer,
		 int len, __u8 mode)
{
	/* buffer should be [D0..D7] */
	__u8 ptchcount;

	/* set the base address */
	reg_w_val(gspca_dev, ET_I2C_BASE, 0x40);
					 /* sensor base for the pas106 */
	/* set count and prefetch */
	ptchcount = ((len & 0x07) << 4) | (mode & 0x03);
	reg_w_val(gspca_dev, ET_I2C_COUNT, ptchcount);
	/* set the register base */
	reg_w_val(gspca_dev, ET_I2C_REG, reg);
	while (--len >= 0)
		reg_w_val(gspca_dev, ET_I2C_DATA0 + len, buffer[len]);
	return 0;
}

static int i2c_r(struct gspca_dev *gspca_dev,
			__u8 reg)
{
	/* set the base address */
	reg_w_val(gspca_dev, ET_I2C_BASE, 0x40);
					/* sensor base for the pas106 */
	/* set count and prefetch (cnd: 4 bits - mode: 4 bits) */
	reg_w_val(gspca_dev, ET_I2C_COUNT, 0x11);
	reg_w_val(gspca_dev, ET_I2C_REG, reg);	/* set the register base */
	reg_w_val(gspca_dev, ET_I2C_PREFETCH, 0x02);	/* prefetch */
	reg_w_val(gspca_dev, ET_I2C_PREFETCH, 0x00);
	reg_r(gspca_dev, ET_I2C_DATA0, 1);	/* read one byte */
	return 0;
}

static int Et_WaitStatus(struct gspca_dev *gspca_dev)
{
	int retry = 10;

	while (retry--) {
		reg_r(gspca_dev, ET_ClCK, 1);
		if (gspca_dev->usb_buf[0] != 0)
			return 1;
	}
	return 0;
}

static int et_video(struct gspca_dev *gspca_dev,
		    int on)
{
	int ret;

	reg_w_val(gspca_dev, ET_GPIO_OUT,
		  on ? 0x10		/* startvideo - set Bit5 */
		     : 0);		/* stopvideo */
	ret = Et_WaitStatus(gspca_dev);
	if (ret != 0)
		PDEBUG(D_ERR, "timeout video on/off");
	return ret;
}

static void Et_init2(struct gspca_dev *gspca_dev)
{
	__u8 value;
	static const __u8 FormLine[] = { 0x84, 0x03, 0x14, 0xf4, 0x01, 0x05 };

	PDEBUG(D_STREAM, "Open Init2 ET");
	reg_w_val(gspca_dev, ET_GPIO_DIR_CTRL, 0x2f);
	reg_w_val(gspca_dev, ET_GPIO_OUT, 0x10);
	reg_r(gspca_dev, ET_GPIO_IN, 1);
	reg_w_val(gspca_dev, ET_ClCK, 0x14); /* 0x14 // 0x16 enabled pattern */
	reg_w_val(gspca_dev, ET_CTRL, 0x1b);

	/*  compression et subsampling */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv)
		value = ET_COMP_VAL1;	/* 320 */
	else
		value = ET_COMP_VAL0;	/* 640 */
	reg_w_val(gspca_dev, ET_COMP, value);
	reg_w_val(gspca_dev, ET_MAXQt, 0x1f);
	reg_w_val(gspca_dev, ET_MINQt, 0x04);
	/* undocumented registers */
	reg_w_val(gspca_dev, ET_REG1d, 0xff);
	reg_w_val(gspca_dev, ET_REG1e, 0xff);
	reg_w_val(gspca_dev, ET_REG1f, 0xff);
	reg_w_val(gspca_dev, ET_REG20, 0x35);
	reg_w_val(gspca_dev, ET_REG21, 0x01);
	reg_w_val(gspca_dev, ET_REG22, 0x00);
	reg_w_val(gspca_dev, ET_REG23, 0xff);
	reg_w_val(gspca_dev, ET_REG24, 0xff);
	reg_w_val(gspca_dev, ET_REG25, 0x0f);
	/* colors setting */
	reg_w_val(gspca_dev, 0x30, 0x11);		/* 0x30 */
	reg_w_val(gspca_dev, 0x31, 0x40);
	reg_w_val(gspca_dev, 0x32, 0x00);
	reg_w_val(gspca_dev, ET_O_RED, 0x00);		/* 0x34 */
	reg_w_val(gspca_dev, ET_O_GREEN1, 0x00);
	reg_w_val(gspca_dev, ET_O_BLUE, 0x00);
	reg_w_val(gspca_dev, ET_O_GREEN2, 0x00);
	/*************/
	reg_w_val(gspca_dev, ET_G_RED, 0x80);		/* 0x4d */
	reg_w_val(gspca_dev, ET_G_GREEN1, 0x80);
	reg_w_val(gspca_dev, ET_G_BLUE, 0x80);
	reg_w_val(gspca_dev, ET_G_GREEN2, 0x80);
	reg_w_val(gspca_dev, ET_G_GR_H, 0x00);
	reg_w_val(gspca_dev, ET_G_GB_H, 0x00);		/* 0x52 */
	/* Window control registers */
	reg_w_val(gspca_dev, 0x61, 0x80);		/* use cmc_out */
	reg_w_val(gspca_dev, 0x62, 0x02);
	reg_w_val(gspca_dev, 0x63, 0x03);
	reg_w_val(gspca_dev, 0x64, 0x14);
	reg_w_val(gspca_dev, 0x65, 0x0e);
	reg_w_val(gspca_dev, 0x66, 0x02);
	reg_w_val(gspca_dev, 0x67, 0x02);

	/**************************************/
	reg_w_val(gspca_dev, ET_SYNCHRO, 0x8f);		/* 0x68 */
	reg_w_val(gspca_dev, ET_STARTX, 0x69);		/* 0x6a //0x69 */
	reg_w_val(gspca_dev, ET_STARTY, 0x0d);		/* 0x0d //0x0c */
	reg_w_val(gspca_dev, ET_WIDTH_LOW, 0x80);
	reg_w_val(gspca_dev, ET_HEIGTH_LOW, 0xe0);
	reg_w_val(gspca_dev, ET_W_H_HEIGTH, 0x60);	/* 6d */
	reg_w_val(gspca_dev, ET_REG6e, 0x86);
	reg_w_val(gspca_dev, ET_REG6f, 0x01);
	reg_w_val(gspca_dev, ET_REG70, 0x26);
	reg_w_val(gspca_dev, ET_REG71, 0x7a);
	reg_w_val(gspca_dev, ET_REG72, 0x01);
	/* Clock Pattern registers ***************** */
	reg_w_val(gspca_dev, ET_REG73, 0x00);
	reg_w_val(gspca_dev, ET_REG74, 0x18);		/* 0x28 */
	reg_w_val(gspca_dev, ET_REG75, 0x0f);		/* 0x01 */
	/**********************************************/
	reg_w_val(gspca_dev, 0x8a, 0x20);
	reg_w_val(gspca_dev, 0x8d, 0x0f);
	reg_w_val(gspca_dev, 0x8e, 0x08);
	/**************************************/
	reg_w_val(gspca_dev, 0x03, 0x08);
	reg_w_val(gspca_dev, ET_PXL_CLK, 0x03);
	reg_w_val(gspca_dev, 0x81, 0xff);
	reg_w_val(gspca_dev, 0x80, 0x00);
	reg_w_val(gspca_dev, 0x81, 0xff);
	reg_w_val(gspca_dev, 0x80, 0x20);
	reg_w_val(gspca_dev, 0x03, 0x01);
	reg_w_val(gspca_dev, 0x03, 0x00);
	reg_w_val(gspca_dev, 0x03, 0x08);
	/********************************************/

/*	reg_r(gspca_dev, ET_I2C_BASE, 1);
					 always 0x40 as the pas106 ??? */
	/* set the sensor */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv)
		value = 0x04;		/* 320 */
	else				/* 640 */
		value = 0x1e;	/* 0x17	 * setting PixelClock
					 * 0x03 mean 24/(3+1) = 6 Mhz
					 * 0x05 -> 24/(5+1) = 4 Mhz
					 * 0x0b -> 24/(11+1) = 2 Mhz
					 * 0x17 -> 24/(23+1) = 1 Mhz
					 */
	reg_w_val(gspca_dev, ET_PXL_CLK, value);
	/* now set by fifo the FormatLine setting */
	reg_w(gspca_dev, 0x62, FormLine, 6);

	/* set exposure times [ 0..0x78] 0->longvalue 0x78->shortvalue */
	reg_w_val(gspca_dev, 0x81, 0x47);	/* 0x47; */
	reg_w_val(gspca_dev, 0x80, 0x40);	/* 0x40; */
	/* Pedro change */
	/* Brightness change Brith+ decrease value */
	/* Brigth- increase value */
	/* original value = 0x70; */
	reg_w_val(gspca_dev, 0x81, 0x30);	/* 0x20; - set brightness */
	reg_w_val(gspca_dev, 0x80, 0x20);	/* 0x20; */
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	__u8 brightness = sd->brightness;

	for (i = 0; i < 4; i++)
		reg_w_val(gspca_dev, ET_O_RED + i, brightness);
}

static void getbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	int brightness = 0;

	for (i = 0; i < 4; i++) {
		reg_r(gspca_dev, ET_O_RED + i, 1);
		brightness += gspca_dev->usb_buf[0];
	}
	sd->brightness = brightness >> 3;
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 RGBG[] = { 0x80, 0x80, 0x80, 0x80, 0x00, 0x00 };
	__u8 contrast = sd->contrast;

	memset(RGBG, contrast, sizeof(RGBG) - 2);
	reg_w(gspca_dev, ET_G_RED, RGBG, 6);
}

static void getcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	int contrast = 0;

	for (i = 0; i < 4; i++) {
		reg_r(gspca_dev, ET_G_RED + i, 1);
		contrast += gspca_dev->usb_buf[0];
	}
	sd->contrast = contrast >> 2;
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 I2cc[] = { 0x05, 0x02, 0x02, 0x05, 0x0d };
	__u8 i2cflags = 0x01;
	/* __u8 green = 0; */
	__u8 colors = sd->colors;

	I2cc[3] = colors;	/* red */
	I2cc[0] = 15 - colors;	/* blue */
	/* green = 15 - ((((7*I2cc[0]) >> 2 ) + I2cc[3]) >> 1); */
	/* I2cc[1] = I2cc[2] = green; */
	if (sd->sensor == SENSOR_PAS106) {
		i2c_w(gspca_dev, PAS106_REG13, &i2cflags, 1, 3);
		i2c_w(gspca_dev, PAS106_REG9, I2cc, sizeof I2cc, 1);
	}
/*	PDEBUG(D_CONF , "Etoms red %d blue %d green %d",
		I2cc[3], I2cc[0], green); */
}

static void getcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_PAS106) {
/*		i2c_r(gspca_dev, PAS106_REG9);		 * blue */
		i2c_r(gspca_dev, PAS106_REG9 + 3);	/* red */
		sd->colors = gspca_dev->usb_buf[0] & 0x0f;
	}
}

static void setautogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->autogain)
		sd->ag_cnt = AG_CNT_START;
	else
		sd->ag_cnt = -1;
}

static void Et_init1(struct gspca_dev *gspca_dev)
{
	__u8 value;
/*	__u8 I2c0 [] = {0x0a, 0x12, 0x05, 0x22, 0xac, 0x00, 0x01, 0x00}; */
	__u8 I2c0[] = { 0x0a, 0x12, 0x05, 0x6d, 0xcd, 0x00, 0x01, 0x00 };
						/* try 1/120 0x6d 0xcd 0x40 */
/*	__u8 I2c0 [] = {0x0a, 0x12, 0x05, 0xfe, 0xfe, 0xc0, 0x01, 0x00};
						 * 1/60000 hmm ?? */

	PDEBUG(D_STREAM, "Open Init1 ET");
	reg_w_val(gspca_dev, ET_GPIO_DIR_CTRL, 7);
	reg_r(gspca_dev, ET_GPIO_IN, 1);
	reg_w_val(gspca_dev, ET_RESET_ALL, 1);
	reg_w_val(gspca_dev, ET_RESET_ALL, 0);
	reg_w_val(gspca_dev, ET_ClCK, 0x10);
	reg_w_val(gspca_dev, ET_CTRL, 0x19);
	/*   compression et subsampling */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv)
		value = ET_COMP_VAL1;
	else
		value = ET_COMP_VAL0;
	PDEBUG(D_STREAM, "Open mode %d Compression %d",
	       gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv,
	       value);
	reg_w_val(gspca_dev, ET_COMP, value);
	reg_w_val(gspca_dev, ET_MAXQt, 0x1d);
	reg_w_val(gspca_dev, ET_MINQt, 0x02);
	/* undocumented registers */
	reg_w_val(gspca_dev, ET_REG1d, 0xff);
	reg_w_val(gspca_dev, ET_REG1e, 0xff);
	reg_w_val(gspca_dev, ET_REG1f, 0xff);
	reg_w_val(gspca_dev, ET_REG20, 0x35);
	reg_w_val(gspca_dev, ET_REG21, 0x01);
	reg_w_val(gspca_dev, ET_REG22, 0x00);
	reg_w_val(gspca_dev, ET_REG23, 0xf7);
	reg_w_val(gspca_dev, ET_REG24, 0xff);
	reg_w_val(gspca_dev, ET_REG25, 0x07);
	/* colors setting */
	reg_w_val(gspca_dev, ET_G_RED, 0x80);
	reg_w_val(gspca_dev, ET_G_GREEN1, 0x80);
	reg_w_val(gspca_dev, ET_G_BLUE, 0x80);
	reg_w_val(gspca_dev, ET_G_GREEN2, 0x80);
	reg_w_val(gspca_dev, ET_G_GR_H, 0x00);
	reg_w_val(gspca_dev, ET_G_GB_H, 0x00);
	/* Window control registers */
	reg_w_val(gspca_dev, ET_SYNCHRO, 0xf0);
	reg_w_val(gspca_dev, ET_STARTX, 0x56);		/* 0x56 */
	reg_w_val(gspca_dev, ET_STARTY, 0x05);		/* 0x04 */
	reg_w_val(gspca_dev, ET_WIDTH_LOW, 0x60);
	reg_w_val(gspca_dev, ET_HEIGTH_LOW, 0x20);
	reg_w_val(gspca_dev, ET_W_H_HEIGTH, 0x50);
	reg_w_val(gspca_dev, ET_REG6e, 0x86);
	reg_w_val(gspca_dev, ET_REG6f, 0x01);
	reg_w_val(gspca_dev, ET_REG70, 0x86);
	reg_w_val(gspca_dev, ET_REG71, 0x14);
	reg_w_val(gspca_dev, ET_REG72, 0x00);
	/* Clock Pattern registers */
	reg_w_val(gspca_dev, ET_REG73, 0x00);
	reg_w_val(gspca_dev, ET_REG74, 0x00);
	reg_w_val(gspca_dev, ET_REG75, 0x0a);
	reg_w_val(gspca_dev, ET_I2C_CLK, 0x04);
	reg_w_val(gspca_dev, ET_PXL_CLK, 0x01);
	/* set the sensor */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv) {
		I2c0[0] = 0x06;
		i2c_w(gspca_dev, PAS106_REG2, I2c0, sizeof I2c0, 1);
		i2c_w(gspca_dev, PAS106_REG9, I2c2, sizeof I2c2, 1);
		value = 0x06;
		i2c_w(gspca_dev, PAS106_REG2, &value, 1, 1);
		i2c_w(gspca_dev, PAS106_REG3, I2c3, sizeof I2c3, 1);
		/* value = 0x1f; */
		value = 0x04;
		i2c_w(gspca_dev, PAS106_REG0e, &value, 1, 1);
	} else {
		I2c0[0] = 0x0a;

		i2c_w(gspca_dev, PAS106_REG2, I2c0, sizeof I2c0, 1);
		i2c_w(gspca_dev, PAS106_REG9, I2c2, sizeof I2c2, 1);
		value = 0x0a;
		i2c_w(gspca_dev, PAS106_REG2, &value, 1, 1);
		i2c_w(gspca_dev, PAS106_REG3, I2c3, sizeof I2c3, 1);
		value = 0x04;
		/* value = 0x10; */
		i2c_w(gspca_dev, PAS106_REG0e, &value, 1, 1);
		/* bit 2 enable bit 1:2 select 0 1 2 3
		   value = 0x07;                                * curve 0 *
		   i2c_w(gspca_dev, PAS106_REG0f, &value, 1, 1);
		 */
	}

/*	value = 0x01; */
/*	value = 0x22; */
/*	i2c_w(gspca_dev, PAS106_REG5, &value, 1, 1); */
	/* magnetude and sign bit for DAC */
	i2c_w(gspca_dev, PAS106_REG7, I2c4, sizeof I2c4, 1);
	/* now set by fifo the whole colors setting */
	reg_w(gspca_dev, ET_G_RED, GainRGBG, 6);
	getcolors(gspca_dev);
	setcolors(gspca_dev);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->epaddr = 1;
	sd->sensor = id->driver_info;
	if (sd->sensor == SENSOR_PAS106) {
		cam->cam_mode = sif_mode;
		cam->nmodes = sizeof sif_mode / sizeof sif_mode[0];
	} else {
		cam->cam_mode = vga_mode;
		cam->nmodes = sizeof vga_mode / sizeof vga_mode[0];
	}
	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->colors = COLOR_DEF;
	sd->autogain = AUTOGAIN_DEF;
	sd->ag_cnt = -1;
	return 0;
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_PAS106)
		Et_init1(gspca_dev);
	else
		Et_init2(gspca_dev);
	reg_w_val(gspca_dev, ET_RESET_ALL, 0x08);
	et_video(gspca_dev, 0);		/* video off */
	return 0;
}

/* -- start the camera -- */
static void sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_PAS106)
		Et_init1(gspca_dev);
	else
		Et_init2(gspca_dev);

	setautogain(gspca_dev);

	reg_w_val(gspca_dev, ET_RESET_ALL, 0x08);
	et_video(gspca_dev, 1);		/* video on */
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	et_video(gspca_dev, 0);		/* video off */
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
}

static void sd_close(struct gspca_dev *gspca_dev)
{
}

static __u8 Et_getgainG(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_PAS106) {
		i2c_r(gspca_dev, PAS106_REG0e);
		PDEBUG(D_CONF, "Etoms gain G %d", gspca_dev->usb_buf[0]);
		return gspca_dev->usb_buf[0];
	}
	return 0x1f;
}

static void Et_setgainG(struct gspca_dev *gspca_dev, __u8 gain)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_PAS106) {
		__u8 i2cflags = 0x01;

		i2c_w(gspca_dev, PAS106_REG13, &i2cflags, 1, 3);
		i2c_w(gspca_dev, PAS106_REG0e, &gain, 1, 1);
	}
}

#define BLIMIT(bright) \
	(__u8)((bright > 0x1f)?0x1f:((bright < 4)?3:bright))
#define LIMIT(color) \
	(unsigned char)((color > 0xff)?0xff:((color < 0)?0:color))

static void do_autogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 luma;
	__u8 luma_mean = 128;
	__u8 luma_delta = 20;
	__u8 spring = 4;
	int Gbright;
	__u8 r, g, b;

	if (sd->ag_cnt < 0)
		return;
	if (--sd->ag_cnt >= 0)
		return;
	sd->ag_cnt = AG_CNT_START;

	Gbright = Et_getgainG(gspca_dev);
	reg_r(gspca_dev, ET_LUMA_CENTER, 4);
	g = (gspca_dev->usb_buf[0] + gspca_dev->usb_buf[3]) >> 1;
	r = gspca_dev->usb_buf[1];
	b = gspca_dev->usb_buf[2];
	r = ((r << 8) - (r << 4) - (r << 3)) >> 10;
	b = ((b << 7) >> 10);
	g = ((g << 9) + (g << 7) + (g << 5)) >> 10;
	luma = LIMIT(r + g + b);
	PDEBUG(D_FRAM, "Etoms luma G %d", luma);
	if (luma < luma_mean - luma_delta || luma > luma_mean + luma_delta) {
		Gbright += (luma_mean - luma) >> spring;
		Gbright = BLIMIT(Gbright);
		PDEBUG(D_FRAM, "Etoms Gbright %d", Gbright);
		Et_setgainG(gspca_dev, (__u8) Gbright);
	}
}

#undef BLIMIT
#undef LIMIT

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	int seqframe;

	seqframe = data[0] & 0x3f;
	len = (int) (((data[0] & 0xc0) << 2) | data[1]);
	if (seqframe == 0x3f) {
		PDEBUG(D_FRAM,
		       "header packet found datalength %d !!", len);
		PDEBUG(D_FRAM, "G %d R %d G %d B %d",
		       data[2], data[3], data[4], data[5]);
		data += 30;
		/* don't change datalength as the chips provided it */
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					data, 0);
		gspca_frame_add(gspca_dev, FIRST_PACKET, frame, data, len);
		return;
	}
	if (len) {
		data += 8;
		gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
	} else {			/* Drop Packet */
		gspca_dev->last_packet_type = DISCARD_PACKET;
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

	getbrightness(gspca_dev);
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

	getcontrast(gspca_dev);
	*val = sd->contrast;
	return 0;
}

static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->colors = val;
	if (gspca_dev->streaming)
		setcolors(gspca_dev);
	return 0;
}

static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	getcolors(gspca_dev);
	*val = sd->colors;
	return 0;
}

static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = val;
	if (gspca_dev->streaming)
		setautogain(gspca_dev);
	return 0;
}

static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

/* sub-driver description */
static struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.open = sd_open,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.close = sd_close,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = do_autogain,
};

/* -- module initialisation -- */
static __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x102c, 0x6151), .driver_info = SENSOR_PAS106},
#ifndef CONFIG_USB_ET61X251
	{USB_DEVICE(0x102c, 0x6251), .driver_info = SENSOR_TAS5130CXX},
#endif
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
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	if (usb_register(&sd_driver) < 0)
		return -1;
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
