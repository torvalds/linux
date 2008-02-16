/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>

#include "wis-i2c.h"

/* #define MPX_DEBUG */

/* AS(IF/MPX) pin:      LOW      HIGH/OPEN
 * IF/MPX address:   0x42/0x40   0x43/0x44
 */
#define IF_I2C_ADDR	0x43
#define MPX_I2C_ADDR	0x44

static v4l2_std_id force_band;
static char force_band_str[] = "-";
module_param_string(force_band, force_band_str, sizeof(force_band_str), 0644);
static int force_mpx_mode = -1;
module_param(force_mpx_mode, int, 0644);

/* Store tuner info in the same format as tuner.c, so maybe we can put the
 * Sony tuner support in there. */
struct sony_tunertype {
	char *name;
	unsigned char Vendor; /* unused here */
	unsigned char Type; /* unused here */

	unsigned short thresh1; /*  band switch VHF_LO <=> VHF_HI */
	unsigned short thresh2; /*  band switch VHF_HI <=> UHF */
	unsigned char VHF_L;
	unsigned char VHF_H;
	unsigned char UHF;
	unsigned char config;
	unsigned short IFPCoff;
};

/* This array is indexed by (tuner_type - 200) */
static struct sony_tunertype sony_tuners[] = {
	{ "Sony PAL+SECAM (BTF-PG472Z)", 0, 0,
	  16*144.25, 16*427.25, 0x01, 0x02, 0x04, 0xc6, 623},
	{ "Sony NTSC_JP (BTF-PK467Z)", 0, 0,
	  16*220.25, 16*467.25, 0x01, 0x02, 0x04, 0xc6, 940},
	{ "Sony NTSC (BTF-PB463Z)", 0, 0,
	  16*130.25, 16*364.25, 0x01, 0x02, 0x04, 0xc6, 732},
};

struct wis_sony_tuner {
	int type;
	v4l2_std_id std;
	unsigned int freq;
	int mpxmode;
	u32 audmode;
};

/* Basically the same as default_set_tv_freq() in tuner.c */
static int set_freq(struct i2c_client *client, int freq)
{
	struct wis_sony_tuner *t = i2c_get_clientdata(client);
	char *band_name;
	int n;
	int band_select;
	struct sony_tunertype *tun;
	u8 buffer[4];

	tun = &sony_tuners[t->type - 200];
	if (freq < tun->thresh1) {
		band_name = "VHF_L";
		band_select = tun->VHF_L;
	} else if (freq < tun->thresh2) {
		band_name = "VHF_H";
		band_select = tun->VHF_H;
	} else {
		band_name = "UHF";
		band_select = tun->UHF;
	}
	printk(KERN_DEBUG "wis-sony-tuner: tuning to frequency %d.%04d (%s)\n",
			freq / 16, (freq % 16) * 625, band_name);
	n = freq + tun->IFPCoff;

	buffer[0] = n >> 8;
	buffer[1] = n & 0xff;
	buffer[2] = tun->config;
	buffer[3] = band_select;
	i2c_master_send(client, buffer, 4);

	return 0;
}

static int mpx_write(struct i2c_client *client, int dev, int addr, int val)
{
	u8 buffer[5];
	struct i2c_msg msg;

	buffer[0] = dev;
	buffer[1] = addr >> 8;
	buffer[2] = addr & 0xff;
	buffer[3] = val >> 8;
	buffer[4] = val & 0xff;
	msg.addr = MPX_I2C_ADDR;
	msg.flags = 0;
	msg.len = 5;
	msg.buf = buffer;
	i2c_transfer(client->adapter, &msg, 1);
	return 0;
}

/*
 * MPX register values for the BTF-PG472Z:
 *
 *                                 FM_     NICAM_  SCART_
 *          MODUS  SOURCE    ACB   PRESCAL PRESCAL PRESCAL SYSTEM  VOLUME
 *         10/0030 12/0008 12/0013 12/000E 12/0010 12/0000 10/0020 12/0000
 *         ---------------------------------------------------------------
 * Auto     1003    0020    0100    2603    5000    XXXX    0001    7500
 *
 * B/G
 *  Mono    1003    0020    0100    2603    5000    XXXX    0003    7500
 *  A2      1003    0020    0100    2601    5000    XXXX    0003    7500
 *  NICAM   1003    0120    0100    2603    5000    XXXX    0008    7500
 *
 * I
 *  Mono    1003    0020    0100    2603    7900    XXXX    000A    7500
 *  NICAM   1003    0120    0100    2603    7900    XXXX    000A    7500
 *
 * D/K
 *  Mono    1003    0020    0100    2603    5000    XXXX    0004    7500
 *  A2-1    1003    0020    0100    2601    5000    XXXX    0004    7500
 *  A2-2    1003    0020    0100    2601    5000    XXXX    0005    7500
 *  A2-3    1003    0020    0100    2601    5000    XXXX    0007    7500
 *  NICAM   1003    0120    0100    2603    5000    XXXX    000B    7500
 *
 * L/L'
 *  Mono    0003    0200    0100    7C03    5000    2200    0009    7500
 *  NICAM   0003    0120    0100    7C03    5000    XXXX    0009    7500
 *
 * M
 *  Mono    1003    0200    0100    2B03    5000    2B00    0002    7500
 *
 * For Asia, replace the 0x26XX in FM_PRESCALE with 0x14XX.
 *
 * Bilingual selection in A2/NICAM:
 *
 *         High byte of SOURCE     Left chan   Right chan
 *                 0x01              MAIN         SUB
 *                 0x03              MAIN         MAIN
 *                 0x04              SUB          SUB
 *
 * Force mono in NICAM by setting the high byte of SOURCE to 0x02 (L/L') or
 * 0x00 (all other bands).  Force mono in A2 with FMONO_A2:
 *
 *                      FMONO_A2
 *                      10/0022
 *                      --------
 *     Forced mono ON     07F0
 *     Forced mono OFF    0190
 */

static struct {
	enum { AUD_MONO, AUD_A2, AUD_NICAM, AUD_NICAM_L } audio_mode;
	u16 modus;
	u16 source;
	u16 acb;
	u16 fm_prescale;
	u16 nicam_prescale;
	u16 scart_prescale;
	u16 system;
	u16 volume;
} mpx_audio_modes[] = {
	/* Auto */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0001, 0x7500 },
	/* B/G Mono */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0003, 0x7500 },
	/* B/G A2 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0003, 0x7500 },
	/* B/G NICAM */ { AUD_NICAM,	0x1003, 0x0120, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0008, 0x7500 },
	/* I Mono */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x7900, 0x0000, 0x000A, 0x7500 },
	/* I NICAM */	{ AUD_NICAM,	0x1003, 0x0120, 0x0100, 0x2603,
					0x7900, 0x0000, 0x000A, 0x7500 },
	/* D/K Mono */	{ AUD_MONO,	0x1003, 0x0020, 0x0100, 0x2603,
					0x5000, 0x0000, 0x0004, 0x7500 },
	/* D/K A2-1 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0004, 0x7500 },
	/* D/K A2-2 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0005, 0x7500 },
	/* D/K A2-3 */	{ AUD_A2,	0x1003, 0x0020, 0x0100, 0x2601,
					0x5000, 0x0000, 0x0007, 0x7500 },
	/* D/K NICAM */	{ AUD_NICAM,	0x1003, 0x0120, 0x0100, 0x2603,
					0x5000, 0x0000, 0x000B, 0x7500 },
	/* L/L' Mono */	{ AUD_MONO,	0x0003, 0x0200, 0x0100, 0x7C03,
					0x5000, 0x2200, 0x0009, 0x7500 },
	/* L/L' NICAM */{ AUD_NICAM_L,	0x0003, 0x0120, 0x0100, 0x7C03,
					0x5000, 0x0000, 0x0009, 0x7500 },
};

#define MPX_NUM_MODES	ARRAY_SIZE(mpx_audio_modes)

static int mpx_setup(struct i2c_client *client)
{
	struct wis_sony_tuner *t = i2c_get_clientdata(client);
	u16 source = 0;
	u8 buffer[3];
	struct i2c_msg msg;

	/* reset MPX */
	buffer[0] = 0x00;
	buffer[1] = 0x80;
	buffer[2] = 0x00;
	msg.addr = MPX_I2C_ADDR;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = buffer;
	i2c_transfer(client->adapter, &msg, 1);
	buffer[1] = 0x00;
	i2c_transfer(client->adapter, &msg, 1);

	if (mpx_audio_modes[t->mpxmode].audio_mode != AUD_MONO) {
		switch (t->audmode) {
		case V4L2_TUNER_MODE_MONO:
			switch (mpx_audio_modes[t->mpxmode].audio_mode) {
			case AUD_A2:
				source = mpx_audio_modes[t->mpxmode].source;
				break;
			case AUD_NICAM:
				source = 0x0000;
				break;
			case AUD_NICAM_L:
				source = 0x0200;
				break;
			default:
				break;
			}
			break;
		case V4L2_TUNER_MODE_STEREO:
			source = mpx_audio_modes[t->mpxmode].source;
			break;
		case V4L2_TUNER_MODE_LANG1:
			source = 0x0300;
			break;
		case V4L2_TUNER_MODE_LANG2:
			source = 0x0400;
			break;
		}
		source |= mpx_audio_modes[t->mpxmode].source & 0x00ff;
	} else
		source = mpx_audio_modes[t->mpxmode].source;

	mpx_write(client, 0x10, 0x0030, mpx_audio_modes[t->mpxmode].modus);
	mpx_write(client, 0x12, 0x0008, source);
	mpx_write(client, 0x12, 0x0013, mpx_audio_modes[t->mpxmode].acb);
	mpx_write(client, 0x12, 0x000e,
			mpx_audio_modes[t->mpxmode].fm_prescale);
	mpx_write(client, 0x12, 0x0010,
			mpx_audio_modes[t->mpxmode].nicam_prescale);
	mpx_write(client, 0x12, 0x000d,
			mpx_audio_modes[t->mpxmode].scart_prescale);
	mpx_write(client, 0x10, 0x0020, mpx_audio_modes[t->mpxmode].system);
	mpx_write(client, 0x12, 0x0000, mpx_audio_modes[t->mpxmode].volume);
	if (mpx_audio_modes[t->mpxmode].audio_mode == AUD_A2)
		mpx_write(client, 0x10, 0x0022,
			t->audmode == V4L2_TUNER_MODE_MONO ?  0x07f0 : 0x0190);

#ifdef MPX_DEBUG
	{
		u8 buf1[3], buf2[2];
		struct i2c_msg msgs[2];

		printk(KERN_DEBUG "wis-sony-tuner: MPX registers: %04x %04x "
				"%04x %04x %04x %04x %04x %04x\n",
				mpx_audio_modes[t->mpxmode].modus,
				source,
				mpx_audio_modes[t->mpxmode].acb,
				mpx_audio_modes[t->mpxmode].fm_prescale,
				mpx_audio_modes[t->mpxmode].nicam_prescale,
				mpx_audio_modes[t->mpxmode].scart_prescale,
				mpx_audio_modes[t->mpxmode].system,
				mpx_audio_modes[t->mpxmode].volume);
		buf1[0] = 0x11;
		buf1[1] = 0x00;
		buf1[2] = 0x7e;
		msgs[0].addr = MPX_I2C_ADDR;
		msgs[0].flags = 0;
		msgs[0].len = 3;
		msgs[0].buf = buf1;
		msgs[1].addr = MPX_I2C_ADDR;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = 2;
		msgs[1].buf = buf2;
		i2c_transfer(client->adapter, msgs, 2);
		printk(KERN_DEBUG "wis-sony-tuner: MPX system: %02x%02x\n",
				buf2[0], buf2[1]);
		buf1[0] = 0x11;
		buf1[1] = 0x02;
		buf1[2] = 0x00;
		i2c_transfer(client->adapter, msgs, 2);
		printk(KERN_DEBUG "wis-sony-tuner: MPX status: %02x%02x\n",
				buf2[0], buf2[1]);
	}
#endif
	return 0;
}

/*
 * IF configuration values for the BTF-PG472Z:
 *
 *	B/G: 0x94 0x70 0x49
 *	I:   0x14 0x70 0x4a
 *	D/K: 0x14 0x70 0x4b
 *	L:   0x04 0x70 0x4b
 *	L':  0x44 0x70 0x53
 *	M:   0x50 0x30 0x4c
 */

static int set_if(struct i2c_client *client)
{
	struct wis_sony_tuner *t = i2c_get_clientdata(client);
	u8 buffer[4];
	struct i2c_msg msg;
	int default_mpx_mode = 0;

	/* configure IF */
	buffer[0] = 0;
	if (t->std & V4L2_STD_PAL_BG) {
		buffer[1] = 0x94;
		buffer[2] = 0x70;
		buffer[3] = 0x49;
		default_mpx_mode = 1;
	} else if (t->std & V4L2_STD_PAL_I) {
		buffer[1] = 0x14;
		buffer[2] = 0x70;
		buffer[3] = 0x4a;
		default_mpx_mode = 4;
	} else if (t->std & V4L2_STD_PAL_DK) {
		buffer[1] = 0x14;
		buffer[2] = 0x70;
		buffer[3] = 0x4b;
		default_mpx_mode = 6;
	} else if (t->std & V4L2_STD_SECAM_L) {
		buffer[1] = 0x04;
		buffer[2] = 0x70;
		buffer[3] = 0x4b;
		default_mpx_mode = 11;
	}
	msg.addr = IF_I2C_ADDR;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = buffer;
	i2c_transfer(client->adapter, &msg, 1);

	/* Select MPX mode if not forced by the user */
	if (force_mpx_mode >= 0 || force_mpx_mode < MPX_NUM_MODES)
		t->mpxmode = force_mpx_mode;
	else
		t->mpxmode = default_mpx_mode;
	printk(KERN_DEBUG "wis-sony-tuner: setting MPX to mode %d\n",
			t->mpxmode);
	mpx_setup(client);

	return 0;
}

static int tuner_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct wis_sony_tuner *t = i2c_get_clientdata(client);

	switch (cmd) {
#ifdef TUNER_SET_TYPE_ADDR
	case TUNER_SET_TYPE_ADDR:
	{
		struct tuner_setup *tun_setup = arg;
		int *type = &tun_setup->type;
#else
	case TUNER_SET_TYPE:
	{
		int *type = arg;
#endif

		if (t->type >= 0) {
			if (t->type != *type)
				printk(KERN_ERR "wis-sony-tuner: type already "
					"set to %d, ignoring request for %d\n",
					t->type, *type);
			break;
		}
		t->type = *type;
		switch (t->type) {
		case TUNER_SONY_BTF_PG472Z:
			switch (force_band_str[0]) {
			case 'b':
			case 'B':
			case 'g':
			case 'G':
				printk(KERN_INFO "wis-sony-tuner: forcing "
						"tuner to PAL-B/G bands\n");
				force_band = V4L2_STD_PAL_BG;
				break;
			case 'i':
			case 'I':
				printk(KERN_INFO "wis-sony-tuner: forcing "
						"tuner to PAL-I band\n");
				force_band = V4L2_STD_PAL_I;
				break;
			case 'd':
			case 'D':
			case 'k':
			case 'K':
				printk(KERN_INFO "wis-sony-tuner: forcing "
						"tuner to PAL-D/K bands\n");
				force_band = V4L2_STD_PAL_I;
				break;
			case 'l':
			case 'L':
				printk(KERN_INFO "wis-sony-tuner: forcing "
						"tuner to SECAM-L band\n");
				force_band = V4L2_STD_SECAM_L;
				break;
			default:
				force_band = 0;
				break;
			}
			if (force_band)
				t->std = force_band;
			else
				t->std = V4L2_STD_PAL_BG;
			set_if(client);
			break;
		case TUNER_SONY_BTF_PK467Z:
			t->std = V4L2_STD_NTSC_M_JP;
			break;
		case TUNER_SONY_BTF_PB463Z:
			t->std = V4L2_STD_NTSC_M;
			break;
		default:
			printk(KERN_ERR "wis-sony-tuner: tuner type %d is not "
					"supported by this module\n", *type);
			break;
		}
		if (type >= 0)
			printk(KERN_INFO
				"wis-sony-tuner: type set to %d (%s)\n",
				t->type, sony_tuners[t->type - 200].name);
		break;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		f->frequency = t->freq;
		break;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		t->freq = f->frequency;
		set_freq(client, t->freq);
		break;
	}
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *std = arg;

		switch (t->type) {
		case TUNER_SONY_BTF_PG472Z:
			switch (std->index) {
			case 0:
				v4l2_video_std_construct(std,
						V4L2_STD_PAL_BG, "PAL-B/G");
				break;
			case 1:
				v4l2_video_std_construct(std,
						V4L2_STD_PAL_I, "PAL-I");
				break;
			case 2:
				v4l2_video_std_construct(std,
						V4L2_STD_PAL_DK, "PAL-D/K");
				break;
			case 3:
				v4l2_video_std_construct(std,
						V4L2_STD_SECAM_L, "SECAM-L");
				break;
			default:
				std->id = 0; /* hack to indicate EINVAL */
				break;
			}
			break;
		case TUNER_SONY_BTF_PK467Z:
			if (std->index != 0) {
				std->id = 0; /* hack to indicate EINVAL */
				break;
			}
			v4l2_video_std_construct(std,
					V4L2_STD_NTSC_M_JP, "NTSC-J");
			break;
		case TUNER_SONY_BTF_PB463Z:
			if (std->index != 0) {
				std->id = 0; /* hack to indicate EINVAL */
				break;
			}
			v4l2_video_std_construct(std, V4L2_STD_NTSC_M, "NTSC");
			break;
		}
		break;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *std = arg;

		*std = t->std;
		break;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *std = arg;
		v4l2_std_id old = t->std;

		switch (t->type) {
		case TUNER_SONY_BTF_PG472Z:
			if (force_band && (*std & force_band) != *std &&
					*std != V4L2_STD_PAL &&
					*std != V4L2_STD_SECAM) {
				printk(KERN_DEBUG "wis-sony-tuner: ignoring "
						"requested TV standard in "
						"favor of force_band value\n");
				t->std = force_band;
			} else if (*std & V4L2_STD_PAL_BG) { /* default */
				t->std = V4L2_STD_PAL_BG;
			} else if (*std & V4L2_STD_PAL_I) {
				t->std = V4L2_STD_PAL_I;
			} else if (*std & V4L2_STD_PAL_DK) {
				t->std = V4L2_STD_PAL_DK;
			} else if (*std & V4L2_STD_SECAM_L) {
				t->std = V4L2_STD_SECAM_L;
			} else {
				printk(KERN_ERR "wis-sony-tuner: TV standard "
						"not supported\n");
				*std = 0; /* hack to indicate EINVAL */
				break;
			}
			if (old != t->std)
				set_if(client);
			break;
		case TUNER_SONY_BTF_PK467Z:
			if (!(*std & V4L2_STD_NTSC_M_JP)) {
				printk(KERN_ERR "wis-sony-tuner: TV standard "
						"not supported\n");
				*std = 0; /* hack to indicate EINVAL */
			}
			break;
		case TUNER_SONY_BTF_PB463Z:
			if (!(*std & V4L2_STD_NTSC_M)) {
				printk(KERN_ERR "wis-sony-tuner: TV standard "
						"not supported\n");
				*std = 0; /* hack to indicate EINVAL */
			}
			break;
		}
		break;
	}
	case VIDIOC_QUERYSTD:
	{
		v4l2_std_id *std = arg;

		switch (t->type) {
		case TUNER_SONY_BTF_PG472Z:
			if (force_band)
				*std = force_band;
			else
				*std = V4L2_STD_PAL_BG | V4L2_STD_PAL_I |
					V4L2_STD_PAL_DK | V4L2_STD_SECAM_L;
			break;
		case TUNER_SONY_BTF_PK467Z:
			*std = V4L2_STD_NTSC_M_JP;
			break;
		case TUNER_SONY_BTF_PB463Z:
			*std = V4L2_STD_NTSC_M;
			break;
		}
		break;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *tun = arg;

		memset(t, 0, sizeof(*tun));
		strcpy(tun->name, "Television");
		tun->type = V4L2_TUNER_ANALOG_TV;
		tun->rangelow = 0UL; /* does anything use these? */
		tun->rangehigh = 0xffffffffUL;
		switch (t->type) {
		case TUNER_SONY_BTF_PG472Z:
			tun->capability = V4L2_TUNER_CAP_NORM |
				V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 |
				V4L2_TUNER_CAP_LANG2;
			tun->rxsubchans = V4L2_TUNER_SUB_MONO |
				V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_LANG1 |
				V4L2_TUNER_SUB_LANG2;
			break;
		case TUNER_SONY_BTF_PK467Z:
		case TUNER_SONY_BTF_PB463Z:
			tun->capability = V4L2_TUNER_CAP_STEREO;
			tun->rxsubchans = V4L2_TUNER_SUB_MONO |
						V4L2_TUNER_SUB_STEREO;
			break;
		}
		tun->audmode = t->audmode;
		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *tun = arg;

		switch (t->type) {
		case TUNER_SONY_BTF_PG472Z:
			if (tun->audmode != t->audmode) {
				t->audmode = tun->audmode;
				mpx_setup(client);
			}
			break;
		case TUNER_SONY_BTF_PK467Z:
		case TUNER_SONY_BTF_PB463Z:
			break;
		}
		return 0;
	}
	default:
		break;
	}
	return 0;
}

static struct i2c_driver wis_sony_tuner_driver;

static struct i2c_client wis_sony_tuner_client_templ = {
	.name		= "Sony TV Tuner (WIS)",
	.driver		= &wis_sony_tuner_driver,
};

static int wis_sony_tuner_detect(struct i2c_adapter *adapter,
					int addr, int kind)
{
	struct i2c_client *client;
	struct wis_sony_tuner *t;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;
	memcpy(client, &wis_sony_tuner_client_templ,
			sizeof(wis_sony_tuner_client_templ));
	client->adapter = adapter;
	client->addr = addr;

	t = kmalloc(sizeof(struct wis_sony_tuner), GFP_KERNEL);
	if (t == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	t->type = -1;
	t->freq = 0;
	t->mpxmode = 0;
	t->audmode = V4L2_TUNER_MODE_STEREO;
	i2c_set_clientdata(client, t);

	printk(KERN_DEBUG
		"wis-sony-tuner: initializing tuner at address %d on %s\n",
		addr, adapter->name);

	i2c_attach_client(client);

	return 0;
}

static int wis_sony_tuner_detach(struct i2c_client *client)
{
	struct wis_sony_tuner *t = i2c_get_clientdata(client);
	int r;

	r = i2c_detach_client(client);
	if (r < 0)
		return r;

	kfree(t);
	kfree(client);
	return 0;
}

static struct i2c_driver wis_sony_tuner_driver = {
	.driver = {
		.name	= "WIS Sony TV Tuner I2C driver",
	},
	.id		= I2C_DRIVERID_WIS_SONY_TUNER,
	.detach_client	= wis_sony_tuner_detach,
	.command	= tuner_command,
};

static int __init wis_sony_tuner_init(void)
{
	int r;

	r = i2c_add_driver(&wis_sony_tuner_driver);
	if (r < 0)
		return r;
	return wis_i2c_add_driver(wis_sony_tuner_driver.id,
					wis_sony_tuner_detect);
}

static void __exit wis_sony_tuner_cleanup(void)
{
	wis_i2c_del_driver(wis_sony_tuner_detect);
	i2c_del_driver(&wis_sony_tuner_driver);
}

module_init(wis_sony_tuner_init);
module_exit(wis_sony_tuner_cleanup);

MODULE_LICENSE("GPL v2");
