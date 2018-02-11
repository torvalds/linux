/* stk-sensor.c: Driver for ov96xx sensor (used in some Syntek webcams)
 *
 * Copyright 2007-2008 Jaime Velasco Juan <jsagarribay@gmail.com>
 *
 * Some parts derived from ov7670.c:
 * Copyright 2006 One Laptop Per Child Association, Inc.  Written
 * by Jonathan Corbet with substantial inspiration from Mark
 * McClelland's ovcamchip code.
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This file may be distributed under the terms of the GNU General
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Controlling the sensor via the STK1125 vendor specific control interface:
 * The camera uses an OmniVision sensor and the stk1125 provides an
 * SCCB(i2c)-USB bridge which let us program the sensor.
 * In my case the sensor id is 0x9652, it can be read from sensor's register
 * 0x0A and 0x0B as follows:
 * - read register #R:
 *   output #R to index 0x0208
 *   output 0x0070 to index 0x0200
 *   input 1 byte from index 0x0201 (some kind of status register)
 *     until its value is 0x01
 *   input 1 byte from index 0x0209. This is the value of #R
 * - write value V to register #R
 *   output #R to index 0x0204
 *   output V to index 0x0205
 *   output 0x0005 to index 0x0200
 *   input 1 byte from index 0x0201 until its value becomes 0x04
 */

/* It seems the i2c bus is controlled with these registers */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "stk-webcam.h"

#define STK_IIC_BASE		(0x0200)
#  define STK_IIC_OP		(STK_IIC_BASE)
#    define STK_IIC_OP_TX	(0x05)
#    define STK_IIC_OP_RX	(0x70)
#  define STK_IIC_STAT		(STK_IIC_BASE+1)
#    define STK_IIC_STAT_TX_OK	(0x04)
#    define STK_IIC_STAT_RX_OK	(0x01)
/* I don't know what does this register.
 * when it is 0x00 or 0x01, we cannot talk to the sensor,
 * other values work */
#  define STK_IIC_ENABLE	(STK_IIC_BASE+2)
#    define STK_IIC_ENABLE_NO	(0x00)
/* This is what the driver writes in windows */
#    define STK_IIC_ENABLE_YES	(0x1e)
/*
 * Address of the slave. Seems like the binary driver look for the
 * sensor in multiple places, attempting a reset sequence.
 * We only know about the ov9650
 */
#  define STK_IIC_ADDR		(STK_IIC_BASE+3)
#  define STK_IIC_TX_INDEX	(STK_IIC_BASE+4)
#  define STK_IIC_TX_VALUE	(STK_IIC_BASE+5)
#  define STK_IIC_RX_INDEX	(STK_IIC_BASE+8)
#  define STK_IIC_RX_VALUE	(STK_IIC_BASE+9)

#define MAX_RETRIES		(50)

#define SENSOR_ADDRESS		(0x60)

/* From ov7670.c (These registers aren't fully accurate) */

/* Registers */
#define REG_GAIN	0x00	/* Gain lower 8 bits (rest in vref) */
#define REG_BLUE	0x01	/* blue gain */
#define REG_RED		0x02	/* red gain */
#define REG_VREF	0x03	/* Pieces of GAIN, VSTART, VSTOP */
#define REG_COM1	0x04	/* Control 1 */
#define  COM1_CCIR656	  0x40  /* CCIR656 enable */
#define  COM1_QFMT	  0x20  /* QVGA/QCIF format */
#define  COM1_SKIP_0	  0x00  /* Do not skip any row */
#define  COM1_SKIP_2	  0x04  /* Skip 2 rows of 4 */
#define  COM1_SKIP_3	  0x08  /* Skip 3 rows of 4 */
#define REG_BAVE	0x05	/* U/B Average level */
#define REG_GbAVE	0x06	/* Y/Gb Average level */
#define REG_AECHH	0x07	/* AEC MS 5 bits */
#define REG_RAVE	0x08	/* V/R Average level */
#define REG_COM2	0x09	/* Control 2 */
#define  COM2_SSLEEP	  0x10	/* Soft sleep mode */
#define REG_PID		0x0a	/* Product ID MSB */
#define REG_VER		0x0b	/* Product ID LSB */
#define REG_COM3	0x0c	/* Control 3 */
#define  COM3_SWAP	  0x40	  /* Byte swap */
#define  COM3_SCALEEN	  0x08	  /* Enable scaling */
#define  COM3_DCWEN	  0x04	  /* Enable downsamp/crop/window */
#define REG_COM4	0x0d	/* Control 4 */
#define REG_COM5	0x0e	/* All "reserved" */
#define REG_COM6	0x0f	/* Control 6 */
#define REG_AECH	0x10	/* More bits of AEC value */
#define REG_CLKRC	0x11	/* Clock control */
#define   CLK_PLL	  0x80	  /* Enable internal PLL */
#define   CLK_EXT	  0x40	  /* Use external clock directly */
#define   CLK_SCALE	  0x3f	  /* Mask for internal clock scale */
#define REG_COM7	0x12	/* Control 7 */
#define   COM7_RESET	  0x80	  /* Register reset */
#define   COM7_FMT_MASK	  0x38
#define   COM7_FMT_SXGA	  0x00
#define   COM7_FMT_VGA	  0x40
#define	  COM7_FMT_CIF	  0x20	  /* CIF format */
#define   COM7_FMT_QVGA	  0x10	  /* QVGA format */
#define   COM7_FMT_QCIF	  0x08	  /* QCIF format */
#define	  COM7_RGB	  0x04	  /* bits 0 and 2 - RGB format */
#define	  COM7_YUV	  0x00	  /* YUV */
#define	  COM7_BAYER	  0x01	  /* Bayer format */
#define	  COM7_PBAYER	  0x05	  /* "Processed bayer" */
#define REG_COM8	0x13	/* Control 8 */
#define   COM8_FASTAEC	  0x80	  /* Enable fast AGC/AEC */
#define   COM8_AECSTEP	  0x40	  /* Unlimited AEC step size */
#define   COM8_BFILT	  0x20	  /* Band filter enable */
#define   COM8_AGC	  0x04	  /* Auto gain enable */
#define   COM8_AWB	  0x02	  /* White balance enable */
#define   COM8_AEC	  0x01	  /* Auto exposure enable */
#define REG_COM9	0x14	/* Control 9  - gain ceiling */
#define REG_COM10	0x15	/* Control 10 */
#define   COM10_HSYNC	  0x40	  /* HSYNC instead of HREF */
#define   COM10_PCLK_HB	  0x20	  /* Suppress PCLK on horiz blank */
#define   COM10_HREF_REV  0x08	  /* Reverse HREF */
#define   COM10_VS_LEAD	  0x04	  /* VSYNC on clock leading edge */
#define   COM10_VS_NEG	  0x02	  /* VSYNC negative */
#define   COM10_HS_NEG	  0x01	  /* HSYNC negative */
#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */
#define REG_PSHFT	0x1b	/* Pixel delay after HREF */
#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */
#define REG_MVFP	0x1e	/* Mirror / vflip */
#define   MVFP_MIRROR	  0x20	  /* Mirror image */
#define   MVFP_FLIP	  0x10	  /* Vertical flip */

#define REG_AEW		0x24	/* AGC upper limit */
#define REG_AEB		0x25	/* AGC lower limit */
#define REG_VPT		0x26	/* AGC/AEC fast mode op region */
#define REG_ADVFL	0x2d	/* Insert dummy lines (LSB) */
#define REG_ADVFH	0x2e	/* Insert dummy lines (MSB) */
#define REG_HSYST	0x30	/* HSYNC rising edge delay */
#define REG_HSYEN	0x31	/* HSYNC falling edge delay */
#define REG_HREF	0x32	/* HREF pieces */
#define REG_TSLB	0x3a	/* lots of stuff */
#define   TSLB_YLAST	  0x04	  /* UYVY or VYUY - see com13 */
#define   TSLB_BYTEORD	  0x08	  /* swap bytes in 16bit mode? */
#define REG_COM11	0x3b	/* Control 11 */
#define   COM11_NIGHT	  0x80	  /* NIght mode enable */
#define   COM11_NMFR	  0x60	  /* Two bit NM frame rate */
#define   COM11_HZAUTO	  0x10	  /* Auto detect 50/60 Hz */
#define	  COM11_50HZ	  0x08	  /* Manual 50Hz select */
#define   COM11_EXP	  0x02
#define REG_COM12	0x3c	/* Control 12 */
#define   COM12_HREF	  0x80	  /* HREF always */
#define REG_COM13	0x3d	/* Control 13 */
#define   COM13_GAMMA	  0x80	  /* Gamma enable */
#define	  COM13_UVSAT	  0x40	  /* UV saturation auto adjustment */
#define	  COM13_CMATRIX	  0x10	  /* Enable color matrix for RGB or YUV */
#define   COM13_UVSWAP	  0x01	  /* V before U - w/TSLB */
#define REG_COM14	0x3e	/* Control 14 */
#define   COM14_DCWEN	  0x10	  /* DCW/PCLK-scale enable */
#define REG_EDGE	0x3f	/* Edge enhancement factor */
#define REG_COM15	0x40	/* Control 15 */
#define   COM15_R10F0	  0x00	  /* Data range 10 to F0 */
#define	  COM15_R01FE	  0x80	  /*            01 to FE */
#define   COM15_R00FF	  0xc0	  /*            00 to FF */
#define   COM15_RGB565	  0x10	  /* RGB565 output */
#define   COM15_RGBFIXME	  0x20	  /* FIXME  */
#define   COM15_RGB555	  0x30	  /* RGB555 output */
#define REG_COM16	0x41	/* Control 16 */
#define   COM16_AWBGAIN   0x08	  /* AWB gain enable */
#define REG_COM17	0x42	/* Control 17 */
#define   COM17_AECWIN	  0xc0	  /* AEC window - must match COM4 */
#define   COM17_CBAR	  0x08	  /* DSP Color bar */

/*
 * This matrix defines how the colors are generated, must be
 * tweaked to adjust hue and saturation.
 *
 * Order: v-red, v-green, v-blue, u-red, u-green, u-blue
 *
 * They are nine-bit signed quantities, with the sign bit
 * stored in 0x58.  Sign for v-red is bit 0, and up from there.
 */
#define	REG_CMATRIX_BASE 0x4f
#define   CMATRIX_LEN 6
#define REG_CMATRIX_SIGN 0x58


#define REG_BRIGHT	0x55	/* Brightness */
#define REG_CONTRAS	0x56	/* Contrast control */

#define REG_GFIX	0x69	/* Fix gain control */

#define REG_RGB444	0x8c	/* RGB 444 control */
#define   R444_ENABLE	  0x02	  /* Turn on RGB444, overrides 5x5 */
#define   R444_RGBX	  0x01	  /* Empty nibble at end */

#define REG_HAECC1	0x9f	/* Hist AEC/AGC control 1 */
#define REG_HAECC2	0xa0	/* Hist AEC/AGC control 2 */

#define REG_BD50MAX	0xa5	/* 50hz banding step limit */
#define REG_HAECC3	0xa6	/* Hist AEC/AGC control 3 */
#define REG_HAECC4	0xa7	/* Hist AEC/AGC control 4 */
#define REG_HAECC5	0xa8	/* Hist AEC/AGC control 5 */
#define REG_HAECC6	0xa9	/* Hist AEC/AGC control 6 */
#define REG_HAECC7	0xaa	/* Hist AEC/AGC control 7 */
#define REG_BD60MAX	0xab	/* 60hz banding step limit */




/* Returns 0 if OK */
static int stk_sensor_outb(struct stk_camera *dev, u8 reg, u8 val)
{
	int i = 0;
	u8 tmpval = 0;

	if (stk_camera_write_reg(dev, STK_IIC_TX_INDEX, reg))
		return 1;
	if (stk_camera_write_reg(dev, STK_IIC_TX_VALUE, val))
		return 1;
	if (stk_camera_write_reg(dev, STK_IIC_OP, STK_IIC_OP_TX))
		return 1;
	do {
		if (stk_camera_read_reg(dev, STK_IIC_STAT, &tmpval))
			return 1;
		i++;
	} while (tmpval == 0 && i < MAX_RETRIES);
	if (tmpval != STK_IIC_STAT_TX_OK) {
		if (tmpval)
			pr_err("stk_sensor_outb failed, status=0x%02x\n",
			       tmpval);
		return 1;
	} else
		return 0;
}

static int stk_sensor_inb(struct stk_camera *dev, u8 reg, u8 *val)
{
	int i = 0;
	u8 tmpval = 0;

	if (stk_camera_write_reg(dev, STK_IIC_RX_INDEX, reg))
		return 1;
	if (stk_camera_write_reg(dev, STK_IIC_OP, STK_IIC_OP_RX))
		return 1;
	do {
		if (stk_camera_read_reg(dev, STK_IIC_STAT, &tmpval))
			return 1;
		i++;
	} while (tmpval == 0 && i < MAX_RETRIES);
	if (tmpval != STK_IIC_STAT_RX_OK) {
		if (tmpval)
			pr_err("stk_sensor_inb failed, status=0x%02x\n",
			       tmpval);
		return 1;
	}

	if (stk_camera_read_reg(dev, STK_IIC_RX_VALUE, &tmpval))
		return 1;

	*val = tmpval;
	return 0;
}

static int stk_sensor_write_regvals(struct stk_camera *dev,
		struct regval *rv)
{
	int ret;
	if (rv == NULL)
		return 0;
	while (rv->reg != 0xff || rv->val != 0xff) {
		ret = stk_sensor_outb(dev, rv->reg, rv->val);
		if (ret != 0)
			return ret;
		rv++;
	}
	return 0;
}

int stk_sensor_sleep(struct stk_camera *dev)
{
	u8 tmp;
	return stk_sensor_inb(dev, REG_COM2, &tmp)
		|| stk_sensor_outb(dev, REG_COM2, tmp|COM2_SSLEEP);
}

int stk_sensor_wakeup(struct stk_camera *dev)
{
	u8 tmp;
	return stk_sensor_inb(dev, REG_COM2, &tmp)
		|| stk_sensor_outb(dev, REG_COM2, tmp&~COM2_SSLEEP);
}

static struct regval ov_initvals[] = {
	{REG_CLKRC, CLK_PLL},
	{REG_COM11, 0x01},
	{0x6a, 0x7d},
	{REG_AECH, 0x40},
	{REG_GAIN, 0x00},
	{REG_BLUE, 0x80},
	{REG_RED, 0x80},
	/* Do not enable fast AEC for now */
	/*{REG_COM8, COM8_FASTAEC|COM8_AECSTEP|COM8_BFILT|COM8_AGC|COM8_AEC},*/
	{REG_COM8, COM8_AECSTEP|COM8_BFILT|COM8_AGC|COM8_AEC},
	{0x39, 0x50}, {0x38, 0x93},
	{0x37, 0x00}, {0x35, 0x81},
	{REG_COM5, 0x20},
	{REG_COM1, 0x00},
	{REG_COM3, 0x00},
	{REG_COM4, 0x00},
	{REG_PSHFT, 0x00},
	{0x16, 0x07},
	{0x33, 0xe2}, {0x34, 0xbf},
	{REG_COM16, 0x00},
	{0x96, 0x04},
	/* Gamma curve values */
/*	{ 0x7a, 0x20 },		{ 0x7b, 0x10 },
	{ 0x7c, 0x1e },		{ 0x7d, 0x35 },
	{ 0x7e, 0x5a },		{ 0x7f, 0x69 },
	{ 0x80, 0x76 },		{ 0x81, 0x80 },
	{ 0x82, 0x88 },		{ 0x83, 0x8f },
	{ 0x84, 0x96 },		{ 0x85, 0xa3 },
	{ 0x86, 0xaf },		{ 0x87, 0xc4 },
	{ 0x88, 0xd7 },		{ 0x89, 0xe8 },
*/
	{REG_GFIX, 0x40},
	{0x8e, 0x00},
	{REG_COM12, 0x73},
	{0x8f, 0xdf}, {0x8b, 0x06},
	{0x8c, 0x20},
	{0x94, 0x88}, {0x95, 0x88},
/*	{REG_COM15, 0xc1}, TODO */
	{0x29, 0x3f},
	{REG_COM6, 0x42},
	{REG_BD50MAX, 0x80},
	{REG_HAECC6, 0xb8}, {REG_HAECC7, 0x92},
	{REG_BD60MAX, 0x0a},
	{0x90, 0x00}, {0x91, 0x00},
	{REG_HAECC1, 0x00}, {REG_HAECC2, 0x00},
	{REG_AEW, 0x68}, {REG_AEB, 0x5c},
	{REG_VPT, 0xc3},
	{REG_COM9, 0x2e},
	{0x2a, 0x00}, {0x2b, 0x00},

	{0xff, 0xff}, /* END MARKER */
};

/* Probe the I2C bus and initialise the sensor chip */
int stk_sensor_init(struct stk_camera *dev)
{
	u8 idl = 0;
	u8 idh = 0;

	if (stk_camera_write_reg(dev, STK_IIC_ENABLE, STK_IIC_ENABLE_YES)
		|| stk_camera_write_reg(dev, STK_IIC_ADDR, SENSOR_ADDRESS)
		|| stk_sensor_outb(dev, REG_COM7, COM7_RESET)) {
		pr_err("Sensor resetting failed\n");
		return -ENODEV;
	}
	msleep(10);
	/* Read the manufacturer ID: ov = 0x7FA2 */
	if (stk_sensor_inb(dev, REG_MIDH, &idh)
	    || stk_sensor_inb(dev, REG_MIDL, &idl)) {
		pr_err("Strange error reading sensor ID\n");
		return -ENODEV;
	}
	if (idh != 0x7f || idl != 0xa2) {
		pr_err("Huh? you don't have a sensor from ovt\n");
		return -ENODEV;
	}
	if (stk_sensor_inb(dev, REG_PID, &idh)
	    || stk_sensor_inb(dev, REG_VER, &idl)) {
		pr_err("Could not read sensor model\n");
		return -ENODEV;
	}
	stk_sensor_write_regvals(dev, ov_initvals);
	msleep(10);
	pr_info("OmniVision sensor detected, id %02X%02X at address %x\n",
		idh, idl, SENSOR_ADDRESS);
	return 0;
}

/* V4L2_PIX_FMT_UYVY */
static struct regval ov_fmt_uyvy[] = {
	{REG_TSLB, TSLB_YLAST|0x08 },
	{ 0x4f, 0x80 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0x80 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x22 }, 	/* "matrix coefficient 4" */
	{ 0x53, 0x5e }, 	/* "matrix coefficient 5" */
	{ 0x54, 0x80 }, 	/* "matrix coefficient 6" */
	{REG_COM13, COM13_UVSAT|COM13_CMATRIX},
	{REG_COM15, COM15_R00FF },
	{0xff, 0xff}, /* END MARKER */
};
/* V4L2_PIX_FMT_YUYV */
static struct regval ov_fmt_yuyv[] = {
	{REG_TSLB, 0 },
	{ 0x4f, 0x80 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0x80 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x22 }, 	/* "matrix coefficient 4" */
	{ 0x53, 0x5e }, 	/* "matrix coefficient 5" */
	{ 0x54, 0x80 }, 	/* "matrix coefficient 6" */
	{REG_COM13, COM13_UVSAT|COM13_CMATRIX},
	{REG_COM15, COM15_R00FF },
	{0xff, 0xff}, /* END MARKER */
};

/* V4L2_PIX_FMT_RGB565X rrrrrggg gggbbbbb */
static struct regval ov_fmt_rgbr[] = {
	{ REG_RGB444, 0 },	/* No RGB444 please */
	{REG_TSLB, 0x00},
	{ REG_COM1, 0x0 },
	{ REG_COM9, 0x38 }, 	/* 16x gain ceiling; 0x8 is reserved bit */
	{ 0x4f, 0xb3 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0xb3 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x3d }, 	/* "matrix coefficient 4" */
	{ 0x53, 0xa7 }, 	/* "matrix coefficient 5" */
	{ 0x54, 0xe4 }, 	/* "matrix coefficient 6" */
	{ REG_COM13, COM13_GAMMA },
	{ REG_COM15, COM15_RGB565|COM15_R00FF },
	{ 0xff, 0xff },
};

/* V4L2_PIX_FMT_RGB565 gggbbbbb rrrrrggg */
static struct regval ov_fmt_rgbp[] = {
	{ REG_RGB444, 0 },	/* No RGB444 please */
	{REG_TSLB, TSLB_BYTEORD },
	{ REG_COM1, 0x0 },
	{ REG_COM9, 0x38 }, 	/* 16x gain ceiling; 0x8 is reserved bit */
	{ 0x4f, 0xb3 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0xb3 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x3d }, 	/* "matrix coefficient 4" */
	{ 0x53, 0xa7 }, 	/* "matrix coefficient 5" */
	{ 0x54, 0xe4 }, 	/* "matrix coefficient 6" */
	{ REG_COM13, COM13_GAMMA },
	{ REG_COM15, COM15_RGB565|COM15_R00FF },
	{ 0xff, 0xff },
};

/* V4L2_PIX_FMT_SRGGB8 */
static struct regval ov_fmt_bayer[] = {
	/* This changes color order */
	{REG_TSLB, 0x40}, /* BGGR */
	/* {REG_TSLB, 0x08}, */ /* BGGR with vertical image flipping */
	{REG_COM15, COM15_R00FF },
	{0xff, 0xff}, /* END MARKER */
};
/*
 * Store a set of start/stop values into the camera.
 */
static int stk_sensor_set_hw(struct stk_camera *dev,
		int hstart, int hstop, int vstart, int vstop)
{
	int ret;
	unsigned char v;
/*
 * Horizontal: 11 bits, top 8 live in hstart and hstop.  Bottom 3 of
 * hstart are in href[2:0], bottom 3 of hstop in href[5:3].  There is
 * a mystery "edge offset" value in the top two bits of href.
 */
	ret =  stk_sensor_outb(dev, REG_HSTART, (hstart >> 3) & 0xff);
	ret += stk_sensor_outb(dev, REG_HSTOP, (hstop >> 3) & 0xff);
	ret += stk_sensor_inb(dev, REG_HREF, &v);
	v = (v & 0xc0) | ((hstop & 0x7) << 3) | (hstart & 0x7);
	msleep(10);
	ret += stk_sensor_outb(dev, REG_HREF, v);
/*
 * Vertical: similar arrangement (note: this is different from ov7670.c)
 */
	ret += stk_sensor_outb(dev, REG_VSTART, (vstart >> 3) & 0xff);
	ret += stk_sensor_outb(dev, REG_VSTOP, (vstop >> 3) & 0xff);
	ret += stk_sensor_inb(dev, REG_VREF, &v);
	v = (v & 0xc0) | ((vstop & 0x7) << 3) | (vstart & 0x7);
	msleep(10);
	ret += stk_sensor_outb(dev, REG_VREF, v);
	return ret;
}


int stk_sensor_configure(struct stk_camera *dev)
{
	int com7;
	/*
	 * We setup the sensor to output dummy lines in low-res modes,
	 * so we don't get absurdly hight framerates.
	 */
	unsigned dummylines;
	int flip;
	struct regval *rv;

	switch (dev->vsettings.mode) {
	case MODE_QCIF: com7 = COM7_FMT_QCIF;
		dummylines = 604;
		break;
	case MODE_QVGA: com7 = COM7_FMT_QVGA;
		dummylines = 267;
		break;
	case MODE_CIF: com7 = COM7_FMT_CIF;
		dummylines = 412;
		break;
	case MODE_VGA: com7 = COM7_FMT_VGA;
		dummylines = 11;
		break;
	case MODE_SXGA: com7 = COM7_FMT_SXGA;
		dummylines = 0;
		break;
	default:
		pr_err("Unsupported mode %d\n", dev->vsettings.mode);
		return -EFAULT;
	}
	switch (dev->vsettings.palette) {
	case V4L2_PIX_FMT_UYVY:
		com7 |= COM7_YUV;
		rv = ov_fmt_uyvy;
		break;
	case V4L2_PIX_FMT_YUYV:
		com7 |= COM7_YUV;
		rv = ov_fmt_yuyv;
		break;
	case V4L2_PIX_FMT_RGB565:
		com7 |= COM7_RGB;
		rv = ov_fmt_rgbp;
		break;
	case V4L2_PIX_FMT_RGB565X:
		com7 |= COM7_RGB;
		rv = ov_fmt_rgbr;
		break;
	case V4L2_PIX_FMT_SBGGR8:
		com7 |= COM7_PBAYER;
		rv = ov_fmt_bayer;
		break;
	default:
		pr_err("Unsupported colorspace\n");
		return -EFAULT;
	}
	/*FIXME sometimes the sensor go to a bad state
	stk_sensor_write_regvals(dev, ov_initvals); */
	stk_sensor_outb(dev, REG_COM7, com7);
	msleep(50);
	stk_sensor_write_regvals(dev, rv);
	flip = (dev->vsettings.vflip?MVFP_FLIP:0)
		| (dev->vsettings.hflip?MVFP_MIRROR:0);
	stk_sensor_outb(dev, REG_MVFP, flip);
	if (dev->vsettings.palette == V4L2_PIX_FMT_SBGGR8
			&& !dev->vsettings.vflip)
		stk_sensor_outb(dev, REG_TSLB, 0x08);
	stk_sensor_outb(dev, REG_ADVFH, dummylines >> 8);
	stk_sensor_outb(dev, REG_ADVFL, dummylines & 0xff);
	msleep(50);
	switch (dev->vsettings.mode) {
	case MODE_VGA:
		if (stk_sensor_set_hw(dev, 302, 1582, 6, 486))
			pr_err("stk_sensor_set_hw failed (VGA)\n");
		break;
	case MODE_SXGA:
	case MODE_CIF:
	case MODE_QVGA:
	case MODE_QCIF:
		/*FIXME These settings seem ignored by the sensor
		if (stk_sensor_set_hw(dev, 220, 1500, 10, 1034))
			pr_err("stk_sensor_set_hw failed (SXGA)\n");
		*/
		break;
	}
	msleep(10);
	return 0;
}

int stk_sensor_set_brightness(struct stk_camera *dev, int br)
{
	if (br < 0 || br > 0xff)
		return -EINVAL;
	stk_sensor_outb(dev, REG_AEB, max(0x00, br - 6));
	stk_sensor_outb(dev, REG_AEW, min(0xff, br + 6));
	return 0;
}

