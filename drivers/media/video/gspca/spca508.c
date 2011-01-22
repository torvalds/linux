/*
 * SPCA508 chip based cameras subdriver
 *
 * Copyright (C) 2009 Jean-Francois Moine <http://moinejf.free.fr>
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

#define MODULE_NAME "spca508"

#include "gspca.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/SPCA508 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */

	u8 brightness;

	u8 subtype;
#define CreativeVista 0
#define HamaUSBSightcam 1
#define HamaUSBSightcam2 2
#define IntelEasyPCCamera 3
#define MicroInnovationIC200 4
#define ViewQuestVQ110 5
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define BRIGHTNESS_DEF 128
		.default_value = BRIGHTNESS_DEF,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
};

static const struct v4l2_pix_format sif_mode[] = {
	{160, 120, V4L2_PIX_FMT_SPCA508, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3},
	{176, 144, V4L2_PIX_FMT_SPCA508, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{320, 240, V4L2_PIX_FMT_SPCA508, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_SPCA508, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/* Frame packet header offsets for the spca508 */
#define SPCA508_OFFSET_DATA 37

/*
 * Initialization data: this is the first set-up data written to the
 * device (before the open data).
 */
static const u16 spca508_init_data[][2] = {
	{0x0000, 0x870b},

	{0x0020, 0x8112},	/* Video drop enable, ISO streaming disable */
	{0x0003, 0x8111},	/* Reset compression & memory */
	{0x0000, 0x8110},	/* Disable all outputs */
	/* READ {0x0000, 0x8114} -> 0000: 00  */
	{0x0000, 0x8114},	/* SW GPIO data */
	{0x0008, 0x8110},	/* Enable charge pump output */
	{0x0002, 0x8116},	/* 200 kHz pump clock */
	/* UNKNOWN DIRECTION (URB_FUNCTION_SELECT_INTERFACE:) */
	{0x0003, 0x8111},	/* Reset compression & memory */
	{0x0000, 0x8111},	/* Normal mode (not reset) */
	{0x0098, 0x8110},
		/* Enable charge pump output, sync.serial,external 2x clock */
	{0x000d, 0x8114},	/* SW GPIO data */
	{0x0002, 0x8116},	/* 200 kHz pump clock */
	{0x0020, 0x8112},	/* Video drop enable, ISO streaming disable */
/* --------------------------------------- */
	{0x000f, 0x8402},	/* memory bank */
	{0x0000, 0x8403},	/* ... address */
/* --------------------------------------- */
/* 0x88__ is Synchronous Serial Interface. */
/* TBD: This table could be expressed more compactly */
/* using spca508_write_i2c_vector(). */
/* TBD: Should see if the values in spca50x_i2c_data */
/* would work with the VQ110 instead of the values */
/* below. */
	{0x00c0, 0x8804},	/* SSI slave addr */
	{0x0008, 0x8802},	/* 375 Khz SSI clock */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},	/* 375 Khz SSI clock */
	{0x0012, 0x8801},	/* SSI reg addr */
	{0x0080, 0x8800},	/* SSI data to write */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},	/* 375 Khz SSI clock */
	{0x0012, 0x8801},	/* SSI reg addr */
	{0x0000, 0x8800},	/* SSI data to write */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},	/* 375 Khz SSI clock */
	{0x0011, 0x8801},	/* SSI reg addr */
	{0x0040, 0x8800},	/* SSI data to write */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0013, 0x8801},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0014, 0x8801},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0015, 0x8801},
	{0x0001, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0016, 0x8801},
	{0x0003, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0017, 0x8801},
	{0x0036, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0018, 0x8801},
	{0x00ec, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x001a, 0x8801},
	{0x0094, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x001b, 0x8801},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0027, 0x8801},
	{0x00a2, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0028, 0x8801},
	{0x0040, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x002a, 0x8801},
	{0x0084, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00 */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x002b, 0x8801},
	{0x00a8, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x002c, 0x8801},
	{0x00fe, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x002d, 0x8801},
	{0x0003, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0038, 0x8801},
	{0x0083, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0033, 0x8801},
	{0x0081, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0034, 0x8801},
	{0x004a, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0039, 0x8801},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0010, 0x8801},
	{0x00a8, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0006, 0x8801},
	{0x0058, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00 */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0000, 0x8801},
	{0x0004, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0040, 0x8801},
	{0x0080, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0041, 0x8801},
	{0x000c, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0042, 0x8801},
	{0x000c, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0043, 0x8801},
	{0x0028, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0044, 0x8801},
	{0x0080, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0045, 0x8801},
	{0x0020, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0046, 0x8801},
	{0x0020, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0047, 0x8801},
	{0x0080, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0048, 0x8801},
	{0x004c, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x0049, 0x8801},
	{0x0084, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x004a, 0x8801},
	{0x0084, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x0008, 0x8802},
	{0x004b, 0x8801},
	{0x0084, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* --------------------------------------- */
	{0x0012, 0x8700},	/* Clock speed 48Mhz/(2+2)/2= 6 Mhz */
	{0x0000, 0x8701},	/* CKx1 clock delay adj */
	{0x0000, 0x8701},	/* CKx1 clock delay adj */
	{0x0001, 0x870c},	/* CKOx2 output */
	/* --------------------------------------- */
	{0x0080, 0x8600},	/* Line memory read counter (L) */
	{0x0001, 0x8606},	/* reserved */
	{0x0064, 0x8607},	/* Line memory read counter (H) 0x6480=25,728 */
	{0x002a, 0x8601},	/* CDSP sharp interpolation mode,
	 *			line sel for color sep, edge enhance enab */
	{0x0000, 0x8602},	/* optical black level for user settng = 0 */
	{0x0080, 0x8600},	/* Line memory read counter (L) */
	{0x000a, 0x8603},	/* optical black level calc mode:
				 * auto; optical black offset = 10 */
	{0x00df, 0x865b},	/* Horiz offset for valid pixels (L)=0xdf */
	{0x0012, 0x865c},	/* Vert offset for valid lines (L)=0x12 */

/* The following two lines seem to be the "wrong" resolution. */
/* But perhaps these indicate the actual size of the sensor */
/* rather than the size of the current video mode. */
	{0x0058, 0x865d},	/* Horiz valid pixels (*4) (L) = 352 */
	{0x0048, 0x865e},	/* Vert valid lines (*4) (L) = 288 */

	{0x0015, 0x8608},	/* A11 Coef ... */
	{0x0030, 0x8609},
	{0x00fb, 0x860a},
	{0x003e, 0x860b},
	{0x00ce, 0x860c},
	{0x00f4, 0x860d},
	{0x00eb, 0x860e},
	{0x00dc, 0x860f},
	{0x0039, 0x8610},
	{0x0001, 0x8611},	/* R offset for white balance ... */
	{0x0000, 0x8612},
	{0x0001, 0x8613},
	{0x0000, 0x8614},
	{0x005b, 0x8651},	/* R gain for white balance ... */
	{0x0040, 0x8652},
	{0x0060, 0x8653},
	{0x0040, 0x8654},
	{0x0000, 0x8655},
	{0x0001, 0x863f},	/* Fixed gamma correction enable, USB control,
				 * lum filter disable, lum noise clip disable */
	{0x00a1, 0x8656},	/* Window1 size 256x256, Windows2 size 64x64,
				 * gamma look-up disable,
				 * new edge enhancement enable */
	{0x0018, 0x8657},	/* Edge gain high thresh */
	{0x0020, 0x8658},	/* Edge gain low thresh */
	{0x000a, 0x8659},	/* Edge bandwidth high threshold */
	{0x0005, 0x865a},	/* Edge bandwidth low threshold */
	/* -------------------------------- */
	{0x0030, 0x8112},	/* Video drop enable, ISO streaming enable */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0xa908, 0x8802},
	{0x0034, 0x8801},	/* SSI reg addr */
	{0x00ca, 0x8800},
	/* SSI data to write */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0x1f08, 0x8802},
	{0x0006, 0x8801},
	{0x0080, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

/* ----- Read back coefs we wrote earlier. */
	/* READ { 0x0000, 0x8608 } -> 0000: 15  */
	/* READ { 0x0000, 0x8609 } -> 0000: 30  */
	/* READ { 0x0000, 0x860a } -> 0000: fb  */
	/* READ { 0x0000, 0x860b } -> 0000: 3e  */
	/* READ { 0x0000, 0x860c } -> 0000: ce  */
	/* READ { 0x0000, 0x860d } -> 0000: f4  */
	/* READ { 0x0000, 0x860e } -> 0000: eb  */
	/* READ { 0x0000, 0x860f } -> 0000: dc  */
	/* READ { 0x0000, 0x8610 } -> 0000: 39  */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 08  */
	{0xb008, 0x8802},
	{0x0006, 0x8801},
	{0x007d, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */


	/* This chunk is seemingly redundant with */
	/* earlier commands (A11 Coef...), but if I disable it, */
	/* the image appears too dark.  Maybe there was some kind of */
	/* reset since the earlier commands, so this is necessary again. */
	{0x0015, 0x8608},
	{0x0030, 0x8609},
	{0xfffb, 0x860a},
	{0x003e, 0x860b},
	{0xffce, 0x860c},
	{0xfff4, 0x860d},
	{0xffeb, 0x860e},
	{0xffdc, 0x860f},
	{0x0039, 0x8610},
	{0x0018, 0x8657},

	{0x0000, 0x8508},	/* Disable compression. */
	/* Previous line was:
	{0x0021, 0x8508},	 * Enable compression. */
	{0x0032, 0x850b},	/* compression stuff */
	{0x0003, 0x8509},	/* compression stuff */
	{0x0011, 0x850a},	/* compression stuff */
	{0x0021, 0x850d},	/* compression stuff */
	{0x0010, 0x850c},	/* compression stuff */
	{0x0003, 0x8500},	/* *** Video mode: 160x120 */
	{0x0001, 0x8501},	/* Hardware-dominated snap control */
	{0x0061, 0x8656},	/* Window1 size 128x128, Windows2 size 128x128,
				 * gamma look-up disable,
				 * new edge enhancement enable */
	{0x0018, 0x8617},	/* Window1 start X (*2) */
	{0x0008, 0x8618},	/* Window1 start Y (*2) */
	{0x0061, 0x8656},	/* Window1 size 128x128, Windows2 size 128x128,
				 * gamma look-up disable,
				 * new edge enhancement enable */
	{0x0058, 0x8619},	/* Window2 start X (*2) */
	{0x0008, 0x861a},	/* Window2 start Y (*2) */
	{0x00ff, 0x8615},	/* High lum thresh for white balance */
	{0x0000, 0x8616},	/* Low lum thresh for white balance */
	{0x0012, 0x8700},	/* Clock speed 48Mhz/(2+2)/2= 6 Mhz */
	{0x0012, 0x8700},	/* Clock speed 48Mhz/(2+2)/2= 6 Mhz */
	/* READ { 0x0000, 0x8656 } -> 0000: 61  */
	{0x0028, 0x8802},    /* 375 Khz SSI clock, SSI r/w sync with VSYNC */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 28  */
	{0x1f28, 0x8802},    /* 375 Khz SSI clock, SSI r/w sync with VSYNC */
	{0x0010, 0x8801},	/* SSI reg addr */
	{0x003e, 0x8800},	/* SSI data to write */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	{0x0028, 0x8802},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 28  */
	{0x1f28, 0x8802},
	{0x0000, 0x8801},
	{0x001f, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	{0x0001, 0x8602},    /* optical black level for user settning = 1 */

	/* Original: */
	{0x0023, 0x8700},	/* Clock speed 48Mhz/(3+2)/4= 2.4 Mhz */
	{0x000f, 0x8602},    /* optical black level for user settning = 15 */

	{0x0028, 0x8802},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 28  */
	{0x1f28, 0x8802},
	{0x0010, 0x8801},
	{0x007b, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	{0x002f, 0x8651},	/* R gain for white balance ... */
	{0x0080, 0x8653},
	/* READ { 0x0000, 0x8655 } -> 0000: 00  */
	{0x0000, 0x8655},

	{0x0030, 0x8112},	/* Video drop enable, ISO streaming enable */
	{0x0020, 0x8112},	/* Video drop enable, ISO streaming disable */
	/* UNKNOWN DIRECTION (URB_FUNCTION_SELECT_INTERFACE: (ALT=0) ) */
	{}
};

/*
 * Initialization data for Intel EasyPC Camera CS110
 */
static const u16 spca508cs110_init_data[][2] = {
	{0x0000, 0x870b},	/* Reset CTL3 */
	{0x0003, 0x8111},	/* Soft Reset compression, memory, TG & CDSP */
	{0x0000, 0x8111},	/* Normal operation on reset */
	{0x0090, 0x8110},
		 /* External Clock 2x & Synchronous Serial Interface Output */
	{0x0020, 0x8112},	/* Video Drop packet enable */
	{0x0000, 0x8114},	/* Software GPIO output data */
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0003, 0x8114},

	/* Initial sequence Synchronous Serial Interface */
	{0x000f, 0x8402},	/* Memory bank Address */
	{0x0000, 0x8403},	/* Memory bank Address */
	{0x00ba, 0x8804},	/* SSI Slave address */
	{0x0010, 0x8802},	/* 93.75kHz SSI Clock Two DataByte */
	{0x0010, 0x8802},	/* 93.75kHz SSI Clock two DataByte */

	{0x0001, 0x8801},
	{0x000a, 0x8805},	/* a - NWG: Dunno what this is about */
	{0x0000, 0x8800},
	{0x0010, 0x8802},

	{0x0002, 0x8801},
	{0x0000, 0x8805},
	{0x0000, 0x8800},
	{0x0010, 0x8802},

	{0x0003, 0x8801},
	{0x0027, 0x8805},
	{0x0001, 0x8800},
	{0x0010, 0x8802},

	{0x0004, 0x8801},
	{0x0065, 0x8805},
	{0x0001, 0x8800},
	{0x0010, 0x8802},

	{0x0005, 0x8801},
	{0x0003, 0x8805},
	{0x0000, 0x8800},
	{0x0010, 0x8802},

	{0x0006, 0x8801},
	{0x001c, 0x8805},
	{0x0000, 0x8800},
	{0x0010, 0x8802},

	{0x0007, 0x8801},
	{0x002a, 0x8805},
	{0x0000, 0x8800},
	{0x0010, 0x8802},

	{0x0002, 0x8704},	/* External input CKIx1 */
	{0x0001, 0x8606},    /* 1 Line memory Read Counter (H) Result: (d)410 */
	{0x009a, 0x8600},	/* Line memory Read Counter (L) */
	{0x0001, 0x865b},	/* 1 Horizontal Offset for Valid Pixel(L) */
	{0x0003, 0x865c},	/* 3 Vertical Offset for Valid Lines(L) */
	{0x0058, 0x865d},	/* 58 Horizontal Valid Pixel Window(L) */

	{0x0006, 0x8660},	/* Nibble data + input order */

	{0x000a, 0x8602},	/* Optical black level set to 0x0a */
	{0x0000, 0x8603},	/* Optical black level Offset */

/*	{0x0000, 0x8611},	 * 0 R  Offset for white Balance */
/*	{0x0000, 0x8612},	 * 1 Gr Offset for white Balance */
/*	{0x0000, 0x8613},	 * 1f B  Offset for white Balance */
/*	{0x0000, 0x8614},	 * f0 Gb Offset for white Balance */

	{0x0040, 0x8651},   /* 2b BLUE gain for white balance  good at all 60 */
	{0x0030, 0x8652},	/* 41 Gr Gain for white Balance (L) */
	{0x0035, 0x8653},	/* 26 RED gain for white balance */
	{0x0035, 0x8654},	/* 40Gb Gain for white Balance (L) */
	{0x0041, 0x863f},
	      /* Fixed Gamma correction enabled (makes colours look better) */

	{0x0000, 0x8655},
		/* High bits for white balance*****brightness control*** */
	{}
};

static const u16 spca508_sightcam_init_data[][2] = {
/* This line seems to setup the frame/canvas */
	{0x000f, 0x8402},

/* Theese 6 lines are needed to startup the webcam */
	{0x0090, 0x8110},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0003, 0x8114},
	{0x0080, 0x8804},

/* This part seems to make the pictures darker? (autobrightness?) */
	{0x0001, 0x8801},
	{0x0004, 0x8800},
	{0x0003, 0x8801},
	{0x00e0, 0x8800},
	{0x0004, 0x8801},
	{0x00b4, 0x8800},
	{0x0005, 0x8801},
	{0x0000, 0x8800},

	{0x0006, 0x8801},
	{0x00e0, 0x8800},
	{0x0007, 0x8801},
	{0x000c, 0x8800},

/* This section is just needed, it probably
 * does something like the previous section,
 * but the cam won't start if it's not included.
 */
	{0x0014, 0x8801},
	{0x0008, 0x8800},
	{0x0015, 0x8801},
	{0x0067, 0x8800},
	{0x0016, 0x8801},
	{0x0000, 0x8800},
	{0x0017, 0x8801},
	{0x0020, 0x8800},
	{0x0018, 0x8801},
	{0x0044, 0x8800},

/* Makes the picture darker - and the
 * cam won't start if not included
 */
	{0x001e, 0x8801},
	{0x00ea, 0x8800},
	{0x001f, 0x8801},
	{0x0001, 0x8800},
	{0x0003, 0x8801},
	{0x00e0, 0x8800},

/* seems to place the colors ontop of each other #1 */
	{0x0006, 0x8704},
	{0x0001, 0x870c},
	{0x0016, 0x8600},
	{0x0002, 0x8606},

/* if not included the pictures becomes _very_ dark */
	{0x0064, 0x8607},
	{0x003a, 0x8601},
	{0x0000, 0x8602},

/* seems to place the colors ontop of each other #2 */
	{0x0016, 0x8600},
	{0x0018, 0x8617},
	{0x0008, 0x8618},
	{0x00a1, 0x8656},

/* webcam won't start if not included */
	{0x0007, 0x865b},
	{0x0001, 0x865c},
	{0x0058, 0x865d},
	{0x0048, 0x865e},

/* adjusts the colors */
	{0x0049, 0x8651},
	{0x0040, 0x8652},
	{0x004c, 0x8653},
	{0x0040, 0x8654},
	{}
};

static const u16 spca508_sightcam2_init_data[][2] = {
	{0x0020, 0x8112},

	{0x000f, 0x8402},
	{0x0000, 0x8403},

	{0x0008, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x0009, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x000a, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x000b, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x000c, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x000d, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x000e, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x0007, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x000f, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},

	{0x0018, 0x8660},
	{0x0010, 0x8201},

	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x0011, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},

	{0x0000, 0x86b0},
	{0x0034, 0x86b1},
	{0x0000, 0x86b2},
	{0x0049, 0x86b3},
	{0x0000, 0x86b4},
	{0x0000, 0x86b4},

	{0x0012, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},
	{0x0013, 0x8201},
	{0x0008, 0x8200},
	{0x0001, 0x8200},

	{0x0001, 0x86b0},
	{0x00aa, 0x86b1},
	{0x0000, 0x86b2},
	{0x00e4, 0x86b3},
	{0x0000, 0x86b4},
	{0x0000, 0x86b4},

	{0x0018, 0x8660},

	{0x0090, 0x8110},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0003, 0x8114},

	{0x0080, 0x8804},
	{0x0003, 0x8801},
	{0x0012, 0x8800},
	{0x0004, 0x8801},
	{0x0005, 0x8800},
	{0x0005, 0x8801},
	{0x0000, 0x8800},
	{0x0006, 0x8801},
	{0x0000, 0x8800},
	{0x0007, 0x8801},
	{0x0000, 0x8800},
	{0x0008, 0x8801},
	{0x0005, 0x8800},
	{0x000a, 0x8700},
	{0x000e, 0x8801},
	{0x0004, 0x8800},
	{0x0005, 0x8801},
	{0x0047, 0x8800},
	{0x0006, 0x8801},
	{0x0000, 0x8800},
	{0x0007, 0x8801},
	{0x00c0, 0x8800},
	{0x0008, 0x8801},
	{0x0003, 0x8800},
	{0x0013, 0x8801},
	{0x0001, 0x8800},
	{0x0009, 0x8801},
	{0x0000, 0x8800},
	{0x000a, 0x8801},
	{0x0000, 0x8800},
	{0x000b, 0x8801},
	{0x0000, 0x8800},
	{0x000c, 0x8801},
	{0x0000, 0x8800},
	{0x000e, 0x8801},
	{0x0004, 0x8800},
	{0x000f, 0x8801},
	{0x0000, 0x8800},
	{0x0010, 0x8801},
	{0x0006, 0x8800},
	{0x0011, 0x8801},
	{0x0006, 0x8800},
	{0x0012, 0x8801},
	{0x0000, 0x8800},
	{0x0013, 0x8801},
	{0x0001, 0x8800},

	{0x000a, 0x8700},
	{0x0000, 0x8702},
	{0x0000, 0x8703},
	{0x00c2, 0x8704},
	{0x0001, 0x870c},

	{0x0044, 0x8600},
	{0x0002, 0x8606},
	{0x0064, 0x8607},
	{0x003a, 0x8601},
	{0x0008, 0x8602},
	{0x0044, 0x8600},
	{0x0018, 0x8617},
	{0x0008, 0x8618},
	{0x00a1, 0x8656},
	{0x0004, 0x865b},
	{0x0002, 0x865c},
	{0x0058, 0x865d},
	{0x0048, 0x865e},
	{0x0012, 0x8608},
	{0x002c, 0x8609},
	{0x0002, 0x860a},
	{0x002c, 0x860b},
	{0x00db, 0x860c},
	{0x00f9, 0x860d},
	{0x00f1, 0x860e},
	{0x00e3, 0x860f},
	{0x002c, 0x8610},
	{0x006c, 0x8651},
	{0x0041, 0x8652},
	{0x0059, 0x8653},
	{0x0040, 0x8654},
	{0x00fa, 0x8611},
	{0x00ff, 0x8612},
	{0x00f8, 0x8613},
	{0x0000, 0x8614},
	{0x0001, 0x863f},
	{0x0000, 0x8640},
	{0x0026, 0x8641},
	{0x0045, 0x8642},
	{0x0060, 0x8643},
	{0x0075, 0x8644},
	{0x0088, 0x8645},
	{0x009b, 0x8646},
	{0x00b0, 0x8647},
	{0x00c5, 0x8648},
	{0x00d2, 0x8649},
	{0x00dc, 0x864a},
	{0x00e5, 0x864b},
	{0x00eb, 0x864c},
	{0x00f0, 0x864d},
	{0x00f6, 0x864e},
	{0x00fa, 0x864f},
	{0x00ff, 0x8650},
	{0x0060, 0x8657},
	{0x0010, 0x8658},
	{0x0018, 0x8659},
	{0x0005, 0x865a},
	{0x0018, 0x8660},
	{0x0003, 0x8509},
	{0x0011, 0x850a},
	{0x0032, 0x850b},
	{0x0010, 0x850c},
	{0x0021, 0x850d},
	{0x0001, 0x8500},
	{0x0000, 0x8508},
	{0x0012, 0x8608},
	{0x002c, 0x8609},
	{0x0002, 0x860a},
	{0x0039, 0x860b},
	{0x00d0, 0x860c},
	{0x00f7, 0x860d},
	{0x00ed, 0x860e},
	{0x00db, 0x860f},
	{0x0039, 0x8610},
	{0x0012, 0x8657},
	{0x000c, 0x8619},
	{0x0004, 0x861a},
	{0x00a1, 0x8656},
	{0x00c8, 0x8615},
	{0x0032, 0x8616},

	{0x0030, 0x8112},
	{0x0020, 0x8112},
	{0x0020, 0x8112},
	{0x000f, 0x8402},
	{0x0000, 0x8403},

	{0x0090, 0x8110},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0003, 0x8114},
	{0x0080, 0x8804},

	{0x0003, 0x8801},
	{0x0012, 0x8800},
	{0x0004, 0x8801},
	{0x0005, 0x8800},
	{0x0005, 0x8801},
	{0x0047, 0x8800},
	{0x0006, 0x8801},
	{0x0000, 0x8800},
	{0x0007, 0x8801},
	{0x00c0, 0x8800},
	{0x0008, 0x8801},
	{0x0003, 0x8800},
	{0x000a, 0x8700},
	{0x000e, 0x8801},
	{0x0004, 0x8800},
	{0x0005, 0x8801},
	{0x0047, 0x8800},
	{0x0006, 0x8801},
	{0x0000, 0x8800},
	{0x0007, 0x8801},
	{0x00c0, 0x8800},
	{0x0008, 0x8801},
	{0x0003, 0x8800},
	{0x0013, 0x8801},
	{0x0001, 0x8800},
	{0x0009, 0x8801},
	{0x0000, 0x8800},
	{0x000a, 0x8801},
	{0x0000, 0x8800},
	{0x000b, 0x8801},
	{0x0000, 0x8800},
	{0x000c, 0x8801},
	{0x0000, 0x8800},
	{0x000e, 0x8801},
	{0x0004, 0x8800},
	{0x000f, 0x8801},
	{0x0000, 0x8800},
	{0x0010, 0x8801},
	{0x0006, 0x8800},
	{0x0011, 0x8801},
	{0x0006, 0x8800},
	{0x0012, 0x8801},
	{0x0000, 0x8800},
	{0x0013, 0x8801},
	{0x0001, 0x8800},
	{0x000a, 0x8700},
	{0x0000, 0x8702},
	{0x0000, 0x8703},
	{0x00c2, 0x8704},
	{0x0001, 0x870c},
	{0x0044, 0x8600},
	{0x0002, 0x8606},
	{0x0064, 0x8607},
	{0x003a, 0x8601},
	{0x0008, 0x8602},
	{0x0044, 0x8600},
	{0x0018, 0x8617},
	{0x0008, 0x8618},
	{0x00a1, 0x8656},
	{0x0004, 0x865b},
	{0x0002, 0x865c},
	{0x0058, 0x865d},
	{0x0048, 0x865e},
	{0x0012, 0x8608},
	{0x002c, 0x8609},
	{0x0002, 0x860a},
	{0x002c, 0x860b},
	{0x00db, 0x860c},
	{0x00f9, 0x860d},
	{0x00f1, 0x860e},
	{0x00e3, 0x860f},
	{0x002c, 0x8610},
	{0x006c, 0x8651},
	{0x0041, 0x8652},
	{0x0059, 0x8653},
	{0x0040, 0x8654},
	{0x00fa, 0x8611},
	{0x00ff, 0x8612},
	{0x00f8, 0x8613},
	{0x0000, 0x8614},
	{0x0001, 0x863f},
	{0x0000, 0x8640},
	{0x0026, 0x8641},
	{0x0045, 0x8642},
	{0x0060, 0x8643},
	{0x0075, 0x8644},
	{0x0088, 0x8645},
	{0x009b, 0x8646},
	{0x00b0, 0x8647},
	{0x00c5, 0x8648},
	{0x00d2, 0x8649},
	{0x00dc, 0x864a},
	{0x00e5, 0x864b},
	{0x00eb, 0x864c},
	{0x00f0, 0x864d},
	{0x00f6, 0x864e},
	{0x00fa, 0x864f},
	{0x00ff, 0x8650},
	{0x0060, 0x8657},
	{0x0010, 0x8658},
	{0x0018, 0x8659},
	{0x0005, 0x865a},
	{0x0018, 0x8660},
	{0x0003, 0x8509},
	{0x0011, 0x850a},
	{0x0032, 0x850b},
	{0x0010, 0x850c},
	{0x0021, 0x850d},
	{0x0001, 0x8500},
	{0x0000, 0x8508},

	{0x0012, 0x8608},
	{0x002c, 0x8609},
	{0x0002, 0x860a},
	{0x0039, 0x860b},
	{0x00d0, 0x860c},
	{0x00f7, 0x860d},
	{0x00ed, 0x860e},
	{0x00db, 0x860f},
	{0x0039, 0x8610},
	{0x0012, 0x8657},
	{0x0064, 0x8619},

/* This line starts it all, it is not needed here */
/* since it has been build into the driver */
/* jfm: don't start now */
/*	{0x0030, 0x8112}, */
	{}
};

/*
 * Initialization data for Creative Webcam Vista
 */
static const u16 spca508_vista_init_data[][2] = {
	{0x0008, 0x8200},	/* Clear register */
	{0x0000, 0x870b},	/* Reset CTL3 */
	{0x0020, 0x8112},	/* Video Drop packet enable */
	{0x0003, 0x8111},	/* Soft Reset compression, memory, TG & CDSP */
	{0x0000, 0x8110},	/* Disable everything */
	{0x0000, 0x8114},	/* Software GPIO output data */
	{0x0000, 0x8114},

	{0x0003, 0x8111},
	{0x0000, 0x8111},
	{0x0090, 0x8110},    /* Enable: SSI output, External 2X clock output */
	{0x0020, 0x8112},
	{0x0000, 0x8114},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0001, 0x8114},
	{0x0003, 0x8114},

	{0x000f, 0x8402},	/* Memory bank Address */
	{0x0000, 0x8403},	/* Memory bank Address */
	{0x00ba, 0x8804},	/* SSI Slave address */
	{0x0010, 0x8802},	/* 93.75kHz SSI Clock Two DataByte */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},	/* Will write 2 bytes (DATA1+DATA2) */
	{0x0020, 0x8801},	/* Register address for SSI read/write */
	{0x0044, 0x8805},	/* DATA2 */
	{0x0004, 0x8800},	/* DATA1 -> write triggered */
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0009, 0x8801},
	{0x0042, 0x8805},
	{0x0001, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x003c, 0x8801},
	{0x0001, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0001, 0x8801},
	{0x000a, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0002, 0x8801},
	{0x0000, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0003, 0x8801},
	{0x0027, 0x8805},
	{0x0001, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0004, 0x8801},
	{0x0065, 0x8805},
	{0x0001, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0005, 0x8801},
	{0x0003, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0006, 0x8801},
	{0x001c, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0007, 0x8801},
	{0x002a, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x000e, 0x8801},
	{0x0000, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0028, 0x8801},
	{0x002e, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0039, 0x8801},
	{0x0013, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x003b, 0x8801},
	{0x000c, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0035, 0x8801},
	{0x0028, 0x8805},
	{0x0000, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	/* READ { 0x0001, 0x8803 } -> 0000: 00  */
	/* READ { 0x0001, 0x8802 } -> 0000: 10  */
	{0x0010, 0x8802},
	{0x0009, 0x8801},
	{0x0042, 0x8805},
	{0x0001, 0x8800},
	/* READ { 0x0001, 0x8803 } -> 0000: 00  */

	{0x0050, 0x8703},
	{0x0002, 0x8704},	/* External input CKIx1 */
	{0x0001, 0x870c},	/* Select CKOx2 output */
	{0x009a, 0x8600},	/* Line memory Read Counter (L) */
	{0x0001, 0x8606},    /* 1 Line memory Read Counter (H) Result: (d)410 */
	{0x0023, 0x8601},
	{0x0010, 0x8602},
	{0x000a, 0x8603},
	{0x009a, 0x8600},
	{0x0001, 0x865b},	/* 1 Horizontal Offset for Valid Pixel(L) */
	{0x0003, 0x865c},	/* Vertical offset for valid lines (L) */
	{0x0058, 0x865d},	/* Horizontal valid pixels window (L) */
	{0x0048, 0x865e},	/* Vertical valid lines window (L) */
	{0x0000, 0x865f},

	{0x0006, 0x8660},
		    /* Enable nibble data input, select nibble input order */

	{0x0013, 0x8608},	/* A11 Coeficients for color correction */
	{0x0028, 0x8609},
		    /* Note: these values are confirmed at the end of array */
	{0x0005, 0x860a},	/* ... */
	{0x0025, 0x860b},
	{0x00e1, 0x860c},
	{0x00fa, 0x860d},
	{0x00f4, 0x860e},
	{0x00e8, 0x860f},
	{0x0025, 0x8610},	/* A33 Coef. */
	{0x00fc, 0x8611},	/* White balance offset: R */
	{0x0001, 0x8612},	/* White balance offset: Gr */
	{0x00fe, 0x8613},	/* White balance offset: B */
	{0x0000, 0x8614},	/* White balance offset: Gb */

	{0x0064, 0x8651},	/* R gain for white balance (L) */
	{0x0040, 0x8652},	/* Gr gain for white balance (L) */
	{0x0066, 0x8653},	/* B gain for white balance (L) */
	{0x0040, 0x8654},	/* Gb gain for white balance (L) */
	{0x0001, 0x863f},	/* Enable fixed gamma correction */

	{0x00a1, 0x8656},	/* Size - Window1: 256x256, Window2: 128x128,
				 * UV division: UV no change,
				 * Enable New edge enhancement */
	{0x0018, 0x8657},	/* Edge gain high threshold */
	{0x0020, 0x8658},	/* Edge gain low threshold */
	{0x000a, 0x8659},	/* Edge bandwidth high threshold */
	{0x0005, 0x865a},	/* Edge bandwidth low threshold */
	{0x0064, 0x8607},	/* UV filter enable */

	{0x0016, 0x8660},
	{0x0000, 0x86b0},	/* Bad pixels compensation address */
	{0x00dc, 0x86b1},	/* X coord for bad pixels compensation (L) */
	{0x0000, 0x86b2},
	{0x0009, 0x86b3},	/* Y coord for bad pixels compensation (L) */
	{0x0000, 0x86b4},

	{0x0001, 0x86b0},
	{0x00f5, 0x86b1},
	{0x0000, 0x86b2},
	{0x00c6, 0x86b3},
	{0x0000, 0x86b4},

	{0x0002, 0x86b0},
	{0x001c, 0x86b1},
	{0x0001, 0x86b2},
	{0x00d7, 0x86b3},
	{0x0000, 0x86b4},

	{0x0003, 0x86b0},
	{0x001c, 0x86b1},
	{0x0001, 0x86b2},
	{0x00d8, 0x86b3},
	{0x0000, 0x86b4},

	{0x0004, 0x86b0},
	{0x001d, 0x86b1},
	{0x0001, 0x86b2},
	{0x00d8, 0x86b3},
	{0x0000, 0x86b4},
	{0x001e, 0x8660},

	/* READ { 0x0000, 0x8608 } -> 0000: 13  */
	/* READ { 0x0000, 0x8609 } -> 0000: 28  */
	/* READ { 0x0000, 0x8610 } -> 0000: 05  */
	/* READ { 0x0000, 0x8611 } -> 0000: 25  */
	/* READ { 0x0000, 0x8612 } -> 0000: e1  */
	/* READ { 0x0000, 0x8613 } -> 0000: fa  */
	/* READ { 0x0000, 0x8614 } -> 0000: f4  */
	/* READ { 0x0000, 0x8615 } -> 0000: e8  */
	/* READ { 0x0000, 0x8616 } -> 0000: 25  */
	{}
};

static int reg_write(struct usb_device *dev,
			u16 index, u16 value)
{
	int ret;

	ret = usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			0,		/* request */
			USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0, 500);
	PDEBUG(D_USBO, "reg write i:0x%04x = 0x%02x",
		index, value);
	if (ret < 0)
		err("reg write: error %d", ret);
	return ret;
}

/* read 1 byte */
/* returns: negative is error, pos or zero is data */
static int reg_read(struct gspca_dev *gspca_dev,
			u16 index)	/* wIndex */
{
	int ret;

	ret = usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			0,			/* register */
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index,
			gspca_dev->usb_buf, 1,
			500);			/* timeout */
	PDEBUG(D_USBI, "reg read i:%04x --> %02x",
		index, gspca_dev->usb_buf[0]);
	if (ret < 0) {
		err("reg_read err %d", ret);
		return ret;
	}
	return gspca_dev->usb_buf[0];
}

/* send 1 or 2 bytes to the sensor via the Synchronous Serial Interface */
static int ssi_w(struct gspca_dev *gspca_dev,
		u16 reg, u16 val)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret, retry;

	ret = reg_write(dev, 0x8802, reg >> 8);
	if (ret < 0)
		goto out;
	ret = reg_write(dev, 0x8801, reg & 0x00ff);
	if (ret < 0)
		goto out;
	if ((reg & 0xff00) == 0x1000) {		/* if 2 bytes */
		ret = reg_write(dev, 0x8805, val & 0x00ff);
		if (ret < 0)
			goto out;
		val >>= 8;
	}
	ret = reg_write(dev, 0x8800, val);
	if (ret < 0)
		goto out;

	/* poll until not busy */
	retry = 10;
	for (;;) {
		ret = reg_read(gspca_dev, 0x8803);
		if (ret < 0)
			break;
		if (gspca_dev->usb_buf[0] == 0)
			break;
		if (--retry <= 0) {
			PDEBUG(D_ERR, "ssi_w busy %02x",
					gspca_dev->usb_buf[0]);
			ret = -1;
			break;
		}
		msleep(8);
	}

out:
	return ret;
}

static int write_vector(struct gspca_dev *gspca_dev,
			const u16 (*data)[2])
{
	struct usb_device *dev = gspca_dev->dev;
	int ret = 0;

	while ((*data)[1] != 0) {
		if ((*data)[1] & 0x8000) {
			if ((*data)[1] == 0xdd00)	/* delay */
				msleep((*data)[0]);
			else
				ret = reg_write(dev, (*data)[1], (*data)[0]);
		} else {
			ret = ssi_w(gspca_dev, (*data)[1], (*data)[0]);
		}
		if (ret < 0)
			break;
		data++;
	}
	return ret;
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
	int data1, data2;
	const u16 (*init_data)[2];
	static const u16 (*(init_data_tb[]))[2] = {
		spca508_vista_init_data,	/* CreativeVista 0 */
		spca508_sightcam_init_data,	/* HamaUSBSightcam 1 */
		spca508_sightcam2_init_data,	/* HamaUSBSightcam2 2 */
		spca508cs110_init_data,		/* IntelEasyPCCamera 3 */
		spca508cs110_init_data,		/* MicroInnovationIC200 4 */
		spca508_init_data,		/* ViewQuestVQ110 5 */
	};

	/* Read from global register the USB product and vendor IDs, just to
	 * prove that we can communicate with the device.  This works, which
	 * confirms at we are communicating properly and that the device
	 * is a 508. */
	data1 = reg_read(gspca_dev, 0x8104);
	data2 = reg_read(gspca_dev, 0x8105);
	PDEBUG(D_PROBE, "Webcam Vendor ID: 0x%02x%02x", data2, data1);

	data1 = reg_read(gspca_dev, 0x8106);
	data2 = reg_read(gspca_dev, 0x8107);
	PDEBUG(D_PROBE, "Webcam Product ID: 0x%02x%02x", data2, data1);

	data1 = reg_read(gspca_dev, 0x8621);
	PDEBUG(D_PROBE, "Window 1 average luminance: %d", data1);

	cam = &gspca_dev->cam;
	cam->cam_mode = sif_mode;
	cam->nmodes = ARRAY_SIZE(sif_mode);

	sd->subtype = id->driver_info;
	sd->brightness = BRIGHTNESS_DEF;

	init_data = init_data_tb[sd->subtype];
	return write_vector(gspca_dev, init_data);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	int mode;

	mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	reg_write(gspca_dev->dev, 0x8500, mode);
	switch (mode) {
	case 0:
	case 1:
		reg_write(gspca_dev->dev, 0x8700, 0x28);	/* clock */
		break;
	default:
/*	case 2: */
/*	case 3: */
		reg_write(gspca_dev->dev, 0x8700, 0x23);	/* clock */
		break;
	}
	reg_write(gspca_dev->dev, 0x8112, 0x10 | 0x20);
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	/* Video ISO disable, Video Drop Packet enable: */
	reg_write(gspca_dev->dev, 0x8112, 0x20);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	switch (data[0]) {
	case 0:				/* start of frame */
		gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
		data += SPCA508_OFFSET_DATA;
		len -= SPCA508_OFFSET_DATA;
		gspca_frame_add(gspca_dev, FIRST_PACKET, data, len);
		break;
	case 0xff:			/* drop */
		break;
	default:
		data += 1;
		len -= 1;
		gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
		break;
	}
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 brightness = sd->brightness;

	/* MX seem contrast */
	reg_write(gspca_dev->dev, 0x8651, brightness);
	reg_write(gspca_dev->dev, 0x8652, brightness);
	reg_write(gspca_dev->dev, 0x8653, brightness);
	reg_write(gspca_dev->dev, 0x8654, brightness);
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
	{USB_DEVICE(0x0130, 0x0130), .driver_info = HamaUSBSightcam},
	{USB_DEVICE(0x041e, 0x4018), .driver_info = CreativeVista},
	{USB_DEVICE(0x0733, 0x0110), .driver_info = ViewQuestVQ110},
	{USB_DEVICE(0x0af9, 0x0010), .driver_info = HamaUSBSightcam},
	{USB_DEVICE(0x0af9, 0x0011), .driver_info = HamaUSBSightcam2},
	{USB_DEVICE(0x8086, 0x0110), .driver_info = IntelEasyPCCamera},
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
	return usb_register(&sd_driver);
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
