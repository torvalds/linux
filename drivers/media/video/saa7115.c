/* saa7115 - Philips SAA7113/SAA7114/SAA7115 video decoder driver
 *
 * Based on saa7114 driver by Maxim Yevtyushkin, which is based on
 * the saa7111 driver by Dave Perks.
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 * Copyright (C) 2002 Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Slight changes for video timing and attachment output by
 * Wolfgang Scherr <scherr@net4you.net>
 *
 * Moved over to the linux >= 2.4.x i2c protocol (1/1/2003)
 * by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * Added saa7115 support by Kevin Thayer <nufan_wfk at yahoo.com>
 * (2/17/2003)
 *
 * VBI support (2004) and cleanups (2005) by Hans Verkuil <hverkuil@xs4all.nl>
 * SAA7113 support by Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/saa7115.h>
#include <asm/div64.h>

MODULE_DESCRIPTION("Philips SAA7113/SAA7114/SAA7115 video decoder driver");
MODULE_AUTHOR(  "Maxim Yevtyushkin, Kevin Thayer, Chris Kennedy, "
		"Hans Verkuil, Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");

static unsigned short normal_i2c[] = {
		0x4a >> 1, 0x48 >> 1,	/* SAA7113 */
		0x42 >> 1, 0x40 >> 1,	/* SAA7114 and SAA7115 */
		I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

struct saa7115_state {
	v4l2_std_id std;
	int input;
	int enable;
	int radio;
	int bright;
	int contrast;
	int hue;
	int sat;
	enum v4l2_chip_ident ident;
	u32 audclk_freq;
	u32 crystal_freq;
	u8 ucgc;
	u8 cgcdiv;
	u8 apll;
};

/* ----------------------------------------------------------------------- */

static inline int saa7115_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int saa7115_writeregs(struct i2c_client *client, const unsigned char *regs)
{
	unsigned char reg, data;

	while (*regs != 0x00) {
		reg = *(regs++);
		data = *(regs++);
		if (saa7115_write(client, reg, data) < 0)
			return -1;
	}
	return 0;
}

static inline int saa7115_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

/* If a value differs from the Hauppauge driver values, then the comment starts with
   'was 0xXX' to denote the Hauppauge value. Otherwise the value is identical to what the
   Hauppauge driver sets. */

static const unsigned char saa7115_init_auto_input[] = {
		/* Front-End Part */
	0x01, 0x48,		/* white peak control disabled */
	0x03, 0x20,		/* was 0x30. 0x20: long vertical blanking */
	0x04, 0x90,		/* analog gain set to 0 */
	0x05, 0x90,		/* analog gain set to 0 */
		/* Decoder Part */
	0x06, 0xeb,		/* horiz sync begin = -21 */
	0x07, 0xe0,		/* horiz sync stop = -17 */
	0x0a, 0x80,		/* was 0x88. decoder brightness, 0x80 is itu standard */
	0x0b, 0x44,		/* was 0x48. decoder contrast, 0x44 is itu standard */
	0x0c, 0x40,		/* was 0x47. decoder saturation, 0x40 is itu standard */
	0x0d, 0x00,		/* chrominance hue control */
	0x0f, 0x00,		/* chrominance gain control: use automicatic mode */
	0x10, 0x06,		/* chrominance/luminance control: active adaptive combfilter */
	0x11, 0x00,		/* delay control */
	0x12, 0x9d,		/* RTS0 output control: VGATE */
	0x13, 0x80,		/* X-port output control: ITU656 standard mode, RTCO output enable RTCE */
	0x14, 0x00,		/* analog/ADC/auto compatibility control */
	0x18, 0x40,		/* raw data gain 0x00 = nominal */
	0x19, 0x80,		/* raw data offset 0x80 = 0 LSB */
	0x1a, 0x77,		/* color killer level control 0x77 = recommended */
	0x1b, 0x42,		/* misc chroma control 0x42 = recommended */
	0x1c, 0xa9,		/* combfilter control 0xA9 = recommended */
	0x1d, 0x01,		/* combfilter control 0x01 = recommended */

		/* Power Device Control */
	0x88, 0xd0,		/* reset device */
	0x88, 0xf0,		/* set device programmed, all in operational mode */
	0x00, 0x00
};

static const unsigned char saa7115_cfg_reset_scaler[] = {
	0x87, 0x00,		/* disable I-port output */
	0x88, 0xd0,		/* reset scaler */
	0x88, 0xf0,		/* activate scaler */
	0x87, 0x01,		/* enable I-port output */
	0x00, 0x00
};

/* ============== SAA7715 VIDEO templates =============  */

static const unsigned char saa7115_cfg_60hz_fullres_x[] = {
	0xcc, 0xd0,		/* hsize low (output), hor. output window size = 0x2d0 = 720 */
	0xcd, 0x02,		/* hsize hi (output) */

	/* Why not in 60hz-Land, too? */
	0xd0, 0x01,		/* downscale = 1 */
	0xd8, 0x00,		/* hor lum scaling 0x0400 = 1 */
	0xd9, 0x04,
	0xdc, 0x00,		/* hor chrom scaling 0x0200. must be hor lum scaling / 2 */
	0xdd, 0x02,		/* H-scaling incr chroma */

	0x00, 0x00
};
static const unsigned char saa7115_cfg_60hz_fullres_y[] = {
	0xce, 0xf8,		/* vsize low (output), ver. output window size = 248 (but 60hz is 240?) */
	0xcf, 0x00,		/* vsize hi (output) */

	/* Why not in 60hz-Land, too? */
	0xd5, 0x40,		/* Lum contrast, nominal value = 0x40 */
	0xd6, 0x40,		/* Chroma satur. nominal value = 0x80 */

	0xe0, 0x00,		/* V-scaling incr luma low */
	0xe1, 0x04,		/* " hi */
	0xe2, 0x00,		/* V-scaling incr chroma low */
	0xe3, 0x04,		/* " hi */

	0x00, 0x00
};

static const unsigned char saa7115_cfg_60hz_video[] = {
	0x80, 0x00,		/* reset tasks */
	0x88, 0xd0,		/* reset scaler */

	0x15, 0x03,		/* VGATE pulse start */
	0x16, 0x11,		/* VGATE pulse stop */
	0x17, 0x9c,		/* VGATE MSB and other values */

	0x08, 0x68,		/* 0xBO: auto detection, 0x68 = NTSC */
	0x0e, 0x07,		/* lots of different stuff... video autodetection is on */

	0x5a, 0x06,		/* Vertical offset, standard 60hz value for ITU656 line counting */

	/* Task A */
	0x90, 0x80,		/* Task Handling Control */
	0x91, 0x48,		/* X-port formats/config */
	0x92, 0x40,		/* Input Ref. signal Def. */
	0x93, 0x84,		/* I-port config */
	0x94, 0x01,		/* hoffset low (input), 0x0002 is minimum */
	0x95, 0x00,		/* hoffset hi (input) */
	0x96, 0xd0,		/* hsize low (input), 0x02d0 = 720 */
	0x97, 0x02,		/* hsize hi (input) */
	0x98, 0x05,		/* voffset low (input) */
	0x99, 0x00,		/* voffset hi (input) */
	0x9a, 0x0c,		/* vsize low (input), 0x0c = 12 */
	0x9b, 0x00,		/* vsize hi (input) */
	0x9c, 0xa0,		/* hsize low (output), 0x05a0 = 1440 */
	0x9d, 0x05,		/* hsize hi (output) */
	0x9e, 0x0c,		/* vsize low (output), 0x0c = 12 */
	0x9f, 0x00,		/* vsize hi (output) */

	/* Task B */
	0xc0, 0x00,		/* Task Handling Control */
	0xc1, 0x08,		/* X-port formats/config */
	0xc2, 0x00,		/* Input Ref. signal Def. */
	0xc3, 0x80,		/* I-port config */
	0xc4, 0x02,		/* hoffset low (input), 0x0002 is minimum */
	0xc5, 0x00,		/* hoffset hi (input) */
	0xc6, 0xd0,		/* hsize low (input), 0x02d0 = 720 */
	0xc7, 0x02,		/* hsize hi (input) */
	0xc8, 0x12,		/* voffset low (input), 0x12 = 18 */
	0xc9, 0x00,		/* voffset hi (input) */
	0xca, 0xf8,		/* vsize low (input), 0xf8 = 248 */
	0xcb, 0x00,		/* vsize hi (input) */
	0xcc, 0xd0,		/* hsize low (output), 0x02d0 = 720 */
	0xcd, 0x02,		/* hsize hi (output) */

	0xf0, 0xad,		/* Set PLL Register. 60hz 525 lines per frame, 27 MHz */
	0xf1, 0x05,		/* low bit with 0xF0 */
	0xf5, 0xad,		/* Set pulse generator register */
	0xf6, 0x01,

	0x87, 0x00,		/* Disable I-port output */
	0x88, 0xd0,		/* reset scaler */
	0x80, 0x20,		/* Activate only task "B", continuous mode (was 0xA0) */
	0x88, 0xf0,		/* activate scaler */
	0x87, 0x01,		/* Enable I-port output */
	0x00, 0x00
};

static const unsigned char saa7115_cfg_50hz_fullres_x[] = {
	0xcc, 0xd0,		/* hsize low (output), 720 same as 60hz */
	0xcd, 0x02,		/* hsize hi (output) */

	0xd0, 0x01,		/* down scale = 1 */
	0xd8, 0x00,		/* hor lum scaling 0x0400 = 1 */
	0xd9, 0x04,
	0xdc, 0x00,		/* hor chrom scaling 0x0200. must be hor lum scaling / 2 */
	0xdd, 0x02,		/* H-scaling incr chroma */

	0x00, 0x00
};
static const unsigned char saa7115_cfg_50hz_fullres_y[] = {
	0xce, 0x20,		/* vsize low (output), 0x0120 = 288 */
	0xcf, 0x01,		/* vsize hi (output) */

	0xd5, 0x40,		/* Lum contrast, nominal value = 0x40 */
	0xd6, 0x40,		/* Chroma satur. nominal value = 0x80 */

	0xe0, 0x00,		/* V-scaling incr luma low */
	0xe1, 0x04,		/* " hi */
	0xe2, 0x00,		/* V-scaling incr chroma low */
	0xe3, 0x04,		/* " hi */

	0x00, 0x00
};

static const unsigned char saa7115_cfg_50hz_video[] = {
	0x80, 0x00,		/* reset tasks */
	0x88, 0xd0,		/* reset scaler */

	0x15, 0x37,		/* VGATE start */
	0x16, 0x16,		/* VGATE stop */
	0x17, 0x99,		/* VGATE MSB and other values */

	0x08, 0x28,		/* 0x28 = PAL */
	0x0e, 0x07,		/* chrominance control 1 */

	0x5a, 0x03,		/* Vertical offset, standard 50hz value */

	/* Task A */
	0x90, 0x81,		/* Task Handling Control */
	0x91, 0x48,		/* X-port formats/config */
	0x92, 0x40,		/* Input Ref. signal Def. */
	0x93, 0x84,		/* I-port config */
	/* This is weird: the datasheet says that you should use 2 as the minimum value, */
	/* but Hauppauge uses 0, and changing that to 2 causes indeed problems (for 50hz) */
	0x94, 0x00,		/* hoffset low (input), 0x0002 is minimum */
	0x95, 0x00,		/* hoffset hi (input) */
	0x96, 0xd0,		/* hsize low (input), 0x02d0 = 720 */
	0x97, 0x02,		/* hsize hi (input) */
	0x98, 0x03,		/* voffset low (input) */
	0x99, 0x00,		/* voffset hi (input) */
	0x9a, 0x12,		/* vsize low (input), 0x12 = 18 */
	0x9b, 0x00,		/* vsize hi (input) */
	0x9c, 0xa0,		/* hsize low (output), 0x05a0 = 1440 */
	0x9d, 0x05,		/* hsize hi (output) */
	0x9e, 0x12,		/* vsize low (output), 0x12 = 18 */
	0x9f, 0x00,		/* vsize hi (output) */

	/* Task B */
	0xc0, 0x00,		/* Task Handling Control */
	0xc1, 0x08,		/* X-port formats/config */
	0xc2, 0x00,		/* Input Ref. signal Def. */
	0xc3, 0x80,		/* I-port config */
	0xc4, 0x00,		/* hoffset low (input), 0x0002 is minimum. See comment at 0x94 above. */
	0xc5, 0x00,		/* hoffset hi (input) */
	0xc6, 0xd0,		/* hsize low (input), 0x02d0 = 720 */
	0xc7, 0x02,		/* hsize hi (input) */
	0xc8, 0x16,		/* voffset low (input), 0x16 = 22 */
	0xc9, 0x00,		/* voffset hi (input) */
	0xca, 0x20,		/* vsize low (input), 0x0120 = 288 */
	0xcb, 0x01,		/* vsize hi (input) */
	0xcc, 0xd0,		/* hsize low (output), 0x02d0 = 720 */
	0xcd, 0x02,		/* hsize hi (output) */
	0xce, 0x20,		/* vsize low (output), 0x0120 = 288 */
	0xcf, 0x01,		/* vsize hi (output) */

	0xf0, 0xb0,		/* Set PLL Register. 50hz 625 lines per frame, 27 MHz */
	0xf1, 0x05,		/* low bit with 0xF0, (was 0x05) */
	0xf5, 0xb0,		/* Set pulse generator register */
	0xf6, 0x01,

	0x87, 0x00,		/* Disable I-port output */
	0x88, 0xd0,		/* reset scaler (was 0xD0) */
	0x80, 0x20,		/* Activate only task "B" */
	0x88, 0xf0,		/* activate scaler */
	0x87, 0x01,		/* Enable I-port output */
	0x00, 0x00
};

/* ============== SAA7715 VIDEO templates (end) =======  */

static const unsigned char saa7115_cfg_vbi_on[] = {
	0x80, 0x00,		/* reset tasks */
	0x88, 0xd0,		/* reset scaler */
	0x80, 0x30,		/* Activate both tasks */
	0x88, 0xf0,		/* activate scaler */
	0x87, 0x01,		/* Enable I-port output */
	0x00, 0x00
};

static const unsigned char saa7115_cfg_vbi_off[] = {
	0x80, 0x00,		/* reset tasks */
	0x88, 0xd0,		/* reset scaler */
	0x80, 0x20,		/* Activate only task "B" */
	0x88, 0xf0,		/* activate scaler */
	0x87, 0x01,		/* Enable I-port output */
	0x00, 0x00
};

static const unsigned char saa7113_init_auto_input[] = {
	0x01, 0x08,	/* PH7113_INCREMENT_DELAY - (1) (1) (1) (1) IDEL3 IDEL2 IDELL1 IDEL0 */
	0x02, 0xc2,	/* PH7113_ANALOG_INPUT_CONTR_1 - FUSE1 FUSE0 GUDL1 GUDL0 MODE3 MODE2 MODE1 MODE0 */
	0x03, 0x30,	/* PH7113_ANALOG_INPUT_CONTR_2 - (1) HLNRS VBSL WPOFF HOLDG GAFIX GAI28 GAI18 */
	0x04, 0x00,	/* PH7113_ANALOG_INPUT_CONTR_3 - GAI17 GAI16 GAI15 GAI14 GAI13 GAI12 GAI11 GAI10 */
	0x05, 0x00,	/* PH7113_ANALOG_INPUT_CONTR_4 - GAI27 GAI26 GAI25 GAI24 GAI23 GAI22 GAI21 GAI20 */
	0x06, 0x89,	/* PH7113_HORIZONTAL_SYNC_START - HSB7 HSB6 HSB5 HSB4 HSB3 HSB2 HSB1 HSB0 */
	0x07, 0x0d,	/* PH7113_HORIZONTAL_SYNC_STOP - HSS7 HSS6 HSS5 HSS4 HSS3 HSS2 HSS1 HSS0 */
	0x08, 0x88,	/* PH7113_SYNC_CONTROL - AUFD FSEL FOET HTC1 HTC0 HPLL VNOI1 VNOI0 */
	0x09, 0x01,	/* PH7113_LUMINANCE_CONTROL - BYPS PREF BPSS1 BPSS0 VBLB UPTCV APER1 APER0 */
	0x0a, 0x80,	/* PH7113_LUMINANCE_BRIGHTNESS - BRIG7 BRIG6 BRIG5 BRIG4 BRIG3 BRIG2 BRIG1 BRIG0 */
	0x0b, 0x47,	/* PH7113_LUMINANCE_CONTRAST - CONT7 CONT6 CONT5 CONT4 CONT3 CONT2 CONT1 CONT0 */
	0x0c, 0x40,	/* PH7113_CHROMA_SATURATION - SATN7 SATN6 SATN5 SATN4 SATN3 SATN2 SATN1 SATN0 */
	0x0d, 0x00,	/* PH7113_CHROMA_HUE_CONTROL - HUEC7 HUEC6 HUEC5 HUEC4 HUEC3 HUEC2 HUEC1 HUEC0 */
	0x0e, 0x01,	/* PH7113_CHROMA_CONTROL - CDTO CSTD2 CSTD1 CSTD0 DCCF FCTC CHBW1 CHBW0 */
	0x0f, 0x2a,	/* PH7113_CHROMA_GAIN_CONTROL - ACGC CGAIN6 CGAIN5 CGAIN4 CGAIN3 CGAIN2 CGAIN1 CGAIN0 */
	0x10, 0x08,	/* PH7113_FORMAT_DELAY_CONTROL - OFTS1 OFTS0 HDEL1 HDEL0 VRLN YDEL2 YDEL1 YDEL0 */
	0x11, 0x0c,	/* PH7113_OUTPUT_CONTROL_1 - GPSW1 CM99 GPSW0 HLSEL OEYC OERT VIPB COLO */
	0x12, 0x07,	/* PH7113_OUTPUT_CONTROL_2 - RTSE13 RTSE12 RTSE11 RTSE10 RTSE03 RTSE02 RTSE01 RTSE00 */
	0x13, 0x00,	/* PH7113_OUTPUT_CONTROL_3 - ADLSB (1) (1) OLDSB FIDP (1) AOSL1 AOSL0 */
	0x14, 0x00,	/* RESERVED 14 - (1) (1) (1) (1) (1) (1) (1) (1) */
	0x15, 0x00,	/* PH7113_V_GATE1_START - VSTA7 VSTA6 VSTA5 VSTA4 VSTA3 VSTA2 VSTA1 VSTA0 */
	0x16, 0x00,	/* PH7113_V_GATE1_STOP - VSTO7 VSTO6 VSTO5 VSTO4 VSTO3 VSTO2 VSTO1 VSTO0 */
	0x17, 0x00,	/* PH7113_V_GATE1_MSB - (1) (1) (1) (1) (1) (1) VSTO8 VSTA8 */
	0x00, 0x00
};

static const unsigned char saa7115_init_misc[] = {
	0x81, 0x01,		/* reg 0x15,0x16 define blanking window */
	0x82, 0x00,
	0x83, 0x01,		/* I port settings */
	0x84, 0x20,
	0x85, 0x21,
	0x86, 0xc5,
	0x87, 0x01,

	/* Task A */
	0xa0, 0x01,		/* down scale = 1 */
	0xa1, 0x00,		/* prescale accumulation length = 1 */
	0xa2, 0x00,		/* dc gain and fir prefilter control */
	0xa4, 0x80,		/* Lum Brightness, nominal value = 0x80 */
	0xa5, 0x40,		/* Lum contrast, nominal value = 0x40 */
	0xa6, 0x40,		/* Chroma satur. nominal value = 0x80 */
	0xa8, 0x00,		/* hor lum scaling 0x0200 = 2 zoom */
	0xa9, 0x02,		/* note: 2 x zoom ensures that VBI lines have same length as video lines. */
	0xaa, 0x00,		/* H-phase offset Luma = 0 */
	0xac, 0x00,		/* hor chrom scaling 0x0200. must be hor lum scaling / 2 */
	0xad, 0x01,		/* H-scaling incr chroma */
	0xae, 0x00,		/* H-phase offset chroma. must be offset luma / 2 */

	0xb0, 0x00,		/* V-scaling incr luma low */
	0xb1, 0x04,		/* " hi */
	0xb2, 0x00,		/* V-scaling incr chroma low */
	0xb3, 0x04,		/* " hi */
	0xb4, 0x01,		/* V-scaling mode control */
	0xb8, 0x00,		/* V-phase offset chroma 00 */
	0xb9, 0x00,		/* V-phase offset chroma 01 */
	0xba, 0x00,		/* V-phase offset chroma 10 */
	0xbb, 0x00,		/* V-phase offset chroma 11 */
	0xbc, 0x00,		/* V-phase offset luma 00 */
	0xbd, 0x00,		/* V-phase offset luma 01 */
	0xbe, 0x00,		/* V-phase offset luma 10 */
	0xbf, 0x00,		/* V-phase offset luma 11 */

	/* Task B */
	0xd0, 0x01,		/* down scale = 1 */
	0xd1, 0x00,		/* prescale accumulation length = 1 */
	0xd2, 0x00,		/* dc gain and fir prefilter control */
	0xd4, 0x80,		/* Lum Brightness, nominal value = 0x80 */
	0xd5, 0x40,		/* Lum contrast, nominal value = 0x40 */
	0xd6, 0x40,		/* Chroma satur. nominal value = 0x80 */
	0xd8, 0x00,		/* hor lum scaling 0x0400 = 1 */
	0xd9, 0x04,
	0xda, 0x00,		/* H-phase offset Luma = 0 */
	0xdc, 0x00,		/* hor chrom scaling 0x0200. must be hor lum scaling / 2 */
	0xdd, 0x02,		/* H-scaling incr chroma */
	0xde, 0x00,		/* H-phase offset chroma. must be offset luma / 2 */

	0xe0, 0x00,		/* V-scaling incr luma low */
	0xe1, 0x04,		/* " hi */
	0xe2, 0x00,		/* V-scaling incr chroma low */
	0xe3, 0x04,		/* " hi */
	0xe4, 0x01,		/* V-scaling mode control */
	0xe8, 0x00,		/* V-phase offset chroma 00 */
	0xe9, 0x00,		/* V-phase offset chroma 01 */
	0xea, 0x00,		/* V-phase offset chroma 10 */
	0xeb, 0x00,		/* V-phase offset chroma 11 */
	0xec, 0x00,		/* V-phase offset luma 00 */
	0xed, 0x00,		/* V-phase offset luma 01 */
	0xee, 0x00,		/* V-phase offset luma 10 */
	0xef, 0x00,		/* V-phase offset luma 11 */

	0xf2, 0x50,		/* crystal clock = 24.576 MHz, target = 27MHz */
	0xf3, 0x46,
	0xf4, 0x00,
	0xf7, 0x4b,		/* not the recommended settings! */
	0xf8, 0x00,
	0xf9, 0x4b,
	0xfa, 0x00,
	0xfb, 0x4b,
	0xff, 0x88,		/* PLL2 lock detection settings: 71 lines 50% phase error */

	/* Turn off VBI */
	0x40, 0x20,             /* No framing code errors allowed. */
	0x41, 0xff,
	0x42, 0xff,
	0x43, 0xff,
	0x44, 0xff,
	0x45, 0xff,
	0x46, 0xff,
	0x47, 0xff,
	0x48, 0xff,
	0x49, 0xff,
	0x4a, 0xff,
	0x4b, 0xff,
	0x4c, 0xff,
	0x4d, 0xff,
	0x4e, 0xff,
	0x4f, 0xff,
	0x50, 0xff,
	0x51, 0xff,
	0x52, 0xff,
	0x53, 0xff,
	0x54, 0xff,
	0x55, 0xff,
	0x56, 0xff,
	0x57, 0xff,
	0x58, 0x40,
	0x59, 0x47,
	0x5b, 0x83,
	0x5d, 0xbd,
	0x5e, 0x35,

	0x02, 0x84,		/* input tuner -> input 4, amplifier active */
	0x09, 0x53,		/* 0x53, was 0x56 for 60hz. luminance control */

	0x80, 0x20,		/* enable task B */
	0x88, 0xd0,
	0x88, 0xf0,
	0x00, 0x00
};

static int saa7115_odd_parity(u8 c)
{
	c ^= (c >> 4);
	c ^= (c >> 2);
	c ^= (c >> 1);

	return c & 1;
}

static int saa7115_decode_vps(u8 * dst, u8 * p)
{
	static const u8 biphase_tbl[] = {
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
		0xd2, 0x5a, 0x52, 0xd2, 0x96, 0x1e, 0x16, 0x96,
		0x92, 0x1a, 0x12, 0x92, 0xd2, 0x5a, 0x52, 0xd2,
		0xd0, 0x58, 0x50, 0xd0, 0x94, 0x1c, 0x14, 0x94,
		0x90, 0x18, 0x10, 0x90, 0xd0, 0x58, 0x50, 0xd0,
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
		0xe1, 0x69, 0x61, 0xe1, 0xa5, 0x2d, 0x25, 0xa5,
		0xa1, 0x29, 0x21, 0xa1, 0xe1, 0x69, 0x61, 0xe1,
		0xc3, 0x4b, 0x43, 0xc3, 0x87, 0x0f, 0x07, 0x87,
		0x83, 0x0b, 0x03, 0x83, 0xc3, 0x4b, 0x43, 0xc3,
		0xc1, 0x49, 0x41, 0xc1, 0x85, 0x0d, 0x05, 0x85,
		0x81, 0x09, 0x01, 0x81, 0xc1, 0x49, 0x41, 0xc1,
		0xe1, 0x69, 0x61, 0xe1, 0xa5, 0x2d, 0x25, 0xa5,
		0xa1, 0x29, 0x21, 0xa1, 0xe1, 0x69, 0x61, 0xe1,
		0xe0, 0x68, 0x60, 0xe0, 0xa4, 0x2c, 0x24, 0xa4,
		0xa0, 0x28, 0x20, 0xa0, 0xe0, 0x68, 0x60, 0xe0,
		0xc2, 0x4a, 0x42, 0xc2, 0x86, 0x0e, 0x06, 0x86,
		0x82, 0x0a, 0x02, 0x82, 0xc2, 0x4a, 0x42, 0xc2,
		0xc0, 0x48, 0x40, 0xc0, 0x84, 0x0c, 0x04, 0x84,
		0x80, 0x08, 0x00, 0x80, 0xc0, 0x48, 0x40, 0xc0,
		0xe0, 0x68, 0x60, 0xe0, 0xa4, 0x2c, 0x24, 0xa4,
		0xa0, 0x28, 0x20, 0xa0, 0xe0, 0x68, 0x60, 0xe0,
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
		0xd2, 0x5a, 0x52, 0xd2, 0x96, 0x1e, 0x16, 0x96,
		0x92, 0x1a, 0x12, 0x92, 0xd2, 0x5a, 0x52, 0xd2,
		0xd0, 0x58, 0x50, 0xd0, 0x94, 0x1c, 0x14, 0x94,
		0x90, 0x18, 0x10, 0x90, 0xd0, 0x58, 0x50, 0xd0,
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
	};
	int i;
	u8 c, err = 0;

	for (i = 0; i < 2 * 13; i += 2) {
		err |= biphase_tbl[p[i]] | biphase_tbl[p[i + 1]];
		c = (biphase_tbl[p[i + 1]] & 0xf) | ((biphase_tbl[p[i]] & 0xf) << 4);
		dst[i / 2] = c;
	}
	return err & 0xf0;
}

static int saa7115_decode_wss(u8 * p)
{
	static const int wss_bits[8] = {
		0, 0, 0, 1, 0, 1, 1, 1
	};
	unsigned char parity;
	int wss = 0;
	int i;

	for (i = 0; i < 16; i++) {
		int b1 = wss_bits[p[i] & 7];
		int b2 = wss_bits[(p[i] >> 3) & 7];

		if (b1 == b2)
			return -1;
		wss |= b2 << i;
	}
	parity = wss & 15;
	parity ^= parity >> 2;
	parity ^= parity >> 1;

	if (!(parity & 1))
		return -1;

	return wss;
}


static int saa7115_set_audio_clock_freq(struct i2c_client *client, u32 freq)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	u32 acpf;
	u32 acni;
	u32 hz;
	u64 f;
	u8 acc = 0; 	/* reg 0x3a, audio clock control */

	v4l_dbg(1, debug, client, "set audio clock freq: %d\n", freq);

	/* sanity check */
	if (freq < 32000 || freq > 48000)
		return -EINVAL;

	/* The saa7113 has no audio clock */
	if (state->ident == V4L2_IDENT_SAA7113)
		return 0;

	/* hz is the refresh rate times 100 */
	hz = (state->std & V4L2_STD_525_60) ? 5994 : 5000;
	/* acpf = (256 * freq) / field_frequency == (256 * 100 * freq) / hz */
	acpf = (25600 * freq) / hz;
	/* acni = (256 * freq * 2^23) / crystal_frequency =
		  (freq * 2^(8+23)) / crystal_frequency =
		  (freq << 31) / crystal_frequency */
	f = freq;
	f = f << 31;
	do_div(f, state->crystal_freq);
	acni = f;
	if (state->ucgc) {
		acpf = acpf * state->cgcdiv / 16;
		acni = acni * state->cgcdiv / 16;
		acc = 0x80;
		if (state->cgcdiv == 3)
			acc |= 0x40;
	}
	if (state->apll)
		acc |= 0x08;

	saa7115_write(client, 0x38, 0x03);
	saa7115_write(client, 0x39, 0x10);
	saa7115_write(client, 0x3a, acc);
	saa7115_write(client, 0x30, acpf & 0xff);
	saa7115_write(client, 0x31, (acpf >> 8) & 0xff);
	saa7115_write(client, 0x32, (acpf >> 16) & 0x03);
	saa7115_write(client, 0x34, acni & 0xff);
	saa7115_write(client, 0x35, (acni >> 8) & 0xff);
	saa7115_write(client, 0x36, (acni >> 16) & 0x3f);
	state->audclk_freq = freq;
	return 0;
}

static int saa7115_set_v4lctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct saa7115_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(client, "invalid brightness setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->bright = ctrl->value;
		saa7115_write(client, 0x0a, state->bright);
		break;

	case V4L2_CID_CONTRAST:
		if (ctrl->value < 0 || ctrl->value > 127) {
			v4l_err(client, "invalid contrast setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->contrast = ctrl->value;
		saa7115_write(client, 0x0b, state->contrast);
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value < 0 || ctrl->value > 127) {
			v4l_err(client, "invalid saturation setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->sat = ctrl->value;
		saa7115_write(client, 0x0c, state->sat);
		break;

	case V4L2_CID_HUE:
		if (ctrl->value < -127 || ctrl->value > 127) {
			v4l_err(client, "invalid hue setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->hue = ctrl->value;
		saa7115_write(client, 0x0d, state->hue);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int saa7115_get_v4lctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct saa7115_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = state->bright;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = state->contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = state->sat;
		break;
	case V4L2_CID_HUE:
		ctrl->value = state->hue;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void saa7115_set_v4lstd(struct i2c_client *client, v4l2_std_id std)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	int taskb = saa7115_read(client, 0x80) & 0x10;

	/* Prevent unnecessary standard changes. During a standard
	   change the I-Port is temporarily disabled. Any devices
	   reading from that port can get confused.
	   Note that VIDIOC_S_STD is also used to switch from
	   radio to TV mode, so if a VIDIOC_S_STD is broadcast to
	   all I2C devices then you do not want to have an unwanted
	   side-effect here. */
	if (std == state->std)
		return;

	// This works for NTSC-M, SECAM-L and the 50Hz PAL variants.
	if (std & V4L2_STD_525_60) {
		v4l_dbg(1, debug, client, "decoder set standard 60 Hz\n");
		saa7115_writeregs(client, saa7115_cfg_60hz_video);
	} else {
		v4l_dbg(1, debug, client, "decoder set standard 50 Hz\n");
		saa7115_writeregs(client, saa7115_cfg_50hz_video);
	}

	/* Register 0E - Bits D6-D4 on NO-AUTO mode
		(SAA7113 doesn't have auto mode)
	    50 Hz / 625 lines           60 Hz / 525 lines
	000 PAL BGDHI (4.43Mhz)         NTSC M (3.58MHz)
	001 NTSC 4.43 (50 Hz)           PAL 4.43 (60 Hz)
	010 Combination-PAL N (3.58MHz) NTSC 4.43 (60 Hz)
	011 NTSC N (3.58MHz)            PAL M (3.58MHz)
	100 reserved                    NTSC-Japan (3.58MHz)
	*/
	if (state->ident == V4L2_IDENT_SAA7113) {
		u8 reg = saa7115_read(client, 0x0e) & 0x8f;

		if (std == V4L2_STD_PAL_M) {
			reg |= 0x30;
		} else if (std == V4L2_STD_PAL_N) {
			reg |= 0x20;
		} else if (std == V4L2_STD_PAL_60) {
			reg |= 0x10;
		} else if (std == V4L2_STD_NTSC_M_JP) {
			reg |= 0x40;
		}
		saa7115_write(client, 0x0e, reg);
	}


	state->std = std;

	/* restart task B if needed */
	if (taskb && state->ident != V4L2_IDENT_SAA7115) {
		saa7115_writeregs(client, saa7115_cfg_vbi_on);
	}

	/* switch audio mode too! */
	saa7115_set_audio_clock_freq(client, state->audclk_freq);
}

static v4l2_std_id saa7115_get_v4lstd(struct i2c_client *client)
{
	struct saa7115_state *state = i2c_get_clientdata(client);

	return state->std;
}

static void saa7115_log_status(struct i2c_client *client)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	int reg1e, reg1f;
	int signalOk;
	int vcr;

	v4l_info(client, "Audio frequency: %d Hz\n", state->audclk_freq);
	if (state->ident != V4L2_IDENT_SAA7115) {
		/* status for the saa7114 */
		reg1f = saa7115_read(client, 0x1f);
		signalOk = (reg1f & 0xc1) == 0x81;
		v4l_info(client, "Video signal:    %s\n", signalOk ? "ok" : "bad");
		v4l_info(client, "Frequency:       %s\n", (reg1f & 0x20) ? "60 Hz" : "50 Hz");
		return;
	}

	/* status for the saa7115 */
	reg1e = saa7115_read(client, 0x1e);
	reg1f = saa7115_read(client, 0x1f);

	signalOk = (reg1f & 0xc1) == 0x81 && (reg1e & 0xc0) == 0x80;
	vcr = !(reg1f & 0x10);

	if (state->input >= 6) {
		v4l_info(client, "Input:           S-Video %d\n", state->input - 6);
	} else {
		v4l_info(client, "Input:           Composite %d\n", state->input);
	}
	v4l_info(client, "Video signal:    %s\n", signalOk ? (vcr ? "VCR" : "broadcast/DVD") : "bad");
	v4l_info(client, "Frequency:       %s\n", (reg1f & 0x20) ? "60 Hz" : "50 Hz");

	switch (reg1e & 0x03) {
		case 1:
			v4l_info(client, "Detected format: NTSC\n");
			break;
		case 2:
			v4l_info(client, "Detected format: PAL\n");
			break;
		case 3:
			v4l_info(client, "Detected format: SECAM\n");
			break;
		default:
			v4l_info(client, "Detected format: BW/No color\n");
			break;
	}
}

/* setup the sliced VBI lcr registers according to the sliced VBI format */
static void saa7115_set_lcr(struct i2c_client *client, struct v4l2_sliced_vbi_format *fmt)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	int is_50hz = (state->std & V4L2_STD_625_50);
	u8 lcr[24];
	int i, x;

	/* saa7113/7114 doesn't yet support VBI */
	if (state->ident != V4L2_IDENT_SAA7115)
		return;

	for (i = 0; i <= 23; i++)
		lcr[i] = 0xff;

	if (fmt->service_set == 0) {
		/* raw VBI */
		if (is_50hz)
			for (i = 6; i <= 23; i++)
				lcr[i] = 0xdd;
		else
			for (i = 10; i <= 21; i++)
				lcr[i] = 0xdd;
	} else {
		/* sliced VBI */
		/* first clear lines that cannot be captured */
		if (is_50hz) {
			for (i = 0; i <= 5; i++)
				fmt->service_lines[0][i] =
					fmt->service_lines[1][i] = 0;
		}
		else {
			for (i = 0; i <= 9; i++)
				fmt->service_lines[0][i] =
					fmt->service_lines[1][i] = 0;
			for (i = 22; i <= 23; i++)
				fmt->service_lines[0][i] =
					fmt->service_lines[1][i] = 0;
		}

		/* Now set the lcr values according to the specified service */
		for (i = 6; i <= 23; i++) {
			lcr[i] = 0;
			for (x = 0; x <= 1; x++) {
				switch (fmt->service_lines[1-x][i]) {
					case 0:
						lcr[i] |= 0xf << (4 * x);
						break;
					case V4L2_SLICED_TELETEXT_B:
						lcr[i] |= 1 << (4 * x);
						break;
					case V4L2_SLICED_CAPTION_525:
						lcr[i] |= 4 << (4 * x);
						break;
					case V4L2_SLICED_WSS_625:
						lcr[i] |= 5 << (4 * x);
						break;
					case V4L2_SLICED_VPS:
						lcr[i] |= 7 << (4 * x);
						break;
				}
			}
		}
	}

	/* write the lcr registers */
	for (i = 2; i <= 23; i++) {
		saa7115_write(client, i - 2 + 0x41, lcr[i]);
	}

	/* enable/disable raw VBI capturing */
	saa7115_writeregs(client, fmt->service_set == 0 ? saa7115_cfg_vbi_on : saa7115_cfg_vbi_off);
}

static int saa7115_get_v4lfmt(struct i2c_client *client, struct v4l2_format *fmt)
{
	static u16 lcr2vbi[] = {
		0, V4L2_SLICED_TELETEXT_B, 0,	/* 1 */
		0, V4L2_SLICED_CAPTION_525,	/* 4 */
		V4L2_SLICED_WSS_625, 0,		/* 5 */
		V4L2_SLICED_VPS, 0, 0, 0, 0,	/* 7 */
		0, 0, 0, 0
	};
	struct v4l2_sliced_vbi_format *sliced = &fmt->fmt.sliced;
	int i;

	if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
		return -EINVAL;
	memset(sliced, 0, sizeof(*sliced));
	/* done if using raw VBI */
	if (saa7115_read(client, 0x80) & 0x10)
		return 0;
	for (i = 2; i <= 23; i++) {
		u8 v = saa7115_read(client, i - 2 + 0x41);

		sliced->service_lines[0][i] = lcr2vbi[v >> 4];
		sliced->service_lines[1][i] = lcr2vbi[v & 0xf];
		sliced->service_set |=
			sliced->service_lines[0][i] | sliced->service_lines[1][i];
	}
	return 0;
}

static int saa7115_set_v4lfmt(struct i2c_client *client, struct v4l2_format *fmt)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	struct v4l2_pix_format *pix;
	int HPSC, HFSC;
	int VSCY, Vsrc;
	int is_50hz = state->std & V4L2_STD_625_50;

	if (fmt->type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) {
		saa7115_set_lcr(client, &fmt->fmt.sliced);
		return 0;
	}
	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	pix = &(fmt->fmt.pix);

	v4l_dbg(1, debug, client, "decoder set size\n");

	/* FIXME need better bounds checking here */
	if ((pix->width < 1) || (pix->width > 1440))
		return -EINVAL;
	if ((pix->height < 1) || (pix->height > 960))
		return -EINVAL;

	/* probably have a valid size, let's set it */
	/* Set output width/height */
	/* width */
	saa7115_write(client, 0xcc, (u8) (pix->width & 0xff));
	saa7115_write(client, 0xcd, (u8) ((pix->width >> 8) & 0xff));
	/* height */
	saa7115_write(client, 0xce, (u8) (pix->height & 0xff));
	saa7115_write(client, 0xcf, (u8) ((pix->height >> 8) & 0xff));

	/* Scaling settings */
	/* Hprescaler is floor(inres/outres) */
	/* FIXME hardcoding input res */
	if (pix->width != 720) {
		HPSC = (int)(720 / pix->width);
		/* 0 is not allowed (div. by zero) */
		HPSC = HPSC ? HPSC : 1;
		HFSC = (int)((1024 * 720) / (HPSC * pix->width));

		v4l_dbg(1, debug, client, "Hpsc: 0x%05x, Hfsc: 0x%05x\n", HPSC, HFSC);
		/* FIXME hardcodes to "Task B"
		 * write H prescaler integer */
		saa7115_write(client, 0xd0, (u8) (HPSC & 0x3f));

		/* write H fine-scaling (luminance) */
		saa7115_write(client, 0xd8, (u8) (HFSC & 0xff));
		saa7115_write(client, 0xd9, (u8) ((HFSC >> 8) & 0xff));
		/* write H fine-scaling (chrominance)
		 * must be lum/2, so i'll just bitshift :) */
		saa7115_write(client, 0xDC, (u8) ((HFSC >> 1) & 0xff));
		saa7115_write(client, 0xDD, (u8) ((HFSC >> 9) & 0xff));
	} else {
		if (is_50hz) {
			v4l_dbg(1, debug, client, "Setting full 50hz width\n");
			saa7115_writeregs(client, saa7115_cfg_50hz_fullres_x);
		} else {
			v4l_dbg(1, debug, client, "Setting full 60hz width\n");
			saa7115_writeregs(client, saa7115_cfg_60hz_fullres_x);
		}
	}

	Vsrc = is_50hz ? 576 : 480;

	if (pix->height != Vsrc) {
		VSCY = (int)((1024 * Vsrc) / pix->height);
		v4l_dbg(1, debug, client, "Vsrc: %d, Vscy: 0x%05x\n", Vsrc, VSCY);

		/* Correct Contrast and Luminance */
		saa7115_write(client, 0xd5, (u8) (64 * 1024 / VSCY));
		saa7115_write(client, 0xd6, (u8) (64 * 1024 / VSCY));

		/* write V fine-scaling (luminance) */
		saa7115_write(client, 0xe0, (u8) (VSCY & 0xff));
		saa7115_write(client, 0xe1, (u8) ((VSCY >> 8) & 0xff));
		/* write V fine-scaling (chrominance) */
		saa7115_write(client, 0xe2, (u8) (VSCY & 0xff));
		saa7115_write(client, 0xe3, (u8) ((VSCY >> 8) & 0xff));
	} else {
		if (is_50hz) {
			v4l_dbg(1, debug, client, "Setting full 50Hz height\n");
			saa7115_writeregs(client, saa7115_cfg_50hz_fullres_y);
		} else {
			v4l_dbg(1, debug, client, "Setting full 60hz height\n");
			saa7115_writeregs(client, saa7115_cfg_60hz_fullres_y);
		}
	}

	saa7115_writeregs(client, saa7115_cfg_reset_scaler);
	return 0;
}

/* Decode the sliced VBI data stream as created by the saa7115.
   The format is described in the saa7115 datasheet in Tables 25 and 26
   and in Figure 33.
   The current implementation uses SAV/EAV codes and not the ancillary data
   headers. The vbi->p pointer points to the SDID byte right after the SAV
   code. */
static void saa7115_decode_vbi_line(struct i2c_client *client,
				    struct v4l2_decode_vbi_line *vbi)
{
	static const char vbi_no_data_pattern[] = {
		0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0
	};
	struct saa7115_state *state = i2c_get_clientdata(client);
	u8 *p = vbi->p;
	u32 wss;
	int id1, id2;   /* the ID1 and ID2 bytes from the internal header */

	vbi->type = 0;  /* mark result as a failure */
	id1 = p[2];
	id2 = p[3];
	/* Note: the field bit is inverted for 60 Hz video */
	if (state->std & V4L2_STD_525_60)
		id1 ^= 0x40;

	/* Skip internal header, p now points to the start of the payload */
	p += 4;
	vbi->p = p;

	/* calculate field and line number of the VBI packet (1-23) */
	vbi->is_second_field = ((id1 & 0x40) != 0);
	vbi->line = (id1 & 0x3f) << 3;
	vbi->line |= (id2 & 0x70) >> 4;

	/* Obtain data type */
	id2 &= 0xf;

	/* If the VBI slicer does not detect any signal it will fill up
	   the payload buffer with 0xa0 bytes. */
	if (!memcmp(p, vbi_no_data_pattern, sizeof(vbi_no_data_pattern)))
		return;

	/* decode payloads */
	switch (id2) {
	case 1:
		vbi->type = V4L2_SLICED_TELETEXT_B;
		break;
	case 4:
		if (!saa7115_odd_parity(p[0]) || !saa7115_odd_parity(p[1]))
			return;
		vbi->type = V4L2_SLICED_CAPTION_525;
		break;
	case 5:
		wss = saa7115_decode_wss(p);
		if (wss == -1)
			return;
		p[0] = wss & 0xff;
		p[1] = wss >> 8;
		vbi->type = V4L2_SLICED_WSS_625;
		break;
	case 7:
		if (saa7115_decode_vps(p, p) != 0)
			return;
		vbi->type = V4L2_SLICED_VPS;
		break;
	default:
		return;
	}
}

/* ============ SAA7115 AUDIO settings (end) ============= */

static struct v4l2_queryctrl saa7115_qctrl[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 128,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 127,
		.step          = 1,
		.default_value = 64,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 127,
		.step          = 1,
		.default_value = 64,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 1,
		.default_value = 0,
		.flags 	       = 0,
	},
};

/* ----------------------------------------------------------------------- */

static int saa7115_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	int *iarg = arg;

	/* ioctls to allow direct access to the saa7115 registers for testing */
	switch (cmd) {
	case VIDIOC_S_FMT:
		return saa7115_set_v4lfmt(client, (struct v4l2_format *)arg);

	case VIDIOC_G_FMT:
		return saa7115_get_v4lfmt(client, (struct v4l2_format *)arg);

	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
		return saa7115_set_audio_clock_freq(client, *(u32 *)arg);

	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *vt = arg;
		int status;

		if (state->radio)
			break;
		status = saa7115_read(client, 0x1f);

		v4l_dbg(1, debug, client, "status: 0x%02x\n", status);
		vt->signal = ((status & (1 << 6)) == 0) ? 0xffff : 0x0;
		break;
	}

	case VIDIOC_LOG_STATUS:
		saa7115_log_status(client);
		break;

	case VIDIOC_G_CTRL:
		return saa7115_get_v4lctrl(client, (struct v4l2_control *)arg);

	case VIDIOC_S_CTRL:
		return saa7115_set_v4lctrl(client, (struct v4l2_control *)arg);

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;
		int i;

		for (i = 0; i < ARRAY_SIZE(saa7115_qctrl); i++)
			if (qc->id && qc->id == saa7115_qctrl[i].id) {
				memcpy(qc, &saa7115_qctrl[i], sizeof(*qc));
				return 0;
			}
		return -EINVAL;
	}

	case VIDIOC_G_STD:
		*(v4l2_std_id *)arg = saa7115_get_v4lstd(client);
		break;

	case VIDIOC_S_STD:
		state->radio = 0;
		saa7115_set_v4lstd(client, *(v4l2_std_id *)arg);
		break;

	case AUDC_SET_RADIO:
		state->radio = 1;
		break;

	case VIDIOC_INT_G_VIDEO_ROUTING:
	{
		struct v4l2_routing *route = arg;

		route->input = state->input;
		route->output = 0;
		break;
	}

	case VIDIOC_INT_S_VIDEO_ROUTING:
	{
		struct v4l2_routing *route = arg;

		v4l_dbg(1, debug, client, "decoder set input %d\n", route->input);
		/* saa7113 does not have these inputs */
		if (state->ident == V4L2_IDENT_SAA7113 &&
		    (route->input == SAA7115_COMPOSITE4 ||
		     route->input == SAA7115_COMPOSITE5)) {
			return -EINVAL;
		}
		if (route->input > SAA7115_SVIDEO3)
			return -EINVAL;
		if (state->input == route->input)
			break;
		v4l_dbg(1, debug, client, "now setting %s input\n",
			(route->input >= SAA7115_SVIDEO0) ? "S-Video" : "Composite");
		state->input = route->input;

		/* select mode */
		saa7115_write(client, 0x02,
			      (saa7115_read(client, 0x02) & 0xf0) |
			       state->input);

		/* bypass chrominance trap for S-Video modes */
		saa7115_write(client, 0x09,
			      (saa7115_read(client, 0x09) & 0x7f) |
			       (state->input >= SAA7115_SVIDEO0 ? 0x80 : 0x0));
		break;
	}

	case VIDIOC_G_INPUT:
		*(int *)arg = state->input;
		break;

	case VIDIOC_S_INPUT:
		v4l_dbg(1, debug, client, "decoder set input %d\n", *iarg);
		/* inputs from 0-9 are available */
		if (*iarg < 0 || *iarg > 9) {
			return -EINVAL;
		}

		if (state->input == *iarg)
			break;
		v4l_dbg(1, debug, client, "now setting %s input\n",
			*iarg >= 6 ? "S-Video" : "Composite");
		state->input = *iarg;

		/* select mode */
		saa7115_write(client, 0x02,
			      (saa7115_read(client, 0x02) & 0xf0) |
			       state->input);

		/* bypass chrominance trap for modes 6..9 */
		saa7115_write(client, 0x09,
			      (saa7115_read(client, 0x09) & 0x7f) |
			       (state->input < 6 ? 0x0 : 0x80));
		break;

	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF:
		v4l_dbg(1, debug, client, "%s output\n",
			(cmd == VIDIOC_STREAMON) ? "enable" : "disable");

		if (state->enable != (cmd == VIDIOC_STREAMON)) {
			state->enable = (cmd == VIDIOC_STREAMON);
			saa7115_write(client, 0x87, state->enable);
		}
		break;

	case VIDIOC_INT_S_CRYSTAL_FREQ:
	{
		struct v4l2_crystal_freq *freq = arg;

		if (freq->freq != SAA7115_FREQ_32_11_MHZ &&
		    freq->freq != SAA7115_FREQ_24_576_MHZ)
			return -EINVAL;
		state->crystal_freq = freq->freq;
		state->cgcdiv = (freq->flags & SAA7115_FREQ_FL_CGCDIV) ? 3 : 4;
		state->ucgc = (freq->flags & SAA7115_FREQ_FL_UCGC) ? 1 : 0;
		state->apll = (freq->flags & SAA7115_FREQ_FL_APLL) ? 1 : 0;
		saa7115_set_audio_clock_freq(client, state->audclk_freq);
		break;
	}

	case VIDIOC_INT_DECODE_VBI_LINE:
		saa7115_decode_vbi_line(client, arg);
		break;

	case VIDIOC_INT_RESET:
		v4l_dbg(1, debug, client, "decoder RESET\n");
		saa7115_writeregs(client, saa7115_cfg_reset_scaler);
		break;

	case VIDIOC_INT_G_VBI_DATA:
	{
		struct v4l2_sliced_vbi_data *data = arg;

		switch (data->id) {
		case V4L2_SLICED_WSS_625:
			if (saa7115_read(client, 0x6b) & 0xc0)
				return -EIO;
			data->data[0] = saa7115_read(client, 0x6c);
			data->data[1] = saa7115_read(client, 0x6d);
			return 0;
		case V4L2_SLICED_CAPTION_525:
			if (data->field == 0) {
				/* CC */
				if (saa7115_read(client, 0x66) & 0xc0)
					return -EIO;
				data->data[0] = saa7115_read(client, 0x67);
				data->data[1] = saa7115_read(client, 0x68);
				return 0;
			}
			/* XDS */
			if (saa7115_read(client, 0x66) & 0x30)
				return -EIO;
			data->data[0] = saa7115_read(client, 0x69);
			data->data[1] = saa7115_read(client, 0x6a);
			return 0;
		default:
			return -EINVAL;
		}
		break;
	}

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_INT_G_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_SAA711X)
			return -EINVAL;
		reg->val = saa7115_read(client, reg->reg & 0xff);
		break;
	}

	case VIDIOC_INT_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_SAA711X)
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		saa7115_write(client, reg->reg & 0xff, reg->val & 0xff);
		break;
	}
#endif

	case VIDIOC_INT_G_CHIP_IDENT:
		*iarg = state->ident;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver_saa7115;

static int saa7115_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct saa7115_state *state;
	u8 chip_id;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_saa7115;
	snprintf(client->name, sizeof(client->name) - 1, "saa7115");

	v4l_dbg(1, debug, client, "detecting saa7115 client on address 0x%x\n", address << 1);

	saa7115_write(client, 0, 5);
	chip_id = saa7115_read(client, 0) & 0x0f;
	if (chip_id < 3 && chip_id > 5) {
		v4l_dbg(1, debug, client, "saa7115 not found\n");
		kfree(client);
		return 0;
	}
	snprintf(client->name, sizeof(client->name) - 1, "saa711%d",chip_id);
	v4l_info(client, "saa711%d found @ 0x%x (%s)\n", chip_id, address << 1, adapter->name);

	state = kzalloc(sizeof(struct saa7115_state), GFP_KERNEL);
	i2c_set_clientdata(client, state);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	state->std = V4L2_STD_NTSC;
	state->input = -1;
	state->enable = 1;
	state->radio = 0;
	state->bright = 128;
	state->contrast = 64;
	state->hue = 0;
	state->sat = 64;
	switch (chip_id) {
	case 3:
		state->ident = V4L2_IDENT_SAA7113;
		break;
	case 4:
		state->ident = V4L2_IDENT_SAA7114;
		break;
	default:
		state->ident = V4L2_IDENT_SAA7115;
		break;
	}

	state->audclk_freq = 48000;

	v4l_dbg(1, debug, client, "writing init values\n");

	/* init to 60hz/48khz */
	if (state->ident == V4L2_IDENT_SAA7113) {
		state->crystal_freq = SAA7115_FREQ_24_576_MHZ;
		saa7115_writeregs(client, saa7113_init_auto_input);
	} else {
		state->crystal_freq = SAA7115_FREQ_32_11_MHZ;
		saa7115_writeregs(client, saa7115_init_auto_input);
	}
	saa7115_writeregs(client, saa7115_init_misc);
	saa7115_writeregs(client, saa7115_cfg_60hz_fullres_x);
	saa7115_writeregs(client, saa7115_cfg_60hz_fullres_y);
	saa7115_writeregs(client, saa7115_cfg_60hz_video);
	saa7115_set_audio_clock_freq(client, state->audclk_freq);
	saa7115_writeregs(client, saa7115_cfg_reset_scaler);

	i2c_attach_client(client);

	v4l_dbg(1, debug, client, "status: (1E) 0x%02x, (1F) 0x%02x\n",
		saa7115_read(client, 0x1e), saa7115_read(client, 0x1f));

	return 0;
}

static int saa7115_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, &saa7115_attach);
	return 0;
}

static int saa7115_detach(struct i2c_client *client)
{
	struct saa7115_state *state = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(state);
	kfree(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver_saa7115 = {
	.driver = {
		.name = "saa7115",
	},
	.id = I2C_DRIVERID_SAA711X,
	.attach_adapter = saa7115_probe,
	.detach_client = saa7115_detach,
	.command = saa7115_command,
};


static int __init saa7115_init_module(void)
{
	return i2c_add_driver(&i2c_driver_saa7115);
}

static void __exit saa7115_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver_saa7115);
}

module_init(saa7115_init_module);
module_exit(saa7115_cleanup_module);
