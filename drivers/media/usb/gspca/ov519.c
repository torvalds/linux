/*
 * OV519 driver
 *
 * Copyright (C) 2008-2011 Jean-François Moine <moinejf@free.fr>
 * Copyright (C) 2009 Hans de Goede <hdegoede@redhat.com>
 *
 * This module is adapted from the ov51x-jpeg package, which itself
 * was adapted from the ov511 driver.
 *
 * Original copyright for the ov511 driver is:
 *
 * Copyright (c) 1999-2006 Mark W. McClelland
 * Support for OV519, OV8610 Copyright (c) 2003 Joerg Heckenbach
 * Many improvements by Bret Wallach <bwallac1@san.rr.com>
 * Color fixes by by Orion Sky Lawlor <olawlor@acm.org> (2/26/2000)
 * OV7620 fixes by Charl P. Botha <cpbotha@ieee.org>
 * Changes by Claudio Matsuoka <claudio@conectiva.com>
 *
 * ov51x-jpeg original copyright is:
 *
 * Copyright (c) 2004-2007 Romain Beauxis <toots@rastageeks.org>
 * Support for OV7670 sensors was contributed by Sam Skipsey <aoanla@yahoo.com>
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "ov519"

#include <linux/input.h>
#include "gspca.h"

/* The jpeg_hdr is used by w996Xcf only */
/* The CONEX_CAM define for jpeg.h needs renaming, now its used here too */
#define CONEX_CAM
#include "jpeg.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("OV519 USB Camera Driver");
MODULE_LICENSE("GPL");

/* global parameters */
static int frame_rate;

/* Number of times to retry a failed I2C transaction. Increase this if you
 * are getting "Failed to read sensor ID..." */
static int i2c_detect_tries = 10;

/* ov519 device descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */

	struct v4l2_ctrl *jpegqual;
	struct v4l2_ctrl *freq;
	struct { /* h/vflip control cluster */
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};
	struct { /* autobrightness/brightness control cluster */
		struct v4l2_ctrl *autobright;
		struct v4l2_ctrl *brightness;
	};

	u8 revision;

	u8 packet_nr;

	char bridge;
#define BRIDGE_OV511		0
#define BRIDGE_OV511PLUS	1
#define BRIDGE_OV518		2
#define BRIDGE_OV518PLUS	3
#define BRIDGE_OV519		4		/* = ov530 */
#define BRIDGE_OVFX2		5
#define BRIDGE_W9968CF		6
#define BRIDGE_MASK		7

	char invert_led;
#define BRIDGE_INVERT_LED	8

	char snapshot_pressed;
	char snapshot_needs_reset;

	/* Determined by sensor type */
	u8 sif;

#define QUALITY_MIN 50
#define QUALITY_MAX 70
#define QUALITY_DEF 50

	u8 stopped;		/* Streaming is temporarily paused */
	u8 first_frame;

	u8 frame_rate;		/* current Framerate */
	u8 clockdiv;		/* clockdiv override */

	s8 sensor;		/* Type of image sensor chip (SEN_*) */

	u8 sensor_addr;
	u16 sensor_width;
	u16 sensor_height;
	s16 sensor_reg_cache[256];

	u8 jpeg_hdr[JPEG_HDR_SZ];
};
enum sensors {
	SEN_OV2610,
	SEN_OV2610AE,
	SEN_OV3610,
	SEN_OV6620,
	SEN_OV6630,
	SEN_OV66308AF,
	SEN_OV7610,
	SEN_OV7620,
	SEN_OV7620AE,
	SEN_OV7640,
	SEN_OV7648,
	SEN_OV7660,
	SEN_OV7670,
	SEN_OV76BE,
	SEN_OV8610,
	SEN_OV9600,
};

/* Note this is a bit of a hack, but the w9968cf driver needs the code for all
   the ov sensors which is already present here. When we have the time we
   really should move the sensor drivers to v4l2 sub drivers. */
#include "w996Xcf.c"

/* table of the disabled controls */
struct ctrl_valid {
	unsigned int has_brightness:1;
	unsigned int has_contrast:1;
	unsigned int has_exposure:1;
	unsigned int has_autogain:1;
	unsigned int has_sat:1;
	unsigned int has_hvflip:1;
	unsigned int has_autobright:1;
	unsigned int has_freq:1;
};

static const struct ctrl_valid valid_controls[] = {
	[SEN_OV2610] = {
		.has_exposure = 1,
		.has_autogain = 1,
	},
	[SEN_OV2610AE] = {
		.has_exposure = 1,
		.has_autogain = 1,
	},
	[SEN_OV3610] = {
		/* No controls */
	},
	[SEN_OV6620] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV6630] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV66308AF] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV7610] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV7620] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV7620AE] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV7640] = {
		.has_brightness = 1,
		.has_sat = 1,
		.has_freq = 1,
	},
	[SEN_OV7648] = {
		.has_brightness = 1,
		.has_sat = 1,
		.has_freq = 1,
	},
	[SEN_OV7660] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_hvflip = 1,
		.has_freq = 1,
	},
	[SEN_OV7670] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_hvflip = 1,
		.has_freq = 1,
	},
	[SEN_OV76BE] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
		.has_freq = 1,
	},
	[SEN_OV8610] = {
		.has_brightness = 1,
		.has_contrast = 1,
		.has_sat = 1,
		.has_autobright = 1,
	},
	[SEN_OV9600] = {
		.has_exposure = 1,
		.has_autogain = 1,
	},
};

static const struct v4l2_pix_format ov519_vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};
static const struct v4l2_pix_format ov519_sif_mode[] = {
	{160, 120, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

/* Note some of the sizeimage values for the ov511 / ov518 may seem
   larger then necessary, however they need to be this big as the ov511 /
   ov518 always fills the entire isoc frame, using 0 padding bytes when
   it doesn't have any data. So with low framerates the amount of data
   transferred can become quite large (libv4l will remove all the 0 padding
   in userspace). */
static const struct v4l2_pix_format ov518_vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_OV518, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_OV518, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};
static const struct v4l2_pix_format ov518_sif_mode[] = {
	{160, 120, V4L2_PIX_FMT_OV518, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 70000,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_OV518, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 70000,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{320, 240, V4L2_PIX_FMT_OV518, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_OV518, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

static const struct v4l2_pix_format ov511_vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_OV511, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_OV511, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 2,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};
static const struct v4l2_pix_format ov511_sif_mode[] = {
	{160, 120, V4L2_PIX_FMT_OV511, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 70000,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_OV511, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 70000,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{320, 240, V4L2_PIX_FMT_OV511, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_OV511, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

static const struct v4l2_pix_format ovfx2_ov2610_mode[] = {
	{800, 600, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 800,
		.sizeimage = 800 * 600,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{1600, 1200, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 1600,
		.sizeimage = 1600 * 1200,
		.colorspace = V4L2_COLORSPACE_SRGB},
};
static const struct v4l2_pix_format ovfx2_ov3610_mode[] = {
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{800, 600, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 800,
		.sizeimage = 800 * 600,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{1024, 768, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 1024,
		.sizeimage = 1024 * 768,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{1600, 1200, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 1600,
		.sizeimage = 1600 * 1200,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{2048, 1536, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 2048,
		.sizeimage = 2048 * 1536,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};
static const struct v4l2_pix_format ovfx2_ov9600_mode[] = {
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{1280, 1024, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 1024,
		.colorspace = V4L2_COLORSPACE_SRGB},
};

/* Registers common to OV511 / OV518 */
#define R51x_FIFO_PSIZE			0x30	/* 2 bytes wide w/ OV518(+) */
#define R51x_SYS_RESET			0x50
	/* Reset type flags */
	#define	OV511_RESET_OMNICE	0x08
#define R51x_SYS_INIT			0x53
#define R51x_SYS_SNAP			0x52
#define R51x_SYS_CUST_ID		0x5f
#define R51x_COMP_LUT_BEGIN		0x80

/* OV511 Camera interface register numbers */
#define R511_CAM_DELAY			0x10
#define R511_CAM_EDGE			0x11
#define R511_CAM_PXCNT			0x12
#define R511_CAM_LNCNT			0x13
#define R511_CAM_PXDIV			0x14
#define R511_CAM_LNDIV			0x15
#define R511_CAM_UV_EN			0x16
#define R511_CAM_LINE_MODE		0x17
#define R511_CAM_OPTS			0x18

#define R511_SNAP_FRAME			0x19
#define R511_SNAP_PXCNT			0x1a
#define R511_SNAP_LNCNT			0x1b
#define R511_SNAP_PXDIV			0x1c
#define R511_SNAP_LNDIV			0x1d
#define R511_SNAP_UV_EN			0x1e
#define R511_SNAP_OPTS			0x1f

#define R511_DRAM_FLOW_CTL		0x20
#define R511_FIFO_OPTS			0x31
#define R511_I2C_CTL			0x40
#define R511_SYS_LED_CTL		0x55	/* OV511+ only */
#define R511_COMP_EN			0x78
#define R511_COMP_LUT_EN		0x79

/* OV518 Camera interface register numbers */
#define R518_GPIO_OUT			0x56	/* OV518(+) only */
#define R518_GPIO_CTL			0x57	/* OV518(+) only */

/* OV519 Camera interface register numbers */
#define OV519_R10_H_SIZE		0x10
#define OV519_R11_V_SIZE		0x11
#define OV519_R12_X_OFFSETL		0x12
#define OV519_R13_X_OFFSETH		0x13
#define OV519_R14_Y_OFFSETL		0x14
#define OV519_R15_Y_OFFSETH		0x15
#define OV519_R16_DIVIDER		0x16
#define OV519_R20_DFR			0x20
#define OV519_R25_FORMAT		0x25

/* OV519 System Controller register numbers */
#define OV519_R51_RESET1		0x51
#define OV519_R54_EN_CLK1		0x54
#define OV519_R57_SNAPSHOT		0x57

#define OV519_GPIO_DATA_OUT0		0x71
#define OV519_GPIO_IO_CTRL0		0x72

/*#define OV511_ENDPOINT_ADDRESS 1	 * Isoc endpoint number */

/*
 * The FX2 chip does not give us a zero length read at end of frame.
 * It does, however, give a short read at the end of a frame, if
 * necessary, rather than run two frames together.
 *
 * By choosing the right bulk transfer size, we are guaranteed to always
 * get a short read for the last read of each frame.  Frame sizes are
 * always a composite number (width * height, or a multiple) so if we
 * choose a prime number, we are guaranteed that the last read of a
 * frame will be short.
 *
 * But it isn't that easy: the 2.6 kernel requires a multiple of 4KB,
 * otherwise EOVERFLOW "babbling" errors occur.  I have not been able
 * to figure out why.  [PMiller]
 *
 * The constant (13 * 4096) is the largest "prime enough" number less than 64KB.
 *
 * It isn't enough to know the number of bytes per frame, in case we
 * have data dropouts or buffer overruns (even though the FX2 double
 * buffers, there are some pretty strict real time constraints for
 * isochronous transfer for larger frame sizes).
 */
/*jfm: this value does not work for 800x600 - see isoc_init */
#define OVFX2_BULK_SIZE (13 * 4096)

/* I2C registers */
#define R51x_I2C_W_SID		0x41
#define R51x_I2C_SADDR_3	0x42
#define R51x_I2C_SADDR_2	0x43
#define R51x_I2C_R_SID		0x44
#define R51x_I2C_DATA		0x45
#define R518_I2C_CTL		0x47	/* OV518(+) only */
#define OVFX2_I2C_ADDR		0x00

/* I2C ADDRESSES */
#define OV7xx0_SID   0x42
#define OV_HIRES_SID 0x60		/* OV9xxx / OV2xxx / OV3xxx */
#define OV8xx0_SID   0xa0
#define OV6xx0_SID   0xc0

/* OV7610 registers */
#define OV7610_REG_GAIN		0x00	/* gain setting (5:0) */
#define OV7610_REG_BLUE		0x01	/* blue channel balance */
#define OV7610_REG_RED		0x02	/* red channel balance */
#define OV7610_REG_SAT		0x03	/* saturation */
#define OV8610_REG_HUE		0x04	/* 04 reserved */
#define OV7610_REG_CNT		0x05	/* Y contrast */
#define OV7610_REG_BRT		0x06	/* Y brightness */
#define OV7610_REG_COM_C	0x14	/* misc common regs */
#define OV7610_REG_ID_HIGH	0x1c	/* manufacturer ID MSB */
#define OV7610_REG_ID_LOW	0x1d	/* manufacturer ID LSB */
#define OV7610_REG_COM_I	0x29	/* misc settings */

/* OV7660 and OV7670 registers */
#define OV7670_R00_GAIN		0x00	/* Gain lower 8 bits (rest in vref) */
#define OV7670_R01_BLUE		0x01	/* blue gain */
#define OV7670_R02_RED		0x02	/* red gain */
#define OV7670_R03_VREF		0x03	/* Pieces of GAIN, VSTART, VSTOP */
#define OV7670_R04_COM1		0x04	/* Control 1 */
/*#define OV7670_R07_AECHH	0x07	 * AEC MS 5 bits */
#define OV7670_R0C_COM3		0x0c	/* Control 3 */
#define OV7670_R0D_COM4		0x0d	/* Control 4 */
#define OV7670_R0E_COM5		0x0e	/* All "reserved" */
#define OV7670_R0F_COM6		0x0f	/* Control 6 */
#define OV7670_R10_AECH		0x10	/* More bits of AEC value */
#define OV7670_R11_CLKRC	0x11	/* Clock control */
#define OV7670_R12_COM7		0x12	/* Control 7 */
#define   OV7670_COM7_FMT_VGA	 0x00
/*#define   OV7670_COM7_YUV	 0x00	 * YUV */
#define   OV7670_COM7_FMT_QVGA	 0x10	/* QVGA format */
#define   OV7670_COM7_FMT_MASK	 0x38
#define   OV7670_COM7_RESET	 0x80	/* Register reset */
#define OV7670_R13_COM8		0x13	/* Control 8 */
#define   OV7670_COM8_AEC	 0x01	/* Auto exposure enable */
#define   OV7670_COM8_AWB	 0x02	/* White balance enable */
#define   OV7670_COM8_AGC	 0x04	/* Auto gain enable */
#define   OV7670_COM8_BFILT	 0x20	/* Band filter enable */
#define   OV7670_COM8_AECSTEP	 0x40	/* Unlimited AEC step size */
#define   OV7670_COM8_FASTAEC	 0x80	/* Enable fast AGC/AEC */
#define OV7670_R14_COM9		0x14	/* Control 9 - gain ceiling */
#define OV7670_R15_COM10	0x15	/* Control 10 */
#define OV7670_R17_HSTART	0x17	/* Horiz start high bits */
#define OV7670_R18_HSTOP	0x18	/* Horiz stop high bits */
#define OV7670_R19_VSTART	0x19	/* Vert start high bits */
#define OV7670_R1A_VSTOP	0x1a	/* Vert stop high bits */
#define OV7670_R1E_MVFP		0x1e	/* Mirror / vflip */
#define   OV7670_MVFP_VFLIP	 0x10	/* vertical flip */
#define   OV7670_MVFP_MIRROR	 0x20	/* Mirror image */
#define OV7670_R24_AEW		0x24	/* AGC upper limit */
#define OV7670_R25_AEB		0x25	/* AGC lower limit */
#define OV7670_R26_VPT		0x26	/* AGC/AEC fast mode op region */
#define OV7670_R32_HREF		0x32	/* HREF pieces */
#define OV7670_R3A_TSLB		0x3a	/* lots of stuff */
#define OV7670_R3B_COM11	0x3b	/* Control 11 */
#define   OV7670_COM11_EXP	 0x02
#define   OV7670_COM11_HZAUTO	 0x10	/* Auto detect 50/60 Hz */
#define OV7670_R3C_COM12	0x3c	/* Control 12 */
#define OV7670_R3D_COM13	0x3d	/* Control 13 */
#define   OV7670_COM13_GAMMA	 0x80	/* Gamma enable */
#define   OV7670_COM13_UVSAT	 0x40	/* UV saturation auto adjustment */
#define OV7670_R3E_COM14	0x3e	/* Control 14 */
#define OV7670_R3F_EDGE		0x3f	/* Edge enhancement factor */
#define OV7670_R40_COM15	0x40	/* Control 15 */
/*#define   OV7670_COM15_R00FF	 0xc0	 *	00 to FF */
#define OV7670_R41_COM16	0x41	/* Control 16 */
#define   OV7670_COM16_AWBGAIN	 0x08	/* AWB gain enable */
/* end of ov7660 common registers */
#define OV7670_R55_BRIGHT	0x55	/* Brightness */
#define OV7670_R56_CONTRAS	0x56	/* Contrast control */
#define OV7670_R69_GFIX		0x69	/* Fix gain control */
/*#define OV7670_R8C_RGB444	0x8c	 * RGB 444 control */
#define OV7670_R9F_HAECC1	0x9f	/* Hist AEC/AGC control 1 */
#define OV7670_RA0_HAECC2	0xa0	/* Hist AEC/AGC control 2 */
#define OV7670_RA5_BD50MAX	0xa5	/* 50hz banding step limit */
#define OV7670_RA6_HAECC3	0xa6	/* Hist AEC/AGC control 3 */
#define OV7670_RA7_HAECC4	0xa7	/* Hist AEC/AGC control 4 */
#define OV7670_RA8_HAECC5	0xa8	/* Hist AEC/AGC control 5 */
#define OV7670_RA9_HAECC6	0xa9	/* Hist AEC/AGC control 6 */
#define OV7670_RAA_HAECC7	0xaa	/* Hist AEC/AGC control 7 */
#define OV7670_RAB_BD60MAX	0xab	/* 60hz banding step limit */

struct ov_regvals {
	u8 reg;
	u8 val;
};
struct ov_i2c_regvals {
	u8 reg;
	u8 val;
};

/* Settings for OV2610 camera chip */
static const struct ov_i2c_regvals norm_2610[] = {
	{ 0x12, 0x80 },	/* reset */
};

static const struct ov_i2c_regvals norm_2610ae[] = {
	{0x12, 0x80},	/* reset */
	{0x13, 0xcd},
	{0x09, 0x01},
	{0x0d, 0x00},
	{0x11, 0x80},
	{0x12, 0x20},	/* 1600x1200 */
	{0x33, 0x0c},
	{0x35, 0x90},
	{0x36, 0x37},
/* ms-win traces */
	{0x11, 0x83},	/* clock / 3 ? */
	{0x2d, 0x00},	/* 60 Hz filter */
	{0x24, 0xb0},	/* normal colors */
	{0x25, 0x90},
	{0x10, 0x43},
};

static const struct ov_i2c_regvals norm_3620b[] = {
	/*
	 * From the datasheet: "Note that after writing to register COMH
	 * (0x12) to change the sensor mode, registers related to the
	 * sensor’s cropping window will be reset back to their default
	 * values."
	 *
	 * "wait 4096 external clock ... to make sure the sensor is
	 * stable and ready to access registers" i.e. 160us at 24MHz
	 */
	{ 0x12, 0x80 }, /* COMH reset */
	{ 0x12, 0x00 }, /* QXGA, master */

	/*
	 * 11 CLKRC "Clock Rate Control"
	 * [7] internal frequency doublers: on
	 * [6] video port mode: master
	 * [5:0] clock divider: 1
	 */
	{ 0x11, 0x80 },

	/*
	 * 13 COMI "Common Control I"
	 *                  = 192 (0xC0) 11000000
	 *    COMI[7] "AEC speed selection"
	 *                  =   1 (0x01) 1....... "Faster AEC correction"
	 *    COMI[6] "AEC speed step selection"
	 *                  =   1 (0x01) .1...... "Big steps, fast"
	 *    COMI[5] "Banding filter on off"
	 *                  =   0 (0x00) ..0..... "Off"
	 *    COMI[4] "Banding filter option"
	 *                  =   0 (0x00) ...0.... "Main clock is 48 MHz and
	 *                                         the PLL is ON"
	 *    COMI[3] "Reserved"
	 *                  =   0 (0x00) ....0...
	 *    COMI[2] "AGC auto manual control selection"
	 *                  =   0 (0x00) .....0.. "Manual"
	 *    COMI[1] "AWB auto manual control selection"
	 *                  =   0 (0x00) ......0. "Manual"
	 *    COMI[0] "Exposure control"
	 *                  =   0 (0x00) .......0 "Manual"
	 */
	{ 0x13, 0xc0 },

	/*
	 * 09 COMC "Common Control C"
	 *                  =   8 (0x08) 00001000
	 *    COMC[7:5] "Reserved"
	 *                  =   0 (0x00) 000.....
	 *    COMC[4] "Sleep Mode Enable"
	 *                  =   0 (0x00) ...0.... "Normal mode"
	 *    COMC[3:2] "Sensor sampling reset timing selection"
	 *                  =   2 (0x02) ....10.. "Longer reset time"
	 *    COMC[1:0] "Output drive current select"
	 *                  =   0 (0x00) ......00 "Weakest"
	 */
	{ 0x09, 0x08 },

	/*
	 * 0C COMD "Common Control D"
	 *                  =   8 (0x08) 00001000
	 *    COMD[7] "Reserved"
	 *                  =   0 (0x00) 0.......
	 *    COMD[6] "Swap MSB and LSB at the output port"
	 *                  =   0 (0x00) .0...... "False"
	 *    COMD[5:3] "Reserved"
	 *                  =   1 (0x01) ..001...
	 *    COMD[2] "Output Average On Off"
	 *                  =   0 (0x00) .....0.. "Output Normal"
	 *    COMD[1] "Sensor precharge voltage selection"
	 *                  =   0 (0x00) ......0. "Selects internal
	 *                                         reference precharge
	 *                                         voltage"
	 *    COMD[0] "Snapshot option"
	 *                  =   0 (0x00) .......0 "Enable live video output
	 *                                         after snapshot sequence"
	 */
	{ 0x0c, 0x08 },

	/*
	 * 0D COME "Common Control E"
	 *                  = 161 (0xA1) 10100001
	 *    COME[7] "Output average option"
	 *                  =   1 (0x01) 1....... "Output average of 4 pixels"
	 *    COME[6] "Anti-blooming control"
	 *                  =   0 (0x00) .0...... "Off"
	 *    COME[5:3] "Reserved"
	 *                  =   4 (0x04) ..100...
	 *    COME[2] "Clock output power down pin status"
	 *                  =   0 (0x00) .....0.. "Tri-state data output pin
	 *                                         on power down"
	 *    COME[1] "Data output pin status selection at power down"
	 *                  =   0 (0x00) ......0. "Tri-state VSYNC, PCLK,
	 *                                         HREF, and CHSYNC pins on
	 *                                         power down"
	 *    COME[0] "Auto zero circuit select"
	 *                  =   1 (0x01) .......1 "On"
	 */
	{ 0x0d, 0xa1 },

	/*
	 * 0E COMF "Common Control F"
	 *                  = 112 (0x70) 01110000
	 *    COMF[7] "System clock selection"
	 *                  =   0 (0x00) 0....... "Use 24 MHz system clock"
	 *    COMF[6:4] "Reserved"
	 *                  =   7 (0x07) .111....
	 *    COMF[3] "Manual auto negative offset canceling selection"
	 *                  =   0 (0x00) ....0... "Auto detect negative
	 *                                         offset and cancel it"
	 *    COMF[2:0] "Reserved"
	 *                  =   0 (0x00) .....000
	 */
	{ 0x0e, 0x70 },

	/*
	 * 0F COMG "Common Control G"
	 *                  =  66 (0x42) 01000010
	 *    COMG[7] "Optical black output selection"
	 *                  =   0 (0x00) 0....... "Disable"
	 *    COMG[6] "Black level calibrate selection"
	 *                  =   1 (0x01) .1...... "Use optical black pixels
	 *                                         to calibrate"
	 *    COMG[5:4] "Reserved"
	 *                  =   0 (0x00) ..00....
	 *    COMG[3] "Channel offset adjustment"
	 *                  =   0 (0x00) ....0... "Disable offset adjustment"
	 *    COMG[2] "ADC black level calibration option"
	 *                  =   0 (0x00) .....0.. "Use B/G line and G/R
	 *                                         line to calibrate each
	 *                                         channel's black level"
	 *    COMG[1] "Reserved"
	 *                  =   1 (0x01) ......1.
	 *    COMG[0] "ADC black level calibration enable"
	 *                  =   0 (0x00) .......0 "Disable"
	 */
	{ 0x0f, 0x42 },

	/*
	 * 14 COMJ "Common Control J"
	 *                  = 198 (0xC6) 11000110
	 *    COMJ[7:6] "AGC gain ceiling"
	 *                  =   3 (0x03) 11...... "8x"
	 *    COMJ[5:4] "Reserved"
	 *                  =   0 (0x00) ..00....
	 *    COMJ[3] "Auto banding filter"
	 *                  =   0 (0x00) ....0... "Banding filter is always
	 *                                         on off depending on
	 *                                         COMI[5] setting"
	 *    COMJ[2] "VSYNC drop option"
	 *                  =   1 (0x01) .....1.. "SYNC is dropped if frame
	 *                                         data is dropped"
	 *    COMJ[1] "Frame data drop"
	 *                  =   1 (0x01) ......1. "Drop frame data if
	 *                                         exposure is not within
	 *                                         tolerance.  In AEC mode,
	 *                                         data is normally dropped
	 *                                         when data is out of
	 *                                         range."
	 *    COMJ[0] "Reserved"
	 *                  =   0 (0x00) .......0
	 */
	{ 0x14, 0xc6 },

	/*
	 * 15 COMK "Common Control K"
	 *                  =   2 (0x02) 00000010
	 *    COMK[7] "CHSYNC pin output swap"
	 *                  =   0 (0x00) 0....... "CHSYNC"
	 *    COMK[6] "HREF pin output swap"
	 *                  =   0 (0x00) .0...... "HREF"
	 *    COMK[5] "PCLK output selection"
	 *                  =   0 (0x00) ..0..... "PCLK always output"
	 *    COMK[4] "PCLK edge selection"
	 *                  =   0 (0x00) ...0.... "Data valid on falling edge"
	 *    COMK[3] "HREF output polarity"
	 *                  =   0 (0x00) ....0... "positive"
	 *    COMK[2] "Reserved"
	 *                  =   0 (0x00) .....0..
	 *    COMK[1] "VSYNC polarity"
	 *                  =   1 (0x01) ......1. "negative"
	 *    COMK[0] "HSYNC polarity"
	 *                  =   0 (0x00) .......0 "positive"
	 */
	{ 0x15, 0x02 },

	/*
	 * 33 CHLF "Current Control"
	 *                  =   9 (0x09) 00001001
	 *    CHLF[7:6] "Sensor current control"
	 *                  =   0 (0x00) 00......
	 *    CHLF[5] "Sensor current range control"
	 *                  =   0 (0x00) ..0..... "normal range"
	 *    CHLF[4] "Sensor current"
	 *                  =   0 (0x00) ...0.... "normal current"
	 *    CHLF[3] "Sensor buffer current control"
	 *                  =   1 (0x01) ....1... "half current"
	 *    CHLF[2] "Column buffer current control"
	 *                  =   0 (0x00) .....0.. "normal current"
	 *    CHLF[1] "Analog DSP current control"
	 *                  =   0 (0x00) ......0. "normal current"
	 *    CHLF[1] "ADC current control"
	 *                  =   0 (0x00) ......0. "normal current"
	 */
	{ 0x33, 0x09 },

	/*
	 * 34 VBLM "Blooming Control"
	 *                  =  80 (0x50) 01010000
	 *    VBLM[7] "Hard soft reset switch"
	 *                  =   0 (0x00) 0....... "Hard reset"
	 *    VBLM[6:4] "Blooming voltage selection"
	 *                  =   5 (0x05) .101....
	 *    VBLM[3:0] "Sensor current control"
	 *                  =   0 (0x00) ....0000
	 */
	{ 0x34, 0x50 },

	/*
	 * 36 VCHG "Sensor Precharge Voltage Control"
	 *                  =   0 (0x00) 00000000
	 *    VCHG[7] "Reserved"
	 *                  =   0 (0x00) 0.......
	 *    VCHG[6:4] "Sensor precharge voltage control"
	 *                  =   0 (0x00) .000....
	 *    VCHG[3:0] "Sensor array common reference"
	 *                  =   0 (0x00) ....0000
	 */
	{ 0x36, 0x00 },

	/*
	 * 37 ADC "ADC Reference Control"
	 *                  =   4 (0x04) 00000100
	 *    ADC[7:4] "Reserved"
	 *                  =   0 (0x00) 0000....
	 *    ADC[3] "ADC input signal range"
	 *                  =   0 (0x00) ....0... "Input signal 1.0x"
	 *    ADC[2:0] "ADC range control"
	 *                  =   4 (0x04) .....100
	 */
	{ 0x37, 0x04 },

	/*
	 * 38 ACOM "Analog Common Ground"
	 *                  =  82 (0x52) 01010010
	 *    ACOM[7] "Analog gain control"
	 *                  =   0 (0x00) 0....... "Gain 1x"
	 *    ACOM[6] "Analog black level calibration"
	 *                  =   1 (0x01) .1...... "On"
	 *    ACOM[5:0] "Reserved"
	 *                  =  18 (0x12) ..010010
	 */
	{ 0x38, 0x52 },

	/*
	 * 3A FREFA "Internal Reference Adjustment"
	 *                  =   0 (0x00) 00000000
	 *    FREFA[7:0] "Range"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x3a, 0x00 },

	/*
	 * 3C FVOPT "Internal Reference Adjustment"
	 *                  =  31 (0x1F) 00011111
	 *    FVOPT[7:0] "Range"
	 *                  =  31 (0x1F) 00011111
	 */
	{ 0x3c, 0x1f },

	/*
	 * 44 Undocumented  =   0 (0x00) 00000000
	 *    44[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x44, 0x00 },

	/*
	 * 40 Undocumented  =   0 (0x00) 00000000
	 *    40[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x40, 0x00 },

	/*
	 * 41 Undocumented  =   0 (0x00) 00000000
	 *    41[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x41, 0x00 },

	/*
	 * 42 Undocumented  =   0 (0x00) 00000000
	 *    42[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x42, 0x00 },

	/*
	 * 43 Undocumented  =   0 (0x00) 00000000
	 *    43[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x43, 0x00 },

	/*
	 * 45 Undocumented  = 128 (0x80) 10000000
	 *    45[7:0] "It's a secret"
	 *                  = 128 (0x80) 10000000
	 */
	{ 0x45, 0x80 },

	/*
	 * 48 Undocumented  = 192 (0xC0) 11000000
	 *    48[7:0] "It's a secret"
	 *                  = 192 (0xC0) 11000000
	 */
	{ 0x48, 0xc0 },

	/*
	 * 49 Undocumented  =  25 (0x19) 00011001
	 *    49[7:0] "It's a secret"
	 *                  =  25 (0x19) 00011001
	 */
	{ 0x49, 0x19 },

	/*
	 * 4B Undocumented  = 128 (0x80) 10000000
	 *    4B[7:0] "It's a secret"
	 *                  = 128 (0x80) 10000000
	 */
	{ 0x4b, 0x80 },

	/*
	 * 4D Undocumented  = 196 (0xC4) 11000100
	 *    4D[7:0] "It's a secret"
	 *                  = 196 (0xC4) 11000100
	 */
	{ 0x4d, 0xc4 },

	/*
	 * 35 VREF "Reference Voltage Control"
	 *                  =  76 (0x4c) 01001100
	 *    VREF[7:5] "Column high reference control"
	 *                  =   2 (0x02) 010..... "higher voltage"
	 *    VREF[4:2] "Column low reference control"
	 *                  =   3 (0x03) ...011.. "Highest voltage"
	 *    VREF[1:0] "Reserved"
	 *                  =   0 (0x00) ......00
	 */
	{ 0x35, 0x4c },

	/*
	 * 3D Undocumented  =   0 (0x00) 00000000
	 *    3D[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x3d, 0x00 },

	/*
	 * 3E Undocumented  =   0 (0x00) 00000000
	 *    3E[7:0] "It's a secret"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x3e, 0x00 },

	/*
	 * 3B FREFB "Internal Reference Adjustment"
	 *                  =  24 (0x18) 00011000
	 *    FREFB[7:0] "Range"
	 *                  =  24 (0x18) 00011000
	 */
	{ 0x3b, 0x18 },

	/*
	 * 33 CHLF "Current Control"
	 *                  =  25 (0x19) 00011001
	 *    CHLF[7:6] "Sensor current control"
	 *                  =   0 (0x00) 00......
	 *    CHLF[5] "Sensor current range control"
	 *                  =   0 (0x00) ..0..... "normal range"
	 *    CHLF[4] "Sensor current"
	 *                  =   1 (0x01) ...1.... "double current"
	 *    CHLF[3] "Sensor buffer current control"
	 *                  =   1 (0x01) ....1... "half current"
	 *    CHLF[2] "Column buffer current control"
	 *                  =   0 (0x00) .....0.. "normal current"
	 *    CHLF[1] "Analog DSP current control"
	 *                  =   0 (0x00) ......0. "normal current"
	 *    CHLF[1] "ADC current control"
	 *                  =   0 (0x00) ......0. "normal current"
	 */
	{ 0x33, 0x19 },

	/*
	 * 34 VBLM "Blooming Control"
	 *                  =  90 (0x5A) 01011010
	 *    VBLM[7] "Hard soft reset switch"
	 *                  =   0 (0x00) 0....... "Hard reset"
	 *    VBLM[6:4] "Blooming voltage selection"
	 *                  =   5 (0x05) .101....
	 *    VBLM[3:0] "Sensor current control"
	 *                  =  10 (0x0A) ....1010
	 */
	{ 0x34, 0x5a },

	/*
	 * 3B FREFB "Internal Reference Adjustment"
	 *                  =   0 (0x00) 00000000
	 *    FREFB[7:0] "Range"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x3b, 0x00 },

	/*
	 * 33 CHLF "Current Control"
	 *                  =   9 (0x09) 00001001
	 *    CHLF[7:6] "Sensor current control"
	 *                  =   0 (0x00) 00......
	 *    CHLF[5] "Sensor current range control"
	 *                  =   0 (0x00) ..0..... "normal range"
	 *    CHLF[4] "Sensor current"
	 *                  =   0 (0x00) ...0.... "normal current"
	 *    CHLF[3] "Sensor buffer current control"
	 *                  =   1 (0x01) ....1... "half current"
	 *    CHLF[2] "Column buffer current control"
	 *                  =   0 (0x00) .....0.. "normal current"
	 *    CHLF[1] "Analog DSP current control"
	 *                  =   0 (0x00) ......0. "normal current"
	 *    CHLF[1] "ADC current control"
	 *                  =   0 (0x00) ......0. "normal current"
	 */
	{ 0x33, 0x09 },

	/*
	 * 34 VBLM "Blooming Control"
	 *                  =  80 (0x50) 01010000
	 *    VBLM[7] "Hard soft reset switch"
	 *                  =   0 (0x00) 0....... "Hard reset"
	 *    VBLM[6:4] "Blooming voltage selection"
	 *                  =   5 (0x05) .101....
	 *    VBLM[3:0] "Sensor current control"
	 *                  =   0 (0x00) ....0000
	 */
	{ 0x34, 0x50 },

	/*
	 * 12 COMH "Common Control H"
	 *                  =  64 (0x40) 01000000
	 *    COMH[7] "SRST"
	 *                  =   0 (0x00) 0....... "No-op"
	 *    COMH[6:4] "Resolution selection"
	 *                  =   4 (0x04) .100.... "XGA"
	 *    COMH[3] "Master slave selection"
	 *                  =   0 (0x00) ....0... "Master mode"
	 *    COMH[2] "Internal B/R channel option"
	 *                  =   0 (0x00) .....0.. "B/R use same channel"
	 *    COMH[1] "Color bar test pattern"
	 *                  =   0 (0x00) ......0. "Off"
	 *    COMH[0] "Reserved"
	 *                  =   0 (0x00) .......0
	 */
	{ 0x12, 0x40 },

	/*
	 * 17 HREFST "Horizontal window start"
	 *                  =  31 (0x1F) 00011111
	 *    HREFST[7:0] "Horizontal window start, 8 MSBs"
	 *                  =  31 (0x1F) 00011111
	 */
	{ 0x17, 0x1f },

	/*
	 * 18 HREFEND "Horizontal window end"
	 *                  =  95 (0x5F) 01011111
	 *    HREFEND[7:0] "Horizontal Window End, 8 MSBs"
	 *                  =  95 (0x5F) 01011111
	 */
	{ 0x18, 0x5f },

	/*
	 * 19 VSTRT "Vertical window start"
	 *                  =   0 (0x00) 00000000
	 *    VSTRT[7:0] "Vertical Window Start, 8 MSBs"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x19, 0x00 },

	/*
	 * 1A VEND "Vertical window end"
	 *                  =  96 (0x60) 01100000
	 *    VEND[7:0] "Vertical Window End, 8 MSBs"
	 *                  =  96 (0x60) 01100000
	 */
	{ 0x1a, 0x60 },

	/*
	 * 32 COMM "Common Control M"
	 *                  =  18 (0x12) 00010010
	 *    COMM[7:6] "Pixel clock divide option"
	 *                  =   0 (0x00) 00...... "/1"
	 *    COMM[5:3] "Horizontal window end position, 3 LSBs"
	 *                  =   2 (0x02) ..010...
	 *    COMM[2:0] "Horizontal window start position, 3 LSBs"
	 *                  =   2 (0x02) .....010
	 */
	{ 0x32, 0x12 },

	/*
	 * 03 COMA "Common Control A"
	 *                  =  74 (0x4A) 01001010
	 *    COMA[7:4] "AWB Update Threshold"
	 *                  =   4 (0x04) 0100....
	 *    COMA[3:2] "Vertical window end line control 2 LSBs"
	 *                  =   2 (0x02) ....10..
	 *    COMA[1:0] "Vertical window start line control 2 LSBs"
	 *                  =   2 (0x02) ......10
	 */
	{ 0x03, 0x4a },

	/*
	 * 11 CLKRC "Clock Rate Control"
	 *                  = 128 (0x80) 10000000
	 *    CLKRC[7] "Internal frequency doublers on off seclection"
	 *                  =   1 (0x01) 1....... "On"
	 *    CLKRC[6] "Digital video master slave selection"
	 *                  =   0 (0x00) .0...... "Master mode, sensor
	 *                                         provides PCLK"
	 *    CLKRC[5:0] "Clock divider { CLK = PCLK/(1+CLKRC[5:0]) }"
	 *                  =   0 (0x00) ..000000
	 */
	{ 0x11, 0x80 },

	/*
	 * 12 COMH "Common Control H"
	 *                  =   0 (0x00) 00000000
	 *    COMH[7] "SRST"
	 *                  =   0 (0x00) 0....... "No-op"
	 *    COMH[6:4] "Resolution selection"
	 *                  =   0 (0x00) .000.... "QXGA"
	 *    COMH[3] "Master slave selection"
	 *                  =   0 (0x00) ....0... "Master mode"
	 *    COMH[2] "Internal B/R channel option"
	 *                  =   0 (0x00) .....0.. "B/R use same channel"
	 *    COMH[1] "Color bar test pattern"
	 *                  =   0 (0x00) ......0. "Off"
	 *    COMH[0] "Reserved"
	 *                  =   0 (0x00) .......0
	 */
	{ 0x12, 0x00 },

	/*
	 * 12 COMH "Common Control H"
	 *                  =  64 (0x40) 01000000
	 *    COMH[7] "SRST"
	 *                  =   0 (0x00) 0....... "No-op"
	 *    COMH[6:4] "Resolution selection"
	 *                  =   4 (0x04) .100.... "XGA"
	 *    COMH[3] "Master slave selection"
	 *                  =   0 (0x00) ....0... "Master mode"
	 *    COMH[2] "Internal B/R channel option"
	 *                  =   0 (0x00) .....0.. "B/R use same channel"
	 *    COMH[1] "Color bar test pattern"
	 *                  =   0 (0x00) ......0. "Off"
	 *    COMH[0] "Reserved"
	 *                  =   0 (0x00) .......0
	 */
	{ 0x12, 0x40 },

	/*
	 * 17 HREFST "Horizontal window start"
	 *                  =  31 (0x1F) 00011111
	 *    HREFST[7:0] "Horizontal window start, 8 MSBs"
	 *                  =  31 (0x1F) 00011111
	 */
	{ 0x17, 0x1f },

	/*
	 * 18 HREFEND "Horizontal window end"
	 *                  =  95 (0x5F) 01011111
	 *    HREFEND[7:0] "Horizontal Window End, 8 MSBs"
	 *                  =  95 (0x5F) 01011111
	 */
	{ 0x18, 0x5f },

	/*
	 * 19 VSTRT "Vertical window start"
	 *                  =   0 (0x00) 00000000
	 *    VSTRT[7:0] "Vertical Window Start, 8 MSBs"
	 *                  =   0 (0x00) 00000000
	 */
	{ 0x19, 0x00 },

	/*
	 * 1A VEND "Vertical window end"
	 *                  =  96 (0x60) 01100000
	 *    VEND[7:0] "Vertical Window End, 8 MSBs"
	 *                  =  96 (0x60) 01100000
	 */
	{ 0x1a, 0x60 },

	/*
	 * 32 COMM "Common Control M"
	 *                  =  18 (0x12) 00010010
	 *    COMM[7:6] "Pixel clock divide option"
	 *                  =   0 (0x00) 00...... "/1"
	 *    COMM[5:3] "Horizontal window end position, 3 LSBs"
	 *                  =   2 (0x02) ..010...
	 *    COMM[2:0] "Horizontal window start position, 3 LSBs"
	 *                  =   2 (0x02) .....010
	 */
	{ 0x32, 0x12 },

	/*
	 * 03 COMA "Common Control A"
	 *                  =  74 (0x4A) 01001010
	 *    COMA[7:4] "AWB Update Threshold"
	 *                  =   4 (0x04) 0100....
	 *    COMA[3:2] "Vertical window end line control 2 LSBs"
	 *                  =   2 (0x02) ....10..
	 *    COMA[1:0] "Vertical window start line control 2 LSBs"
	 *                  =   2 (0x02) ......10
	 */
	{ 0x03, 0x4a },

	/*
	 * 02 RED "Red Gain Control"
	 *                  = 175 (0xAF) 10101111
	 *    RED[7] "Action"
	 *                  =   1 (0x01) 1....... "gain = 1/(1+bitrev([6:0]))"
	 *    RED[6:0] "Value"
	 *                  =  47 (0x2F) .0101111
	 */
	{ 0x02, 0xaf },

	/*
	 * 2D ADDVSL "VSYNC Pulse Width"
	 *                  = 210 (0xD2) 11010010
	 *    ADDVSL[7:0] "VSYNC pulse width, LSB"
	 *                  = 210 (0xD2) 11010010
	 */
	{ 0x2d, 0xd2 },

	/*
	 * 00 GAIN          =  24 (0x18) 00011000
	 *    GAIN[7:6] "Reserved"
	 *                  =   0 (0x00) 00......
	 *    GAIN[5] "Double"
	 *                  =   0 (0x00) ..0..... "False"
	 *    GAIN[4] "Double"
	 *                  =   1 (0x01) ...1.... "True"
	 *    GAIN[3:0] "Range"
	 *                  =   8 (0x08) ....1000
	 */
	{ 0x00, 0x18 },

	/*
	 * 01 BLUE "Blue Gain Control"
	 *                  = 240 (0xF0) 11110000
	 *    BLUE[7] "Action"
	 *                  =   1 (0x01) 1....... "gain = 1/(1+bitrev([6:0]))"
	 *    BLUE[6:0] "Value"
	 *                  = 112 (0x70) .1110000
	 */
	{ 0x01, 0xf0 },

	/*
	 * 10 AEC "Automatic Exposure Control"
	 *                  =  10 (0x0A) 00001010
	 *    AEC[7:0] "Automatic Exposure Control, 8 MSBs"
	 *                  =  10 (0x0A) 00001010
	 */
	{ 0x10, 0x0a },

	{ 0xe1, 0x67 },
	{ 0xe3, 0x03 },
	{ 0xe4, 0x26 },
	{ 0xe5, 0x3e },
	{ 0xf8, 0x01 },
	{ 0xff, 0x01 },
};

static const struct ov_i2c_regvals norm_6x20[] = {
	{ 0x12, 0x80 }, /* reset */
	{ 0x11, 0x01 },
	{ 0x03, 0x60 },
	{ 0x05, 0x7f }, /* For when autoadjust is off */
	{ 0x07, 0xa8 },
	/* The ratio of 0x0c and 0x0d controls the white point */
	{ 0x0c, 0x24 },
	{ 0x0d, 0x24 },
	{ 0x0f, 0x15 }, /* COMS */
	{ 0x10, 0x75 }, /* AEC Exposure time */
	{ 0x12, 0x24 }, /* Enable AGC */
	{ 0x14, 0x04 },
	/* 0x16: 0x06 helps frame stability with moving objects */
	{ 0x16, 0x06 },
/*	{ 0x20, 0x30 },  * Aperture correction enable */
	{ 0x26, 0xb2 }, /* BLC enable */
	/* 0x28: 0x05 Selects RGB format if RGB on */
	{ 0x28, 0x05 },
	{ 0x2a, 0x04 }, /* Disable framerate adjust */
/*	{ 0x2b, 0xac },  * Framerate; Set 2a[7] first */
	{ 0x2d, 0x85 },
	{ 0x33, 0xa0 }, /* Color Processing Parameter */
	{ 0x34, 0xd2 }, /* Max A/D range */
	{ 0x38, 0x8b },
	{ 0x39, 0x40 },

	{ 0x3c, 0x39 }, /* Enable AEC mode changing */
	{ 0x3c, 0x3c }, /* Change AEC mode */
	{ 0x3c, 0x24 }, /* Disable AEC mode changing */

	{ 0x3d, 0x80 },
	/* These next two registers (0x4a, 0x4b) are undocumented.
	 * They control the color balance */
	{ 0x4a, 0x80 },
	{ 0x4b, 0x80 },
	{ 0x4d, 0xd2 }, /* This reduces noise a bit */
	{ 0x4e, 0xc1 },
	{ 0x4f, 0x04 },
/* Do 50-53 have any effect? */
/* Toggle 0x12[2] off and on here? */
};

static const struct ov_i2c_regvals norm_6x30[] = {
	{ 0x12, 0x80 }, /* Reset */
	{ 0x00, 0x1f }, /* Gain */
	{ 0x01, 0x99 }, /* Blue gain */
	{ 0x02, 0x7c }, /* Red gain */
	{ 0x03, 0xc0 }, /* Saturation */
	{ 0x05, 0x0a }, /* Contrast */
	{ 0x06, 0x95 }, /* Brightness */
	{ 0x07, 0x2d }, /* Sharpness */
	{ 0x0c, 0x20 },
	{ 0x0d, 0x20 },
	{ 0x0e, 0xa0 }, /* Was 0x20, bit7 enables a 2x gain which we need */
	{ 0x0f, 0x05 },
	{ 0x10, 0x9a },
	{ 0x11, 0x00 }, /* Pixel clock = fastest */
	{ 0x12, 0x24 }, /* Enable AGC and AWB */
	{ 0x13, 0x21 },
	{ 0x14, 0x80 },
	{ 0x15, 0x01 },
	{ 0x16, 0x03 },
	{ 0x17, 0x38 },
	{ 0x18, 0xea },
	{ 0x19, 0x04 },
	{ 0x1a, 0x93 },
	{ 0x1b, 0x00 },
	{ 0x1e, 0xc4 },
	{ 0x1f, 0x04 },
	{ 0x20, 0x20 },
	{ 0x21, 0x10 },
	{ 0x22, 0x88 },
	{ 0x23, 0xc0 }, /* Crystal circuit power level */
	{ 0x25, 0x9a }, /* Increase AEC black ratio */
	{ 0x26, 0xb2 }, /* BLC enable */
	{ 0x27, 0xa2 },
	{ 0x28, 0x00 },
	{ 0x29, 0x00 },
	{ 0x2a, 0x84 }, /* 60 Hz power */
	{ 0x2b, 0xa8 }, /* 60 Hz power */
	{ 0x2c, 0xa0 },
	{ 0x2d, 0x95 }, /* Enable auto-brightness */
	{ 0x2e, 0x88 },
	{ 0x33, 0x26 },
	{ 0x34, 0x03 },
	{ 0x36, 0x8f },
	{ 0x37, 0x80 },
	{ 0x38, 0x83 },
	{ 0x39, 0x80 },
	{ 0x3a, 0x0f },
	{ 0x3b, 0x3c },
	{ 0x3c, 0x1a },
	{ 0x3d, 0x80 },
	{ 0x3e, 0x80 },
	{ 0x3f, 0x0e },
	{ 0x40, 0x00 }, /* White bal */
	{ 0x41, 0x00 }, /* White bal */
	{ 0x42, 0x80 },
	{ 0x43, 0x3f }, /* White bal */
	{ 0x44, 0x80 },
	{ 0x45, 0x20 },
	{ 0x46, 0x20 },
	{ 0x47, 0x80 },
	{ 0x48, 0x7f },
	{ 0x49, 0x00 },
	{ 0x4a, 0x00 },
	{ 0x4b, 0x80 },
	{ 0x4c, 0xd0 },
	{ 0x4d, 0x10 }, /* U = 0.563u, V = 0.714v */
	{ 0x4e, 0x40 },
	{ 0x4f, 0x07 }, /* UV avg., col. killer: max */
	{ 0x50, 0xff },
	{ 0x54, 0x23 }, /* Max AGC gain: 18dB */
	{ 0x55, 0xff },
	{ 0x56, 0x12 },
	{ 0x57, 0x81 },
	{ 0x58, 0x75 },
	{ 0x59, 0x01 }, /* AGC dark current comp.: +1 */
	{ 0x5a, 0x2c },
	{ 0x5b, 0x0f }, /* AWB chrominance levels */
	{ 0x5c, 0x10 },
	{ 0x3d, 0x80 },
	{ 0x27, 0xa6 },
	{ 0x12, 0x20 }, /* Toggle AWB */
	{ 0x12, 0x24 },
};

/* Lawrence Glaister <lg@jfm.bc.ca> reports:
 *
 * Register 0x0f in the 7610 has the following effects:
 *
 * 0x85 (AEC method 1): Best overall, good contrast range
 * 0x45 (AEC method 2): Very overexposed
 * 0xa5 (spec sheet default): Ok, but the black level is
 *	shifted resulting in loss of contrast
 * 0x05 (old driver setting): very overexposed, too much
 *	contrast
 */
static const struct ov_i2c_regvals norm_7610[] = {
	{ 0x10, 0xff },
	{ 0x16, 0x06 },
	{ 0x28, 0x24 },
	{ 0x2b, 0xac },
	{ 0x12, 0x00 },
	{ 0x38, 0x81 },
	{ 0x28, 0x24 },	/* 0c */
	{ 0x0f, 0x85 },	/* lg's setting */
	{ 0x15, 0x01 },
	{ 0x20, 0x1c },
	{ 0x23, 0x2a },
	{ 0x24, 0x10 },
	{ 0x25, 0x8a },
	{ 0x26, 0xa2 },
	{ 0x27, 0xc2 },
	{ 0x2a, 0x04 },
	{ 0x2c, 0xfe },
	{ 0x2d, 0x93 },
	{ 0x30, 0x71 },
	{ 0x31, 0x60 },
	{ 0x32, 0x26 },
	{ 0x33, 0x20 },
	{ 0x34, 0x48 },
	{ 0x12, 0x24 },
	{ 0x11, 0x01 },
	{ 0x0c, 0x24 },
	{ 0x0d, 0x24 },
};

static const struct ov_i2c_regvals norm_7620[] = {
	{ 0x12, 0x80 },		/* reset */
	{ 0x00, 0x00 },		/* gain */
	{ 0x01, 0x80 },		/* blue gain */
	{ 0x02, 0x80 },		/* red gain */
	{ 0x03, 0xc0 },		/* OV7670_R03_VREF */
	{ 0x06, 0x60 },
	{ 0x07, 0x00 },
	{ 0x0c, 0x24 },
	{ 0x0c, 0x24 },
	{ 0x0d, 0x24 },
	{ 0x11, 0x01 },
	{ 0x12, 0x24 },
	{ 0x13, 0x01 },
	{ 0x14, 0x84 },
	{ 0x15, 0x01 },
	{ 0x16, 0x03 },
	{ 0x17, 0x2f },
	{ 0x18, 0xcf },
	{ 0x19, 0x06 },
	{ 0x1a, 0xf5 },
	{ 0x1b, 0x00 },
	{ 0x20, 0x18 },
	{ 0x21, 0x80 },
	{ 0x22, 0x80 },
	{ 0x23, 0x00 },
	{ 0x26, 0xa2 },
	{ 0x27, 0xea },
	{ 0x28, 0x22 }, /* Was 0x20, bit1 enables a 2x gain which we need */
	{ 0x29, 0x00 },
	{ 0x2a, 0x10 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x88 },
	{ 0x2d, 0x91 },
	{ 0x2e, 0x80 },
	{ 0x2f, 0x44 },
	{ 0x60, 0x27 },
	{ 0x61, 0x02 },
	{ 0x62, 0x5f },
	{ 0x63, 0xd5 },
	{ 0x64, 0x57 },
	{ 0x65, 0x83 },
	{ 0x66, 0x55 },
	{ 0x67, 0x92 },
	{ 0x68, 0xcf },
	{ 0x69, 0x76 },
	{ 0x6a, 0x22 },
	{ 0x6b, 0x00 },
	{ 0x6c, 0x02 },
	{ 0x6d, 0x44 },
	{ 0x6e, 0x80 },
	{ 0x6f, 0x1d },
	{ 0x70, 0x8b },
	{ 0x71, 0x00 },
	{ 0x72, 0x14 },
	{ 0x73, 0x54 },
	{ 0x74, 0x00 },
	{ 0x75, 0x8e },
	{ 0x76, 0x00 },
	{ 0x77, 0xff },
	{ 0x78, 0x80 },
	{ 0x79, 0x80 },
	{ 0x7a, 0x80 },
	{ 0x7b, 0xe2 },
	{ 0x7c, 0x00 },
};

/* 7640 and 7648. The defaults should be OK for most registers. */
static const struct ov_i2c_regvals norm_7640[] = {
	{ 0x12, 0x80 },
	{ 0x12, 0x14 },
};

static const struct ov_regvals init_519_ov7660[] = {
	{ 0x5d,	0x03 }, /* Turn off suspend mode */
	{ 0x53,	0x9b }, /* 0x9f enables the (unused) microcontroller */
	{ 0x54,	0x0f }, /* bit2 (jpeg enable) */
	{ 0xa2,	0x20 }, /* a2-a5 are undocumented */
	{ 0xa3,	0x18 },
	{ 0xa4,	0x04 },
	{ 0xa5,	0x28 },
	{ 0x37,	0x00 },	/* SetUsbInit */
	{ 0x55,	0x02 }, /* 4.096 Mhz audio clock */
	/* Enable both fields, YUV Input, disable defect comp (why?) */
	{ 0x20,	0x0c },	/* 0x0d does U <-> V swap */
	{ 0x21,	0x38 },
	{ 0x22,	0x1d },
	{ 0x17,	0x50 }, /* undocumented */
	{ 0x37,	0x00 }, /* undocumented */
	{ 0x40,	0xff }, /* I2C timeout counter */
	{ 0x46,	0x00 }, /* I2C clock prescaler */
};
static const struct ov_i2c_regvals norm_7660[] = {
	{OV7670_R12_COM7, OV7670_COM7_RESET},
	{OV7670_R11_CLKRC, 0x81},
	{0x92, 0x00},			/* DM_LNL */
	{0x93, 0x00},			/* DM_LNH */
	{0x9d, 0x4c},			/* BD50ST */
	{0x9e, 0x3f},			/* BD60ST */
	{OV7670_R3B_COM11, 0x02},
	{OV7670_R13_COM8, 0xf5},
	{OV7670_R10_AECH, 0x00},
	{OV7670_R00_GAIN, 0x00},
	{OV7670_R01_BLUE, 0x7c},
	{OV7670_R02_RED, 0x9d},
	{OV7670_R12_COM7, 0x00},
	{OV7670_R04_COM1, 00},
	{OV7670_R18_HSTOP, 0x01},
	{OV7670_R17_HSTART, 0x13},
	{OV7670_R32_HREF, 0x92},
	{OV7670_R19_VSTART, 0x02},
	{OV7670_R1A_VSTOP, 0x7a},
	{OV7670_R03_VREF, 0x00},
	{OV7670_R0E_COM5, 0x04},
	{OV7670_R0F_COM6, 0x62},
	{OV7670_R15_COM10, 0x00},
	{0x16, 0x02},			/* RSVD */
	{0x1b, 0x00},			/* PSHFT */
	{OV7670_R1E_MVFP, 0x01},
	{0x29, 0x3c},			/* RSVD */
	{0x33, 0x00},			/* CHLF */
	{0x34, 0x07},			/* ARBLM */
	{0x35, 0x84},			/* RSVD */
	{0x36, 0x00},			/* RSVD */
	{0x37, 0x04},			/* ADC */
	{0x39, 0x43},			/* OFON */
	{OV7670_R3A_TSLB, 0x00},
	{OV7670_R3C_COM12, 0x6c},
	{OV7670_R3D_COM13, 0x98},
	{OV7670_R3F_EDGE, 0x23},
	{OV7670_R40_COM15, 0xc1},
	{OV7670_R41_COM16, 0x22},
	{0x6b, 0x0a},			/* DBLV */
	{0xa1, 0x08},			/* RSVD */
	{0x69, 0x80},			/* HV */
	{0x43, 0xf0},			/* RSVD.. */
	{0x44, 0x10},
	{0x45, 0x78},
	{0x46, 0xa8},
	{0x47, 0x60},
	{0x48, 0x80},
	{0x59, 0xba},
	{0x5a, 0x9a},
	{0x5b, 0x22},
	{0x5c, 0xb9},
	{0x5d, 0x9b},
	{0x5e, 0x10},
	{0x5f, 0xe0},
	{0x60, 0x85},
	{0x61, 0x60},
	{0x9f, 0x9d},			/* RSVD */
	{0xa0, 0xa0},			/* DSPC2 */
	{0x4f, 0x60},			/* matrix */
	{0x50, 0x64},
	{0x51, 0x04},
	{0x52, 0x18},
	{0x53, 0x3c},
	{0x54, 0x54},
	{0x55, 0x40},
	{0x56, 0x40},
	{0x57, 0x40},
	{0x58, 0x0d},			/* matrix sign */
	{0x8b, 0xcc},			/* RSVD */
	{0x8c, 0xcc},
	{0x8d, 0xcf},
	{0x6c, 0x40},			/* gamma curve */
	{0x6d, 0xe0},
	{0x6e, 0xa0},
	{0x6f, 0x80},
	{0x70, 0x70},
	{0x71, 0x80},
	{0x72, 0x60},
	{0x73, 0x60},
	{0x74, 0x50},
	{0x75, 0x40},
	{0x76, 0x38},
	{0x77, 0x3c},
	{0x78, 0x32},
	{0x79, 0x1a},
	{0x7a, 0x28},
	{0x7b, 0x24},
	{0x7c, 0x04},			/* gamma curve */
	{0x7d, 0x12},
	{0x7e, 0x26},
	{0x7f, 0x46},
	{0x80, 0x54},
	{0x81, 0x64},
	{0x82, 0x70},
	{0x83, 0x7c},
	{0x84, 0x86},
	{0x85, 0x8e},
	{0x86, 0x9c},
	{0x87, 0xab},
	{0x88, 0xc4},
	{0x89, 0xd1},
	{0x8a, 0xe5},
	{OV7670_R14_COM9, 0x1e},
	{OV7670_R24_AEW, 0x80},
	{OV7670_R25_AEB, 0x72},
	{OV7670_R26_VPT, 0xb3},
	{0x62, 0x80},			/* LCC1 */
	{0x63, 0x80},			/* LCC2 */
	{0x64, 0x06},			/* LCC3 */
	{0x65, 0x00},			/* LCC4 */
	{0x66, 0x01},			/* LCC5 */
	{0x94, 0x0e},			/* RSVD.. */
	{0x95, 0x14},
	{OV7670_R13_COM8, OV7670_COM8_FASTAEC
			| OV7670_COM8_AECSTEP
			| OV7670_COM8_BFILT
			| 0x10
			| OV7670_COM8_AGC
			| OV7670_COM8_AWB
			| OV7670_COM8_AEC},
	{0xa1, 0xc8}
};
static const struct ov_i2c_regvals norm_9600[] = {
	{0x12, 0x80},
	{0x0c, 0x28},
	{0x11, 0x80},
	{0x13, 0xb5},
	{0x14, 0x3e},
	{0x1b, 0x04},
	{0x24, 0xb0},
	{0x25, 0x90},
	{0x26, 0x94},
	{0x35, 0x90},
	{0x37, 0x07},
	{0x38, 0x08},
	{0x01, 0x8e},
	{0x02, 0x85}
};

/* 7670. Defaults taken from OmniVision provided data,
*  as provided by Jonathan Corbet of OLPC		*/
static const struct ov_i2c_regvals norm_7670[] = {
	{ OV7670_R12_COM7, OV7670_COM7_RESET },
	{ OV7670_R3A_TSLB, 0x04 },		/* OV */
	{ OV7670_R12_COM7, OV7670_COM7_FMT_VGA }, /* VGA */
	{ OV7670_R11_CLKRC, 0x01 },
/*
 * Set the hardware window.  These values from OV don't entirely
 * make sense - hstop is less than hstart.  But they work...
 */
	{ OV7670_R17_HSTART, 0x13 },
	{ OV7670_R18_HSTOP, 0x01 },
	{ OV7670_R32_HREF, 0xb6 },
	{ OV7670_R19_VSTART, 0x02 },
	{ OV7670_R1A_VSTOP, 0x7a },
	{ OV7670_R03_VREF, 0x0a },

	{ OV7670_R0C_COM3, 0x00 },
	{ OV7670_R3E_COM14, 0x00 },
/* Mystery scaling numbers */
	{ 0x70, 0x3a },
	{ 0x71, 0x35 },
	{ 0x72, 0x11 },
	{ 0x73, 0xf0 },
	{ 0xa2, 0x02 },
/*	{ OV7670_R15_COM10, 0x0 }, */

/* Gamma curve values */
	{ 0x7a, 0x20 },
	{ 0x7b, 0x10 },
	{ 0x7c, 0x1e },
	{ 0x7d, 0x35 },
	{ 0x7e, 0x5a },
	{ 0x7f, 0x69 },
	{ 0x80, 0x76 },
	{ 0x81, 0x80 },
	{ 0x82, 0x88 },
	{ 0x83, 0x8f },
	{ 0x84, 0x96 },
	{ 0x85, 0xa3 },
	{ 0x86, 0xaf },
	{ 0x87, 0xc4 },
	{ 0x88, 0xd7 },
	{ 0x89, 0xe8 },

/* AGC and AEC parameters.  Note we start by disabling those features,
   then turn them only after tweaking the values. */
	{ OV7670_R13_COM8, OV7670_COM8_FASTAEC
			 | OV7670_COM8_AECSTEP
			 | OV7670_COM8_BFILT },
	{ OV7670_R00_GAIN, 0x00 },
	{ OV7670_R10_AECH, 0x00 },
	{ OV7670_R0D_COM4, 0x40 }, /* magic reserved bit */
	{ OV7670_R14_COM9, 0x18 }, /* 4x gain + magic rsvd bit */
	{ OV7670_RA5_BD50MAX, 0x05 },
	{ OV7670_RAB_BD60MAX, 0x07 },
	{ OV7670_R24_AEW, 0x95 },
	{ OV7670_R25_AEB, 0x33 },
	{ OV7670_R26_VPT, 0xe3 },
	{ OV7670_R9F_HAECC1, 0x78 },
	{ OV7670_RA0_HAECC2, 0x68 },
	{ 0xa1, 0x03 }, /* magic */
	{ OV7670_RA6_HAECC3, 0xd8 },
	{ OV7670_RA7_HAECC4, 0xd8 },
	{ OV7670_RA8_HAECC5, 0xf0 },
	{ OV7670_RA9_HAECC6, 0x90 },
	{ OV7670_RAA_HAECC7, 0x94 },
	{ OV7670_R13_COM8, OV7670_COM8_FASTAEC
			| OV7670_COM8_AECSTEP
			| OV7670_COM8_BFILT
			| OV7670_COM8_AGC
			| OV7670_COM8_AEC },

/* Almost all of these are magic "reserved" values.  */
	{ OV7670_R0E_COM5, 0x61 },
	{ OV7670_R0F_COM6, 0x4b },
	{ 0x16, 0x02 },
	{ OV7670_R1E_MVFP, 0x07 },
	{ 0x21, 0x02 },
	{ 0x22, 0x91 },
	{ 0x29, 0x07 },
	{ 0x33, 0x0b },
	{ 0x35, 0x0b },
	{ 0x37, 0x1d },
	{ 0x38, 0x71 },
	{ 0x39, 0x2a },
	{ OV7670_R3C_COM12, 0x78 },
	{ 0x4d, 0x40 },
	{ 0x4e, 0x20 },
	{ OV7670_R69_GFIX, 0x00 },
	{ 0x6b, 0x4a },
	{ 0x74, 0x10 },
	{ 0x8d, 0x4f },
	{ 0x8e, 0x00 },
	{ 0x8f, 0x00 },
	{ 0x90, 0x00 },
	{ 0x91, 0x00 },
	{ 0x96, 0x00 },
	{ 0x9a, 0x00 },
	{ 0xb0, 0x84 },
	{ 0xb1, 0x0c },
	{ 0xb2, 0x0e },
	{ 0xb3, 0x82 },
	{ 0xb8, 0x0a },

/* More reserved magic, some of which tweaks white balance */
	{ 0x43, 0x0a },
	{ 0x44, 0xf0 },
	{ 0x45, 0x34 },
	{ 0x46, 0x58 },
	{ 0x47, 0x28 },
	{ 0x48, 0x3a },
	{ 0x59, 0x88 },
	{ 0x5a, 0x88 },
	{ 0x5b, 0x44 },
	{ 0x5c, 0x67 },
	{ 0x5d, 0x49 },
	{ 0x5e, 0x0e },
	{ 0x6c, 0x0a },
	{ 0x6d, 0x55 },
	{ 0x6e, 0x11 },
	{ 0x6f, 0x9f },			/* "9e for advance AWB" */
	{ 0x6a, 0x40 },
	{ OV7670_R01_BLUE, 0x40 },
	{ OV7670_R02_RED, 0x60 },
	{ OV7670_R13_COM8, OV7670_COM8_FASTAEC
			| OV7670_COM8_AECSTEP
			| OV7670_COM8_BFILT
			| OV7670_COM8_AGC
			| OV7670_COM8_AEC
			| OV7670_COM8_AWB },

/* Matrix coefficients */
	{ 0x4f, 0x80 },
	{ 0x50, 0x80 },
	{ 0x51, 0x00 },
	{ 0x52, 0x22 },
	{ 0x53, 0x5e },
	{ 0x54, 0x80 },
	{ 0x58, 0x9e },

	{ OV7670_R41_COM16, OV7670_COM16_AWBGAIN },
	{ OV7670_R3F_EDGE, 0x00 },
	{ 0x75, 0x05 },
	{ 0x76, 0xe1 },
	{ 0x4c, 0x00 },
	{ 0x77, 0x01 },
	{ OV7670_R3D_COM13, OV7670_COM13_GAMMA
			  | OV7670_COM13_UVSAT
			  | 2},		/* was 3 */
	{ 0x4b, 0x09 },
	{ 0xc9, 0x60 },
	{ OV7670_R41_COM16, 0x38 },
	{ 0x56, 0x40 },

	{ 0x34, 0x11 },
	{ OV7670_R3B_COM11, OV7670_COM11_EXP|OV7670_COM11_HZAUTO },
	{ 0xa4, 0x88 },
	{ 0x96, 0x00 },
	{ 0x97, 0x30 },
	{ 0x98, 0x20 },
	{ 0x99, 0x30 },
	{ 0x9a, 0x84 },
	{ 0x9b, 0x29 },
	{ 0x9c, 0x03 },
	{ 0x9d, 0x4c },
	{ 0x9e, 0x3f },
	{ 0x78, 0x04 },

/* Extra-weird stuff.  Some sort of multiplexor register */
	{ 0x79, 0x01 },
	{ 0xc8, 0xf0 },
	{ 0x79, 0x0f },
	{ 0xc8, 0x00 },
	{ 0x79, 0x10 },
	{ 0xc8, 0x7e },
	{ 0x79, 0x0a },
	{ 0xc8, 0x80 },
	{ 0x79, 0x0b },
	{ 0xc8, 0x01 },
	{ 0x79, 0x0c },
	{ 0xc8, 0x0f },
	{ 0x79, 0x0d },
	{ 0xc8, 0x20 },
	{ 0x79, 0x09 },
	{ 0xc8, 0x80 },
	{ 0x79, 0x02 },
	{ 0xc8, 0xc0 },
	{ 0x79, 0x03 },
	{ 0xc8, 0x40 },
	{ 0x79, 0x05 },
	{ 0xc8, 0x30 },
	{ 0x79, 0x26 },
};

static const struct ov_i2c_regvals norm_8610[] = {
	{ 0x12, 0x80 },
	{ 0x00, 0x00 },
	{ 0x01, 0x80 },
	{ 0x02, 0x80 },
	{ 0x03, 0xc0 },
	{ 0x04, 0x30 },
	{ 0x05, 0x30 }, /* was 0x10, new from windrv 090403 */
	{ 0x06, 0x70 }, /* was 0x80, new from windrv 090403 */
	{ 0x0a, 0x86 },
	{ 0x0b, 0xb0 },
	{ 0x0c, 0x20 },
	{ 0x0d, 0x20 },
	{ 0x11, 0x01 },
	{ 0x12, 0x25 },
	{ 0x13, 0x01 },
	{ 0x14, 0x04 },
	{ 0x15, 0x01 }, /* Lin and Win think different about UV order */
	{ 0x16, 0x03 },
	{ 0x17, 0x38 }, /* was 0x2f, new from windrv 090403 */
	{ 0x18, 0xea }, /* was 0xcf, new from windrv 090403 */
	{ 0x19, 0x02 }, /* was 0x06, new from windrv 090403 */
	{ 0x1a, 0xf5 },
	{ 0x1b, 0x00 },
	{ 0x20, 0xd0 }, /* was 0x90, new from windrv 090403 */
	{ 0x23, 0xc0 }, /* was 0x00, new from windrv 090403 */
	{ 0x24, 0x30 }, /* was 0x1d, new from windrv 090403 */
	{ 0x25, 0x50 }, /* was 0x57, new from windrv 090403 */
	{ 0x26, 0xa2 },
	{ 0x27, 0xea },
	{ 0x28, 0x00 },
	{ 0x29, 0x00 },
	{ 0x2a, 0x80 },
	{ 0x2b, 0xc8 }, /* was 0xcc, new from windrv 090403 */
	{ 0x2c, 0xac },
	{ 0x2d, 0x45 }, /* was 0xd5, new from windrv 090403 */
	{ 0x2e, 0x80 },
	{ 0x2f, 0x14 }, /* was 0x01, new from windrv 090403 */
	{ 0x4c, 0x00 },
	{ 0x4d, 0x30 }, /* was 0x10, new from windrv 090403 */
	{ 0x60, 0x02 }, /* was 0x01, new from windrv 090403 */
	{ 0x61, 0x00 }, /* was 0x09, new from windrv 090403 */
	{ 0x62, 0x5f }, /* was 0xd7, new from windrv 090403 */
	{ 0x63, 0xff },
	{ 0x64, 0x53 }, /* new windrv 090403 says 0x57,
			 * maybe thats wrong */
	{ 0x65, 0x00 },
	{ 0x66, 0x55 },
	{ 0x67, 0xb0 },
	{ 0x68, 0xc0 }, /* was 0xaf, new from windrv 090403 */
	{ 0x69, 0x02 },
	{ 0x6a, 0x22 },
	{ 0x6b, 0x00 },
	{ 0x6c, 0x99 }, /* was 0x80, old windrv says 0x00, but
			 * deleting bit7 colors the first images red */
	{ 0x6d, 0x11 }, /* was 0x00, new from windrv 090403 */
	{ 0x6e, 0x11 }, /* was 0x00, new from windrv 090403 */
	{ 0x6f, 0x01 },
	{ 0x70, 0x8b },
	{ 0x71, 0x00 },
	{ 0x72, 0x14 },
	{ 0x73, 0x54 },
	{ 0x74, 0x00 },/* 0x60? - was 0x00, new from windrv 090403 */
	{ 0x75, 0x0e },
	{ 0x76, 0x02 }, /* was 0x02, new from windrv 090403 */
	{ 0x77, 0xff },
	{ 0x78, 0x80 },
	{ 0x79, 0x80 },
	{ 0x7a, 0x80 },
	{ 0x7b, 0x10 }, /* was 0x13, new from windrv 090403 */
	{ 0x7c, 0x00 },
	{ 0x7d, 0x08 }, /* was 0x09, new from windrv 090403 */
	{ 0x7e, 0x08 }, /* was 0xc0, new from windrv 090403 */
	{ 0x7f, 0xfb },
	{ 0x80, 0x28 },
	{ 0x81, 0x00 },
	{ 0x82, 0x23 },
	{ 0x83, 0x0b },
	{ 0x84, 0x00 },
	{ 0x85, 0x62 }, /* was 0x61, new from windrv 090403 */
	{ 0x86, 0xc9 },
	{ 0x87, 0x00 },
	{ 0x88, 0x00 },
	{ 0x89, 0x01 },
	{ 0x12, 0x20 },
	{ 0x12, 0x25 }, /* was 0x24, new from windrv 090403 */
};

static unsigned char ov7670_abs_to_sm(unsigned char v)
{
	if (v > 127)
		return v & 0x7f;
	return (128 - v) | 0x80;
}

/* Write a OV519 register */
static void reg_w(struct sd *sd, u16 index, u16 value)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret, req = 0;

	if (sd->gspca_dev.usb_err < 0)
		return;

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		req = 2;
		break;
	case BRIDGE_OVFX2:
		req = 0x0a;
		/* fall through */
	case BRIDGE_W9968CF:
		gspca_dbg(gspca_dev, D_USBO, "SET %02x %04x %04x\n",
			  req, value, index);
		ret = usb_control_msg(sd->gspca_dev.dev,
			usb_sndctrlpipe(sd->gspca_dev.dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0, 500);
		goto leave;
	default:
		req = 1;
	}

	gspca_dbg(gspca_dev, D_USBO, "SET %02x 0000 %04x %02x\n",
		  req, index, value);
	sd->gspca_dev.usb_buf[0] = value;
	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_sndctrlpipe(sd->gspca_dev.dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index,
			sd->gspca_dev.usb_buf, 1, 500);
leave:
	if (ret < 0) {
		gspca_err(gspca_dev, "reg_w %02x failed %d\n", index, ret);
		sd->gspca_dev.usb_err = ret;
		return;
	}
}

/* Read from a OV519 register, note not valid for the w9968cf!! */
/* returns: negative is error, pos or zero is data */
static int reg_r(struct sd *sd, u16 index)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret;
	int req;

	if (sd->gspca_dev.usb_err < 0)
		return -1;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		req = 3;
		break;
	case BRIDGE_OVFX2:
		req = 0x0b;
		break;
	default:
		req = 1;
	}

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);
	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_rcvctrlpipe(sd->gspca_dev.dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index, sd->gspca_dev.usb_buf, 1, 500);

	if (ret >= 0) {
		ret = sd->gspca_dev.usb_buf[0];
		gspca_dbg(gspca_dev, D_USBI, "GET %02x 0000 %04x %02x\n",
			  req, index, ret);
	} else {
		gspca_err(gspca_dev, "reg_r %02x failed %d\n", index, ret);
		sd->gspca_dev.usb_err = ret;
		/*
		 * Make sure the result is zeroed to avoid uninitialized
		 * values.
		 */
		gspca_dev->usb_buf[0] = 0;
	}

	return ret;
}

/* Read 8 values from a OV519 register */
static int reg_r8(struct sd *sd,
		  u16 index)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return -1;

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);
	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_rcvctrlpipe(sd->gspca_dev.dev, 0),
			1,			/* REQ_IO */
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index, sd->gspca_dev.usb_buf, 8, 500);

	if (ret >= 0) {
		ret = sd->gspca_dev.usb_buf[0];
	} else {
		gspca_err(gspca_dev, "reg_r8 %02x failed %d\n", index, ret);
		sd->gspca_dev.usb_err = ret;
		/*
		 * Make sure the buffer is zeroed to avoid uninitialized
		 * values.
		 */
		memset(gspca_dev->usb_buf, 0, 8);
	}

	return ret;
}

/*
 * Writes bits at positions specified by mask to an OV51x reg. Bits that are in
 * the same position as 1's in "mask" are cleared and set to "value". Bits
 * that are in the same position as 0's in "mask" are preserved, regardless
 * of their respective state in "value".
 */
static void reg_w_mask(struct sd *sd,
			u16 index,
			u8 value,
			u8 mask)
{
	int ret;
	u8 oldval;

	if (mask != 0xff) {
		value &= mask;			/* Enforce mask on value */
		ret = reg_r(sd, index);
		if (ret < 0)
			return;

		oldval = ret & ~mask;		/* Clear the masked bits */
		value |= oldval;		/* Set the desired bits */
	}
	reg_w(sd, index, value);
}

/*
 * Writes multiple (n) byte value to a single register. Only valid with certain
 * registers (0x30 and 0xc4 - 0xce).
 */
static void ov518_reg_w32(struct sd *sd, u16 index, u32 value, int n)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return;

	*((__le32 *) sd->gspca_dev.usb_buf) = __cpu_to_le32(value);

	/* Avoid things going to fast for the bridge with a xhci host */
	udelay(150);
	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_sndctrlpipe(sd->gspca_dev.dev, 0),
			1 /* REG_IO */,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index,
			sd->gspca_dev.usb_buf, n, 500);
	if (ret < 0) {
		gspca_err(gspca_dev, "reg_w32 %02x failed %d\n", index, ret);
		sd->gspca_dev.usb_err = ret;
	}
}

static void ov511_i2c_w(struct sd *sd, u8 reg, u8 value)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int rc, retries;

	gspca_dbg(gspca_dev, D_USBO, "ov511_i2c_w %02x %02x\n", reg, value);

	/* Three byte write cycle */
	for (retries = 6; ; ) {
		/* Select camera register */
		reg_w(sd, R51x_I2C_SADDR_3, reg);

		/* Write "value" to I2C data port of OV511 */
		reg_w(sd, R51x_I2C_DATA, value);

		/* Initiate 3-byte write cycle */
		reg_w(sd, R511_I2C_CTL, 0x01);

		do {
			rc = reg_r(sd, R511_I2C_CTL);
		} while (rc > 0 && ((rc & 1) == 0)); /* Retry until idle */

		if (rc < 0)
			return;

		if ((rc & 2) == 0) /* Ack? */
			break;
		if (--retries < 0) {
			gspca_dbg(gspca_dev, D_USBO, "i2c write retries exhausted\n");
			return;
		}
	}
}

static int ov511_i2c_r(struct sd *sd, u8 reg)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int rc, value, retries;

	/* Two byte write cycle */
	for (retries = 6; ; ) {
		/* Select camera register */
		reg_w(sd, R51x_I2C_SADDR_2, reg);

		/* Initiate 2-byte write cycle */
		reg_w(sd, R511_I2C_CTL, 0x03);

		do {
			rc = reg_r(sd, R511_I2C_CTL);
		} while (rc > 0 && ((rc & 1) == 0)); /* Retry until idle */

		if (rc < 0)
			return rc;

		if ((rc & 2) == 0) /* Ack? */
			break;

		/* I2C abort */
		reg_w(sd, R511_I2C_CTL, 0x10);

		if (--retries < 0) {
			gspca_dbg(gspca_dev, D_USBI, "i2c write retries exhausted\n");
			return -1;
		}
	}

	/* Two byte read cycle */
	for (retries = 6; ; ) {
		/* Initiate 2-byte read cycle */
		reg_w(sd, R511_I2C_CTL, 0x05);

		do {
			rc = reg_r(sd, R511_I2C_CTL);
		} while (rc > 0 && ((rc & 1) == 0)); /* Retry until idle */

		if (rc < 0)
			return rc;

		if ((rc & 2) == 0) /* Ack? */
			break;

		/* I2C abort */
		reg_w(sd, R511_I2C_CTL, 0x10);

		if (--retries < 0) {
			gspca_dbg(gspca_dev, D_USBI, "i2c read retries exhausted\n");
			return -1;
		}
	}

	value = reg_r(sd, R51x_I2C_DATA);

	gspca_dbg(gspca_dev, D_USBI, "ov511_i2c_r %02x %02x\n", reg, value);

	/* This is needed to make i2c_w() work */
	reg_w(sd, R511_I2C_CTL, 0x05);

	return value;
}

/*
 * The OV518 I2C I/O procedure is different, hence, this function.
 * This is normally only called from i2c_w(). Note that this function
 * always succeeds regardless of whether the sensor is present and working.
 */
static void ov518_i2c_w(struct sd *sd,
		u8 reg,
		u8 value)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	gspca_dbg(gspca_dev, D_USBO, "ov518_i2c_w %02x %02x\n", reg, value);

	/* Select camera register */
	reg_w(sd, R51x_I2C_SADDR_3, reg);

	/* Write "value" to I2C data port of OV511 */
	reg_w(sd, R51x_I2C_DATA, value);

	/* Initiate 3-byte write cycle */
	reg_w(sd, R518_I2C_CTL, 0x01);

	/* wait for write complete */
	msleep(4);
	reg_r8(sd, R518_I2C_CTL);
}

/*
 * returns: negative is error, pos or zero is data
 *
 * The OV518 I2C I/O procedure is different, hence, this function.
 * This is normally only called from i2c_r(). Note that this function
 * always succeeds regardless of whether the sensor is present and working.
 */
static int ov518_i2c_r(struct sd *sd, u8 reg)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int value;

	/* Select camera register */
	reg_w(sd, R51x_I2C_SADDR_2, reg);

	/* Initiate 2-byte write cycle */
	reg_w(sd, R518_I2C_CTL, 0x03);
	reg_r8(sd, R518_I2C_CTL);

	/* Initiate 2-byte read cycle */
	reg_w(sd, R518_I2C_CTL, 0x05);
	reg_r8(sd, R518_I2C_CTL);

	value = reg_r(sd, R51x_I2C_DATA);
	gspca_dbg(gspca_dev, D_USBI, "ov518_i2c_r %02x %02x\n", reg, value);
	return value;
}

static void ovfx2_i2c_w(struct sd *sd, u8 reg, u8 value)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return;

	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_sndctrlpipe(sd->gspca_dev.dev, 0),
			0x02,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			(u16) value, (u16) reg, NULL, 0, 500);

	if (ret < 0) {
		gspca_err(gspca_dev, "ovfx2_i2c_w %02x failed %d\n", reg, ret);
		sd->gspca_dev.usb_err = ret;
	}

	gspca_dbg(gspca_dev, D_USBO, "ovfx2_i2c_w %02x %02x\n", reg, value);
}

static int ovfx2_i2c_r(struct sd *sd, u8 reg)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int ret;

	if (sd->gspca_dev.usb_err < 0)
		return -1;

	ret = usb_control_msg(sd->gspca_dev.dev,
			usb_rcvctrlpipe(sd->gspca_dev.dev, 0),
			0x03,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, (u16) reg, sd->gspca_dev.usb_buf, 1, 500);

	if (ret >= 0) {
		ret = sd->gspca_dev.usb_buf[0];
		gspca_dbg(gspca_dev, D_USBI, "ovfx2_i2c_r %02x %02x\n",
			  reg, ret);
	} else {
		gspca_err(gspca_dev, "ovfx2_i2c_r %02x failed %d\n", reg, ret);
		sd->gspca_dev.usb_err = ret;
	}

	return ret;
}

static void i2c_w(struct sd *sd, u8 reg, u8 value)
{
	if (sd->sensor_reg_cache[reg] == value)
		return;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		ov511_i2c_w(sd, reg, value);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
	case BRIDGE_OV519:
		ov518_i2c_w(sd, reg, value);
		break;
	case BRIDGE_OVFX2:
		ovfx2_i2c_w(sd, reg, value);
		break;
	case BRIDGE_W9968CF:
		w9968cf_i2c_w(sd, reg, value);
		break;
	}

	if (sd->gspca_dev.usb_err >= 0) {
		/* Up on sensor reset empty the register cache */
		if (reg == 0x12 && (value & 0x80))
			memset(sd->sensor_reg_cache, -1,
				sizeof(sd->sensor_reg_cache));
		else
			sd->sensor_reg_cache[reg] = value;
	}
}

static int i2c_r(struct sd *sd, u8 reg)
{
	int ret = -1;

	if (sd->sensor_reg_cache[reg] != -1)
		return sd->sensor_reg_cache[reg];

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		ret = ov511_i2c_r(sd, reg);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
	case BRIDGE_OV519:
		ret = ov518_i2c_r(sd, reg);
		break;
	case BRIDGE_OVFX2:
		ret = ovfx2_i2c_r(sd, reg);
		break;
	case BRIDGE_W9968CF:
		ret = w9968cf_i2c_r(sd, reg);
		break;
	}

	if (ret >= 0)
		sd->sensor_reg_cache[reg] = ret;

	return ret;
}

/* Writes bits at positions specified by mask to an I2C reg. Bits that are in
 * the same position as 1's in "mask" are cleared and set to "value". Bits
 * that are in the same position as 0's in "mask" are preserved, regardless
 * of their respective state in "value".
 */
static void i2c_w_mask(struct sd *sd,
			u8 reg,
			u8 value,
			u8 mask)
{
	int rc;
	u8 oldval;

	value &= mask;			/* Enforce mask on value */
	rc = i2c_r(sd, reg);
	if (rc < 0)
		return;
	oldval = rc & ~mask;		/* Clear the masked bits */
	value |= oldval;		/* Set the desired bits */
	i2c_w(sd, reg, value);
}

/* Temporarily stops OV511 from functioning. Must do this before changing
 * registers while the camera is streaming */
static inline void ov51x_stop(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	gspca_dbg(gspca_dev, D_STREAM, "stopping\n");
	sd->stopped = 1;
	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		reg_w(sd, R51x_SYS_RESET, 0x3d);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		reg_w_mask(sd, R51x_SYS_RESET, 0x3a, 0x3a);
		break;
	case BRIDGE_OV519:
		reg_w(sd, OV519_R51_RESET1, 0x0f);
		reg_w(sd, OV519_R51_RESET1, 0x00);
		reg_w(sd, 0x22, 0x00);		/* FRAR */
		break;
	case BRIDGE_OVFX2:
		reg_w_mask(sd, 0x0f, 0x00, 0x02);
		break;
	case BRIDGE_W9968CF:
		reg_w(sd, 0x3c, 0x0a05); /* stop USB transfer */
		break;
	}
}

/* Restarts OV511 after ov511_stop() is called. Has no effect if it is not
 * actually stopped (for performance). */
static inline void ov51x_restart(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	gspca_dbg(gspca_dev, D_STREAM, "restarting\n");
	if (!sd->stopped)
		return;
	sd->stopped = 0;

	/* Reinitialize the stream */
	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		reg_w(sd, R51x_SYS_RESET, 0x00);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		reg_w(sd, 0x2f, 0x80);
		reg_w(sd, R51x_SYS_RESET, 0x00);
		break;
	case BRIDGE_OV519:
		reg_w(sd, OV519_R51_RESET1, 0x0f);
		reg_w(sd, OV519_R51_RESET1, 0x00);
		reg_w(sd, 0x22, 0x1d);		/* FRAR */
		break;
	case BRIDGE_OVFX2:
		reg_w_mask(sd, 0x0f, 0x02, 0x02);
		break;
	case BRIDGE_W9968CF:
		reg_w(sd, 0x3c, 0x8a05); /* USB FIFO enable */
		break;
	}
}

static void ov51x_set_slave_ids(struct sd *sd, u8 slave);

/* This does an initial reset of an OmniVision sensor and ensures that I2C
 * is synchronized. Returns <0 on failure.
 */
static int init_ov_sensor(struct sd *sd, u8 slave)
{
	int i;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	ov51x_set_slave_ids(sd, slave);

	/* Reset the sensor */
	i2c_w(sd, 0x12, 0x80);

	/* Wait for it to initialize */
	msleep(150);

	for (i = 0; i < i2c_detect_tries; i++) {
		if (i2c_r(sd, OV7610_REG_ID_HIGH) == 0x7f &&
		    i2c_r(sd, OV7610_REG_ID_LOW) == 0xa2) {
			gspca_dbg(gspca_dev, D_PROBE, "I2C synced in %d attempt(s)\n",
				  i);
			return 0;
		}

		/* Reset the sensor */
		i2c_w(sd, 0x12, 0x80);

		/* Wait for it to initialize */
		msleep(150);

		/* Dummy read to sync I2C */
		if (i2c_r(sd, 0x00) < 0)
			return -1;
	}
	return -1;
}

/* Set the read and write slave IDs. The "slave" argument is the write slave,
 * and the read slave will be set to (slave + 1).
 * This should not be called from outside the i2c I/O functions.
 * Sets I2C read and write slave IDs. Returns <0 for error
 */
static void ov51x_set_slave_ids(struct sd *sd,
				u8 slave)
{
	switch (sd->bridge) {
	case BRIDGE_OVFX2:
		reg_w(sd, OVFX2_I2C_ADDR, slave);
		return;
	case BRIDGE_W9968CF:
		sd->sensor_addr = slave;
		return;
	}

	reg_w(sd, R51x_I2C_W_SID, slave);
	reg_w(sd, R51x_I2C_R_SID, slave + 1);
}

static void write_regvals(struct sd *sd,
			 const struct ov_regvals *regvals,
			 int n)
{
	while (--n >= 0) {
		reg_w(sd, regvals->reg, regvals->val);
		regvals++;
	}
}

static void write_i2c_regvals(struct sd *sd,
			const struct ov_i2c_regvals *regvals,
			int n)
{
	while (--n >= 0) {
		i2c_w(sd, regvals->reg, regvals->val);
		regvals++;
	}
}

/****************************************************************************
 *
 * OV511 and sensor configuration
 *
 ***************************************************************************/

/* This initializes the OV2x10 / OV3610 / OV3620 / OV9600 */
static void ov_hires_configure(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int high, low;

	if (sd->bridge != BRIDGE_OVFX2) {
		gspca_err(gspca_dev, "error hires sensors only supported with ovfx2\n");
		return;
	}

	gspca_dbg(gspca_dev, D_PROBE, "starting ov hires configuration\n");

	/* Detect sensor (sub)type */
	high = i2c_r(sd, 0x0a);
	low = i2c_r(sd, 0x0b);
	/* info("%x, %x", high, low); */
	switch (high) {
	case 0x96:
		switch (low) {
		case 0x40:
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is a OV2610\n");
			sd->sensor = SEN_OV2610;
			return;
		case 0x41:
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is a OV2610AE\n");
			sd->sensor = SEN_OV2610AE;
			return;
		case 0xb1:
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is a OV9600\n");
			sd->sensor = SEN_OV9600;
			return;
		}
		break;
	case 0x36:
		if ((low & 0x0f) == 0x00) {
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is a OV3610\n");
			sd->sensor = SEN_OV3610;
			return;
		}
		break;
	}
	gspca_err(gspca_dev, "Error unknown sensor type: %02x%02x\n",
		  high, low);
}

/* This initializes the OV8110, OV8610 sensor. The OV8110 uses
 * the same register settings as the OV8610, since they are very similar.
 */
static void ov8xx0_configure(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int rc;

	gspca_dbg(gspca_dev, D_PROBE, "starting ov8xx0 configuration\n");

	/* Detect sensor (sub)type */
	rc = i2c_r(sd, OV7610_REG_COM_I);
	if (rc < 0) {
		gspca_err(gspca_dev, "Error detecting sensor type\n");
		return;
	}
	if ((rc & 3) == 1)
		sd->sensor = SEN_OV8610;
	else
		gspca_err(gspca_dev, "Unknown image sensor version: %d\n",
			  rc & 3);
}

/* This initializes the OV7610, OV7620, or OV76BE sensor. The OV76BE uses
 * the same register settings as the OV7610, since they are very similar.
 */
static void ov7xx0_configure(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int rc, high, low;

	gspca_dbg(gspca_dev, D_PROBE, "starting OV7xx0 configuration\n");

	/* Detect sensor (sub)type */
	rc = i2c_r(sd, OV7610_REG_COM_I);

	/* add OV7670 here
	 * it appears to be wrongly detected as a 7610 by default */
	if (rc < 0) {
		gspca_err(gspca_dev, "Error detecting sensor type\n");
		return;
	}
	if ((rc & 3) == 3) {
		/* quick hack to make OV7670s work */
		high = i2c_r(sd, 0x0a);
		low = i2c_r(sd, 0x0b);
		/* info("%x, %x", high, low); */
		if (high == 0x76 && (low & 0xf0) == 0x70) {
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV76%02x\n",
				  low);
			sd->sensor = SEN_OV7670;
		} else {
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV7610\n");
			sd->sensor = SEN_OV7610;
		}
	} else if ((rc & 3) == 1) {
		/* I don't know what's different about the 76BE yet. */
		if (i2c_r(sd, 0x15) & 1) {
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV7620AE\n");
			sd->sensor = SEN_OV7620AE;
		} else {
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV76BE\n");
			sd->sensor = SEN_OV76BE;
		}
	} else if ((rc & 3) == 0) {
		/* try to read product id registers */
		high = i2c_r(sd, 0x0a);
		if (high < 0) {
			gspca_err(gspca_dev, "Error detecting camera chip PID\n");
			return;
		}
		low = i2c_r(sd, 0x0b);
		if (low < 0) {
			gspca_err(gspca_dev, "Error detecting camera chip VER\n");
			return;
		}
		if (high == 0x76) {
			switch (low) {
			case 0x30:
				gspca_err(gspca_dev, "Sensor is an OV7630/OV7635\n");
				gspca_err(gspca_dev, "7630 is not supported by this driver\n");
				return;
			case 0x40:
				gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV7645\n");
				sd->sensor = SEN_OV7640; /* FIXME */
				break;
			case 0x45:
				gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV7645B\n");
				sd->sensor = SEN_OV7640; /* FIXME */
				break;
			case 0x48:
				gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV7648\n");
				sd->sensor = SEN_OV7648;
				break;
			case 0x60:
				gspca_dbg(gspca_dev, D_PROBE, "Sensor is a OV7660\n");
				sd->sensor = SEN_OV7660;
				break;
			default:
				gspca_err(gspca_dev, "Unknown sensor: 0x76%02x\n",
					  low);
				return;
			}
		} else {
			gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV7620\n");
			sd->sensor = SEN_OV7620;
		}
	} else {
		gspca_err(gspca_dev, "Unknown image sensor version: %d\n",
			  rc & 3);
	}
}

/* This initializes the OV6620, OV6630, OV6630AE, or OV6630AF sensor. */
static void ov6xx0_configure(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int rc;

	gspca_dbg(gspca_dev, D_PROBE, "starting OV6xx0 configuration\n");

	/* Detect sensor (sub)type */
	rc = i2c_r(sd, OV7610_REG_COM_I);
	if (rc < 0) {
		gspca_err(gspca_dev, "Error detecting sensor type\n");
		return;
	}

	/* Ugh. The first two bits are the version bits, but
	 * the entire register value must be used. I guess OVT
	 * underestimated how many variants they would make. */
	switch (rc) {
	case 0x00:
		sd->sensor = SEN_OV6630;
		pr_warn("WARNING: Sensor is an OV66308. Your camera may have been misdetected in previous driver versions.\n");
		break;
	case 0x01:
		sd->sensor = SEN_OV6620;
		gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV6620\n");
		break;
	case 0x02:
		sd->sensor = SEN_OV6630;
		gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV66308AE\n");
		break;
	case 0x03:
		sd->sensor = SEN_OV66308AF;
		gspca_dbg(gspca_dev, D_PROBE, "Sensor is an OV66308AF\n");
		break;
	case 0x90:
		sd->sensor = SEN_OV6630;
		pr_warn("WARNING: Sensor is an OV66307. Your camera may have been misdetected in previous driver versions.\n");
		break;
	default:
		gspca_err(gspca_dev, "FATAL: Unknown sensor version: 0x%02x\n",
			  rc);
		return;
	}

	/* Set sensor-specific vars */
	sd->sif = 1;
}

/* Turns on or off the LED. Only has an effect with OV511+/OV518(+)/OV519 */
static void ov51x_led_control(struct sd *sd, int on)
{
	if (sd->invert_led)
		on = !on;

	switch (sd->bridge) {
	/* OV511 has no LED control */
	case BRIDGE_OV511PLUS:
		reg_w(sd, R511_SYS_LED_CTL, on);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		reg_w_mask(sd, R518_GPIO_OUT, 0x02 * on, 0x02);
		break;
	case BRIDGE_OV519:
		reg_w_mask(sd, OV519_GPIO_DATA_OUT0, on, 1);
		break;
	}
}

static void sd_reset_snapshot(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!sd->snapshot_needs_reset)
		return;

	/* Note it is important that we clear sd->snapshot_needs_reset,
	   before actually clearing the snapshot state in the bridge
	   otherwise we might race with the pkt_scan interrupt handler */
	sd->snapshot_needs_reset = 0;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		reg_w(sd, R51x_SYS_SNAP, 0x02);
		reg_w(sd, R51x_SYS_SNAP, 0x00);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		reg_w(sd, R51x_SYS_SNAP, 0x02); /* Reset */
		reg_w(sd, R51x_SYS_SNAP, 0x01); /* Enable */
		break;
	case BRIDGE_OV519:
		reg_w(sd, R51x_SYS_RESET, 0x40);
		reg_w(sd, R51x_SYS_RESET, 0x00);
		break;
	}
}

static void ov51x_upload_quan_tables(struct sd *sd)
{
	static const unsigned char yQuanTable511[] = {
		0, 1, 1, 2, 2, 3, 3, 4,
		1, 1, 1, 2, 2, 3, 4, 4,
		1, 1, 2, 2, 3, 4, 4, 4,
		2, 2, 2, 3, 4, 4, 4, 4,
		2, 2, 3, 4, 4, 5, 5, 5,
		3, 3, 4, 4, 5, 5, 5, 5,
		3, 4, 4, 4, 5, 5, 5, 5,
		4, 4, 4, 4, 5, 5, 5, 5
	};

	static const unsigned char uvQuanTable511[] = {
		0, 2, 2, 3, 4, 4, 4, 4,
		2, 2, 2, 4, 4, 4, 4, 4,
		2, 2, 3, 4, 4, 4, 4, 4,
		3, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4
	};

	/* OV518 quantization tables are 8x4 (instead of 8x8) */
	static const unsigned char yQuanTable518[] = {
		5, 4, 5, 6, 6, 7, 7, 7,
		5, 5, 5, 5, 6, 7, 7, 7,
		6, 6, 6, 6, 7, 7, 7, 8,
		7, 7, 6, 7, 7, 7, 8, 8
	};
	static const unsigned char uvQuanTable518[] = {
		6, 6, 6, 7, 7, 7, 7, 7,
		6, 6, 6, 7, 7, 7, 7, 7,
		6, 6, 6, 7, 7, 7, 7, 8,
		7, 7, 7, 7, 7, 7, 8, 8
	};

	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	const unsigned char *pYTable, *pUVTable;
	unsigned char val0, val1;
	int i, size, reg = R51x_COMP_LUT_BEGIN;

	gspca_dbg(gspca_dev, D_PROBE, "Uploading quantization tables\n");

	if (sd->bridge == BRIDGE_OV511 || sd->bridge == BRIDGE_OV511PLUS) {
		pYTable = yQuanTable511;
		pUVTable = uvQuanTable511;
		size = 32;
	} else {
		pYTable = yQuanTable518;
		pUVTable = uvQuanTable518;
		size = 16;
	}

	for (i = 0; i < size; i++) {
		val0 = *pYTable++;
		val1 = *pYTable++;
		val0 &= 0x0f;
		val1 &= 0x0f;
		val0 |= val1 << 4;
		reg_w(sd, reg, val0);

		val0 = *pUVTable++;
		val1 = *pUVTable++;
		val0 &= 0x0f;
		val1 &= 0x0f;
		val0 |= val1 << 4;
		reg_w(sd, reg + size, val0);

		reg++;
	}
}

/* This initializes the OV511/OV511+ and the sensor */
static void ov511_configure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* For 511 and 511+ */
	static const struct ov_regvals init_511[] = {
		{ R51x_SYS_RESET,	0x7f },
		{ R51x_SYS_INIT,	0x01 },
		{ R51x_SYS_RESET,	0x7f },
		{ R51x_SYS_INIT,	0x01 },
		{ R51x_SYS_RESET,	0x3f },
		{ R51x_SYS_INIT,	0x01 },
		{ R51x_SYS_RESET,	0x3d },
	};

	static const struct ov_regvals norm_511[] = {
		{ R511_DRAM_FLOW_CTL,	0x01 },
		{ R51x_SYS_SNAP,	0x00 },
		{ R51x_SYS_SNAP,	0x02 },
		{ R51x_SYS_SNAP,	0x00 },
		{ R511_FIFO_OPTS,	0x1f },
		{ R511_COMP_EN,		0x00 },
		{ R511_COMP_LUT_EN,	0x03 },
	};

	static const struct ov_regvals norm_511_p[] = {
		{ R511_DRAM_FLOW_CTL,	0xff },
		{ R51x_SYS_SNAP,	0x00 },
		{ R51x_SYS_SNAP,	0x02 },
		{ R51x_SYS_SNAP,	0x00 },
		{ R511_FIFO_OPTS,	0xff },
		{ R511_COMP_EN,		0x00 },
		{ R511_COMP_LUT_EN,	0x03 },
	};

	static const struct ov_regvals compress_511[] = {
		{ 0x70, 0x1f },
		{ 0x71, 0x05 },
		{ 0x72, 0x06 },
		{ 0x73, 0x06 },
		{ 0x74, 0x14 },
		{ 0x75, 0x03 },
		{ 0x76, 0x04 },
		{ 0x77, 0x04 },
	};

	gspca_dbg(gspca_dev, D_PROBE, "Device custom id %x\n",
		  reg_r(sd, R51x_SYS_CUST_ID));

	write_regvals(sd, init_511, ARRAY_SIZE(init_511));

	switch (sd->bridge) {
	case BRIDGE_OV511:
		write_regvals(sd, norm_511, ARRAY_SIZE(norm_511));
		break;
	case BRIDGE_OV511PLUS:
		write_regvals(sd, norm_511_p, ARRAY_SIZE(norm_511_p));
		break;
	}

	/* Init compression */
	write_regvals(sd, compress_511, ARRAY_SIZE(compress_511));

	ov51x_upload_quan_tables(sd);
}

/* This initializes the OV518/OV518+ and the sensor */
static void ov518_configure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* For 518 and 518+ */
	static const struct ov_regvals init_518[] = {
		{ R51x_SYS_RESET,	0x40 },
		{ R51x_SYS_INIT,	0xe1 },
		{ R51x_SYS_RESET,	0x3e },
		{ R51x_SYS_INIT,	0xe1 },
		{ R51x_SYS_RESET,	0x00 },
		{ R51x_SYS_INIT,	0xe1 },
		{ 0x46,			0x00 },
		{ 0x5d,			0x03 },
	};

	static const struct ov_regvals norm_518[] = {
		{ R51x_SYS_SNAP,	0x02 }, /* Reset */
		{ R51x_SYS_SNAP,	0x01 }, /* Enable */
		{ 0x31,			0x0f },
		{ 0x5d,			0x03 },
		{ 0x24,			0x9f },
		{ 0x25,			0x90 },
		{ 0x20,			0x00 },
		{ 0x51,			0x04 },
		{ 0x71,			0x19 },
		{ 0x2f,			0x80 },
	};

	static const struct ov_regvals norm_518_p[] = {
		{ R51x_SYS_SNAP,	0x02 }, /* Reset */
		{ R51x_SYS_SNAP,	0x01 }, /* Enable */
		{ 0x31,			0x0f },
		{ 0x5d,			0x03 },
		{ 0x24,			0x9f },
		{ 0x25,			0x90 },
		{ 0x20,			0x60 },
		{ 0x51,			0x02 },
		{ 0x71,			0x19 },
		{ 0x40,			0xff },
		{ 0x41,			0x42 },
		{ 0x46,			0x00 },
		{ 0x33,			0x04 },
		{ 0x21,			0x19 },
		{ 0x3f,			0x10 },
		{ 0x2f,			0x80 },
	};

	/* First 5 bits of custom ID reg are a revision ID on OV518 */
	sd->revision = reg_r(sd, R51x_SYS_CUST_ID) & 0x1f;
	gspca_dbg(gspca_dev, D_PROBE, "Device revision %d\n", sd->revision);

	write_regvals(sd, init_518, ARRAY_SIZE(init_518));

	/* Set LED GPIO pin to output mode */
	reg_w_mask(sd, R518_GPIO_CTL, 0x00, 0x02);

	switch (sd->bridge) {
	case BRIDGE_OV518:
		write_regvals(sd, norm_518, ARRAY_SIZE(norm_518));
		break;
	case BRIDGE_OV518PLUS:
		write_regvals(sd, norm_518_p, ARRAY_SIZE(norm_518_p));
		break;
	}

	ov51x_upload_quan_tables(sd);

	reg_w(sd, 0x2f, 0x80);
}

static void ov519_configure(struct sd *sd)
{
	static const struct ov_regvals init_519[] = {
		{ 0x5a, 0x6d }, /* EnableSystem */
		{ 0x53, 0x9b }, /* don't enable the microcontroller */
		{ OV519_R54_EN_CLK1, 0xff }, /* set bit2 to enable jpeg */
		{ 0x5d, 0x03 },
		{ 0x49, 0x01 },
		{ 0x48, 0x00 },
		/* Set LED pin to output mode. Bit 4 must be cleared or sensor
		 * detection will fail. This deserves further investigation. */
		{ OV519_GPIO_IO_CTRL0,   0xee },
		{ OV519_R51_RESET1, 0x0f },
		{ OV519_R51_RESET1, 0x00 },
		{ 0x22, 0x00 },
		/* windows reads 0x55 at this point*/
	};

	write_regvals(sd, init_519, ARRAY_SIZE(init_519));
}

static void ovfx2_configure(struct sd *sd)
{
	static const struct ov_regvals init_fx2[] = {
		{ 0x00, 0x60 },
		{ 0x02, 0x01 },
		{ 0x0f, 0x1d },
		{ 0xe9, 0x82 },
		{ 0xea, 0xc7 },
		{ 0xeb, 0x10 },
		{ 0xec, 0xf6 },
	};

	sd->stopped = 1;

	write_regvals(sd, init_fx2, ARRAY_SIZE(init_fx2));
}

/* set the mode */
/* This function works for ov7660 only */
static void ov519_set_mode(struct sd *sd)
{
	static const struct ov_regvals bridge_ov7660[2][10] = {
		{{0x10, 0x14}, {0x11, 0x1e}, {0x12, 0x00}, {0x13, 0x00},
		 {0x14, 0x00}, {0x15, 0x00}, {0x16, 0x00}, {0x20, 0x0c},
		 {0x25, 0x01}, {0x26, 0x00}},
		{{0x10, 0x28}, {0x11, 0x3c}, {0x12, 0x00}, {0x13, 0x00},
		 {0x14, 0x00}, {0x15, 0x00}, {0x16, 0x00}, {0x20, 0x0c},
		 {0x25, 0x03}, {0x26, 0x00}}
	};
	static const struct ov_i2c_regvals sensor_ov7660[2][3] = {
		{{0x12, 0x00}, {0x24, 0x00}, {0x0c, 0x0c}},
		{{0x12, 0x00}, {0x04, 0x00}, {0x0c, 0x00}}
	};
	static const struct ov_i2c_regvals sensor_ov7660_2[] = {
		{OV7670_R17_HSTART, 0x13},
		{OV7670_R18_HSTOP, 0x01},
		{OV7670_R32_HREF, 0x92},
		{OV7670_R19_VSTART, 0x02},
		{OV7670_R1A_VSTOP, 0x7a},
		{OV7670_R03_VREF, 0x00},
/*		{0x33, 0x00}, */
/*		{0x34, 0x07}, */
/*		{0x36, 0x00}, */
/*		{0x6b, 0x0a}, */
	};

	write_regvals(sd, bridge_ov7660[sd->gspca_dev.curr_mode],
			ARRAY_SIZE(bridge_ov7660[0]));
	write_i2c_regvals(sd, sensor_ov7660[sd->gspca_dev.curr_mode],
			ARRAY_SIZE(sensor_ov7660[0]));
	write_i2c_regvals(sd, sensor_ov7660_2,
			ARRAY_SIZE(sensor_ov7660_2));
}

/* set the frame rate */
/* This function works for sensors ov7640, ov7648 ov7660 and ov7670 only */
static void ov519_set_fr(struct sd *sd)
{
	int fr;
	u8 clock;
	/* frame rate table with indices:
	 *	- mode = 0: 320x240, 1: 640x480
	 *	- fr rate = 0: 30, 1: 25, 2: 20, 3: 15, 4: 10, 5: 5
	 *	- reg = 0: bridge a4, 1: bridge 23, 2: sensor 11 (clock)
	 */
	static const u8 fr_tb[2][6][3] = {
		{{0x04, 0xff, 0x00},
		 {0x04, 0x1f, 0x00},
		 {0x04, 0x1b, 0x00},
		 {0x04, 0x15, 0x00},
		 {0x04, 0x09, 0x00},
		 {0x04, 0x01, 0x00}},
		{{0x0c, 0xff, 0x00},
		 {0x0c, 0x1f, 0x00},
		 {0x0c, 0x1b, 0x00},
		 {0x04, 0xff, 0x01},
		 {0x04, 0x1f, 0x01},
		 {0x04, 0x1b, 0x01}},
	};

	if (frame_rate > 0)
		sd->frame_rate = frame_rate;
	if (sd->frame_rate >= 30)
		fr = 0;
	else if (sd->frame_rate >= 25)
		fr = 1;
	else if (sd->frame_rate >= 20)
		fr = 2;
	else if (sd->frame_rate >= 15)
		fr = 3;
	else if (sd->frame_rate >= 10)
		fr = 4;
	else
		fr = 5;
	reg_w(sd, 0xa4, fr_tb[sd->gspca_dev.curr_mode][fr][0]);
	reg_w(sd, 0x23, fr_tb[sd->gspca_dev.curr_mode][fr][1]);
	clock = fr_tb[sd->gspca_dev.curr_mode][fr][2];
	if (sd->sensor == SEN_OV7660)
		clock |= 0x80;		/* enable double clock */
	ov518_i2c_w(sd, OV7670_R11_CLKRC, clock);
}

static void setautogain(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w_mask(sd, 0x13, val ? 0x05 : 0x00, 0x05);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam = &gspca_dev->cam;

	sd->bridge = id->driver_info & BRIDGE_MASK;
	sd->invert_led = (id->driver_info & BRIDGE_INVERT_LED) != 0;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		cam->cam_mode = ov511_vga_mode;
		cam->nmodes = ARRAY_SIZE(ov511_vga_mode);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		cam->cam_mode = ov518_vga_mode;
		cam->nmodes = ARRAY_SIZE(ov518_vga_mode);
		break;
	case BRIDGE_OV519:
		cam->cam_mode = ov519_vga_mode;
		cam->nmodes = ARRAY_SIZE(ov519_vga_mode);
		break;
	case BRIDGE_OVFX2:
		cam->cam_mode = ov519_vga_mode;
		cam->nmodes = ARRAY_SIZE(ov519_vga_mode);
		cam->bulk_size = OVFX2_BULK_SIZE;
		cam->bulk_nurbs = MAX_NURBS;
		cam->bulk = 1;
		break;
	case BRIDGE_W9968CF:
		cam->cam_mode = w9968cf_vga_mode;
		cam->nmodes = ARRAY_SIZE(w9968cf_vga_mode);
		break;
	}

	sd->frame_rate = 15;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam = &gspca_dev->cam;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		ov511_configure(gspca_dev);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		ov518_configure(gspca_dev);
		break;
	case BRIDGE_OV519:
		ov519_configure(sd);
		break;
	case BRIDGE_OVFX2:
		ovfx2_configure(sd);
		break;
	case BRIDGE_W9968CF:
		w9968cf_configure(sd);
		break;
	}

	/* The OV519 must be more aggressive about sensor detection since
	 * I2C write will never fail if the sensor is not present. We have
	 * to try to initialize the sensor to detect its presence */
	sd->sensor = -1;

	/* Test for 76xx */
	if (init_ov_sensor(sd, OV7xx0_SID) >= 0) {
		ov7xx0_configure(sd);

	/* Test for 6xx0 */
	} else if (init_ov_sensor(sd, OV6xx0_SID) >= 0) {
		ov6xx0_configure(sd);

	/* Test for 8xx0 */
	} else if (init_ov_sensor(sd, OV8xx0_SID) >= 0) {
		ov8xx0_configure(sd);

	/* Test for 3xxx / 2xxx */
	} else if (init_ov_sensor(sd, OV_HIRES_SID) >= 0) {
		ov_hires_configure(sd);
	} else {
		gspca_err(gspca_dev, "Can't determine sensor slave IDs\n");
		goto error;
	}

	if (sd->sensor < 0)
		goto error;

	ov51x_led_control(sd, 0);	/* turn LED off */

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		if (sd->sif) {
			cam->cam_mode = ov511_sif_mode;
			cam->nmodes = ARRAY_SIZE(ov511_sif_mode);
		}
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		if (sd->sif) {
			cam->cam_mode = ov518_sif_mode;
			cam->nmodes = ARRAY_SIZE(ov518_sif_mode);
		}
		break;
	case BRIDGE_OV519:
		if (sd->sif) {
			cam->cam_mode = ov519_sif_mode;
			cam->nmodes = ARRAY_SIZE(ov519_sif_mode);
		}
		break;
	case BRIDGE_OVFX2:
		switch (sd->sensor) {
		case SEN_OV2610:
		case SEN_OV2610AE:
			cam->cam_mode = ovfx2_ov2610_mode;
			cam->nmodes = ARRAY_SIZE(ovfx2_ov2610_mode);
			break;
		case SEN_OV3610:
			cam->cam_mode = ovfx2_ov3610_mode;
			cam->nmodes = ARRAY_SIZE(ovfx2_ov3610_mode);
			break;
		case SEN_OV9600:
			cam->cam_mode = ovfx2_ov9600_mode;
			cam->nmodes = ARRAY_SIZE(ovfx2_ov9600_mode);
			break;
		default:
			if (sd->sif) {
				cam->cam_mode = ov519_sif_mode;
				cam->nmodes = ARRAY_SIZE(ov519_sif_mode);
			}
			break;
		}
		break;
	case BRIDGE_W9968CF:
		if (sd->sif)
			cam->nmodes = ARRAY_SIZE(w9968cf_vga_mode) - 1;

		/* w9968cf needs initialisation once the sensor is known */
		w9968cf_init(sd);
		break;
	}

	/* initialize the sensor */
	switch (sd->sensor) {
	case SEN_OV2610:
		write_i2c_regvals(sd, norm_2610, ARRAY_SIZE(norm_2610));

		/* Enable autogain, autoexpo, awb, bandfilter */
		i2c_w_mask(sd, 0x13, 0x27, 0x27);
		break;
	case SEN_OV2610AE:
		write_i2c_regvals(sd, norm_2610ae, ARRAY_SIZE(norm_2610ae));

		/* enable autoexpo */
		i2c_w_mask(sd, 0x13, 0x05, 0x05);
		break;
	case SEN_OV3610:
		write_i2c_regvals(sd, norm_3620b, ARRAY_SIZE(norm_3620b));

		/* Enable autogain, autoexpo, awb, bandfilter */
		i2c_w_mask(sd, 0x13, 0x27, 0x27);
		break;
	case SEN_OV6620:
		write_i2c_regvals(sd, norm_6x20, ARRAY_SIZE(norm_6x20));
		break;
	case SEN_OV6630:
	case SEN_OV66308AF:
		write_i2c_regvals(sd, norm_6x30, ARRAY_SIZE(norm_6x30));
		break;
	default:
/*	case SEN_OV7610: */
/*	case SEN_OV76BE: */
		write_i2c_regvals(sd, norm_7610, ARRAY_SIZE(norm_7610));
		i2c_w_mask(sd, 0x0e, 0x00, 0x40);
		break;
	case SEN_OV7620:
	case SEN_OV7620AE:
		write_i2c_regvals(sd, norm_7620, ARRAY_SIZE(norm_7620));
		break;
	case SEN_OV7640:
	case SEN_OV7648:
		write_i2c_regvals(sd, norm_7640, ARRAY_SIZE(norm_7640));
		break;
	case SEN_OV7660:
		i2c_w(sd, OV7670_R12_COM7, OV7670_COM7_RESET);
		msleep(14);
		reg_w(sd, OV519_R57_SNAPSHOT, 0x23);
		write_regvals(sd, init_519_ov7660,
				ARRAY_SIZE(init_519_ov7660));
		write_i2c_regvals(sd, norm_7660, ARRAY_SIZE(norm_7660));
		sd->gspca_dev.curr_mode = 1;	/* 640x480 */
		ov519_set_mode(sd);
		ov519_set_fr(sd);
		sd_reset_snapshot(gspca_dev);
		ov51x_restart(sd);
		ov51x_stop(sd);			/* not in win traces */
		ov51x_led_control(sd, 0);
		break;
	case SEN_OV7670:
		write_i2c_regvals(sd, norm_7670, ARRAY_SIZE(norm_7670));
		break;
	case SEN_OV8610:
		write_i2c_regvals(sd, norm_8610, ARRAY_SIZE(norm_8610));
		break;
	case SEN_OV9600:
		write_i2c_regvals(sd, norm_9600, ARRAY_SIZE(norm_9600));

		/* enable autoexpo */
/*		i2c_w_mask(sd, 0x13, 0x05, 0x05); */
		break;
	}
	return gspca_dev->usb_err;
error:
	gspca_err(gspca_dev, "OV519 Config failed\n");
	return -EINVAL;
}

/* function called at start time before URB creation */
static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->bridge) {
	case BRIDGE_OVFX2:
		if (gspca_dev->pixfmt.width != 800)
			gspca_dev->cam.bulk_size = OVFX2_BULK_SIZE;
		else
			gspca_dev->cam.bulk_size = 7 * 4096;
		break;
	}
	return 0;
}

/* Set up the OV511/OV511+ with the given image parameters.
 *
 * Do not put any sensor-specific code in here (including I2C I/O functions)
 */
static void ov511_mode_init_regs(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int hsegs, vsegs, packet_size, fps, needed;
	int interlaced = 0;
	struct usb_host_interface *alt;
	struct usb_interface *intf;

	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt) {
		gspca_err(gspca_dev, "Couldn't get altsetting\n");
		sd->gspca_dev.usb_err = -EIO;
		return;
	}

	if (alt->desc.bNumEndpoints < 1) {
		sd->gspca_dev.usb_err = -ENODEV;
		return;
	}

	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
	reg_w(sd, R51x_FIFO_PSIZE, packet_size >> 5);

	reg_w(sd, R511_CAM_UV_EN, 0x01);
	reg_w(sd, R511_SNAP_UV_EN, 0x01);
	reg_w(sd, R511_SNAP_OPTS, 0x03);

	/* Here I'm assuming that snapshot size == image size.
	 * I hope that's always true. --claudio
	 */
	hsegs = (sd->gspca_dev.pixfmt.width >> 3) - 1;
	vsegs = (sd->gspca_dev.pixfmt.height >> 3) - 1;

	reg_w(sd, R511_CAM_PXCNT, hsegs);
	reg_w(sd, R511_CAM_LNCNT, vsegs);
	reg_w(sd, R511_CAM_PXDIV, 0x00);
	reg_w(sd, R511_CAM_LNDIV, 0x00);

	/* YUV420, low pass filter on */
	reg_w(sd, R511_CAM_OPTS, 0x03);

	/* Snapshot additions */
	reg_w(sd, R511_SNAP_PXCNT, hsegs);
	reg_w(sd, R511_SNAP_LNCNT, vsegs);
	reg_w(sd, R511_SNAP_PXDIV, 0x00);
	reg_w(sd, R511_SNAP_LNDIV, 0x00);

	/******** Set the framerate ********/
	if (frame_rate > 0)
		sd->frame_rate = frame_rate;

	switch (sd->sensor) {
	case SEN_OV6620:
		/* No framerate control, doesn't like higher rates yet */
		sd->clockdiv = 3;
		break;

	/* Note once the FIXME's in mode_init_ov_sensor_regs() are fixed
	   for more sensors we need to do this for them too */
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV7640:
	case SEN_OV7648:
	case SEN_OV76BE:
		if (sd->gspca_dev.pixfmt.width == 320)
			interlaced = 1;
		/* Fall through */
	case SEN_OV6630:
	case SEN_OV7610:
	case SEN_OV7670:
		switch (sd->frame_rate) {
		case 30:
		case 25:
			/* Not enough bandwidth to do 640x480 @ 30 fps */
			if (sd->gspca_dev.pixfmt.width != 640) {
				sd->clockdiv = 0;
				break;
			}
			/* For 640x480 case */
			/* fall through */
		default:
/*		case 20: */
/*		case 15: */
			sd->clockdiv = 1;
			break;
		case 10:
			sd->clockdiv = 2;
			break;
		case 5:
			sd->clockdiv = 5;
			break;
		}
		if (interlaced) {
			sd->clockdiv = (sd->clockdiv + 1) * 2 - 1;
			/* Higher then 10 does not work */
			if (sd->clockdiv > 10)
				sd->clockdiv = 10;
		}
		break;

	case SEN_OV8610:
		/* No framerate control ?? */
		sd->clockdiv = 0;
		break;
	}

	/* Check if we have enough bandwidth to disable compression */
	fps = (interlaced ? 60 : 30) / (sd->clockdiv + 1) + 1;
	needed = fps * sd->gspca_dev.pixfmt.width *
			sd->gspca_dev.pixfmt.height * 3 / 2;
	/* 1000 isoc packets/sec */
	if (needed > 1000 * packet_size) {
		/* Enable Y and UV quantization and compression */
		reg_w(sd, R511_COMP_EN, 0x07);
		reg_w(sd, R511_COMP_LUT_EN, 0x03);
	} else {
		reg_w(sd, R511_COMP_EN, 0x06);
		reg_w(sd, R511_COMP_LUT_EN, 0x00);
	}

	reg_w(sd, R51x_SYS_RESET, OV511_RESET_OMNICE);
	reg_w(sd, R51x_SYS_RESET, 0);
}

/* Sets up the OV518/OV518+ with the given image parameters
 *
 * OV518 needs a completely different approach, until we can figure out what
 * the individual registers do. Also, only 15 FPS is supported now.
 *
 * Do not put any sensor-specific code in here (including I2C I/O functions)
 */
static void ov518_mode_init_regs(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int hsegs, vsegs, packet_size;
	struct usb_host_interface *alt;
	struct usb_interface *intf;

	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt) {
		gspca_err(gspca_dev, "Couldn't get altsetting\n");
		sd->gspca_dev.usb_err = -EIO;
		return;
	}

	if (alt->desc.bNumEndpoints < 1) {
		sd->gspca_dev.usb_err = -ENODEV;
		return;
	}

	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
	ov518_reg_w32(sd, R51x_FIFO_PSIZE, packet_size & ~7, 2);

	/******** Set the mode ********/
	reg_w(sd, 0x2b, 0);
	reg_w(sd, 0x2c, 0);
	reg_w(sd, 0x2d, 0);
	reg_w(sd, 0x2e, 0);
	reg_w(sd, 0x3b, 0);
	reg_w(sd, 0x3c, 0);
	reg_w(sd, 0x3d, 0);
	reg_w(sd, 0x3e, 0);

	if (sd->bridge == BRIDGE_OV518) {
		/* Set 8-bit (YVYU) input format */
		reg_w_mask(sd, 0x20, 0x08, 0x08);

		/* Set 12-bit (4:2:0) output format */
		reg_w_mask(sd, 0x28, 0x80, 0xf0);
		reg_w_mask(sd, 0x38, 0x80, 0xf0);
	} else {
		reg_w(sd, 0x28, 0x80);
		reg_w(sd, 0x38, 0x80);
	}

	hsegs = sd->gspca_dev.pixfmt.width / 16;
	vsegs = sd->gspca_dev.pixfmt.height / 4;

	reg_w(sd, 0x29, hsegs);
	reg_w(sd, 0x2a, vsegs);

	reg_w(sd, 0x39, hsegs);
	reg_w(sd, 0x3a, vsegs);

	/* Windows driver does this here; who knows why */
	reg_w(sd, 0x2f, 0x80);

	/******** Set the framerate ********/
	if (sd->bridge == BRIDGE_OV518PLUS && sd->revision == 0 &&
					      sd->sensor == SEN_OV7620AE)
		sd->clockdiv = 0;
	else
		sd->clockdiv = 1;

	/* Mode independent, but framerate dependent, regs */
	/* 0x51: Clock divider; Only works on some cams which use 2 crystals */
	reg_w(sd, 0x51, 0x04);
	reg_w(sd, 0x22, 0x18);
	reg_w(sd, 0x23, 0xff);

	if (sd->bridge == BRIDGE_OV518PLUS) {
		switch (sd->sensor) {
		case SEN_OV7620AE:
			/*
			 * HdG: 640x480 needs special handling on device
			 * revision 2, we check for device revison > 0 to
			 * avoid regressions, as we don't know the correct
			 * thing todo for revision 1.
			 *
			 * Also this likely means we don't need to
			 * differentiate between the OV7620 and OV7620AE,
			 * earlier testing hitting this same problem likely
			 * happened to be with revision < 2 cams using an
			 * OV7620 and revision 2 cams using an OV7620AE.
			 */
			if (sd->revision > 0 &&
					sd->gspca_dev.pixfmt.width == 640) {
				reg_w(sd, 0x20, 0x60);
				reg_w(sd, 0x21, 0x1f);
			} else {
				reg_w(sd, 0x20, 0x00);
				reg_w(sd, 0x21, 0x19);
			}
			break;
		case SEN_OV7620:
			reg_w(sd, 0x20, 0x00);
			reg_w(sd, 0x21, 0x19);
			break;
		default:
			reg_w(sd, 0x21, 0x19);
		}
	} else
		reg_w(sd, 0x71, 0x17);	/* Compression-related? */

	/* FIXME: Sensor-specific */
	/* Bit 5 is what matters here. Of course, it is "reserved" */
	i2c_w(sd, 0x54, 0x23);

	reg_w(sd, 0x2f, 0x80);

	if (sd->bridge == BRIDGE_OV518PLUS) {
		reg_w(sd, 0x24, 0x94);
		reg_w(sd, 0x25, 0x90);
		ov518_reg_w32(sd, 0xc4,    400, 2);	/* 190h   */
		ov518_reg_w32(sd, 0xc6,    540, 2);	/* 21ch   */
		ov518_reg_w32(sd, 0xc7,    540, 2);	/* 21ch   */
		ov518_reg_w32(sd, 0xc8,    108, 2);	/* 6ch    */
		ov518_reg_w32(sd, 0xca, 131098, 3);	/* 2001ah */
		ov518_reg_w32(sd, 0xcb,    532, 2);	/* 214h   */
		ov518_reg_w32(sd, 0xcc,   2400, 2);	/* 960h   */
		ov518_reg_w32(sd, 0xcd,     32, 2);	/* 20h    */
		ov518_reg_w32(sd, 0xce,    608, 2);	/* 260h   */
	} else {
		reg_w(sd, 0x24, 0x9f);
		reg_w(sd, 0x25, 0x90);
		ov518_reg_w32(sd, 0xc4,    400, 2);	/* 190h   */
		ov518_reg_w32(sd, 0xc6,    381, 2);	/* 17dh   */
		ov518_reg_w32(sd, 0xc7,    381, 2);	/* 17dh   */
		ov518_reg_w32(sd, 0xc8,    128, 2);	/* 80h    */
		ov518_reg_w32(sd, 0xca, 183331, 3);	/* 2cc23h */
		ov518_reg_w32(sd, 0xcb,    746, 2);	/* 2eah   */
		ov518_reg_w32(sd, 0xcc,   1750, 2);	/* 6d6h   */
		ov518_reg_w32(sd, 0xcd,     45, 2);	/* 2dh    */
		ov518_reg_w32(sd, 0xce,    851, 2);	/* 353h   */
	}

	reg_w(sd, 0x2f, 0x80);
}

/* Sets up the OV519 with the given image parameters
 *
 * OV519 needs a completely different approach, until we can figure out what
 * the individual registers do.
 *
 * Do not put any sensor-specific code in here (including I2C I/O functions)
 */
static void ov519_mode_init_regs(struct sd *sd)
{
	static const struct ov_regvals mode_init_519_ov7670[] = {
		{ 0x5d,	0x03 }, /* Turn off suspend mode */
		{ 0x53,	0x9f }, /* was 9b in 1.65-1.08 */
		{ OV519_R54_EN_CLK1, 0x0f }, /* bit2 (jpeg enable) */
		{ 0xa2,	0x20 }, /* a2-a5 are undocumented */
		{ 0xa3,	0x18 },
		{ 0xa4,	0x04 },
		{ 0xa5,	0x28 },
		{ 0x37,	0x00 },	/* SetUsbInit */
		{ 0x55,	0x02 }, /* 4.096 Mhz audio clock */
		/* Enable both fields, YUV Input, disable defect comp (why?) */
		{ 0x20,	0x0c },
		{ 0x21,	0x38 },
		{ 0x22,	0x1d },
		{ 0x17,	0x50 }, /* undocumented */
		{ 0x37,	0x00 }, /* undocumented */
		{ 0x40,	0xff }, /* I2C timeout counter */
		{ 0x46,	0x00 }, /* I2C clock prescaler */
		{ 0x59,	0x04 },	/* new from windrv 090403 */
		{ 0xff,	0x00 }, /* undocumented */
		/* windows reads 0x55 at this point, why? */
	};

	static const struct ov_regvals mode_init_519[] = {
		{ 0x5d,	0x03 }, /* Turn off suspend mode */
		{ 0x53,	0x9f }, /* was 9b in 1.65-1.08 */
		{ OV519_R54_EN_CLK1, 0x0f }, /* bit2 (jpeg enable) */
		{ 0xa2,	0x20 }, /* a2-a5 are undocumented */
		{ 0xa3,	0x18 },
		{ 0xa4,	0x04 },
		{ 0xa5,	0x28 },
		{ 0x37,	0x00 },	/* SetUsbInit */
		{ 0x55,	0x02 }, /* 4.096 Mhz audio clock */
		/* Enable both fields, YUV Input, disable defect comp (why?) */
		{ 0x22,	0x1d },
		{ 0x17,	0x50 }, /* undocumented */
		{ 0x37,	0x00 }, /* undocumented */
		{ 0x40,	0xff }, /* I2C timeout counter */
		{ 0x46,	0x00 }, /* I2C clock prescaler */
		{ 0x59,	0x04 },	/* new from windrv 090403 */
		{ 0xff,	0x00 }, /* undocumented */
		/* windows reads 0x55 at this point, why? */
	};

	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	/******** Set the mode ********/
	switch (sd->sensor) {
	default:
		write_regvals(sd, mode_init_519, ARRAY_SIZE(mode_init_519));
		if (sd->sensor == SEN_OV7640 ||
		    sd->sensor == SEN_OV7648) {
			/* Select 8-bit input mode */
			reg_w_mask(sd, OV519_R20_DFR, 0x10, 0x10);
		}
		break;
	case SEN_OV7660:
		return;		/* done by ov519_set_mode/fr() */
	case SEN_OV7670:
		write_regvals(sd, mode_init_519_ov7670,
				ARRAY_SIZE(mode_init_519_ov7670));
		break;
	}

	reg_w(sd, OV519_R10_H_SIZE,	sd->gspca_dev.pixfmt.width >> 4);
	reg_w(sd, OV519_R11_V_SIZE,	sd->gspca_dev.pixfmt.height >> 3);
	if (sd->sensor == SEN_OV7670 &&
	    sd->gspca_dev.cam.cam_mode[sd->gspca_dev.curr_mode].priv)
		reg_w(sd, OV519_R12_X_OFFSETL, 0x04);
	else if (sd->sensor == SEN_OV7648 &&
	    sd->gspca_dev.cam.cam_mode[sd->gspca_dev.curr_mode].priv)
		reg_w(sd, OV519_R12_X_OFFSETL, 0x01);
	else
		reg_w(sd, OV519_R12_X_OFFSETL, 0x00);
	reg_w(sd, OV519_R13_X_OFFSETH,	0x00);
	reg_w(sd, OV519_R14_Y_OFFSETL,	0x00);
	reg_w(sd, OV519_R15_Y_OFFSETH,	0x00);
	reg_w(sd, OV519_R16_DIVIDER,	0x00);
	reg_w(sd, OV519_R25_FORMAT,	0x03); /* YUV422 */
	reg_w(sd, 0x26,			0x00); /* Undocumented */

	/******** Set the framerate ********/
	if (frame_rate > 0)
		sd->frame_rate = frame_rate;

/* FIXME: These are only valid at the max resolution. */
	sd->clockdiv = 0;
	switch (sd->sensor) {
	case SEN_OV7640:
	case SEN_OV7648:
		switch (sd->frame_rate) {
		default:
/*		case 30: */
			reg_w(sd, 0xa4, 0x0c);
			reg_w(sd, 0x23, 0xff);
			break;
		case 25:
			reg_w(sd, 0xa4, 0x0c);
			reg_w(sd, 0x23, 0x1f);
			break;
		case 20:
			reg_w(sd, 0xa4, 0x0c);
			reg_w(sd, 0x23, 0x1b);
			break;
		case 15:
			reg_w(sd, 0xa4, 0x04);
			reg_w(sd, 0x23, 0xff);
			sd->clockdiv = 1;
			break;
		case 10:
			reg_w(sd, 0xa4, 0x04);
			reg_w(sd, 0x23, 0x1f);
			sd->clockdiv = 1;
			break;
		case 5:
			reg_w(sd, 0xa4, 0x04);
			reg_w(sd, 0x23, 0x1b);
			sd->clockdiv = 1;
			break;
		}
		break;
	case SEN_OV8610:
		switch (sd->frame_rate) {
		default:	/* 15 fps */
/*		case 15: */
			reg_w(sd, 0xa4, 0x06);
			reg_w(sd, 0x23, 0xff);
			break;
		case 10:
			reg_w(sd, 0xa4, 0x06);
			reg_w(sd, 0x23, 0x1f);
			break;
		case 5:
			reg_w(sd, 0xa4, 0x06);
			reg_w(sd, 0x23, 0x1b);
			break;
		}
		break;
	case SEN_OV7670:		/* guesses, based on 7640 */
		gspca_dbg(gspca_dev, D_STREAM, "Setting framerate to %d fps\n",
			  (sd->frame_rate == 0) ? 15 : sd->frame_rate);
		reg_w(sd, 0xa4, 0x10);
		switch (sd->frame_rate) {
		case 30:
			reg_w(sd, 0x23, 0xff);
			break;
		case 20:
			reg_w(sd, 0x23, 0x1b);
			break;
		default:
/*		case 15: */
			reg_w(sd, 0x23, 0xff);
			sd->clockdiv = 1;
			break;
		}
		break;
	}
}

static void mode_init_ov_sensor_regs(struct sd *sd)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int qvga, xstart, xend, ystart, yend;
	u8 v;

	qvga = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv & 1;

	/******** Mode (VGA/QVGA) and sensor specific regs ********/
	switch (sd->sensor) {
	case SEN_OV2610:
		i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x28, qvga ? 0x00 : 0x20, 0x20);
		i2c_w(sd, 0x24, qvga ? 0x20 : 0x3a);
		i2c_w(sd, 0x25, qvga ? 0x30 : 0x60);
		i2c_w_mask(sd, 0x2d, qvga ? 0x40 : 0x00, 0x40);
		i2c_w_mask(sd, 0x67, qvga ? 0xf0 : 0x90, 0xf0);
		i2c_w_mask(sd, 0x74, qvga ? 0x20 : 0x00, 0x20);
		return;
	case SEN_OV2610AE: {
		u8 v;

		/* frame rates:
		 *	10fps / 5 fps for 1600x1200
		 *	40fps / 20fps for 800x600
		 */
		v = 80;
		if (qvga) {
			if (sd->frame_rate < 25)
				v = 0x81;
		} else {
			if (sd->frame_rate < 10)
				v = 0x81;
		}
		i2c_w(sd, 0x11, v);
		i2c_w(sd, 0x12, qvga ? 0x60 : 0x20);
		return;
	    }
	case SEN_OV3610:
		if (qvga) {
			xstart = (1040 - gspca_dev->pixfmt.width) / 2 +
				(0x1f << 4);
			ystart = (776 - gspca_dev->pixfmt.height) / 2;
		} else {
			xstart = (2076 - gspca_dev->pixfmt.width) / 2 +
				(0x10 << 4);
			ystart = (1544 - gspca_dev->pixfmt.height) / 2;
		}
		xend = xstart + gspca_dev->pixfmt.width;
		yend = ystart + gspca_dev->pixfmt.height;
		/* Writing to the COMH register resets the other windowing regs
		   to their default values, so we must do this first. */
		i2c_w_mask(sd, 0x12, qvga ? 0x40 : 0x00, 0xf0);
		i2c_w_mask(sd, 0x32,
			   (((xend >> 1) & 7) << 3) | ((xstart >> 1) & 7),
			   0x3f);
		i2c_w_mask(sd, 0x03,
			   (((yend >> 1) & 3) << 2) | ((ystart >> 1) & 3),
			   0x0f);
		i2c_w(sd, 0x17, xstart >> 4);
		i2c_w(sd, 0x18, xend >> 4);
		i2c_w(sd, 0x19, ystart >> 3);
		i2c_w(sd, 0x1a, yend >> 3);
		return;
	case SEN_OV8610:
		/* For OV8610 qvga means qsvga */
		i2c_w_mask(sd, OV7610_REG_COM_C, qvga ? (1 << 5) : 0, 1 << 5);
		i2c_w_mask(sd, 0x13, 0x00, 0x20); /* Select 16 bit data bus */
		i2c_w_mask(sd, 0x12, 0x04, 0x06); /* AWB: 1 Test pattern: 0 */
		i2c_w_mask(sd, 0x2d, 0x00, 0x40); /* from windrv 090403 */
		i2c_w_mask(sd, 0x28, 0x20, 0x20); /* progressive mode on */
		break;
	case SEN_OV7610:
		i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
		i2c_w(sd, 0x35, qvga ? 0x1e : 0x9e);
		i2c_w_mask(sd, 0x13, 0x00, 0x20); /* Select 16 bit data bus */
		i2c_w_mask(sd, 0x12, 0x04, 0x06); /* AWB: 1 Test pattern: 0 */
		break;
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV76BE:
		i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x28, qvga ? 0x00 : 0x20, 0x20);
		i2c_w(sd, 0x24, qvga ? 0x20 : 0x3a);
		i2c_w(sd, 0x25, qvga ? 0x30 : 0x60);
		i2c_w_mask(sd, 0x2d, qvga ? 0x40 : 0x00, 0x40);
		i2c_w_mask(sd, 0x67, qvga ? 0xb0 : 0x90, 0xf0);
		i2c_w_mask(sd, 0x74, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x13, 0x00, 0x20); /* Select 16 bit data bus */
		i2c_w_mask(sd, 0x12, 0x04, 0x06); /* AWB: 1 Test pattern: 0 */
		if (sd->sensor == SEN_OV76BE)
			i2c_w(sd, 0x35, qvga ? 0x1e : 0x9e);
		break;
	case SEN_OV7640:
	case SEN_OV7648:
		i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x28, qvga ? 0x00 : 0x20, 0x20);
		/* Setting this undocumented bit in qvga mode removes a very
		   annoying vertical shaking of the image */
		i2c_w_mask(sd, 0x2d, qvga ? 0x40 : 0x00, 0x40);
		/* Unknown */
		i2c_w_mask(sd, 0x67, qvga ? 0xf0 : 0x90, 0xf0);
		/* Allow higher automatic gain (to allow higher framerates) */
		i2c_w_mask(sd, 0x74, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x12, 0x04, 0x04); /* AWB: 1 */
		break;
	case SEN_OV7670:
		/* set COM7_FMT_VGA or COM7_FMT_QVGA
		 * do we need to set anything else?
		 *	HSTART etc are set in set_ov_sensor_window itself */
		i2c_w_mask(sd, OV7670_R12_COM7,
			 qvga ? OV7670_COM7_FMT_QVGA : OV7670_COM7_FMT_VGA,
			 OV7670_COM7_FMT_MASK);
		i2c_w_mask(sd, 0x13, 0x00, 0x20); /* Select 16 bit data bus */
		i2c_w_mask(sd, OV7670_R13_COM8, OV7670_COM8_AWB,
				OV7670_COM8_AWB);
		if (qvga) {		/* QVGA from ov7670.c by
					 * Jonathan Corbet */
			xstart = 164;
			xend = 28;
			ystart = 14;
			yend = 494;
		} else {		/* VGA */
			xstart = 158;
			xend = 14;
			ystart = 10;
			yend = 490;
		}
		/* OV7670 hardware window registers are split across
		 * multiple locations */
		i2c_w(sd, OV7670_R17_HSTART, xstart >> 3);
		i2c_w(sd, OV7670_R18_HSTOP, xend >> 3);
		v = i2c_r(sd, OV7670_R32_HREF);
		v = (v & 0xc0) | ((xend & 0x7) << 3) | (xstart & 0x07);
		msleep(10);	/* need to sleep between read and write to
				 * same reg! */
		i2c_w(sd, OV7670_R32_HREF, v);

		i2c_w(sd, OV7670_R19_VSTART, ystart >> 2);
		i2c_w(sd, OV7670_R1A_VSTOP, yend >> 2);
		v = i2c_r(sd, OV7670_R03_VREF);
		v = (v & 0xc0) | ((yend & 0x3) << 2) | (ystart & 0x03);
		msleep(10);	/* need to sleep between read and write to
				 * same reg! */
		i2c_w(sd, OV7670_R03_VREF, v);
		break;
	case SEN_OV6620:
		i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x13, 0x00, 0x20); /* Select 16 bit data bus */
		i2c_w_mask(sd, 0x12, 0x04, 0x06); /* AWB: 1 Test pattern: 0 */
		break;
	case SEN_OV6630:
	case SEN_OV66308AF:
		i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
		i2c_w_mask(sd, 0x12, 0x04, 0x06); /* AWB: 1 Test pattern: 0 */
		break;
	case SEN_OV9600: {
		const struct ov_i2c_regvals *vals;
		static const struct ov_i2c_regvals sxga_15[] = {
			{0x11, 0x80}, {0x14, 0x3e}, {0x24, 0x85}, {0x25, 0x75}
		};
		static const struct ov_i2c_regvals sxga_7_5[] = {
			{0x11, 0x81}, {0x14, 0x3e}, {0x24, 0x85}, {0x25, 0x75}
		};
		static const struct ov_i2c_regvals vga_30[] = {
			{0x11, 0x81}, {0x14, 0x7e}, {0x24, 0x70}, {0x25, 0x60}
		};
		static const struct ov_i2c_regvals vga_15[] = {
			{0x11, 0x83}, {0x14, 0x3e}, {0x24, 0x80}, {0x25, 0x70}
		};

		/* frame rates:
		 *	15fps / 7.5 fps for 1280x1024
		 *	30fps / 15fps for 640x480
		 */
		i2c_w_mask(sd, 0x12, qvga ? 0x40 : 0x00, 0x40);
		if (qvga)
			vals = sd->frame_rate < 30 ? vga_15 : vga_30;
		else
			vals = sd->frame_rate < 15 ? sxga_7_5 : sxga_15;
		write_i2c_regvals(sd, vals, ARRAY_SIZE(sxga_15));
		return;
	    }
	default:
		return;
	}

	/******** Clock programming ********/
	i2c_w(sd, 0x11, sd->clockdiv);
}

/* this function works for bridge ov519 and sensors ov7660 and ov7670 only */
static void sethvflip(struct gspca_dev *gspca_dev, s32 hflip, s32 vflip)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->gspca_dev.streaming)
		reg_w(sd, OV519_R51_RESET1, 0x0f);	/* block stream */
	i2c_w_mask(sd, OV7670_R1E_MVFP,
		OV7670_MVFP_MIRROR * hflip | OV7670_MVFP_VFLIP * vflip,
		OV7670_MVFP_MIRROR | OV7670_MVFP_VFLIP);
	if (sd->gspca_dev.streaming)
		reg_w(sd, OV519_R51_RESET1, 0x00);	/* restart stream */
}

static void set_ov_sensor_window(struct sd *sd)
{
	struct gspca_dev *gspca_dev;
	int qvga, crop;
	int hwsbase, hwebase, vwsbase, vwebase, hwscale, vwscale;

	/* mode setup is fully handled in mode_init_ov_sensor_regs for these */
	switch (sd->sensor) {
	case SEN_OV2610:
	case SEN_OV2610AE:
	case SEN_OV3610:
	case SEN_OV7670:
	case SEN_OV9600:
		mode_init_ov_sensor_regs(sd);
		return;
	case SEN_OV7660:
		ov519_set_mode(sd);
		ov519_set_fr(sd);
		return;
	}

	gspca_dev = &sd->gspca_dev;
	qvga = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv & 1;
	crop = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv & 2;

	/* The different sensor ICs handle setting up of window differently.
	 * IF YOU SET IT WRONG, YOU WILL GET ALL ZERO ISOC DATA FROM OV51x!! */
	switch (sd->sensor) {
	case SEN_OV8610:
		hwsbase = 0x1e;
		hwebase = 0x1e;
		vwsbase = 0x02;
		vwebase = 0x02;
		break;
	case SEN_OV7610:
	case SEN_OV76BE:
		hwsbase = 0x38;
		hwebase = 0x3a;
		vwsbase = vwebase = 0x05;
		break;
	case SEN_OV6620:
	case SEN_OV6630:
	case SEN_OV66308AF:
		hwsbase = 0x38;
		hwebase = 0x3a;
		vwsbase = 0x05;
		vwebase = 0x06;
		if (sd->sensor == SEN_OV66308AF && qvga)
			/* HDG: this fixes U and V getting swapped */
			hwsbase++;
		if (crop) {
			hwsbase += 8;
			hwebase += 8;
			vwsbase += 11;
			vwebase += 11;
		}
		break;
	case SEN_OV7620:
	case SEN_OV7620AE:
		hwsbase = 0x2f;		/* From 7620.SET (spec is wrong) */
		hwebase = 0x2f;
		vwsbase = vwebase = 0x05;
		break;
	case SEN_OV7640:
	case SEN_OV7648:
		hwsbase = 0x1a;
		hwebase = 0x1a;
		vwsbase = vwebase = 0x03;
		break;
	default:
		return;
	}

	switch (sd->sensor) {
	case SEN_OV6620:
	case SEN_OV6630:
	case SEN_OV66308AF:
		if (qvga) {		/* QCIF */
			hwscale = 0;
			vwscale = 0;
		} else {		/* CIF */
			hwscale = 1;
			vwscale = 1;	/* The datasheet says 0;
					 * it's wrong */
		}
		break;
	case SEN_OV8610:
		if (qvga) {		/* QSVGA */
			hwscale = 1;
			vwscale = 1;
		} else {		/* SVGA */
			hwscale = 2;
			vwscale = 2;
		}
		break;
	default:			/* SEN_OV7xx0 */
		if (qvga) {		/* QVGA */
			hwscale = 1;
			vwscale = 0;
		} else {		/* VGA */
			hwscale = 2;
			vwscale = 1;
		}
	}

	mode_init_ov_sensor_regs(sd);

	i2c_w(sd, 0x17, hwsbase);
	i2c_w(sd, 0x18, hwebase + (sd->sensor_width >> hwscale));
	i2c_w(sd, 0x19, vwsbase);
	i2c_w(sd, 0x1a, vwebase + (sd->sensor_height >> vwscale));
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* Default for most bridges, allow bridge_mode_init_regs to override */
	sd->sensor_width = sd->gspca_dev.pixfmt.width;
	sd->sensor_height = sd->gspca_dev.pixfmt.height;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		ov511_mode_init_regs(sd);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		ov518_mode_init_regs(sd);
		break;
	case BRIDGE_OV519:
		ov519_mode_init_regs(sd);
		break;
	/* case BRIDGE_OVFX2: nothing to do */
	case BRIDGE_W9968CF:
		w9968cf_mode_init_regs(sd);
		break;
	}

	set_ov_sensor_window(sd);

	/* Force clear snapshot state in case the snapshot button was
	   pressed while we weren't streaming */
	sd->snapshot_needs_reset = 1;
	sd_reset_snapshot(gspca_dev);

	sd->first_frame = 3;

	ov51x_restart(sd);
	ov51x_led_control(sd, 1);
	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	ov51x_stop(sd);
	ov51x_led_control(sd, 0);
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!sd->gspca_dev.present)
		return;
	if (sd->bridge == BRIDGE_W9968CF)
		w9968cf_stop0(sd);

#if IS_ENABLED(CONFIG_INPUT)
	/* If the last button state is pressed, release it now! */
	if (sd->snapshot_pressed) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		sd->snapshot_pressed = 0;
	}
#endif
	if (sd->bridge == BRIDGE_OV519)
		reg_w(sd, OV519_R57_SNAPSHOT, 0x23);
}

static void ov51x_handle_button(struct gspca_dev *gspca_dev, u8 state)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->snapshot_pressed != state) {
#if IS_ENABLED(CONFIG_INPUT)
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, state);
		input_sync(gspca_dev->input_dev);
#endif
		if (state)
			sd->snapshot_needs_reset = 1;

		sd->snapshot_pressed = state;
	} else {
		/* On the ov511 / ov519 we need to reset the button state
		   multiple times, as resetting does not work as long as the
		   button stays pressed */
		switch (sd->bridge) {
		case BRIDGE_OV511:
		case BRIDGE_OV511PLUS:
		case BRIDGE_OV519:
			if (state)
				sd->snapshot_needs_reset = 1;
			break;
		}
	}
}

static void ov511_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *in,			/* isoc packet */
			int len)		/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* SOF/EOF packets have 1st to 8th bytes zeroed and the 9th
	 * byte non-zero. The EOF packet has image width/height in the
	 * 10th and 11th bytes. The 9th byte is given as follows:
	 *
	 * bit 7: EOF
	 *     6: compression enabled
	 *     5: 422/420/400 modes
	 *     4: 422/420/400 modes
	 *     3: 1
	 *     2: snapshot button on
	 *     1: snapshot frame
	 *     0: even/odd field
	 */
	if (!(in[0] | in[1] | in[2] | in[3] | in[4] | in[5] | in[6] | in[7]) &&
	    (in[8] & 0x08)) {
		ov51x_handle_button(gspca_dev, (in[8] >> 2) & 1);
		if (in[8] & 0x80) {
			/* Frame end */
			if ((in[9] + 1) * 8 != gspca_dev->pixfmt.width ||
			    (in[10] + 1) * 8 != gspca_dev->pixfmt.height) {
				gspca_err(gspca_dev, "Invalid frame size, got: %dx%d, requested: %dx%d\n",
					  (in[9] + 1) * 8, (in[10] + 1) * 8,
					  gspca_dev->pixfmt.width,
					  gspca_dev->pixfmt.height);
				gspca_dev->last_packet_type = DISCARD_PACKET;
				return;
			}
			/* Add 11 byte footer to frame, might be useful */
			gspca_frame_add(gspca_dev, LAST_PACKET, in, 11);
			return;
		} else {
			/* Frame start */
			gspca_frame_add(gspca_dev, FIRST_PACKET, in, 0);
			sd->packet_nr = 0;
		}
	}

	/* Ignore the packet number */
	len--;

	/* intermediate packet */
	gspca_frame_add(gspca_dev, INTER_PACKET, in, len);
}

static void ov518_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* A false positive here is likely, until OVT gives me
	 * the definitive SOF/EOF format */
	if ((!(data[0] | data[1] | data[2] | data[3] | data[5])) && data[6]) {
		ov51x_handle_button(gspca_dev, (data[6] >> 1) & 1);
		gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
		gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
		sd->packet_nr = 0;
	}

	if (gspca_dev->last_packet_type == DISCARD_PACKET)
		return;

	/* Does this device use packet numbers ? */
	if (len & 7) {
		len--;
		if (sd->packet_nr == data[len])
			sd->packet_nr++;
		/* The last few packets of the frame (which are all 0's
		   except that they may contain part of the footer), are
		   numbered 0 */
		else if (sd->packet_nr == 0 || data[len]) {
			gspca_err(gspca_dev, "Invalid packet nr: %d (expect: %d)\n",
				  (int)data[len], (int)sd->packet_nr);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			return;
		}
	}

	/* intermediate packet */
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static void ov519_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	/* Header of ov519 is 16 bytes:
	 *     Byte     Value      Description
	 *	0	0xff	magic
	 *	1	0xff	magic
	 *	2	0xff	magic
	 *	3	0xXX	0x50 = SOF, 0x51 = EOF
	 *	9	0xXX	0x01 initial frame without data,
	 *			0x00 standard frame with image
	 *	14	Lo	in EOF: length of image data / 8
	 *	15	Hi
	 */

	if (data[0] == 0xff && data[1] == 0xff && data[2] == 0xff) {
		switch (data[3]) {
		case 0x50:		/* start of frame */
			/* Don't check the button state here, as the state
			   usually (always ?) changes at EOF and checking it
			   here leads to unnecessary snapshot state resets. */
#define HDRSZ 16
			data += HDRSZ;
			len -= HDRSZ;
#undef HDRSZ
			if (data[0] == 0xff || data[1] == 0xd8)
				gspca_frame_add(gspca_dev, FIRST_PACKET,
						data, len);
			else
				gspca_dev->last_packet_type = DISCARD_PACKET;
			return;
		case 0x51:		/* end of frame */
			ov51x_handle_button(gspca_dev, data[11] & 1);
			if (data[9] != 0)
				gspca_dev->last_packet_type = DISCARD_PACKET;
			gspca_frame_add(gspca_dev, LAST_PACKET,
					NULL, 0);
			return;
		}
	}

	/* intermediate packet */
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static void ovfx2_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);

	/* A short read signals EOF */
	if (len < gspca_dev->cam.bulk_size) {
		/* If the frame is short, and it is one of the first ones
		   the sensor and bridge are still syncing, so drop it. */
		if (sd->first_frame) {
			sd->first_frame--;
			if (gspca_dev->image_len <
				  sd->gspca_dev.pixfmt.width *
					sd->gspca_dev.pixfmt.height)
				gspca_dev->last_packet_type = DISCARD_PACKET;
		}
		gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
		gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
	}
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->bridge) {
	case BRIDGE_OV511:
	case BRIDGE_OV511PLUS:
		ov511_pkt_scan(gspca_dev, data, len);
		break;
	case BRIDGE_OV518:
	case BRIDGE_OV518PLUS:
		ov518_pkt_scan(gspca_dev, data, len);
		break;
	case BRIDGE_OV519:
		ov519_pkt_scan(gspca_dev, data, len);
		break;
	case BRIDGE_OVFX2:
		ovfx2_pkt_scan(gspca_dev, data, len);
		break;
	case BRIDGE_W9968CF:
		w9968cf_pkt_scan(gspca_dev, data, len);
		break;
	}
}

/* -- management routines -- */

static void setbrightness(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	static const struct ov_i2c_regvals brit_7660[][7] = {
		{{0x0f, 0x6a}, {0x24, 0x40}, {0x25, 0x2b}, {0x26, 0x90},
			{0x27, 0xe0}, {0x28, 0xe0}, {0x2c, 0xe0}},
		{{0x0f, 0x6a}, {0x24, 0x50}, {0x25, 0x40}, {0x26, 0xa1},
			{0x27, 0xc0}, {0x28, 0xc0}, {0x2c, 0xc0}},
		{{0x0f, 0x6a}, {0x24, 0x68}, {0x25, 0x58}, {0x26, 0xc2},
			{0x27, 0xa0}, {0x28, 0xa0}, {0x2c, 0xa0}},
		{{0x0f, 0x6a}, {0x24, 0x70}, {0x25, 0x68}, {0x26, 0xd3},
			{0x27, 0x80}, {0x28, 0x80}, {0x2c, 0x80}},
		{{0x0f, 0x6a}, {0x24, 0x80}, {0x25, 0x70}, {0x26, 0xd3},
			{0x27, 0x20}, {0x28, 0x20}, {0x2c, 0x20}},
		{{0x0f, 0x6a}, {0x24, 0x88}, {0x25, 0x78}, {0x26, 0xd3},
			{0x27, 0x40}, {0x28, 0x40}, {0x2c, 0x40}},
		{{0x0f, 0x6a}, {0x24, 0x90}, {0x25, 0x80}, {0x26, 0xd4},
			{0x27, 0x60}, {0x28, 0x60}, {0x2c, 0x60}}
	};

	switch (sd->sensor) {
	case SEN_OV8610:
	case SEN_OV7610:
	case SEN_OV76BE:
	case SEN_OV6620:
	case SEN_OV6630:
	case SEN_OV66308AF:
	case SEN_OV7640:
	case SEN_OV7648:
		i2c_w(sd, OV7610_REG_BRT, val);
		break;
	case SEN_OV7620:
	case SEN_OV7620AE:
		i2c_w(sd, OV7610_REG_BRT, val);
		break;
	case SEN_OV7660:
		write_i2c_regvals(sd, brit_7660[val],
				ARRAY_SIZE(brit_7660[0]));
		break;
	case SEN_OV7670:
/*win trace
 *		i2c_w_mask(sd, OV7670_R13_COM8, 0, OV7670_COM8_AEC); */
		i2c_w(sd, OV7670_R55_BRIGHT, ov7670_abs_to_sm(val));
		break;
	}
}

static void setcontrast(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	static const struct ov_i2c_regvals contrast_7660[][31] = {
		{{0x6c, 0xf0}, {0x6d, 0xf0}, {0x6e, 0xf8}, {0x6f, 0xa0},
		 {0x70, 0x58}, {0x71, 0x38}, {0x72, 0x30}, {0x73, 0x30},
		 {0x74, 0x28}, {0x75, 0x28}, {0x76, 0x24}, {0x77, 0x24},
		 {0x78, 0x22}, {0x79, 0x28}, {0x7a, 0x2a}, {0x7b, 0x34},
		 {0x7c, 0x0f}, {0x7d, 0x1e}, {0x7e, 0x3d}, {0x7f, 0x65},
		 {0x80, 0x70}, {0x81, 0x77}, {0x82, 0x7d}, {0x83, 0x83},
		 {0x84, 0x88}, {0x85, 0x8d}, {0x86, 0x96}, {0x87, 0x9f},
		 {0x88, 0xb0}, {0x89, 0xc4}, {0x8a, 0xd9}},
		{{0x6c, 0xf0}, {0x6d, 0xf0}, {0x6e, 0xf8}, {0x6f, 0x94},
		 {0x70, 0x58}, {0x71, 0x40}, {0x72, 0x30}, {0x73, 0x30},
		 {0x74, 0x30}, {0x75, 0x30}, {0x76, 0x2c}, {0x77, 0x24},
		 {0x78, 0x22}, {0x79, 0x28}, {0x7a, 0x2a}, {0x7b, 0x31},
		 {0x7c, 0x0f}, {0x7d, 0x1e}, {0x7e, 0x3d}, {0x7f, 0x62},
		 {0x80, 0x6d}, {0x81, 0x75}, {0x82, 0x7b}, {0x83, 0x81},
		 {0x84, 0x87}, {0x85, 0x8d}, {0x86, 0x98}, {0x87, 0xa1},
		 {0x88, 0xb2}, {0x89, 0xc6}, {0x8a, 0xdb}},
		{{0x6c, 0xf0}, {0x6d, 0xf0}, {0x6e, 0xf0}, {0x6f, 0x84},
		 {0x70, 0x58}, {0x71, 0x48}, {0x72, 0x40}, {0x73, 0x40},
		 {0x74, 0x28}, {0x75, 0x28}, {0x76, 0x28}, {0x77, 0x24},
		 {0x78, 0x26}, {0x79, 0x28}, {0x7a, 0x28}, {0x7b, 0x34},
		 {0x7c, 0x0f}, {0x7d, 0x1e}, {0x7e, 0x3c}, {0x7f, 0x5d},
		 {0x80, 0x68}, {0x81, 0x71}, {0x82, 0x79}, {0x83, 0x81},
		 {0x84, 0x86}, {0x85, 0x8b}, {0x86, 0x95}, {0x87, 0x9e},
		 {0x88, 0xb1}, {0x89, 0xc5}, {0x8a, 0xd9}},
		{{0x6c, 0xf0}, {0x6d, 0xf0}, {0x6e, 0xf0}, {0x6f, 0x70},
		 {0x70, 0x58}, {0x71, 0x58}, {0x72, 0x48}, {0x73, 0x48},
		 {0x74, 0x38}, {0x75, 0x40}, {0x76, 0x34}, {0x77, 0x34},
		 {0x78, 0x2e}, {0x79, 0x28}, {0x7a, 0x24}, {0x7b, 0x22},
		 {0x7c, 0x0f}, {0x7d, 0x1e}, {0x7e, 0x3c}, {0x7f, 0x58},
		 {0x80, 0x63}, {0x81, 0x6e}, {0x82, 0x77}, {0x83, 0x80},
		 {0x84, 0x87}, {0x85, 0x8f}, {0x86, 0x9c}, {0x87, 0xa9},
		 {0x88, 0xc0}, {0x89, 0xd4}, {0x8a, 0xe6}},
		{{0x6c, 0xa0}, {0x6d, 0xf0}, {0x6e, 0x90}, {0x6f, 0x80},
		 {0x70, 0x70}, {0x71, 0x80}, {0x72, 0x60}, {0x73, 0x60},
		 {0x74, 0x58}, {0x75, 0x60}, {0x76, 0x4c}, {0x77, 0x38},
		 {0x78, 0x38}, {0x79, 0x2a}, {0x7a, 0x20}, {0x7b, 0x0e},
		 {0x7c, 0x0a}, {0x7d, 0x14}, {0x7e, 0x26}, {0x7f, 0x46},
		 {0x80, 0x54}, {0x81, 0x64}, {0x82, 0x70}, {0x83, 0x7c},
		 {0x84, 0x87}, {0x85, 0x93}, {0x86, 0xa6}, {0x87, 0xb4},
		 {0x88, 0xd0}, {0x89, 0xe5}, {0x8a, 0xf5}},
		{{0x6c, 0x60}, {0x6d, 0x80}, {0x6e, 0x60}, {0x6f, 0x80},
		 {0x70, 0x80}, {0x71, 0x80}, {0x72, 0x88}, {0x73, 0x30},
		 {0x74, 0x70}, {0x75, 0x68}, {0x76, 0x64}, {0x77, 0x50},
		 {0x78, 0x3c}, {0x79, 0x22}, {0x7a, 0x10}, {0x7b, 0x08},
		 {0x7c, 0x06}, {0x7d, 0x0e}, {0x7e, 0x1a}, {0x7f, 0x3a},
		 {0x80, 0x4a}, {0x81, 0x5a}, {0x82, 0x6b}, {0x83, 0x7b},
		 {0x84, 0x89}, {0x85, 0x96}, {0x86, 0xaf}, {0x87, 0xc3},
		 {0x88, 0xe1}, {0x89, 0xf2}, {0x8a, 0xfa}},
		{{0x6c, 0x20}, {0x6d, 0x40}, {0x6e, 0x20}, {0x6f, 0x60},
		 {0x70, 0x88}, {0x71, 0xc8}, {0x72, 0xc0}, {0x73, 0xb8},
		 {0x74, 0xa8}, {0x75, 0xb8}, {0x76, 0x80}, {0x77, 0x5c},
		 {0x78, 0x26}, {0x79, 0x10}, {0x7a, 0x08}, {0x7b, 0x04},
		 {0x7c, 0x02}, {0x7d, 0x06}, {0x7e, 0x0a}, {0x7f, 0x22},
		 {0x80, 0x33}, {0x81, 0x4c}, {0x82, 0x64}, {0x83, 0x7b},
		 {0x84, 0x90}, {0x85, 0xa7}, {0x86, 0xc7}, {0x87, 0xde},
		 {0x88, 0xf1}, {0x89, 0xf9}, {0x8a, 0xfd}},
	};

	switch (sd->sensor) {
	case SEN_OV7610:
	case SEN_OV6620:
		i2c_w(sd, OV7610_REG_CNT, val);
		break;
	case SEN_OV6630:
	case SEN_OV66308AF:
		i2c_w_mask(sd, OV7610_REG_CNT, val >> 4, 0x0f);
		break;
	case SEN_OV8610: {
		static const u8 ctab[] = {
			0x03, 0x09, 0x0b, 0x0f, 0x53, 0x6f, 0x35, 0x7f
		};

		/* Use Y gamma control instead. Bit 0 enables it. */
		i2c_w(sd, 0x64, ctab[val >> 5]);
		break;
	    }
	case SEN_OV7620:
	case SEN_OV7620AE: {
		static const u8 ctab[] = {
			0x01, 0x05, 0x09, 0x11, 0x15, 0x35, 0x37, 0x57,
			0x5b, 0xa5, 0xa7, 0xc7, 0xc9, 0xcf, 0xef, 0xff
		};

		/* Use Y gamma control instead. Bit 0 enables it. */
		i2c_w(sd, 0x64, ctab[val >> 4]);
		break;
	    }
	case SEN_OV7660:
		write_i2c_regvals(sd, contrast_7660[val],
					ARRAY_SIZE(contrast_7660[0]));
		break;
	case SEN_OV7670:
		/* check that this isn't just the same as ov7610 */
		i2c_w(sd, OV7670_R56_CONTRAS, val >> 1);
		break;
	}
}

static void setexposure(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w(sd, 0x10, val);
}

static void setcolors(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	static const struct ov_i2c_regvals colors_7660[][6] = {
		{{0x4f, 0x28}, {0x50, 0x2a}, {0x51, 0x02}, {0x52, 0x0a},
		 {0x53, 0x19}, {0x54, 0x23}},
		{{0x4f, 0x47}, {0x50, 0x4a}, {0x51, 0x03}, {0x52, 0x11},
		 {0x53, 0x2c}, {0x54, 0x3e}},
		{{0x4f, 0x66}, {0x50, 0x6b}, {0x51, 0x05}, {0x52, 0x19},
		 {0x53, 0x40}, {0x54, 0x59}},
		{{0x4f, 0x84}, {0x50, 0x8b}, {0x51, 0x06}, {0x52, 0x20},
		 {0x53, 0x53}, {0x54, 0x73}},
		{{0x4f, 0xa3}, {0x50, 0xab}, {0x51, 0x08}, {0x52, 0x28},
		 {0x53, 0x66}, {0x54, 0x8e}},
	};

	switch (sd->sensor) {
	case SEN_OV8610:
	case SEN_OV7610:
	case SEN_OV76BE:
	case SEN_OV6620:
	case SEN_OV6630:
	case SEN_OV66308AF:
		i2c_w(sd, OV7610_REG_SAT, val);
		break;
	case SEN_OV7620:
	case SEN_OV7620AE:
		/* Use UV gamma control instead. Bits 0 & 7 are reserved. */
/*		rc = ov_i2c_write(sd->dev, 0x62, (val >> 9) & 0x7e);
		if (rc < 0)
			goto out; */
		i2c_w(sd, OV7610_REG_SAT, val);
		break;
	case SEN_OV7640:
	case SEN_OV7648:
		i2c_w(sd, OV7610_REG_SAT, val & 0xf0);
		break;
	case SEN_OV7660:
		write_i2c_regvals(sd, colors_7660[val],
					ARRAY_SIZE(colors_7660[0]));
		break;
	case SEN_OV7670:
		/* supported later once I work out how to do it
		 * transparently fail now! */
		/* set REG_COM13 values for UV sat auto mode */
		break;
	}
}

static void setautobright(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w_mask(sd, 0x2d, val ? 0x10 : 0x00, 0x10);
}

static void setfreq_i(struct sd *sd, s32 val)
{
	if (sd->sensor == SEN_OV7660
	 || sd->sensor == SEN_OV7670) {
		switch (val) {
		case 0: /* Banding filter disabled */
			i2c_w_mask(sd, OV7670_R13_COM8, 0, OV7670_COM8_BFILT);
			break;
		case 1: /* 50 hz */
			i2c_w_mask(sd, OV7670_R13_COM8, OV7670_COM8_BFILT,
				   OV7670_COM8_BFILT);
			i2c_w_mask(sd, OV7670_R3B_COM11, 0x08, 0x18);
			break;
		case 2: /* 60 hz */
			i2c_w_mask(sd, OV7670_R13_COM8, OV7670_COM8_BFILT,
				   OV7670_COM8_BFILT);
			i2c_w_mask(sd, OV7670_R3B_COM11, 0x00, 0x18);
			break;
		case 3: /* Auto hz - ov7670 only */
			i2c_w_mask(sd, OV7670_R13_COM8, OV7670_COM8_BFILT,
				   OV7670_COM8_BFILT);
			i2c_w_mask(sd, OV7670_R3B_COM11, OV7670_COM11_HZAUTO,
				   0x18);
			break;
		}
	} else {
		switch (val) {
		case 0: /* Banding filter disabled */
			i2c_w_mask(sd, 0x2d, 0x00, 0x04);
			i2c_w_mask(sd, 0x2a, 0x00, 0x80);
			break;
		case 1: /* 50 hz (filter on and framerate adj) */
			i2c_w_mask(sd, 0x2d, 0x04, 0x04);
			i2c_w_mask(sd, 0x2a, 0x80, 0x80);
			/* 20 fps -> 16.667 fps */
			if (sd->sensor == SEN_OV6620 ||
			    sd->sensor == SEN_OV6630 ||
			    sd->sensor == SEN_OV66308AF)
				i2c_w(sd, 0x2b, 0x5e);
			else
				i2c_w(sd, 0x2b, 0xac);
			break;
		case 2: /* 60 hz (filter on, ...) */
			i2c_w_mask(sd, 0x2d, 0x04, 0x04);
			if (sd->sensor == SEN_OV6620 ||
			    sd->sensor == SEN_OV6630 ||
			    sd->sensor == SEN_OV66308AF) {
				/* 20 fps -> 15 fps */
				i2c_w_mask(sd, 0x2a, 0x80, 0x80);
				i2c_w(sd, 0x2b, 0xa8);
			} else {
				/* no framerate adj. */
				i2c_w_mask(sd, 0x2a, 0x00, 0x80);
			}
			break;
		}
	}
}

static void setfreq(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	setfreq_i(sd, val);

	/* Ugly but necessary */
	if (sd->bridge == BRIDGE_W9968CF)
		w9968cf_set_crop_window(sd);
}

static int sd_get_jcomp(struct gspca_dev *gspca_dev,
			struct v4l2_jpegcompression *jcomp)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->bridge != BRIDGE_W9968CF)
		return -ENOTTY;

	memset(jcomp, 0, sizeof *jcomp);
	jcomp->quality = v4l2_ctrl_g_ctrl(sd->jpegqual);
	jcomp->jpeg_markers = V4L2_JPEG_MARKER_DHT | V4L2_JPEG_MARKER_DQT |
			      V4L2_JPEG_MARKER_DRI;
	return 0;
}

static int sd_set_jcomp(struct gspca_dev *gspca_dev,
			const struct v4l2_jpegcompression *jcomp)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->bridge != BRIDGE_W9968CF)
		return -ENOTTY;

	v4l2_ctrl_s_ctrl(sd->jpegqual, jcomp->quality);
	return 0;
}

static int sd_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		gspca_dev->exposure->val = i2c_r(sd, 0x10);
		break;
	}
	return 0;
}

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		setbrightness(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		setcontrast(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		setfreq(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_AUTOBRIGHTNESS:
		if (ctrl->is_new)
			setautobright(gspca_dev, ctrl->val);
		if (!ctrl->val && sd->brightness->is_new)
			setbrightness(gspca_dev, sd->brightness->val);
		break;
	case V4L2_CID_SATURATION:
		setcolors(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		sethvflip(gspca_dev, ctrl->val, sd->vflip->val);
		break;
	case V4L2_CID_AUTOGAIN:
		if (ctrl->is_new)
			setautogain(gspca_dev, ctrl->val);
		if (!ctrl->val && gspca_dev->exposure->is_new)
			setexposure(gspca_dev, gspca_dev->exposure->val);
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		return -EBUSY; /* Should never happen, as we grab the ctrl */
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.g_volatile_ctrl = sd_g_volatile_ctrl,
	.s_ctrl = sd_s_ctrl,
};

static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *)gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 10);
	if (valid_controls[sd->sensor].has_brightness)
		sd->brightness = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0,
			sd->sensor == SEN_OV7660 ? 6 : 255, 1,
			sd->sensor == SEN_OV7660 ? 3 : 127);
	if (valid_controls[sd->sensor].has_contrast) {
		if (sd->sensor == SEN_OV7660)
			v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_CONTRAST, 0, 6, 1, 3);
		else
			v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_CONTRAST, 0, 255, 1,
				(sd->sensor == SEN_OV6630 ||
				 sd->sensor == SEN_OV66308AF) ? 200 : 127);
	}
	if (valid_controls[sd->sensor].has_sat)
		v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_SATURATION, 0,
			sd->sensor == SEN_OV7660 ? 4 : 255, 1,
			sd->sensor == SEN_OV7660 ? 2 : 127);
	if (valid_controls[sd->sensor].has_exposure)
		gspca_dev->exposure = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 255, 1, 127);
	if (valid_controls[sd->sensor].has_hvflip) {
		sd->hflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
		sd->vflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	}
	if (valid_controls[sd->sensor].has_autobright)
		sd->autobright = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_AUTOBRIGHTNESS, 0, 1, 1, 1);
	if (valid_controls[sd->sensor].has_autogain)
		gspca_dev->autogain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	if (valid_controls[sd->sensor].has_freq) {
		if (sd->sensor == SEN_OV7670)
			sd->freq = v4l2_ctrl_new_std_menu(hdl, &sd_ctrl_ops,
				V4L2_CID_POWER_LINE_FREQUENCY,
				V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
				V4L2_CID_POWER_LINE_FREQUENCY_AUTO);
		else
			sd->freq = v4l2_ctrl_new_std_menu(hdl, &sd_ctrl_ops,
				V4L2_CID_POWER_LINE_FREQUENCY,
				V4L2_CID_POWER_LINE_FREQUENCY_60HZ, 0, 0);
	}
	if (sd->bridge == BRIDGE_W9968CF)
		sd->jpegqual = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_JPEG_COMPRESSION_QUALITY,
			QUALITY_MIN, QUALITY_MAX, 1, QUALITY_DEF);

	if (hdl->error) {
		gspca_err(gspca_dev, "Could not initialize controls\n");
		return hdl->error;
	}
	if (gspca_dev->autogain)
		v4l2_ctrl_auto_cluster(3, &gspca_dev->autogain, 0, true);
	if (sd->autobright)
		v4l2_ctrl_auto_cluster(2, &sd->autobright, 0, false);
	if (sd->hflip)
		v4l2_ctrl_cluster(2, &sd->hflip);
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init = sd_isoc_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = sd_reset_snapshot,
	.get_jcomp = sd_get_jcomp,
	.set_jcomp = sd_set_jcomp,
#if IS_ENABLED(CONFIG_INPUT)
	.other_input = 1,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x041e, 0x4003), .driver_info = BRIDGE_W9968CF },
	{USB_DEVICE(0x041e, 0x4052),
		.driver_info = BRIDGE_OV519 | BRIDGE_INVERT_LED },
	{USB_DEVICE(0x041e, 0x405f), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x041e, 0x4060), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x041e, 0x4061), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x041e, 0x4064), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x041e, 0x4067), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x041e, 0x4068), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x045e, 0x028c),
		.driver_info = BRIDGE_OV519 | BRIDGE_INVERT_LED },
	{USB_DEVICE(0x054c, 0x0154), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x054c, 0x0155), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x05a9, 0x0511), .driver_info = BRIDGE_OV511 },
	{USB_DEVICE(0x05a9, 0x0518), .driver_info = BRIDGE_OV518 },
	{USB_DEVICE(0x05a9, 0x0519),
		.driver_info = BRIDGE_OV519 | BRIDGE_INVERT_LED },
	{USB_DEVICE(0x05a9, 0x0530),
		.driver_info = BRIDGE_OV519 | BRIDGE_INVERT_LED },
	{USB_DEVICE(0x05a9, 0x2800), .driver_info = BRIDGE_OVFX2 },
	{USB_DEVICE(0x05a9, 0x4519), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x05a9, 0x8519), .driver_info = BRIDGE_OV519 },
	{USB_DEVICE(0x05a9, 0xa511), .driver_info = BRIDGE_OV511PLUS },
	{USB_DEVICE(0x05a9, 0xa518), .driver_info = BRIDGE_OV518PLUS },
	{USB_DEVICE(0x0813, 0x0002), .driver_info = BRIDGE_OV511PLUS },
	{USB_DEVICE(0x0b62, 0x0059), .driver_info = BRIDGE_OVFX2 },
	{USB_DEVICE(0x0e96, 0xc001), .driver_info = BRIDGE_OVFX2 },
	{USB_DEVICE(0x1046, 0x9967), .driver_info = BRIDGE_W9968CF },
	{USB_DEVICE(0x8020, 0xef04), .driver_info = BRIDGE_OVFX2 },
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
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);

module_param(frame_rate, int, 0644);
MODULE_PARM_DESC(frame_rate, "Frame rate (5, 10, 15, 20 or 30 fps)");
