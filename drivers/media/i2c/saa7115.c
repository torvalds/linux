/* saa711x - Philips SAA711x video decoder driver
 * This driver can work with saa7111, saa7111a, saa7113, saa7114,
 *			     saa7115 and saa7118.
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
 *
 * Copyright (c) 2005-2006 Mauro Carvalho Chehab <mchehab@infradead.org>
 *	SAA7111, SAA7113 and SAA7118 support
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

#include "saa711x_regs.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-chip-ident.h>
#include <media/saa7115.h>
#include <asm/div64.h>

#define VRES_60HZ	(480+16)

MODULE_DESCRIPTION("Philips SAA7111/SAA7113/SAA7114/SAA7115/SAA7118 video decoder driver");
MODULE_AUTHOR(  "Maxim Yevtyushkin, Kevin Thayer, Chris Kennedy, "
		"Hans Verkuil, Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");

static bool debug;
module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");


struct saa711x_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;

	struct {
		/* chroma gain control cluster */
		struct v4l2_ctrl *agc;
		struct v4l2_ctrl *gain;
	};

	v4l2_std_id std;
	int input;
	int output;
	int enable;
	int radio;
	int width;
	int height;
	u32 ident;
	u32 audclk_freq;
	u32 crystal_freq;
	bool ucgc;
	u8 cgcdiv;
	bool apll;
	bool double_asclk;
};

static inline struct saa711x_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa711x_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct saa711x_state, hdl)->sd;
}

/* ----------------------------------------------------------------------- */

static inline int saa711x_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

/* Sanity routine to check if a register is present */
static int saa711x_has_reg(const int id, const u8 reg)
{
	if (id == V4L2_IDENT_SAA7111)
		return reg < 0x20 && reg != 0x01 && reg != 0x0f &&
		       (reg < 0x13 || reg > 0x19) && reg != 0x1d && reg != 0x1e;
	if (id == V4L2_IDENT_SAA7111A)
		return reg < 0x20 && reg != 0x01 && reg != 0x0f &&
		       reg != 0x14 && reg != 0x18 && reg != 0x19 &&
		       reg != 0x1d && reg != 0x1e;

	/* common for saa7113/4/5/8 */
	if (unlikely((reg >= 0x3b && reg <= 0x3f) || reg == 0x5c || reg == 0x5f ||
	    reg == 0xa3 || reg == 0xa7 || reg == 0xab || reg == 0xaf || (reg >= 0xb5 && reg <= 0xb7) ||
	    reg == 0xd3 || reg == 0xd7 || reg == 0xdb || reg == 0xdf || (reg >= 0xe5 && reg <= 0xe7) ||
	    reg == 0x82 || (reg >= 0x89 && reg <= 0x8e)))
		return 0;

	switch (id) {
	case V4L2_IDENT_GM7113C:
		return reg != 0x14 && (reg < 0x18 || reg > 0x1e) && reg < 0x20;
	case V4L2_IDENT_SAA7113:
		return reg != 0x14 && (reg < 0x18 || reg > 0x1e) && (reg < 0x20 || reg > 0x3f) &&
		       reg != 0x5d && reg < 0x63;
	case V4L2_IDENT_SAA7114:
		return (reg < 0x1a || reg > 0x1e) && (reg < 0x20 || reg > 0x2f) &&
		       (reg < 0x63 || reg > 0x7f) && reg != 0x33 && reg != 0x37 &&
		       reg != 0x81 && reg < 0xf0;
	case V4L2_IDENT_SAA7115:
		return (reg < 0x20 || reg > 0x2f) && reg != 0x65 && (reg < 0xfc || reg > 0xfe);
	case V4L2_IDENT_SAA7118:
		return (reg < 0x1a || reg > 0x1d) && (reg < 0x20 || reg > 0x22) &&
		       (reg < 0x26 || reg > 0x28) && reg != 0x33 && reg != 0x37 &&
		       (reg < 0x63 || reg > 0x7f) && reg != 0x81 && reg < 0xf0;
	}
	return 1;
}

static int saa711x_writeregs(struct v4l2_subdev *sd, const unsigned char *regs)
{
	struct saa711x_state *state = to_state(sd);
	unsigned char reg, data;

	while (*regs != 0x00) {
		reg = *(regs++);
		data = *(regs++);

		/* According with datasheets, reserved regs should be
		   filled with 0 - seems better not to touch on they */
		if (saa711x_has_reg(state->ident, reg)) {
			if (saa711x_write(sd, reg, data) < 0)
				return -1;
		} else {
			v4l2_dbg(1, debug, sd, "tried to access reserved reg 0x%02x\n", reg);
		}
	}
	return 0;
}

static inline int saa711x_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

/* SAA7111 initialization table */
static const unsigned char saa7111_init[] = {
	R_01_INC_DELAY, 0x00,		/* reserved */

	/*front end */
	R_02_INPUT_CNTL_1, 0xd0,	/* FUSE=3, GUDL=2, MODE=0 */
	R_03_INPUT_CNTL_2, 0x23,	/* HLNRS=0, VBSL=1, WPOFF=0, HOLDG=0,
					 * GAFIX=0, GAI1=256, GAI2=256 */
	R_04_INPUT_CNTL_3, 0x00,	/* GAI1=256 */
	R_05_INPUT_CNTL_4, 0x00,	/* GAI2=256 */

	/* decoder */
	R_06_H_SYNC_START, 0xf3,	/* HSB at  13(50Hz) /  17(60Hz)
					 * pixels after end of last line */
	R_07_H_SYNC_STOP, 0xe8,		/* HSS seems to be needed to
					 * work with NTSC, too */
	R_08_SYNC_CNTL, 0xc8,		/* AUFD=1, FSEL=1, EXFIL=0,
					 * VTRC=1, HPLL=0, VNOI=0 */
	R_09_LUMA_CNTL, 0x01,		/* BYPS=0, PREF=0, BPSS=0,
					 * VBLB=0, UPTCV=0, APER=1 */
	R_0A_LUMA_BRIGHT_CNTL, 0x80,
	R_0B_LUMA_CONTRAST_CNTL, 0x47,	/* 0b - CONT=1.109 */
	R_0C_CHROMA_SAT_CNTL, 0x40,
	R_0D_CHROMA_HUE_CNTL, 0x00,
	R_0E_CHROMA_CNTL_1, 0x01,	/* 0e - CDTO=0, CSTD=0, DCCF=0,
					 * FCTC=0, CHBW=1 */
	R_0F_CHROMA_GAIN_CNTL, 0x00,	/* reserved */
	R_10_CHROMA_CNTL_2, 0x48,	/* 10 - OFTS=1, HDEL=0, VRLN=1, YDEL=0 */
	R_11_MODE_DELAY_CNTL, 0x1c,	/* 11 - GPSW=0, CM99=0, FECO=0, COMPO=1,
					 * OEYC=1, OEHV=1, VIPB=0, COLO=0 */
	R_12_RT_SIGNAL_CNTL, 0x00,	/* 12 - output control 2 */
	R_13_RT_X_PORT_OUT_CNTL, 0x00,	/* 13 - output control 3 */
	R_14_ANAL_ADC_COMPAT_CNTL, 0x00,
	R_15_VGATE_START_FID_CHG, 0x00,
	R_16_VGATE_STOP, 0x00,
	R_17_MISC_VGATE_CONF_AND_MSB, 0x00,

	0x00, 0x00
};

/* SAA7113/GM7113C init codes
 * It's important that R_14... R_17 == 0x00
 * for the gm7113c chip to deliver stable video
 */
static const unsigned char saa7113_init[] = {
	R_01_INC_DELAY, 0x08,
	R_02_INPUT_CNTL_1, 0xc2,
	R_03_INPUT_CNTL_2, 0x30,
	R_04_INPUT_CNTL_3, 0x00,
	R_05_INPUT_CNTL_4, 0x00,
	R_06_H_SYNC_START, 0x89,
	R_07_H_SYNC_STOP, 0x0d,
	R_08_SYNC_CNTL, 0x88,
	R_09_LUMA_CNTL, 0x01,
	R_0A_LUMA_BRIGHT_CNTL, 0x80,
	R_0B_LUMA_CONTRAST_CNTL, 0x47,
	R_0C_CHROMA_SAT_CNTL, 0x40,
	R_0D_CHROMA_HUE_CNTL, 0x00,
	R_0E_CHROMA_CNTL_1, 0x01,
	R_0F_CHROMA_GAIN_CNTL, 0x2a,
	R_10_CHROMA_CNTL_2, 0x08,
	R_11_MODE_DELAY_CNTL, 0x0c,
	R_12_RT_SIGNAL_CNTL, 0x07,
	R_13_RT_X_PORT_OUT_CNTL, 0x00,
	R_14_ANAL_ADC_COMPAT_CNTL, 0x00,
	R_15_VGATE_START_FID_CHG, 0x00,
	R_16_VGATE_STOP, 0x00,
	R_17_MISC_VGATE_CONF_AND_MSB, 0x00,

	0x00, 0x00
};

/* If a value differs from the Hauppauge driver values, then the comment starts with
   'was 0xXX' to denote the Hauppauge value. Otherwise the value is identical to what the
   Hauppauge driver sets. */

/* SAA7114 and SAA7115 initialization table */
static const unsigned char saa7115_init_auto_input[] = {
		/* Front-End Part */
	R_01_INC_DELAY, 0x48,			/* white peak control disabled */
	R_03_INPUT_CNTL_2, 0x20,		/* was 0x30. 0x20: long vertical blanking */
	R_04_INPUT_CNTL_3, 0x90,		/* analog gain set to 0 */
	R_05_INPUT_CNTL_4, 0x90,		/* analog gain set to 0 */
		/* Decoder Part */
	R_06_H_SYNC_START, 0xeb,		/* horiz sync begin = -21 */
	R_07_H_SYNC_STOP, 0xe0,			/* horiz sync stop = -17 */
	R_09_LUMA_CNTL, 0x53,			/* 0x53, was 0x56 for 60hz. luminance control */
	R_0A_LUMA_BRIGHT_CNTL, 0x80,		/* was 0x88. decoder brightness, 0x80 is itu standard */
	R_0B_LUMA_CONTRAST_CNTL, 0x44,		/* was 0x48. decoder contrast, 0x44 is itu standard */
	R_0C_CHROMA_SAT_CNTL, 0x40,		/* was 0x47. decoder saturation, 0x40 is itu standard */
	R_0D_CHROMA_HUE_CNTL, 0x00,
	R_0F_CHROMA_GAIN_CNTL, 0x00,		/* use automatic gain  */
	R_10_CHROMA_CNTL_2, 0x06,		/* chroma: active adaptive combfilter */
	R_11_MODE_DELAY_CNTL, 0x00,
	R_12_RT_SIGNAL_CNTL, 0x9d,		/* RTS0 output control: VGATE */
	R_13_RT_X_PORT_OUT_CNTL, 0x80,		/* ITU656 standard mode, RTCO output enable RTCE */
	R_14_ANAL_ADC_COMPAT_CNTL, 0x00,
	R_18_RAW_DATA_GAIN_CNTL, 0x40,		/* gain 0x00 = nominal */
	R_19_RAW_DATA_OFF_CNTL, 0x80,
	R_1A_COLOR_KILL_LVL_CNTL, 0x77,		/* recommended value */
	R_1B_MISC_TVVCRDET, 0x42,		/* recommended value */
	R_1C_ENHAN_COMB_CTRL1, 0xa9,		/* recommended value */
	R_1D_ENHAN_COMB_CTRL2, 0x01,		/* recommended value */


	R_80_GLOBAL_CNTL_1, 0x0,		/* No tasks enabled at init */

		/* Power Device Control */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,	/* reset device */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xf0,	/* set device programmed, all in operational mode */
	0x00, 0x00
};

/* Used to reset saa7113, saa7114 and saa7115 */
static const unsigned char saa7115_cfg_reset_scaler[] = {
	R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED, 0x00,	/* disable I-port output */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,		/* reset scaler */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xf0,		/* activate scaler */
	R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED, 0x01,	/* enable I-port output */
	0x00, 0x00
};

/* ============== SAA7715 VIDEO templates =============  */

static const unsigned char saa7115_cfg_60hz_video[] = {
	R_80_GLOBAL_CNTL_1, 0x00,			/* reset tasks */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,		/* reset scaler */

	R_15_VGATE_START_FID_CHG, 0x03,
	R_16_VGATE_STOP, 0x11,
	R_17_MISC_VGATE_CONF_AND_MSB, 0x9c,

	R_08_SYNC_CNTL, 0x68,			/* 0xBO: auto detection, 0x68 = NTSC */
	R_0E_CHROMA_CNTL_1, 0x07,		/* video autodetection is on */

	R_5A_V_OFF_FOR_SLICER, 0x06,		/* standard 60hz value for ITU656 line counting */

	/* Task A */
	R_90_A_TASK_HANDLING_CNTL, 0x80,
	R_91_A_X_PORT_FORMATS_AND_CONF, 0x48,
	R_92_A_X_PORT_INPUT_REFERENCE_SIGNAL, 0x40,
	R_93_A_I_PORT_OUTPUT_FORMATS_AND_CONF, 0x84,

	/* hoffset low (input), 0x0002 is minimum */
	R_94_A_HORIZ_INPUT_WINDOW_START, 0x01,
	R_95_A_HORIZ_INPUT_WINDOW_START_MSB, 0x00,

	/* hsize low (input), 0x02d0 = 720 */
	R_96_A_HORIZ_INPUT_WINDOW_LENGTH, 0xd0,
	R_97_A_HORIZ_INPUT_WINDOW_LENGTH_MSB, 0x02,

	R_98_A_VERT_INPUT_WINDOW_START, 0x05,
	R_99_A_VERT_INPUT_WINDOW_START_MSB, 0x00,

	R_9A_A_VERT_INPUT_WINDOW_LENGTH, 0x0c,
	R_9B_A_VERT_INPUT_WINDOW_LENGTH_MSB, 0x00,

	R_9C_A_HORIZ_OUTPUT_WINDOW_LENGTH, 0xa0,
	R_9D_A_HORIZ_OUTPUT_WINDOW_LENGTH_MSB, 0x05,

	R_9E_A_VERT_OUTPUT_WINDOW_LENGTH, 0x0c,
	R_9F_A_VERT_OUTPUT_WINDOW_LENGTH_MSB, 0x00,

	/* Task B */
	R_C0_B_TASK_HANDLING_CNTL, 0x00,
	R_C1_B_X_PORT_FORMATS_AND_CONF, 0x08,
	R_C2_B_INPUT_REFERENCE_SIGNAL_DEFINITION, 0x00,
	R_C3_B_I_PORT_FORMATS_AND_CONF, 0x80,

	/* 0x0002 is minimum */
	R_C4_B_HORIZ_INPUT_WINDOW_START, 0x02,
	R_C5_B_HORIZ_INPUT_WINDOW_START_MSB, 0x00,

	/* 0x02d0 = 720 */
	R_C6_B_HORIZ_INPUT_WINDOW_LENGTH, 0xd0,
	R_C7_B_HORIZ_INPUT_WINDOW_LENGTH_MSB, 0x02,

	/* vwindow start 0x12 = 18 */
	R_C8_B_VERT_INPUT_WINDOW_START, 0x12,
	R_C9_B_VERT_INPUT_WINDOW_START_MSB, 0x00,

	/* vwindow length 0xf8 = 248 */
	R_CA_B_VERT_INPUT_WINDOW_LENGTH, VRES_60HZ>>1,
	R_CB_B_VERT_INPUT_WINDOW_LENGTH_MSB, VRES_60HZ>>9,

	/* hwindow 0x02d0 = 720 */
	R_CC_B_HORIZ_OUTPUT_WINDOW_LENGTH, 0xd0,
	R_CD_B_HORIZ_OUTPUT_WINDOW_LENGTH_MSB, 0x02,

	R_F0_LFCO_PER_LINE, 0xad,		/* Set PLL Register. 60hz 525 lines per frame, 27 MHz */
	R_F1_P_I_PARAM_SELECT, 0x05,		/* low bit with 0xF0 */
	R_F5_PULSGEN_LINE_LENGTH, 0xad,
	R_F6_PULSE_A_POS_LSB_AND_PULSEGEN_CONFIG, 0x01,

	0x00, 0x00
};

static const unsigned char saa7115_cfg_50hz_video[] = {
	R_80_GLOBAL_CNTL_1, 0x00,
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,	/* reset scaler */

	R_15_VGATE_START_FID_CHG, 0x37,		/* VGATE start */
	R_16_VGATE_STOP, 0x16,
	R_17_MISC_VGATE_CONF_AND_MSB, 0x99,

	R_08_SYNC_CNTL, 0x28,			/* 0x28 = PAL */
	R_0E_CHROMA_CNTL_1, 0x07,

	R_5A_V_OFF_FOR_SLICER, 0x03,		/* standard 50hz value */

	/* Task A */
	R_90_A_TASK_HANDLING_CNTL, 0x81,
	R_91_A_X_PORT_FORMATS_AND_CONF, 0x48,
	R_92_A_X_PORT_INPUT_REFERENCE_SIGNAL, 0x40,
	R_93_A_I_PORT_OUTPUT_FORMATS_AND_CONF, 0x84,

	/* This is weird: the datasheet says that you should use 2 as the minimum value, */
	/* but Hauppauge uses 0, and changing that to 2 causes indeed problems (for 50hz) */
	/* hoffset low (input), 0x0002 is minimum */
	R_94_A_HORIZ_INPUT_WINDOW_START, 0x00,
	R_95_A_HORIZ_INPUT_WINDOW_START_MSB, 0x00,

	/* hsize low (input), 0x02d0 = 720 */
	R_96_A_HORIZ_INPUT_WINDOW_LENGTH, 0xd0,
	R_97_A_HORIZ_INPUT_WINDOW_LENGTH_MSB, 0x02,

	R_98_A_VERT_INPUT_WINDOW_START, 0x03,
	R_99_A_VERT_INPUT_WINDOW_START_MSB, 0x00,

	/* vsize 0x12 = 18 */
	R_9A_A_VERT_INPUT_WINDOW_LENGTH, 0x12,
	R_9B_A_VERT_INPUT_WINDOW_LENGTH_MSB, 0x00,

	/* hsize 0x05a0 = 1440 */
	R_9C_A_HORIZ_OUTPUT_WINDOW_LENGTH, 0xa0,
	R_9D_A_HORIZ_OUTPUT_WINDOW_LENGTH_MSB, 0x05,	/* hsize hi (output) */
	R_9E_A_VERT_OUTPUT_WINDOW_LENGTH, 0x12,		/* vsize low (output), 0x12 = 18 */
	R_9F_A_VERT_OUTPUT_WINDOW_LENGTH_MSB, 0x00,	/* vsize hi (output) */

	/* Task B */
	R_C0_B_TASK_HANDLING_CNTL, 0x00,
	R_C1_B_X_PORT_FORMATS_AND_CONF, 0x08,
	R_C2_B_INPUT_REFERENCE_SIGNAL_DEFINITION, 0x00,
	R_C3_B_I_PORT_FORMATS_AND_CONF, 0x80,

	/* This is weird: the datasheet says that you should use 2 as the minimum value, */
	/* but Hauppauge uses 0, and changing that to 2 causes indeed problems (for 50hz) */
	/* hoffset low (input), 0x0002 is minimum. See comment above. */
	R_C4_B_HORIZ_INPUT_WINDOW_START, 0x00,
	R_C5_B_HORIZ_INPUT_WINDOW_START_MSB, 0x00,

	/* hsize 0x02d0 = 720 */
	R_C6_B_HORIZ_INPUT_WINDOW_LENGTH, 0xd0,
	R_C7_B_HORIZ_INPUT_WINDOW_LENGTH_MSB, 0x02,

	/* voffset 0x16 = 22 */
	R_C8_B_VERT_INPUT_WINDOW_START, 0x16,
	R_C9_B_VERT_INPUT_WINDOW_START_MSB, 0x00,

	/* vsize 0x0120 = 288 */
	R_CA_B_VERT_INPUT_WINDOW_LENGTH, 0x20,
	R_CB_B_VERT_INPUT_WINDOW_LENGTH_MSB, 0x01,

	/* hsize 0x02d0 = 720 */
	R_CC_B_HORIZ_OUTPUT_WINDOW_LENGTH, 0xd0,
	R_CD_B_HORIZ_OUTPUT_WINDOW_LENGTH_MSB, 0x02,

	R_F0_LFCO_PER_LINE, 0xb0,		/* Set PLL Register. 50hz 625 lines per frame, 27 MHz */
	R_F1_P_I_PARAM_SELECT, 0x05,		/* low bit with 0xF0, (was 0x05) */
	R_F5_PULSGEN_LINE_LENGTH, 0xb0,
	R_F6_PULSE_A_POS_LSB_AND_PULSEGEN_CONFIG, 0x01,

	0x00, 0x00
};

/* ============== SAA7715 VIDEO templates (end) =======  */

/* ============== GM7113C VIDEO templates =============  */
static const unsigned char gm7113c_cfg_60hz_video[] = {
	R_08_SYNC_CNTL, 0x68,			/* 0xBO: auto detection, 0x68 = NTSC */
	R_0E_CHROMA_CNTL_1, 0x07,		/* video autodetection is on */

	0x00, 0x00
};

static const unsigned char gm7113c_cfg_50hz_video[] = {
	R_08_SYNC_CNTL, 0x28,			/* 0x28 = PAL */
	R_0E_CHROMA_CNTL_1, 0x07,

	0x00, 0x00
};

/* ============== GM7113C VIDEO templates (end) =======  */


static const unsigned char saa7115_cfg_vbi_on[] = {
	R_80_GLOBAL_CNTL_1, 0x00,			/* reset tasks */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,		/* reset scaler */
	R_80_GLOBAL_CNTL_1, 0x30,			/* Activate both tasks */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xf0,		/* activate scaler */
	R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED, 0x01,	/* Enable I-port output */

	0x00, 0x00
};

static const unsigned char saa7115_cfg_vbi_off[] = {
	R_80_GLOBAL_CNTL_1, 0x00,			/* reset tasks */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,		/* reset scaler */
	R_80_GLOBAL_CNTL_1, 0x20,			/* Activate only task "B" */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xf0,		/* activate scaler */
	R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED, 0x01,	/* Enable I-port output */

	0x00, 0x00
};


static const unsigned char saa7115_init_misc[] = {
	R_81_V_SYNC_FLD_ID_SRC_SEL_AND_RETIMED_V_F, 0x01,
	R_83_X_PORT_I_O_ENA_AND_OUT_CLK, 0x01,
	R_84_I_PORT_SIGNAL_DEF, 0x20,
	R_85_I_PORT_SIGNAL_POLAR, 0x21,
	R_86_I_PORT_FIFO_FLAG_CNTL_AND_ARBIT, 0xc5,
	R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED, 0x01,

	/* Task A */
	R_A0_A_HORIZ_PRESCALING, 0x01,
	R_A1_A_ACCUMULATION_LENGTH, 0x00,
	R_A2_A_PRESCALER_DC_GAIN_AND_FIR_PREFILTER, 0x00,

	/* Configure controls at nominal value*/
	R_A4_A_LUMA_BRIGHTNESS_CNTL, 0x80,
	R_A5_A_LUMA_CONTRAST_CNTL, 0x40,
	R_A6_A_CHROMA_SATURATION_CNTL, 0x40,

	/* note: 2 x zoom ensures that VBI lines have same length as video lines. */
	R_A8_A_HORIZ_LUMA_SCALING_INC, 0x00,
	R_A9_A_HORIZ_LUMA_SCALING_INC_MSB, 0x02,

	R_AA_A_HORIZ_LUMA_PHASE_OFF, 0x00,

	/* must be horiz lum scaling / 2 */
	R_AC_A_HORIZ_CHROMA_SCALING_INC, 0x00,
	R_AD_A_HORIZ_CHROMA_SCALING_INC_MSB, 0x01,

	/* must be offset luma / 2 */
	R_AE_A_HORIZ_CHROMA_PHASE_OFF, 0x00,

	R_B0_A_VERT_LUMA_SCALING_INC, 0x00,
	R_B1_A_VERT_LUMA_SCALING_INC_MSB, 0x04,

	R_B2_A_VERT_CHROMA_SCALING_INC, 0x00,
	R_B3_A_VERT_CHROMA_SCALING_INC_MSB, 0x04,

	R_B4_A_VERT_SCALING_MODE_CNTL, 0x01,

	R_B8_A_VERT_CHROMA_PHASE_OFF_00, 0x00,
	R_B9_A_VERT_CHROMA_PHASE_OFF_01, 0x00,
	R_BA_A_VERT_CHROMA_PHASE_OFF_10, 0x00,
	R_BB_A_VERT_CHROMA_PHASE_OFF_11, 0x00,

	R_BC_A_VERT_LUMA_PHASE_OFF_00, 0x00,
	R_BD_A_VERT_LUMA_PHASE_OFF_01, 0x00,
	R_BE_A_VERT_LUMA_PHASE_OFF_10, 0x00,
	R_BF_A_VERT_LUMA_PHASE_OFF_11, 0x00,

	/* Task B */
	R_D0_B_HORIZ_PRESCALING, 0x01,
	R_D1_B_ACCUMULATION_LENGTH, 0x00,
	R_D2_B_PRESCALER_DC_GAIN_AND_FIR_PREFILTER, 0x00,

	/* Configure controls at nominal value*/
	R_D4_B_LUMA_BRIGHTNESS_CNTL, 0x80,
	R_D5_B_LUMA_CONTRAST_CNTL, 0x40,
	R_D6_B_CHROMA_SATURATION_CNTL, 0x40,

	/* hor lum scaling 0x0400 = 1 */
	R_D8_B_HORIZ_LUMA_SCALING_INC, 0x00,
	R_D9_B_HORIZ_LUMA_SCALING_INC_MSB, 0x04,

	R_DA_B_HORIZ_LUMA_PHASE_OFF, 0x00,

	/* must be hor lum scaling / 2 */
	R_DC_B_HORIZ_CHROMA_SCALING, 0x00,
	R_DD_B_HORIZ_CHROMA_SCALING_MSB, 0x02,

	/* must be offset luma / 2 */
	R_DE_B_HORIZ_PHASE_OFFSET_CRHOMA, 0x00,

	R_E0_B_VERT_LUMA_SCALING_INC, 0x00,
	R_E1_B_VERT_LUMA_SCALING_INC_MSB, 0x04,

	R_E2_B_VERT_CHROMA_SCALING_INC, 0x00,
	R_E3_B_VERT_CHROMA_SCALING_INC_MSB, 0x04,

	R_E4_B_VERT_SCALING_MODE_CNTL, 0x01,

	R_E8_B_VERT_CHROMA_PHASE_OFF_00, 0x00,
	R_E9_B_VERT_CHROMA_PHASE_OFF_01, 0x00,
	R_EA_B_VERT_CHROMA_PHASE_OFF_10, 0x00,
	R_EB_B_VERT_CHROMA_PHASE_OFF_11, 0x00,

	R_EC_B_VERT_LUMA_PHASE_OFF_00, 0x00,
	R_ED_B_VERT_LUMA_PHASE_OFF_01, 0x00,
	R_EE_B_VERT_LUMA_PHASE_OFF_10, 0x00,
	R_EF_B_VERT_LUMA_PHASE_OFF_11, 0x00,

	R_F2_NOMINAL_PLL2_DTO, 0x50,		/* crystal clock = 24.576 MHz, target = 27MHz */
	R_F3_PLL_INCREMENT, 0x46,
	R_F4_PLL2_STATUS, 0x00,
	R_F7_PULSE_A_POS_MSB, 0x4b,		/* not the recommended settings! */
	R_F8_PULSE_B_POS, 0x00,
	R_F9_PULSE_B_POS_MSB, 0x4b,
	R_FA_PULSE_C_POS, 0x00,
	R_FB_PULSE_C_POS_MSB, 0x4b,

	/* PLL2 lock detection settings: 71 lines 50% phase error */
	R_FF_S_PLL_MAX_PHASE_ERR_THRESH_NUM_LINES, 0x88,

	/* Turn off VBI */
	R_40_SLICER_CNTL_1, 0x20,             /* No framing code errors allowed. */
	R_41_LCR_BASE, 0xff,
	R_41_LCR_BASE+1, 0xff,
	R_41_LCR_BASE+2, 0xff,
	R_41_LCR_BASE+3, 0xff,
	R_41_LCR_BASE+4, 0xff,
	R_41_LCR_BASE+5, 0xff,
	R_41_LCR_BASE+6, 0xff,
	R_41_LCR_BASE+7, 0xff,
	R_41_LCR_BASE+8, 0xff,
	R_41_LCR_BASE+9, 0xff,
	R_41_LCR_BASE+10, 0xff,
	R_41_LCR_BASE+11, 0xff,
	R_41_LCR_BASE+12, 0xff,
	R_41_LCR_BASE+13, 0xff,
	R_41_LCR_BASE+14, 0xff,
	R_41_LCR_BASE+15, 0xff,
	R_41_LCR_BASE+16, 0xff,
	R_41_LCR_BASE+17, 0xff,
	R_41_LCR_BASE+18, 0xff,
	R_41_LCR_BASE+19, 0xff,
	R_41_LCR_BASE+20, 0xff,
	R_41_LCR_BASE+21, 0xff,
	R_41_LCR_BASE+22, 0xff,
	R_58_PROGRAM_FRAMING_CODE, 0x40,
	R_59_H_OFF_FOR_SLICER, 0x47,
	R_5B_FLD_OFF_AND_MSB_FOR_H_AND_V_OFF, 0x83,
	R_5D_DID, 0xbd,
	R_5E_SDID, 0x35,

	R_02_INPUT_CNTL_1, 0xc4, /* input tuner -> input 4, amplifier active */

	R_80_GLOBAL_CNTL_1, 0x20,		/* enable task B */
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xd0,
	R_88_POWER_SAVE_ADC_PORT_CNTL, 0xf0,
	0x00, 0x00
};

static int saa711x_odd_parity(u8 c)
{
	c ^= (c >> 4);
	c ^= (c >> 2);
	c ^= (c >> 1);

	return c & 1;
}

static int saa711x_decode_vps(u8 *dst, u8 *p)
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

static int saa711x_decode_wss(u8 *p)
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

static int saa711x_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	struct saa711x_state *state = to_state(sd);
	u32 acpf;
	u32 acni;
	u32 hz;
	u64 f;
	u8 acc = 0; 	/* reg 0x3a, audio clock control */

	/* Checks for chips that don't have audio clock (saa7111, saa7113) */
	if (!saa711x_has_reg(state->ident, R_30_AUD_MAST_CLK_CYCLES_PER_FIELD))
		return 0;

	v4l2_dbg(1, debug, sd, "set audio clock freq: %d\n", freq);

	/* sanity check */
	if (freq < 32000 || freq > 48000)
		return -EINVAL;

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

	if (state->double_asclk) {
		acpf <<= 1;
		acni <<= 1;
	}
	saa711x_write(sd, R_38_CLK_RATIO_AMXCLK_TO_ASCLK, 0x03);
	saa711x_write(sd, R_39_CLK_RATIO_ASCLK_TO_ALRCLK, 0x10 << state->double_asclk);
	saa711x_write(sd, R_3A_AUD_CLK_GEN_BASIC_SETUP, acc);

	saa711x_write(sd, R_30_AUD_MAST_CLK_CYCLES_PER_FIELD, acpf & 0xff);
	saa711x_write(sd, R_30_AUD_MAST_CLK_CYCLES_PER_FIELD+1,
							(acpf >> 8) & 0xff);
	saa711x_write(sd, R_30_AUD_MAST_CLK_CYCLES_PER_FIELD+2,
							(acpf >> 16) & 0x03);

	saa711x_write(sd, R_34_AUD_MAST_CLK_NOMINAL_INC, acni & 0xff);
	saa711x_write(sd, R_34_AUD_MAST_CLK_NOMINAL_INC+1, (acni >> 8) & 0xff);
	saa711x_write(sd, R_34_AUD_MAST_CLK_NOMINAL_INC+2, (acni >> 16) & 0x3f);
	state->audclk_freq = freq;
	return 0;
}

static int saa711x_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct saa711x_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_CHROMA_AGC:
		/* chroma gain cluster */
		if (state->agc->val)
			state->gain->val =
				saa711x_read(sd, R_0F_CHROMA_GAIN_CNTL) & 0x7f;
		break;
	}
	return 0;
}

static int saa711x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct saa711x_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		saa711x_write(sd, R_0A_LUMA_BRIGHT_CNTL, ctrl->val);
		break;

	case V4L2_CID_CONTRAST:
		saa711x_write(sd, R_0B_LUMA_CONTRAST_CNTL, ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		saa711x_write(sd, R_0C_CHROMA_SAT_CNTL, ctrl->val);
		break;

	case V4L2_CID_HUE:
		saa711x_write(sd, R_0D_CHROMA_HUE_CNTL, ctrl->val);
		break;

	case V4L2_CID_CHROMA_AGC:
		/* chroma gain cluster */
		if (state->agc->val)
			saa711x_write(sd, R_0F_CHROMA_GAIN_CNTL, state->gain->val);
		else
			saa711x_write(sd, R_0F_CHROMA_GAIN_CNTL, state->gain->val | 0x80);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int saa711x_set_size(struct v4l2_subdev *sd, int width, int height)
{
	struct saa711x_state *state = to_state(sd);
	int HPSC, HFSC;
	int VSCY;
	int res;
	int is_50hz = state->std & V4L2_STD_625_50;
	int Vsrc = is_50hz ? 576 : 480;

	v4l2_dbg(1, debug, sd, "decoder set size to %ix%i\n", width, height);

	/* FIXME need better bounds checking here */
	if ((width < 1) || (width > 1440))
		return -EINVAL;
	if ((height < 1) || (height > Vsrc))
		return -EINVAL;

	if (!saa711x_has_reg(state->ident, R_D0_B_HORIZ_PRESCALING)) {
		/* Decoder only supports 720 columns and 480 or 576 lines */
		if (width != 720)
			return -EINVAL;
		if (height != Vsrc)
			return -EINVAL;
	}

	state->width = width;
	state->height = height;

	if (!saa711x_has_reg(state->ident, R_CC_B_HORIZ_OUTPUT_WINDOW_LENGTH))
		return 0;

	/* probably have a valid size, let's set it */
	/* Set output width/height */
	/* width */

	saa711x_write(sd, R_CC_B_HORIZ_OUTPUT_WINDOW_LENGTH,
					(u8) (width & 0xff));
	saa711x_write(sd, R_CD_B_HORIZ_OUTPUT_WINDOW_LENGTH_MSB,
					(u8) ((width >> 8) & 0xff));

	/* Vertical Scaling uses height/2 */
	res = height / 2;

	/* On 60Hz, it is using a higher Vertical Output Size */
	if (!is_50hz)
		res += (VRES_60HZ - 480) >> 1;

		/* height */
	saa711x_write(sd, R_CE_B_VERT_OUTPUT_WINDOW_LENGTH,
					(u8) (res & 0xff));
	saa711x_write(sd, R_CF_B_VERT_OUTPUT_WINDOW_LENGTH_MSB,
					(u8) ((res >> 8) & 0xff));

	/* Scaling settings */
	/* Hprescaler is floor(inres/outres) */
	HPSC = (int)(720 / width);
	/* 0 is not allowed (div. by zero) */
	HPSC = HPSC ? HPSC : 1;
	HFSC = (int)((1024 * 720) / (HPSC * width));
	/* FIXME hardcodes to "Task B"
	 * write H prescaler integer */
	saa711x_write(sd, R_D0_B_HORIZ_PRESCALING,
				(u8) (HPSC & 0x3f));

	v4l2_dbg(1, debug, sd, "Hpsc: 0x%05x, Hfsc: 0x%05x\n", HPSC, HFSC);
	/* write H fine-scaling (luminance) */
	saa711x_write(sd, R_D8_B_HORIZ_LUMA_SCALING_INC,
				(u8) (HFSC & 0xff));
	saa711x_write(sd, R_D9_B_HORIZ_LUMA_SCALING_INC_MSB,
				(u8) ((HFSC >> 8) & 0xff));
	/* write H fine-scaling (chrominance)
	 * must be lum/2, so i'll just bitshift :) */
	saa711x_write(sd, R_DC_B_HORIZ_CHROMA_SCALING,
				(u8) ((HFSC >> 1) & 0xff));
	saa711x_write(sd, R_DD_B_HORIZ_CHROMA_SCALING_MSB,
				(u8) ((HFSC >> 9) & 0xff));

	VSCY = (int)((1024 * Vsrc) / height);
	v4l2_dbg(1, debug, sd, "Vsrc: %d, Vscy: 0x%05x\n", Vsrc, VSCY);

	/* Correct Contrast and Luminance */
	saa711x_write(sd, R_D5_B_LUMA_CONTRAST_CNTL,
					(u8) (64 * 1024 / VSCY));
	saa711x_write(sd, R_D6_B_CHROMA_SATURATION_CNTL,
					(u8) (64 * 1024 / VSCY));

		/* write V fine-scaling (luminance) */
	saa711x_write(sd, R_E0_B_VERT_LUMA_SCALING_INC,
					(u8) (VSCY & 0xff));
	saa711x_write(sd, R_E1_B_VERT_LUMA_SCALING_INC_MSB,
					(u8) ((VSCY >> 8) & 0xff));
		/* write V fine-scaling (chrominance) */
	saa711x_write(sd, R_E2_B_VERT_CHROMA_SCALING_INC,
					(u8) (VSCY & 0xff));
	saa711x_write(sd, R_E3_B_VERT_CHROMA_SCALING_INC_MSB,
					(u8) ((VSCY >> 8) & 0xff));

	saa711x_writeregs(sd, saa7115_cfg_reset_scaler);

	/* Activates task "B" */
	saa711x_write(sd, R_80_GLOBAL_CNTL_1,
				saa711x_read(sd, R_80_GLOBAL_CNTL_1) | 0x20);

	return 0;
}

static void saa711x_set_v4lstd(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa711x_state *state = to_state(sd);

	/* Prevent unnecessary standard changes. During a standard
	   change the I-Port is temporarily disabled. Any devices
	   reading from that port can get confused.
	   Note that s_std is also used to switch from
	   radio to TV mode, so if a s_std is broadcast to
	   all I2C devices then you do not want to have an unwanted
	   side-effect here. */
	if (std == state->std)
		return;

	state->std = std;

	// This works for NTSC-M, SECAM-L and the 50Hz PAL variants.
	if (std & V4L2_STD_525_60) {
		v4l2_dbg(1, debug, sd, "decoder set standard 60 Hz\n");
		if (state->ident == V4L2_IDENT_GM7113C)
			saa711x_writeregs(sd, gm7113c_cfg_60hz_video);
		else
			saa711x_writeregs(sd, saa7115_cfg_60hz_video);
		saa711x_set_size(sd, 720, 480);
	} else {
		v4l2_dbg(1, debug, sd, "decoder set standard 50 Hz\n");
		if (state->ident == V4L2_IDENT_GM7113C)
			saa711x_writeregs(sd, gm7113c_cfg_50hz_video);
		else
			saa711x_writeregs(sd, saa7115_cfg_50hz_video);
		saa711x_set_size(sd, 720, 576);
	}

	/* Register 0E - Bits D6-D4 on NO-AUTO mode
		(SAA7111 and SAA7113 doesn't have auto mode)
	    50 Hz / 625 lines           60 Hz / 525 lines
	000 PAL BGDHI (4.43Mhz)         NTSC M (3.58MHz)
	001 NTSC 4.43 (50 Hz)           PAL 4.43 (60 Hz)
	010 Combination-PAL N (3.58MHz) NTSC 4.43 (60 Hz)
	011 NTSC N (3.58MHz)            PAL M (3.58MHz)
	100 reserved                    NTSC-Japan (3.58MHz)
	*/
	if (state->ident <= V4L2_IDENT_SAA7113 ||
	    state->ident == V4L2_IDENT_GM7113C) {
		u8 reg = saa711x_read(sd, R_0E_CHROMA_CNTL_1) & 0x8f;

		if (std == V4L2_STD_PAL_M) {
			reg |= 0x30;
		} else if (std == V4L2_STD_PAL_Nc) {
			reg |= 0x20;
		} else if (std == V4L2_STD_PAL_60) {
			reg |= 0x10;
		} else if (std == V4L2_STD_NTSC_M_JP) {
			reg |= 0x40;
		} else if (std & V4L2_STD_SECAM) {
			reg |= 0x50;
		}
		saa711x_write(sd, R_0E_CHROMA_CNTL_1, reg);
	} else {
		/* restart task B if needed */
		int taskb = saa711x_read(sd, R_80_GLOBAL_CNTL_1) & 0x10;

		if (taskb && state->ident == V4L2_IDENT_SAA7114) {
			saa711x_writeregs(sd, saa7115_cfg_vbi_on);
		}

		/* switch audio mode too! */
		saa711x_s_clock_freq(sd, state->audclk_freq);
	}
}

/* setup the sliced VBI lcr registers according to the sliced VBI format */
static void saa711x_set_lcr(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt)
{
	struct saa711x_state *state = to_state(sd);
	int is_50hz = (state->std & V4L2_STD_625_50);
	u8 lcr[24];
	int i, x;

#if 1
	/* saa7113/7114/7118 VBI support are experimental */
	if (!saa711x_has_reg(state->ident, R_41_LCR_BASE))
		return;

#else
	/* SAA7113 and SAA7118 also should support VBI - Need testing */
	if (state->ident != V4L2_IDENT_SAA7115)
		return;
#endif

	for (i = 0; i <= 23; i++)
		lcr[i] = 0xff;

	if (fmt == NULL) {
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
		saa711x_write(sd, i - 2 + R_41_LCR_BASE, lcr[i]);
	}

	/* enable/disable raw VBI capturing */
	saa711x_writeregs(sd, fmt == NULL ?
				saa7115_cfg_vbi_on :
				saa7115_cfg_vbi_off);
}

static int saa711x_g_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *sliced)
{
	static u16 lcr2vbi[] = {
		0, V4L2_SLICED_TELETEXT_B, 0,	/* 1 */
		0, V4L2_SLICED_CAPTION_525,	/* 4 */
		V4L2_SLICED_WSS_625, 0,		/* 5 */
		V4L2_SLICED_VPS, 0, 0, 0, 0,	/* 7 */
		0, 0, 0, 0
	};
	int i;

	memset(sliced->service_lines, 0, sizeof(sliced->service_lines));
	sliced->service_set = 0;
	/* done if using raw VBI */
	if (saa711x_read(sd, R_80_GLOBAL_CNTL_1) & 0x10)
		return 0;
	for (i = 2; i <= 23; i++) {
		u8 v = saa711x_read(sd, i - 2 + R_41_LCR_BASE);

		sliced->service_lines[0][i] = lcr2vbi[v >> 4];
		sliced->service_lines[1][i] = lcr2vbi[v & 0xf];
		sliced->service_set |=
			sliced->service_lines[0][i] | sliced->service_lines[1][i];
	}
	return 0;
}

static int saa711x_s_raw_fmt(struct v4l2_subdev *sd, struct v4l2_vbi_format *fmt)
{
	saa711x_set_lcr(sd, NULL);
	return 0;
}

static int saa711x_s_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt)
{
	saa711x_set_lcr(sd, fmt);
	return 0;
}

static int saa711x_s_mbus_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	if (fmt->code != V4L2_MBUS_FMT_FIXED)
		return -EINVAL;
	fmt->field = V4L2_FIELD_INTERLACED;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return saa711x_set_size(sd, fmt->width, fmt->height);
}

/* Decode the sliced VBI data stream as created by the saa7115.
   The format is described in the saa7115 datasheet in Tables 25 and 26
   and in Figure 33.
   The current implementation uses SAV/EAV codes and not the ancillary data
   headers. The vbi->p pointer points to the R_5E_SDID byte right after the SAV
   code. */
static int saa711x_decode_vbi_line(struct v4l2_subdev *sd, struct v4l2_decode_vbi_line *vbi)
{
	struct saa711x_state *state = to_state(sd);
	static const char vbi_no_data_pattern[] = {
		0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0
	};
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
		return 0;

	/* decode payloads */
	switch (id2) {
	case 1:
		vbi->type = V4L2_SLICED_TELETEXT_B;
		break;
	case 4:
		if (!saa711x_odd_parity(p[0]) || !saa711x_odd_parity(p[1]))
			return 0;
		vbi->type = V4L2_SLICED_CAPTION_525;
		break;
	case 5:
		wss = saa711x_decode_wss(p);
		if (wss == -1)
			return 0;
		p[0] = wss & 0xff;
		p[1] = wss >> 8;
		vbi->type = V4L2_SLICED_WSS_625;
		break;
	case 7:
		if (saa711x_decode_vps(p, p) != 0)
			return 0;
		vbi->type = V4L2_SLICED_VPS;
		break;
	default:
		break;
	}
	return 0;
}

/* ============ SAA7115 AUDIO settings (end) ============= */

static int saa711x_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct saa711x_state *state = to_state(sd);
	int status;

	if (state->radio)
		return 0;
	status = saa711x_read(sd, R_1F_STATUS_BYTE_2_VD_DEC);

	v4l2_dbg(1, debug, sd, "status: 0x%02x\n", status);
	vt->signal = ((status & (1 << 6)) == 0) ? 0xffff : 0x0;
	return 0;
}

static int saa711x_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa711x_state *state = to_state(sd);

	state->radio = 0;
	saa711x_set_v4lstd(sd, std);
	return 0;
}

static int saa711x_s_radio(struct v4l2_subdev *sd)
{
	struct saa711x_state *state = to_state(sd);

	state->radio = 1;
	return 0;
}

static int saa711x_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct saa711x_state *state = to_state(sd);
	u8 mask = (state->ident <= V4L2_IDENT_SAA7111A) ? 0xf8 : 0xf0;

	v4l2_dbg(1, debug, sd, "decoder set input %d output %d\n",
		input, output);

	/* saa7111/3 does not have these inputs */
	if ((state->ident <= V4L2_IDENT_SAA7113 ||
	     state->ident == V4L2_IDENT_GM7113C) &&
	    (input == SAA7115_COMPOSITE4 ||
	     input == SAA7115_COMPOSITE5)) {
		return -EINVAL;
	}
	if (input > SAA7115_SVIDEO3)
		return -EINVAL;
	if (state->input == input && state->output == output)
		return 0;
	v4l2_dbg(1, debug, sd, "now setting %s input %s output\n",
		(input >= SAA7115_SVIDEO0) ? "S-Video" : "Composite",
		(output == SAA7115_IPORT_ON) ? "iport on" : "iport off");
	state->input = input;

	/* saa7111 has slightly different input numbering */
	if (state->ident <= V4L2_IDENT_SAA7111A) {
		if (input >= SAA7115_COMPOSITE4)
			input -= 2;
		/* saa7111 specific */
		saa711x_write(sd, R_10_CHROMA_CNTL_2,
				(saa711x_read(sd, R_10_CHROMA_CNTL_2) & 0x3f) |
				((output & 0xc0) ^ 0x40));
		saa711x_write(sd, R_13_RT_X_PORT_OUT_CNTL,
				(saa711x_read(sd, R_13_RT_X_PORT_OUT_CNTL) & 0xf0) |
				((output & 2) ? 0x0a : 0));
	}

	/* select mode */
	saa711x_write(sd, R_02_INPUT_CNTL_1,
		      (saa711x_read(sd, R_02_INPUT_CNTL_1) & mask) |
		       input);

	/* bypass chrominance trap for S-Video modes */
	saa711x_write(sd, R_09_LUMA_CNTL,
			(saa711x_read(sd, R_09_LUMA_CNTL) & 0x7f) |
			(state->input >= SAA7115_SVIDEO0 ? 0x80 : 0x0));

	state->output = output;
	if (state->ident == V4L2_IDENT_SAA7114 ||
			state->ident == V4L2_IDENT_SAA7115) {
		saa711x_write(sd, R_83_X_PORT_I_O_ENA_AND_OUT_CLK,
				(saa711x_read(sd, R_83_X_PORT_I_O_ENA_AND_OUT_CLK) & 0xfe) |
				(state->output & 0x01));
	}
	if (state->ident > V4L2_IDENT_SAA7111A) {
		if (config & SAA7115_IDQ_IS_DEFAULT)
			saa711x_write(sd, R_85_I_PORT_SIGNAL_POLAR, 0x20);
		else
			saa711x_write(sd, R_85_I_PORT_SIGNAL_POLAR, 0x21);
	}
	return 0;
}

static int saa711x_s_gpio(struct v4l2_subdev *sd, u32 val)
{
	struct saa711x_state *state = to_state(sd);

	if (state->ident > V4L2_IDENT_SAA7111A)
		return -EINVAL;
	saa711x_write(sd, 0x11, (saa711x_read(sd, 0x11) & 0x7f) |
		(val ? 0x80 : 0));
	return 0;
}

static int saa711x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct saa711x_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s output\n",
			enable ? "enable" : "disable");

	if (state->enable == enable)
		return 0;
	state->enable = enable;
	if (!saa711x_has_reg(state->ident, R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED))
		return 0;
	saa711x_write(sd, R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED, state->enable);
	return 0;
}

static int saa711x_s_crystal_freq(struct v4l2_subdev *sd, u32 freq, u32 flags)
{
	struct saa711x_state *state = to_state(sd);

	if (freq != SAA7115_FREQ_32_11_MHZ && freq != SAA7115_FREQ_24_576_MHZ)
		return -EINVAL;
	state->crystal_freq = freq;
	state->double_asclk = flags & SAA7115_FREQ_FL_DOUBLE_ASCLK;
	state->cgcdiv = (flags & SAA7115_FREQ_FL_CGCDIV) ? 3 : 4;
	state->ucgc = flags & SAA7115_FREQ_FL_UCGC;
	state->apll = flags & SAA7115_FREQ_FL_APLL;
	saa711x_s_clock_freq(sd, state->audclk_freq);
	return 0;
}

static int saa711x_reset(struct v4l2_subdev *sd, u32 val)
{
	v4l2_dbg(1, debug, sd, "decoder RESET\n");
	saa711x_writeregs(sd, saa7115_cfg_reset_scaler);
	return 0;
}

static int saa711x_g_vbi_data(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_data *data)
{
	/* Note: the internal field ID is inverted for NTSC,
	   so data->field 0 maps to the saa7115 even field,
	   whereas for PAL it maps to the saa7115 odd field. */
	switch (data->id) {
	case V4L2_SLICED_WSS_625:
		if (saa711x_read(sd, 0x6b) & 0xc0)
			return -EIO;
		data->data[0] = saa711x_read(sd, 0x6c);
		data->data[1] = saa711x_read(sd, 0x6d);
		return 0;
	case V4L2_SLICED_CAPTION_525:
		if (data->field == 0) {
			/* CC */
			if (saa711x_read(sd, 0x66) & 0x30)
				return -EIO;
			data->data[0] = saa711x_read(sd, 0x69);
			data->data[1] = saa711x_read(sd, 0x6a);
			return 0;
		}
		/* XDS */
		if (saa711x_read(sd, 0x66) & 0xc0)
			return -EIO;
		data->data[0] = saa711x_read(sd, 0x67);
		data->data[1] = saa711x_read(sd, 0x68);
		return 0;
	default:
		return -EINVAL;
	}
}

static int saa711x_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct saa711x_state *state = to_state(sd);
	int reg1f, reg1e;

	/*
	 * The V4L2 core already initializes std with all supported
	 * Standards. All driver needs to do is to mask it, to remove
	 * standards that don't apply from the mask
	 */

	reg1f = saa711x_read(sd, R_1F_STATUS_BYTE_2_VD_DEC);

	if (state->ident == V4L2_IDENT_SAA7115) {
		reg1e = saa711x_read(sd, R_1E_STATUS_BYTE_1_VD_DEC);

		v4l2_dbg(1, debug, sd, "Status byte 1 (0x1e)=0x%02x\n", reg1e);

		switch (reg1e & 0x03) {
		case 1:
			*std &= V4L2_STD_NTSC;
			break;
		case 2:
			/*
			 * V4L2_STD_PAL just cover the european PAL standards.
			 * This is wrong, as the device could also be using an
			 * other PAL standard.
			 */
			*std &= V4L2_STD_PAL   | V4L2_STD_PAL_N  | V4L2_STD_PAL_Nc |
				V4L2_STD_PAL_M | V4L2_STD_PAL_60;
			break;
		case 3:
			*std &= V4L2_STD_SECAM;
			break;
		default:
			/* Can't detect anything */
			break;
		}
	}

	v4l2_dbg(1, debug, sd, "Status byte 2 (0x1f)=0x%02x\n", reg1f);

	/* horizontal/vertical not locked */
	if (reg1f & 0x40)
		goto ret;

	if (reg1f & 0x20)
		*std &= V4L2_STD_525_60;
	else
		*std &= V4L2_STD_625_50;

ret:
	v4l2_dbg(1, debug, sd, "detected std mask = %08Lx\n", *std);

	return 0;
}

static int saa711x_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct saa711x_state *state = to_state(sd);
	int reg1e = 0x80;
	int reg1f;

	*status = V4L2_IN_ST_NO_SIGNAL;
	if (state->ident == V4L2_IDENT_SAA7115)
		reg1e = saa711x_read(sd, R_1E_STATUS_BYTE_1_VD_DEC);
	reg1f = saa711x_read(sd, R_1F_STATUS_BYTE_2_VD_DEC);
	if ((reg1f & 0xc1) == 0x81 && (reg1e & 0xc0) == 0x80)
		*status = 0;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int saa711x_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->val = saa711x_read(sd, reg->reg & 0xff);
	reg->size = 1;
	return 0;
}

static int saa711x_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	saa711x_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

static int saa711x_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct saa711x_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, state->ident, 0);
}

static int saa711x_log_status(struct v4l2_subdev *sd)
{
	struct saa711x_state *state = to_state(sd);
	int reg1e, reg1f;
	int signalOk;
	int vcr;

	v4l2_info(sd, "Audio frequency: %d Hz\n", state->audclk_freq);
	if (state->ident != V4L2_IDENT_SAA7115) {
		/* status for the saa7114 */
		reg1f = saa711x_read(sd, R_1F_STATUS_BYTE_2_VD_DEC);
		signalOk = (reg1f & 0xc1) == 0x81;
		v4l2_info(sd, "Video signal:    %s\n", signalOk ? "ok" : "bad");
		v4l2_info(sd, "Frequency:       %s\n", (reg1f & 0x20) ? "60 Hz" : "50 Hz");
		return 0;
	}

	/* status for the saa7115 */
	reg1e = saa711x_read(sd, R_1E_STATUS_BYTE_1_VD_DEC);
	reg1f = saa711x_read(sd, R_1F_STATUS_BYTE_2_VD_DEC);

	signalOk = (reg1f & 0xc1) == 0x81 && (reg1e & 0xc0) == 0x80;
	vcr = !(reg1f & 0x10);

	if (state->input >= 6)
		v4l2_info(sd, "Input:           S-Video %d\n", state->input - 6);
	else
		v4l2_info(sd, "Input:           Composite %d\n", state->input);
	v4l2_info(sd, "Video signal:    %s\n", signalOk ? (vcr ? "VCR" : "broadcast/DVD") : "bad");
	v4l2_info(sd, "Frequency:       %s\n", (reg1f & 0x20) ? "60 Hz" : "50 Hz");

	switch (reg1e & 0x03) {
	case 1:
		v4l2_info(sd, "Detected format: NTSC\n");
		break;
	case 2:
		v4l2_info(sd, "Detected format: PAL\n");
		break;
	case 3:
		v4l2_info(sd, "Detected format: SECAM\n");
		break;
	default:
		v4l2_info(sd, "Detected format: BW/No color\n");
		break;
	}
	v4l2_info(sd, "Width, Height:   %d, %d\n", state->width, state->height);
	v4l2_ctrl_handler_log_status(&state->hdl, sd->name);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops saa711x_ctrl_ops = {
	.s_ctrl = saa711x_s_ctrl,
	.g_volatile_ctrl = saa711x_g_volatile_ctrl,
};

static const struct v4l2_subdev_core_ops saa711x_core_ops = {
	.log_status = saa711x_log_status,
	.g_chip_ident = saa711x_g_chip_ident,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
	.s_std = saa711x_s_std,
	.reset = saa711x_reset,
	.s_gpio = saa711x_s_gpio,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = saa711x_g_register,
	.s_register = saa711x_s_register,
#endif
};

static const struct v4l2_subdev_tuner_ops saa711x_tuner_ops = {
	.s_radio = saa711x_s_radio,
	.g_tuner = saa711x_g_tuner,
};

static const struct v4l2_subdev_audio_ops saa711x_audio_ops = {
	.s_clock_freq = saa711x_s_clock_freq,
};

static const struct v4l2_subdev_video_ops saa711x_video_ops = {
	.s_routing = saa711x_s_routing,
	.s_crystal_freq = saa711x_s_crystal_freq,
	.s_mbus_fmt = saa711x_s_mbus_fmt,
	.s_stream = saa711x_s_stream,
	.querystd = saa711x_querystd,
	.g_input_status = saa711x_g_input_status,
};

static const struct v4l2_subdev_vbi_ops saa711x_vbi_ops = {
	.g_vbi_data = saa711x_g_vbi_data,
	.decode_vbi_line = saa711x_decode_vbi_line,
	.g_sliced_fmt = saa711x_g_sliced_fmt,
	.s_sliced_fmt = saa711x_s_sliced_fmt,
	.s_raw_fmt = saa711x_s_raw_fmt,
};

static const struct v4l2_subdev_ops saa711x_ops = {
	.core = &saa711x_core_ops,
	.tuner = &saa711x_tuner_ops,
	.audio = &saa711x_audio_ops,
	.video = &saa711x_video_ops,
	.vbi = &saa711x_vbi_ops,
};

/* ----------------------------------------------------------------------- */

/**
 * saa711x_detect_chip - Detects the saa711x (or clone) variant
 * @client:		I2C client structure.
 * @id:			I2C device ID structure.
 * @name:		Name of the device to be filled.
 * @size:		Size of the name var.
 *
 * Detects the Philips/NXP saa711x chip, or some clone of it.
 * if 'id' is NULL or id->driver_data is equal to 1, it auto-probes
 * the analog demod.
 * If the tuner is not found, it returns -ENODEV.
 * If auto-detection is disabled and the tuner doesn't match what it was
 *	requred, it returns -EINVAL and fills 'name'.
 * If the chip is found, it returns the chip ID and fills 'name'.
 */
static int saa711x_detect_chip(struct i2c_client *client,
			       const struct i2c_device_id *id,
			       char *name, unsigned size)
{
	char chip_ver[size - 1];
	char chip_id;
	int i;
	int autodetect;

	autodetect = !id || id->driver_data == 1;

	/* Read the chip version register */
	for (i = 0; i < size - 1; i++) {
		i2c_smbus_write_byte_data(client, 0, i);
		chip_ver[i] = i2c_smbus_read_byte_data(client, 0);
		name[i] = (chip_ver[i] & 0x0f) + '0';
		if (name[i] > '9')
			name[i] += 'a' - '9' - 1;
	}
	name[i] = '\0';

	/* Check if it is a Philips/NXP chip */
	if (!memcmp(name + 1, "f711", 4)) {
		chip_id = name[5];
		snprintf(name, size, "saa711%c", chip_id);

		if (!autodetect && strcmp(name, id->name))
			return -EINVAL;

		switch (chip_id) {
		case '1':
			if (chip_ver[0] & 0xf0) {
				snprintf(name, size, "saa711%ca", chip_id);
				v4l_info(client, "saa7111a variant found\n");
				return V4L2_IDENT_SAA7111A;
			}
			return V4L2_IDENT_SAA7111;
		case '3':
			return V4L2_IDENT_SAA7113;
		case '4':
			return V4L2_IDENT_SAA7114;
		case '5':
			return V4L2_IDENT_SAA7115;
		case '8':
			return V4L2_IDENT_SAA7118;
		default:
			v4l2_info(client,
				  "WARNING: Philips/NXP chip unknown - Falling back to saa7111\n");
			return V4L2_IDENT_SAA7111;
		}
	}

	/* Check if it is a gm7113c */
	if (!memcmp(name, "0000", 4)) {
		chip_id = 0;
		for (i = 0; i < 4; i++) {
			chip_id = chip_id << 1;
			chip_id |= (chip_ver[i] & 0x80) ? 1 : 0;
		}

		/*
		 * Note: From the datasheet, only versions 1 and 2
		 * exists. However, tests on a device labeled as:
		 * "GM7113C 1145" returned "10" on all 16 chip
		 * version (reg 0x00) reads. So, we need to also
		 * accept at least verion 0. For now, let's just
		 * assume that a device that returns "0000" for
		 * the lower nibble is a gm7113c.
		 */

		strlcpy(name, "gm7113c", size);

		if (!autodetect && strcmp(name, id->name))
			return -EINVAL;

		v4l_dbg(1, debug, client,
			"It seems to be a %s chip (%*ph) @ 0x%x.\n",
			name, 16, chip_ver, client->addr << 1);

		return V4L2_IDENT_GM7113C;
	}

	/* Chip was not discovered. Return its ID and don't bind */
	v4l_dbg(1, debug, client, "chip %*ph @ 0x%x is unknown.\n",
		16, chip_ver, client->addr << 1);
	return -ENODEV;
}

static int saa711x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct saa711x_state *state;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	int ident;
	char name[17];

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	ident = saa711x_detect_chip(client, id, name, sizeof(name));
	if (ident == -EINVAL) {
		/* Chip exists, but doesn't match */
		v4l_warn(client, "found %s while %s was expected\n",
			 name, id->name);
		return -ENODEV;
	}
	if (ident < 0)
		return ident;

	strlcpy(client->name, name, sizeof(client->name));

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &saa711x_ops);

	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 6);
	/* add in ascending ID order */
	v4l2_ctrl_new_std(hdl, &saa711x_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &saa711x_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(hdl, &saa711x_ctrl_ops,
			V4L2_CID_SATURATION, 0, 127, 1, 64);
	v4l2_ctrl_new_std(hdl, &saa711x_ctrl_ops,
			V4L2_CID_HUE, -128, 127, 1, 0);
	state->agc = v4l2_ctrl_new_std(hdl, &saa711x_ctrl_ops,
			V4L2_CID_CHROMA_AGC, 0, 1, 1, 1);
	state->gain = v4l2_ctrl_new_std(hdl, &saa711x_ctrl_ops,
			V4L2_CID_CHROMA_GAIN, 0, 127, 1, 40);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}
	v4l2_ctrl_auto_cluster(2, &state->agc, 0, true);

	state->input = -1;
	state->output = SAA7115_IPORT_ON;
	state->enable = 1;
	state->radio = 0;
	state->ident = ident;

	state->audclk_freq = 48000;

	v4l2_dbg(1, debug, sd, "writing init values\n");

	/* init to 60hz/48khz */
	state->crystal_freq = SAA7115_FREQ_24_576_MHZ;
	switch (state->ident) {
	case V4L2_IDENT_SAA7111:
	case V4L2_IDENT_SAA7111A:
		saa711x_writeregs(sd, saa7111_init);
		break;
	case V4L2_IDENT_GM7113C:
	case V4L2_IDENT_SAA7113:
		saa711x_writeregs(sd, saa7113_init);
		break;
	default:
		state->crystal_freq = SAA7115_FREQ_32_11_MHZ;
		saa711x_writeregs(sd, saa7115_init_auto_input);
	}
	if (state->ident > V4L2_IDENT_SAA7111A)
		saa711x_writeregs(sd, saa7115_init_misc);
	saa711x_set_v4lstd(sd, V4L2_STD_NTSC);
	v4l2_ctrl_handler_setup(hdl);

	v4l2_dbg(1, debug, sd, "status: (1E) 0x%02x, (1F) 0x%02x\n",
		saa711x_read(sd, R_1E_STATUS_BYTE_1_VD_DEC),
		saa711x_read(sd, R_1F_STATUS_BYTE_2_VD_DEC));
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa711x_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	return 0;
}

static const struct i2c_device_id saa711x_id[] = {
	{ "saa7115_auto", 1 }, /* autodetect */
	{ "saa7111", 0 },
	{ "saa7113", 0 },
	{ "saa7114", 0 },
	{ "saa7115", 0 },
	{ "saa7118", 0 },
	{ "gm7113c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa711x_id);

static struct i2c_driver saa711x_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "saa7115",
	},
	.probe		= saa711x_probe,
	.remove		= saa711x_remove,
	.id_table	= saa711x_id,
};

module_i2c_driver(saa711x_driver);
