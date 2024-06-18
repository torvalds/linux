// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Video Capture Driver (Video for Linux 1/2)
 * for the Matrox Marvel G200,G400 and Rainbow Runner-G series
 *
 * This module is an interface to the KS0127 video decoder chip.
 *
 * Copyright (C) 1999  Ryan Drake <stiletto@mediaone.net>
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
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include "ks0127.h"

MODULE_DESCRIPTION("KS0127 video decoder driver");
MODULE_AUTHOR("Ryan Drake");
MODULE_LICENSE("GPL");

/* Addresses */
#define I2C_KS0127_ADDON   0xD8
#define I2C_KS0127_ONBOARD 0xDA


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
	struct v4l2_subdev sd;
	v4l2_std_id	norm;
	u8		regs[256];
};

static inline struct ks0127 *to_ks0127(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ks0127, sd);
}


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


static u8 ks0127_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	char val = 0;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.len = sizeof(reg),
			.buf = &reg
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD | I2C_M_NO_RD_ACK,
			.len = sizeof(val),
			.buf = &val
		}
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		v4l2_dbg(1, debug, sd, "read error\n");

	return val;
}


static void ks0127_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ks0127 *ks = to_ks0127(sd);
	char msg[] = { reg, val };

	if (i2c_master_send(client, msg, sizeof(msg)) != sizeof(msg))
		v4l2_dbg(1, debug, sd, "write error\n");

	ks->regs[reg] = val;
}


/* generic bit-twiddling */
static void ks0127_and_or(struct v4l2_subdev *sd, u8 reg, u8 and_v, u8 or_v)
{
	struct ks0127 *ks = to_ks0127(sd);

	u8 val = ks->regs[reg];
	val = (val & and_v) | or_v;
	ks0127_write(sd, reg, val);
}



/****************************************************************************
* ks0127 private api
****************************************************************************/
static void ks0127_init(struct v4l2_subdev *sd)
{
	u8 *table = reg_defaults;
	int i;

	v4l2_dbg(1, debug, sd, "reset\n");
	msleep(1);

	/* initialize all registers to known values */
	/* (except STAT, 0x21, 0x22, TEST and 0x38,0x39) */

	for (i = 1; i < 33; i++)
		ks0127_write(sd, i, table[i]);

	for (i = 35; i < 40; i++)
		ks0127_write(sd, i, table[i]);

	for (i = 41; i < 56; i++)
		ks0127_write(sd, i, table[i]);

	for (i = 58; i < 64; i++)
		ks0127_write(sd, i, table[i]);


	if ((ks0127_read(sd, KS_STAT) & 0x80) == 0) {
		v4l2_dbg(1, debug, sd, "ks0122s found\n");
		return;
	}

	switch (ks0127_read(sd, KS_CMDE) & 0x0f) {
	case 0:
		v4l2_dbg(1, debug, sd, "ks0127 found\n");
		break;

	case 9:
		v4l2_dbg(1, debug, sd, "ks0127B Revision A found\n");
		break;

	default:
		v4l2_dbg(1, debug, sd, "unknown revision\n");
		break;
	}
}

static int ks0127_s_routing(struct v4l2_subdev *sd,
			    u32 input, u32 output, u32 config)
{
	struct ks0127 *ks = to_ks0127(sd);

	switch (input) {
	case KS_INPUT_COMPOSITE_1:
	case KS_INPUT_COMPOSITE_2:
	case KS_INPUT_COMPOSITE_3:
	case KS_INPUT_COMPOSITE_4:
	case KS_INPUT_COMPOSITE_5:
	case KS_INPUT_COMPOSITE_6:
		v4l2_dbg(1, debug, sd,
			"s_routing %d: Composite\n", input);
		/* autodetect 50/60 Hz */
		ks0127_and_or(sd, KS_CMDA,   0xfc, 0x00);
		/* VSE=0 */
		ks0127_and_or(sd, KS_CMDA,   ~0x40, 0x00);
		/* set input line */
		ks0127_and_or(sd, KS_CMDB,   0xb0, input);
		/* non-freerunning mode */
		ks0127_and_or(sd, KS_CMDC,   0x70, 0x0a);
		/* analog input */
		ks0127_and_or(sd, KS_CMDD,   0x03, 0x00);
		/* enable chroma demodulation */
		ks0127_and_or(sd, KS_CTRACK, 0xcf, 0x00);
		/* chroma trap, HYBWR=1 */
		ks0127_and_or(sd, KS_LUMA,   0x00,
			       (reg_defaults[KS_LUMA])|0x0c);
		/* scaler fullbw, luma comb off */
		ks0127_and_or(sd, KS_VERTIA, 0x08, 0x81);
		/* manual chroma comb .25 .5 .25 */
		ks0127_and_or(sd, KS_VERTIC, 0x0f, 0x90);

		/* chroma path delay */
		ks0127_and_or(sd, KS_CHROMB, 0x0f, 0x90);

		ks0127_write(sd, KS_UGAIN, reg_defaults[KS_UGAIN]);
		ks0127_write(sd, KS_VGAIN, reg_defaults[KS_VGAIN]);
		ks0127_write(sd, KS_UVOFFH, reg_defaults[KS_UVOFFH]);
		ks0127_write(sd, KS_UVOFFL, reg_defaults[KS_UVOFFL]);
		break;

	case KS_INPUT_SVIDEO_1:
	case KS_INPUT_SVIDEO_2:
	case KS_INPUT_SVIDEO_3:
		v4l2_dbg(1, debug, sd,
			"s_routing %d: S-Video\n", input);
		/* autodetect 50/60 Hz */
		ks0127_and_or(sd, KS_CMDA,   0xfc, 0x00);
		/* VSE=0 */
		ks0127_and_or(sd, KS_CMDA,   ~0x40, 0x00);
		/* set input line */
		ks0127_and_or(sd, KS_CMDB,   0xb0, input);
		/* non-freerunning mode */
		ks0127_and_or(sd, KS_CMDC,   0x70, 0x0a);
		/* analog input */
		ks0127_and_or(sd, KS_CMDD,   0x03, 0x00);
		/* enable chroma demodulation */
		ks0127_and_or(sd, KS_CTRACK, 0xcf, 0x00);
		ks0127_and_or(sd, KS_LUMA, 0x00,
			       reg_defaults[KS_LUMA]);
		/* disable luma comb */
		ks0127_and_or(sd, KS_VERTIA, 0x08,
			       (reg_defaults[KS_VERTIA]&0xf0)|0x01);
		ks0127_and_or(sd, KS_VERTIC, 0x0f,
			       reg_defaults[KS_VERTIC]&0xf0);

		ks0127_and_or(sd, KS_CHROMB, 0x0f,
			       reg_defaults[KS_CHROMB]&0xf0);

		ks0127_write(sd, KS_UGAIN, reg_defaults[KS_UGAIN]);
		ks0127_write(sd, KS_VGAIN, reg_defaults[KS_VGAIN]);
		ks0127_write(sd, KS_UVOFFH, reg_defaults[KS_UVOFFH]);
		ks0127_write(sd, KS_UVOFFL, reg_defaults[KS_UVOFFL]);
		break;

	case KS_INPUT_YUV656:
		v4l2_dbg(1, debug, sd, "s_routing 15: YUV656\n");
		if (ks->norm & V4L2_STD_525_60)
			/* force 60 Hz */
			ks0127_and_or(sd, KS_CMDA,   0xfc, 0x03);
		else
			/* force 50 Hz */
			ks0127_and_or(sd, KS_CMDA,   0xfc, 0x02);

		ks0127_and_or(sd, KS_CMDA,   0xff, 0x40); /* VSE=1 */
		/* set input line and VALIGN */
		ks0127_and_or(sd, KS_CMDB,   0xb0, (input | 0x40));
		/* freerunning mode, */
		/* TSTGEN = 1 TSTGFR=11 TSTGPH=0 TSTGPK=0  VMEM=1*/
		ks0127_and_or(sd, KS_CMDC,   0x70, 0x87);
		/* digital input, SYNDIR = 0 INPSL=01 CLKDIR=0 EAV=0 */
		ks0127_and_or(sd, KS_CMDD,   0x03, 0x08);
		/* disable chroma demodulation */
		ks0127_and_or(sd, KS_CTRACK, 0xcf, 0x30);
		/* HYPK =01 CTRAP = 0 HYBWR=0 PED=1 RGBH=1 UNIT=1 */
		ks0127_and_or(sd, KS_LUMA,   0x00, 0x71);
		ks0127_and_or(sd, KS_VERTIC, 0x0f,
			       reg_defaults[KS_VERTIC]&0xf0);

		/* scaler fullbw, luma comb off */
		ks0127_and_or(sd, KS_VERTIA, 0x08, 0x81);

		ks0127_and_or(sd, KS_CHROMB, 0x0f,
			       reg_defaults[KS_CHROMB]&0xf0);

		ks0127_and_or(sd, KS_CON, 0x00, 0x00);
		ks0127_and_or(sd, KS_BRT, 0x00, 32);	/* spec: 34 */
			/* spec: 229 (e5) */
		ks0127_and_or(sd, KS_SAT, 0x00, 0xe8);
		ks0127_and_or(sd, KS_HUE, 0x00, 0);

		ks0127_and_or(sd, KS_UGAIN, 0x00, 238);
		ks0127_and_or(sd, KS_VGAIN, 0x00, 0x00);

		/*UOFF:0x30, VOFF:0x30, TSTCGN=1 */
		ks0127_and_or(sd, KS_UVOFFH, 0x00, 0x4f);
		ks0127_and_or(sd, KS_UVOFFL, 0x00, 0x00);
		break;

	default:
		v4l2_dbg(1, debug, sd,
			"s_routing: Unknown input %d\n", input);
		break;
	}

	/* hack: CDMLPF sometimes spontaneously switches on; */
	/* force back off */
	ks0127_write(sd, KS_DEMOD, reg_defaults[KS_DEMOD]);
	return 0;
}

static int ks0127_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct ks0127 *ks = to_ks0127(sd);

	/* Set to automatic SECAM/Fsc mode */
	ks0127_and_or(sd, KS_DEMOD, 0xf0, 0x00);

	ks->norm = std;
	if (std & V4L2_STD_NTSC) {
		v4l2_dbg(1, debug, sd,
			"s_std: NTSC_M\n");
		ks0127_and_or(sd, KS_CHROMA, 0x9f, 0x20);
	} else if (std & V4L2_STD_PAL_N) {
		v4l2_dbg(1, debug, sd,
			"s_std: NTSC_N (fixme)\n");
		ks0127_and_or(sd, KS_CHROMA, 0x9f, 0x40);
	} else if (std & V4L2_STD_PAL) {
		v4l2_dbg(1, debug, sd,
			"s_std: PAL_N\n");
		ks0127_and_or(sd, KS_CHROMA, 0x9f, 0x20);
	} else if (std & V4L2_STD_PAL_M) {
		v4l2_dbg(1, debug, sd,
			"s_std: PAL_M (fixme)\n");
		ks0127_and_or(sd, KS_CHROMA, 0x9f, 0x40);
	} else if (std & V4L2_STD_SECAM) {
		v4l2_dbg(1, debug, sd,
			"s_std: SECAM\n");

		/* set to secam autodetection */
		ks0127_and_or(sd, KS_CHROMA, 0xdf, 0x20);
		ks0127_and_or(sd, KS_DEMOD, 0xf0, 0x00);
		schedule_timeout_interruptible(HZ/10+1);

		/* did it autodetect? */
		if (!(ks0127_read(sd, KS_DEMOD) & 0x40))
			/* force to secam mode */
			ks0127_and_or(sd, KS_DEMOD, 0xf0, 0x0f);
	} else {
		v4l2_dbg(1, debug, sd, "s_std: Unknown norm %llx\n",
			       (unsigned long long)std);
	}
	return 0;
}

static int ks0127_s_stream(struct v4l2_subdev *sd, int enable)
{
	v4l2_dbg(1, debug, sd, "s_stream(%d)\n", enable);
	if (enable) {
		/* All output pins on */
		ks0127_and_or(sd, KS_OFMTA, 0xcf, 0x30);
		/* Obey the OEN pin */
		ks0127_and_or(sd, KS_CDEM, 0x7f, 0x00);
	} else {
		/* Video output pins off */
		ks0127_and_or(sd, KS_OFMTA, 0xcf, 0x00);
		/* Ignore the OEN pin */
		ks0127_and_or(sd, KS_CDEM, 0x7f, 0x80);
	}
	return 0;
}

static int ks0127_status(struct v4l2_subdev *sd, u32 *pstatus, v4l2_std_id *pstd)
{
	int stat = V4L2_IN_ST_NO_SIGNAL;
	u8 status;
	v4l2_std_id std = pstd ? *pstd : V4L2_STD_ALL;

	status = ks0127_read(sd, KS_STAT);
	if (!(status & 0x20))		 /* NOVID not set */
		stat = 0;
	if (!(status & 0x01)) {		      /* CLOCK set */
		stat |= V4L2_IN_ST_NO_COLOR;
		std = V4L2_STD_UNKNOWN;
	} else {
		if ((status & 0x08))		   /* PALDET set */
			std &= V4L2_STD_PAL;
		else
			std &= V4L2_STD_NTSC;
	}
	if ((status & 0x10))		   /* PALDET set */
		std &= V4L2_STD_525_60;
	else
		std &= V4L2_STD_625_50;
	if (pstd)
		*pstd = std;
	if (pstatus)
		*pstatus = stat;
	return 0;
}

static int ks0127_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	v4l2_dbg(1, debug, sd, "querystd\n");
	return ks0127_status(sd, NULL, std);
}

static int ks0127_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	v4l2_dbg(1, debug, sd, "g_input_status\n");
	return ks0127_status(sd, status, NULL);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_video_ops ks0127_video_ops = {
	.s_std = ks0127_s_std,
	.s_routing = ks0127_s_routing,
	.s_stream = ks0127_s_stream,
	.querystd = ks0127_querystd,
	.g_input_status = ks0127_g_input_status,
};

static const struct v4l2_subdev_ops ks0127_ops = {
	.video = &ks0127_video_ops,
};

/* ----------------------------------------------------------------------- */


static int ks0127_probe(struct i2c_client *client)
{
	struct ks0127 *ks;
	struct v4l2_subdev *sd;

	v4l_info(client, "%s chip found @ 0x%x (%s)\n",
		client->addr == (I2C_KS0127_ADDON >> 1) ? "addon" : "on-board",
		client->addr << 1, client->adapter->name);

	ks = devm_kzalloc(&client->dev, sizeof(*ks), GFP_KERNEL);
	if (ks == NULL)
		return -ENOMEM;
	sd = &ks->sd;
	v4l2_i2c_subdev_init(sd, client, &ks0127_ops);

	/* power up */
	init_reg_defaults();
	ks0127_write(sd, KS_CMDA, 0x2c);
	mdelay(10);

	/* reset the device */
	ks0127_init(sd);
	return 0;
}

static void ks0127_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	ks0127_write(sd, KS_OFMTA, 0x20); /* tristate */
	ks0127_write(sd, KS_CMDA, 0x2c | 0x80); /* power down */
}

static const struct i2c_device_id ks0127_id[] = {
	{ "ks0127", 0 },
	{ "ks0127b", 0 },
	{ "ks0122s", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ks0127_id);

static struct i2c_driver ks0127_driver = {
	.driver = {
		.name	= "ks0127",
	},
	.probe		= ks0127_probe,
	.remove		= ks0127_remove,
	.id_table	= ks0127_id,
};

module_i2c_driver(ks0127_driver);
