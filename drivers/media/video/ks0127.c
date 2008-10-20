/*
 * Video Capture Driver (Video for Linux 1/2)
 * for the Matrox Marvel G200,G400 and Rainbow Runner-G series
 *
 * This module is an interface to the KS0127 video decoder chip.
 *
 * Copyright (C) 1999  Ryan Drake <stiletto@mediaone.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *****************************************************************************
 *
 * Modified and extended by
 *	Mike Bernson <mike@mlb.org>
 *	Gerard v.d. Horst
 *	Leon van Stuivenberg <l.vanstuivenberg@chello.nl>
 *	Gernot Ziegler <gz@lysator.liu.se>
 *
 * Version History:
 * V1.0 Ryan Drake	   Initial version by Ryan Drake
 * V1.1 Gerard v.d. Horst  Added some debugoutput, reset the video-standard
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>
#include "ks0127.h"

MODULE_DESCRIPTION("KS0127 video decoder driver");
MODULE_AUTHOR("Ryan Drake");
MODULE_LICENSE("GPL");

#define KS_TYPE_UNKNOWN	0
#define KS_TYPE_0122S	1
#define KS_TYPE_0127	2
#define KS_TYPE_0127B	3

/* ks0127 control registers */
#define KS_STAT     0x00
#define KS_CMDA     0x01
#define KS_CMDB     0x02
#define KS_CMDC     0x03
#define KS_CMDD     0x04
#define KS_HAVB     0x05
#define KS_HAVE     0x06
#define KS_HS1B     0x07
#define KS_HS1E     0x08
#define KS_HS2B     0x09
#define KS_HS2E     0x0a
#define KS_AGC      0x0b
#define KS_HXTRA    0x0c
#define KS_CDEM     0x0d
#define KS_PORTAB   0x0e
#define KS_LUMA     0x0f
#define KS_CON      0x10
#define KS_BRT      0x11
#define KS_CHROMA   0x12
#define KS_CHROMB   0x13
#define KS_DEMOD    0x14
#define KS_SAT      0x15
#define KS_HUE      0x16
#define KS_VERTIA   0x17
#define KS_VERTIB   0x18
#define KS_VERTIC   0x19
#define KS_HSCLL    0x1a
#define KS_HSCLH    0x1b
#define KS_VSCLL    0x1c
#define KS_VSCLH    0x1d
#define KS_OFMTA    0x1e
#define KS_OFMTB    0x1f
#define KS_VBICTL   0x20
#define KS_CCDAT2   0x21
#define KS_CCDAT1   0x22
#define KS_VBIL30   0x23
#define KS_VBIL74   0x24
#define KS_VBIL118  0x25
#define KS_VBIL1512 0x26
#define KS_TTFRAM   0x27
#define KS_TESTA    0x28
#define KS_UVOFFH   0x29
#define KS_UVOFFL   0x2a
#define KS_UGAIN    0x2b
#define KS_VGAIN    0x2c
#define KS_VAVB     0x2d
#define KS_VAVE     0x2e
#define KS_CTRACK   0x2f
#define KS_POLCTL   0x30
#define KS_REFCOD   0x31
#define KS_INVALY   0x32
#define KS_INVALU   0x33
#define KS_INVALV   0x34
#define KS_UNUSEY   0x35
#define KS_UNUSEU   0x36
#define KS_UNUSEV   0x37
#define KS_USRSAV   0x38
#define KS_USREAV   0x39
#define KS_SHS1A    0x3a
#define KS_SHS1B    0x3b
#define KS_SHS1C    0x3c
#define KS_CMDE     0x3d
#define KS_VSDEL    0x3e
#define KS_CMDF     0x3f
#define KS_GAMMA0   0x40
#define KS_GAMMA1   0x41
#define KS_GAMMA2   0x42
#define KS_GAMMA3   0x43
#define KS_GAMMA4   0x44
#define KS_GAMMA5   0x45
#define KS_GAMMA6   0x46
#define KS_GAMMA7   0x47
#define KS_GAMMA8   0x48
#define KS_GAMMA9   0x49
#define KS_GAMMA10  0x4a
#define KS_GAMMA11  0x4b
#define KS_GAMMA12  0x4c
#define KS_GAMMA13  0x4d
#define KS_GAMMA14  0x4e
#define KS_GAMMA15  0x4f
#define KS_GAMMA16  0x50
#define KS_GAMMA17  0x51
#define KS_GAMMA18  0x52
#define KS_GAMMA19  0x53
#define KS_GAMMA20  0x54
#define KS_GAMMA21  0x55
#define KS_GAMMA22  0x56
#define KS_GAMMA23  0x57
#define KS_GAMMA24  0x58
#define KS_GAMMA25  0x59
#define KS_GAMMA26  0x5a
#define KS_GAMMA27  0x5b
#define KS_GAMMA28  0x5c
#define KS_GAMMA29  0x5d
#define KS_GAMMA30  0x5e
#define KS_GAMMA31  0x5f
#define KS_GAMMAD0  0x60
#define KS_GAMMAD1  0x61
#define KS_GAMMAD2  0x62
#define KS_GAMMAD3  0x63
#define KS_GAMMAD4  0x64
#define KS_GAMMAD5  0x65
#define KS_GAMMAD6  0x66
#define KS_GAMMAD7  0x67
#define KS_GAMMAD8  0x68
#define KS_GAMMAD9  0x69
#define KS_GAMMAD10 0x6a
#define KS_GAMMAD11 0x6b
#define KS_GAMMAD12 0x6c
#define KS_GAMMAD13 0x6d
#define KS_GAMMAD14 0x6e
#define KS_GAMMAD15 0x6f
#define KS_GAMMAD16 0x70
#define KS_GAMMAD17 0x71
#define KS_GAMMAD18 0x72
#define KS_GAMMAD19 0x73
#define KS_GAMMAD20 0x74
#define KS_GAMMAD21 0x75
#define KS_GAMMAD22 0x76
#define KS_GAMMAD23 0x77
#define KS_GAMMAD24 0x78
#define KS_GAMMAD25 0x79
#define KS_GAMMAD26 0x7a
#define KS_GAMMAD27 0x7b
#define KS_GAMMAD28 0x7c
#define KS_GAMMAD29 0x7d
#define KS_GAMMAD30 0x7e
#define KS_GAMMAD31 0x7f


/****************************************************************************
* mga_dev : represents one ks0127 chip.
****************************************************************************/

struct adjust {
	int	contrast;
	int	bright;
	int	hue;
	int	ugain;
	int	vgain;
};

struct ks0127 {
	int		format_width;
	int		format_height;
	int		cap_width;
	int		cap_height;
	int		norm;
	int		ks_type;
	u8 		regs[256];
};


static int debug; /* insmod parameter */

module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug output");

static u8 reg_defaults[64];

static void init_reg_defaults(void)
{
	static int initialized;
	u8 *table = reg_defaults;

	if (initialized)
		return;
	initialized = 1;

	table[KS_CMDA]     = 0x2c;  /* VSE=0, CCIR 601, autodetect standard */
	table[KS_CMDB]     = 0x12;  /* VALIGN=0, AGC control and input */
	table[KS_CMDC]     = 0x00;  /* Test options */
	/* clock & input select, write 1 to PORTA */
	table[KS_CMDD]     = 0x01;
	table[KS_HAVB]     = 0x00;  /* HAV Start Control */
	table[KS_HAVE]     = 0x00;  /* HAV End Control */
	table[KS_HS1B]     = 0x10;  /* HS1 Start Control */
	table[KS_HS1E]     = 0x00;  /* HS1 End Control */
	table[KS_HS2B]     = 0x00;  /* HS2 Start Control */
	table[KS_HS2E]     = 0x00;  /* HS2 End Control */
	table[KS_AGC]      = 0x53;  /* Manual setting for AGC */
	table[KS_HXTRA]    = 0x00;  /* Extra Bits for HAV and HS1/2 */
	table[KS_CDEM]     = 0x00;  /* Chroma Demodulation Control */
	table[KS_PORTAB]   = 0x0f;  /* port B is input, port A output GPPORT */
	table[KS_LUMA]     = 0x01;  /* Luma control */
	table[KS_CON]      = 0x00;  /* Contrast Control */
	table[KS_BRT]      = 0x00;  /* Brightness Control */
	table[KS_CHROMA]   = 0x2a;  /* Chroma control A */
	table[KS_CHROMB]   = 0x90;  /* Chroma control B */
	table[KS_DEMOD]    = 0x00;  /* Chroma Demodulation Control & Status */
	table[KS_SAT]      = 0x00;  /* Color Saturation Control*/
	table[KS_HUE]      = 0x00;  /* Hue Control */
	table[KS_VERTIA]   = 0x00;  /* Vertical Processing Control A */
	/* Vertical Processing Control B, luma 1 line delayed */
	table[KS_VERTIB]   = 0x12;
	table[KS_VERTIC]   = 0x0b;  /* Vertical Processing Control C */
	table[KS_HSCLL]    = 0x00;  /* Horizontal Scaling Ratio Low */
	table[KS_HSCLH]    = 0x00;  /* Horizontal Scaling Ratio High */
	table[KS_VSCLL]    = 0x00;  /* Vertical Scaling Ratio Low */
	table[KS_VSCLH]    = 0x00;  /* Vertical Scaling Ratio High */
	/* 16 bit YCbCr 4:2:2 output; I can't make the bt866 like 8 bit /Sam */
	table[KS_OFMTA]    = 0x30;
	table[KS_OFMTB]    = 0x00;  /* Output Control B */
	/* VBI Decoder Control; 4bit fmt: avoid Y overflow */
	table[KS_VBICTL]   = 0x5d;
	table[KS_CCDAT2]   = 0x00;  /* Read Only register */
	table[KS_CCDAT1]   = 0x00;  /* Read Only register */
	table[KS_VBIL30]   = 0xa8;  /* VBI data decoding options */
	table[KS_VBIL74]   = 0xaa;  /* VBI data decoding options */
	table[KS_VBIL118]  = 0x2a;  /* VBI data decoding options */
	table[KS_VBIL1512] = 0x00;  /* VBI data decoding options */
	table[KS_TTFRAM]   = 0x00;  /* Teletext frame alignment pattern */
	table[KS_TESTA]    = 0x00;  /* test register, shouldn't be written */
	table[KS_UVOFFH]   = 0x00;  /* UV Offset Adjustment High */
	table[KS_UVOFFL]   = 0x00;  /* UV Offset Adjustment Low */
	table[KS_UGAIN]    = 0x00;  /* U Component Gain Adjustment */
	table[KS_VGAIN]    = 0x00;  /* V Component Gain Adjustment */
	table[KS_VAVB]     = 0x07;  /* VAV Begin */
	table[KS_VAVE]     = 0x00;  /* VAV End */
	table[KS_CTRACK]   = 0x00;  /* Chroma Tracking Control */
	table[KS_POLCTL]   = 0x41;  /* Timing Signal Polarity Control */
	table[KS_REFCOD]   = 0x80;  /* Reference Code Insertion Control */
	table[KS_INVALY]   = 0x10;  /* Invalid Y Code */
	table[KS_INVALU]   = 0x80;  /* Invalid U Code */
	table[KS_INVALV]   = 0x80;  /* Invalid V Code */
	table[KS_UNUSEY]   = 0x10;  /* Unused Y Code */
	table[KS_UNUSEU]   = 0x80;  /* Unused U Code */
	table[KS_UNUSEV]   = 0x80;  /* Unused V Code */
	table[KS_USRSAV]   = 0x00;  /* reserved */
	table[KS_USREAV]   = 0x00;  /* reserved */
	table[KS_SHS1A]    = 0x00;  /* User Defined SHS1 A */
	/* User Defined SHS1 B, ALT656=1 on 0127B */
	table[KS_SHS1B]    = 0x80;
	table[KS_SHS1C]    = 0x00;  /* User Defined SHS1 C */
	table[KS_CMDE]     = 0x00;  /* Command Register E */
	table[KS_VSDEL]    = 0x00;  /* VS Delay Control */
	/* Command Register F, update -immediately- */
	/* (there might come no vsync)*/
	table[KS_CMDF]     = 0x02;
}


/* We need to manually read because of a bug in the KS0127 chip.
 *
 * An explanation from kayork@mail.utexas.edu:
 *
 * During I2C reads, the KS0127 only samples for a stop condition
 * during the place where the acknowledge bit should be. Any standard
 * I2C implementation (correctly) throws in another clock transition
 * at the 9th bit, and the KS0127 will not recognize the stop condition
 * and will continue to clock out data.
 *
 * So we have to do the read ourself.  Big deal.
 *	   workaround in i2c-algo-bit
 */


static u8 ks0127_read(struct i2c_client *c, u8 reg)
{
	char val = 0;
	struct i2c_msg msgs[] = {
		{ c->addr, 0, sizeof(reg), &reg },
		{ c->addr, I2C_M_RD | I2C_M_NO_RD_ACK, sizeof(val), &val }
	};
	int ret;

	ret = i2c_transfer(c->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		v4l_dbg(1, debug, c, "read error\n");

	return val;
}


static void ks0127_write(struct i2c_client *c, u8 reg, u8 val)
{
	struct ks0127 *ks = i2c_get_clientdata(c);
	char msg[] = { reg, val };

	if (i2c_master_send(c, msg, sizeof(msg)) != sizeof(msg))
		v4l_dbg(1, debug, c, "write error\n");

	ks->regs[reg] = val;
}


/* generic bit-twiddling */
static void ks0127_and_or(struct i2c_client *client, u8 reg, u8 and_v, u8 or_v)
{
	struct ks0127 *ks = i2c_get_clientdata(client);

	u8 val = ks->regs[reg];
	val = (val & and_v) | or_v;
	ks0127_write(client, reg, val);
}



/****************************************************************************
* ks0127 private api
****************************************************************************/
static void ks0127_reset(struct i2c_client *c)
{
	struct ks0127 *ks = i2c_get_clientdata(c);
	u8 *table = reg_defaults;
	int i;

	ks->ks_type = KS_TYPE_UNKNOWN;

	v4l_dbg(1, debug, c, "reset\n");
	msleep(1);

	/* initialize all registers to known values */
	/* (except STAT, 0x21, 0x22, TEST and 0x38,0x39) */

	for (i = 1; i < 33; i++)
		ks0127_write(c, i, table[i]);

	for (i = 35; i < 40; i++)
		ks0127_write(c, i, table[i]);

	for (i = 41; i < 56; i++)
		ks0127_write(c, i, table[i]);

	for (i = 58; i < 64; i++)
		ks0127_write(c, i, table[i]);


	if ((ks0127_read(c, KS_STAT) & 0x80) == 0) {
		ks->ks_type = KS_TYPE_0122S;
		v4l_dbg(1, debug, c, "ks0122s found\n");
		return;
	}

	switch (ks0127_read(c, KS_CMDE) & 0x0f) {
	case 0:
		ks->ks_type = KS_TYPE_0127;
		v4l_dbg(1, debug, c, "ks0127 found\n");
		break;

	case 9:
		ks->ks_type = KS_TYPE_0127B;
		v4l_dbg(1, debug, c, "ks0127B Revision A found\n");
		break;

	default:
		v4l_dbg(1, debug, c, "unknown revision\n");
		break;
	}
}

static int ks0127_command(struct i2c_client *c, unsigned cmd, void *arg)
{
	struct ks0127 *ks = i2c_get_clientdata(c);
	int		*iarg = (int *)arg;
	int		status;

	if (!ks)
		return -ENODEV;

	switch (cmd) {
	case DECODER_INIT:
		v4l_dbg(1, debug, c, "DECODER_INIT\n");
		ks0127_reset(c);
		break;

	case DECODER_SET_INPUT:
		switch(*iarg) {
		case KS_INPUT_COMPOSITE_1:
		case KS_INPUT_COMPOSITE_2:
		case KS_INPUT_COMPOSITE_3:
		case KS_INPUT_COMPOSITE_4:
		case KS_INPUT_COMPOSITE_5:
		case KS_INPUT_COMPOSITE_6:
			v4l_dbg(1, debug, c,
				"DECODER_SET_INPUT %d: Composite\n", *iarg);
			/* autodetect 50/60 Hz */
			ks0127_and_or(c, KS_CMDA,   0xfc, 0x00);
			/* VSE=0 */
			ks0127_and_or(c, KS_CMDA,   ~0x40, 0x00);
			/* set input line */
			ks0127_and_or(c, KS_CMDB,   0xb0, *iarg);
			/* non-freerunning mode */
			ks0127_and_or(c, KS_CMDC,   0x70, 0x0a);
			/* analog input */
			ks0127_and_or(c, KS_CMDD,   0x03, 0x00);
			/* enable chroma demodulation */
			ks0127_and_or(c, KS_CTRACK, 0xcf, 0x00);
			/* chroma trap, HYBWR=1 */
			ks0127_and_or(c, KS_LUMA,   0x00,
				       (reg_defaults[KS_LUMA])|0x0c);
			/* scaler fullbw, luma comb off */
			ks0127_and_or(c, KS_VERTIA, 0x08, 0x81);
			/* manual chroma comb .25 .5 .25 */
			ks0127_and_or(c, KS_VERTIC, 0x0f, 0x90);

			/* chroma path delay */
			ks0127_and_or(c, KS_CHROMB, 0x0f, 0x90);

			ks0127_write(c, KS_UGAIN, reg_defaults[KS_UGAIN]);
			ks0127_write(c, KS_VGAIN, reg_defaults[KS_VGAIN]);
			ks0127_write(c, KS_UVOFFH, reg_defaults[KS_UVOFFH]);
			ks0127_write(c, KS_UVOFFL, reg_defaults[KS_UVOFFL]);
			break;

		case KS_INPUT_SVIDEO_1:
		case KS_INPUT_SVIDEO_2:
		case KS_INPUT_SVIDEO_3:
			v4l_dbg(1, debug, c,
				"DECODER_SET_INPUT %d: S-Video\n", *iarg);
			/* autodetect 50/60 Hz */
			ks0127_and_or(c, KS_CMDA,   0xfc, 0x00);
			/* VSE=0 */
			ks0127_and_or(c, KS_CMDA,   ~0x40, 0x00);
			/* set input line */
			ks0127_and_or(c, KS_CMDB,   0xb0, *iarg);
			/* non-freerunning mode */
			ks0127_and_or(c, KS_CMDC,   0x70, 0x0a);
			/* analog input */
			ks0127_and_or(c, KS_CMDD,   0x03, 0x00);
			/* enable chroma demodulation */
			ks0127_and_or(c, KS_CTRACK, 0xcf, 0x00);
			ks0127_and_or(c, KS_LUMA, 0x00,
				       reg_defaults[KS_LUMA]);
			/* disable luma comb */
			ks0127_and_or(c, KS_VERTIA, 0x08,
				       (reg_defaults[KS_VERTIA]&0xf0)|0x01);
			ks0127_and_or(c, KS_VERTIC, 0x0f,
				       reg_defaults[KS_VERTIC]&0xf0);

			ks0127_and_or(c, KS_CHROMB, 0x0f,
				       reg_defaults[KS_CHROMB]&0xf0);

			ks0127_write(c, KS_UGAIN, reg_defaults[KS_UGAIN]);
			ks0127_write(c, KS_VGAIN, reg_defaults[KS_VGAIN]);
			ks0127_write(c, KS_UVOFFH, reg_defaults[KS_UVOFFH]);
			ks0127_write(c, KS_UVOFFL, reg_defaults[KS_UVOFFL]);
			break;

		case KS_INPUT_YUV656:
			v4l_dbg(1, debug, c,
				"DECODER_SET_INPUT 15: YUV656\n");
			if (ks->norm == VIDEO_MODE_NTSC ||
			    ks->norm == KS_STD_PAL_M)
				/* force 60 Hz */
				ks0127_and_or(c, KS_CMDA,   0xfc, 0x03);
			else
				/* force 50 Hz */
				ks0127_and_or(c, KS_CMDA,   0xfc, 0x02);

			ks0127_and_or(c, KS_CMDA,   0xff, 0x40); /* VSE=1 */
			/* set input line and VALIGN */
			ks0127_and_or(c, KS_CMDB,   0xb0, (*iarg | 0x40));
			/* freerunning mode, */
			/* TSTGEN = 1 TSTGFR=11 TSTGPH=0 TSTGPK=0  VMEM=1*/
			ks0127_and_or(c, KS_CMDC,   0x70, 0x87);
			/* digital input, SYNDIR = 0 INPSL=01 CLKDIR=0 EAV=0 */
			ks0127_and_or(c, KS_CMDD,   0x03, 0x08);
			/* disable chroma demodulation */
			ks0127_and_or(c, KS_CTRACK, 0xcf, 0x30);
			/* HYPK =01 CTRAP = 0 HYBWR=0 PED=1 RGBH=1 UNIT=1 */
			ks0127_and_or(c, KS_LUMA,   0x00, 0x71);
			ks0127_and_or(c, KS_VERTIC, 0x0f,
				       reg_defaults[KS_VERTIC]&0xf0);

			/* scaler fullbw, luma comb off */
			ks0127_and_or(c, KS_VERTIA, 0x08, 0x81);

			ks0127_and_or(c, KS_CHROMB, 0x0f,
				       reg_defaults[KS_CHROMB]&0xf0);

			ks0127_and_or(c, KS_CON, 0x00, 0x00);
			ks0127_and_or(c, KS_BRT, 0x00, 32);	/* spec: 34 */
				/* spec: 229 (e5) */
			ks0127_and_or(c, KS_SAT, 0x00, 0xe8);
			ks0127_and_or(c, KS_HUE, 0x00, 0);

			ks0127_and_or(c, KS_UGAIN, 0x00, 238);
			ks0127_and_or(c, KS_VGAIN, 0x00, 0x00);

			/*UOFF:0x30, VOFF:0x30, TSTCGN=1 */
			ks0127_and_or(c, KS_UVOFFH, 0x00, 0x4f);
			ks0127_and_or(c, KS_UVOFFL, 0x00, 0x00);
			break;

		default:
			v4l_dbg(1, debug, c,
				"DECODER_SET_INPUT: Unknown input %d\n", *iarg);
			break;
		}

		/* hack: CDMLPF sometimes spontaneously switches on; */
		/* force back off */
		ks0127_write(c, KS_DEMOD, reg_defaults[KS_DEMOD]);
		break;

	case DECODER_SET_OUTPUT:
		switch(*iarg) {
		case KS_OUTPUT_YUV656E:
			v4l_dbg(1, debug, c,
				"DECODER_SET_OUTPUT: OUTPUT_YUV656E (Missing)\n");
			return -EINVAL;

		case KS_OUTPUT_EXV:
			v4l_dbg(1, debug, c,
				"DECODER_SET_OUTPUT: OUTPUT_EXV\n");
			ks0127_and_or(c, KS_OFMTA, 0xf0, 0x09);
			break;
		}
		break;

	case DECODER_SET_NORM: /* sam This block mixes old and new norm names... */
		/* Set to automatic SECAM/Fsc mode */
		ks0127_and_or(c, KS_DEMOD, 0xf0, 0x00);

		ks->norm = *iarg;
		switch (*iarg) {
		/* this is untested !! */
		/* It just detects PAL_N/NTSC_M (no special frequencies) */
		/* And you have to set the standard a second time afterwards */
		case VIDEO_MODE_AUTO:
			v4l_dbg(1, debug, c,
				"DECODER_SET_NORM: AUTO\n");

			/* The chip determines the format */
			/* based on the current field rate */
			ks0127_and_or(c, KS_CMDA,   0xfc, 0x00);
			ks0127_and_or(c, KS_CHROMA, 0x9f, 0x20);
			/* This is wrong for PAL ! As I said, */
			/* you need to set the standard once again !! */
			ks->format_height = 240;
			ks->format_width = 704;
			break;

		case VIDEO_MODE_NTSC:
			v4l_dbg(1, debug, c,
				"DECODER_SET_NORM: NTSC_M\n");
			ks0127_and_or(c, KS_CHROMA, 0x9f, 0x20);
			ks->format_height = 240;
			ks->format_width = 704;
			break;

		case KS_STD_NTSC_N:
			v4l_dbg(1, debug, c,
				"KS0127_SET_NORM: NTSC_N (fixme)\n");
			ks0127_and_or(c, KS_CHROMA, 0x9f, 0x40);
			ks->format_height = 240;
			ks->format_width = 704;
			break;

		case VIDEO_MODE_PAL:
			v4l_dbg(1, debug, c,
				"DECODER_SET_NORM: PAL_N\n");
			ks0127_and_or(c, KS_CHROMA, 0x9f, 0x20);
			ks->format_height = 290;
			ks->format_width = 704;
			break;

		case KS_STD_PAL_M:
			v4l_dbg(1, debug, c,
				"KS0127_SET_NORM: PAL_M (fixme)\n");
			ks0127_and_or(c, KS_CHROMA, 0x9f, 0x40);
			ks->format_height = 290;
			ks->format_width = 704;
			break;

		case VIDEO_MODE_SECAM:
			v4l_dbg(1, debug, c,
				"KS0127_SET_NORM: SECAM\n");
			ks->format_height = 290;
			ks->format_width = 704;

			/* set to secam autodetection */
			ks0127_and_or(c, KS_CHROMA, 0xdf, 0x20);
			ks0127_and_or(c, KS_DEMOD, 0xf0, 0x00);
			schedule_timeout_interruptible(HZ/10+1);

			/* did it autodetect? */
			if (ks0127_read(c, KS_DEMOD) & 0x40)
				break;

			/* force to secam mode */
			ks0127_and_or(c, KS_DEMOD, 0xf0, 0x0f);
			break;

		default:
			v4l_dbg(1, debug, c,
				"DECODER_SET_NORM: Unknown norm %d\n", *iarg);
			break;
		}
		break;

	case DECODER_SET_PICTURE:
		v4l_dbg(1, debug, c,
			"DECODER_SET_PICTURE: not yet supported\n");
		return -EINVAL;

	/* sam todo: KS0127_SET_BRIGHTNESS: Merge into DECODER_SET_PICTURE */
	/* sam todo: KS0127_SET_CONTRAST: Merge into DECODER_SET_PICTURE */
	/* sam todo: KS0127_SET_HUE: Merge into DECODER_SET_PICTURE? */
	/* sam todo: KS0127_SET_SATURATION: Merge into DECODER_SET_PICTURE */
	/* sam todo: KS0127_SET_AGC_MODE: */
	/* sam todo: KS0127_SET_AGC: */
	/* sam todo: KS0127_SET_CHROMA_MODE: */
	/* sam todo: KS0127_SET_PIXCLK_MODE: */
	/* sam todo: KS0127_SET_GAMMA_MODE: */
	/* sam todo: KS0127_SET_UGAIN: */
	/* sam todo: KS0127_SET_VGAIN: */
	/* sam todo: KS0127_SET_INVALY: */
	/* sam todo: KS0127_SET_INVALU: */
	/* sam todo: KS0127_SET_INVALV: */
	/* sam todo: KS0127_SET_UNUSEY: */
	/* sam todo: KS0127_SET_UNUSEU: */
	/* sam todo: KS0127_SET_UNUSEV: */
	/* sam todo: KS0127_SET_VSALIGN_MODE: */

	case DECODER_ENABLE_OUTPUT:
	{
		int enable;

		iarg = arg;
		enable = (*iarg != 0);
		if (enable) {
			v4l_dbg(1, debug, c,
				"DECODER_ENABLE_OUTPUT on\n");
			/* All output pins on */
			ks0127_and_or(c, KS_OFMTA, 0xcf, 0x30);
			/* Obey the OEN pin */
			ks0127_and_or(c, KS_CDEM, 0x7f, 0x00);
		} else {
			v4l_dbg(1, debug, c,
				"DECODER_ENABLE_OUTPUT off\n");
			/* Video output pins off */
			ks0127_and_or(c, KS_OFMTA, 0xcf, 0x00);
			/* Ignore the OEN pin */
			ks0127_and_or(c, KS_CDEM, 0x7f, 0x80);
		}
		break;
	}

	/* sam todo: KS0127_SET_OUTPUT_MODE: */
	/* sam todo: KS0127_SET_WIDTH: */
	/* sam todo: KS0127_SET_HEIGHT: */
	/* sam todo: KS0127_SET_HSCALE: */

	case DECODER_GET_STATUS:
		v4l_dbg(1, debug, c, "DECODER_GET_STATUS\n");
		*iarg = 0;
		status = ks0127_read(c, KS_STAT);
		if (!(status & 0x20))		 /* NOVID not set */
			*iarg = (*iarg | DECODER_STATUS_GOOD);
		if ((status & 0x01))		      /* CLOCK set */
			*iarg = (*iarg | DECODER_STATUS_COLOR);
		if ((status & 0x08))		   /* PALDET set */
			*iarg = (*iarg | DECODER_STATUS_PAL);
		else
			*iarg = (*iarg | DECODER_STATUS_NTSC);
		break;

	/* Catch any unknown command */
	default:
		v4l_dbg(1, debug, c, "unknown: 0x%08x\n", cmd);
		return -EINVAL;
	}
	return 0;
}


/* Addresses to scan */
#define I2C_KS0127_ADDON   0xD8
#define I2C_KS0127_ONBOARD 0xDA

static unsigned short normal_i2c[] = {
	I2C_KS0127_ADDON >> 1,
	I2C_KS0127_ONBOARD >> 1,
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;

static int ks0127_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	struct ks0127 *ks;

	v4l_info(c, "%s chip found @ 0x%x (%s)\n",
		c->addr == (I2C_KS0127_ADDON >> 1) ? "addon" : "on-board",
		c->addr << 1, c->adapter->name);

	ks = kzalloc(sizeof(*ks), GFP_KERNEL);
	if (ks == NULL)
		return -ENOMEM;

	i2c_set_clientdata(c, ks);

	ks->ks_type = KS_TYPE_UNKNOWN;

	/* power up */
	init_reg_defaults();
	ks0127_write(c, KS_CMDA, 0x2c);
	mdelay(10);

	/* reset the device */
	ks0127_reset(c);
	return 0;
}

static int ks0127_remove(struct i2c_client *c)
{
	struct ks0127 *ks = i2c_get_clientdata(c);

	ks0127_write(c, KS_OFMTA, 0x20); /* tristate */
	ks0127_write(c, KS_CMDA, 0x2c | 0x80); /* power down */

	kfree(ks);
	return 0;
}

static int ks0127_legacy_probe(struct i2c_adapter *adapter)
{
	return adapter->id == I2C_HW_B_ZR36067;
}

static const struct i2c_device_id ks0127_id[] = {
	{ "ks0127", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ks0127_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "ks0127",
	.driverid = I2C_DRIVERID_KS0127,
	.command = ks0127_command,
	.probe = ks0127_probe,
	.remove = ks0127_remove,
	.legacy_probe = ks0127_legacy_probe,
	.id_table = ks0127_id,
};
