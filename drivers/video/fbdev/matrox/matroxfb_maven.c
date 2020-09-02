// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.65 2002/08/14
 *
 * See matroxfb_base.c for contributors.
 *
 */

#include "matroxfb_maven.h"
#include "matroxfb_misc.h"
#include "matroxfb_DAC1064.h"
#include <linux/i2c.h>
#include <linux/matroxfb.h>
#include <linux/slab.h>
#include <asm/div64.h>

#define MGATVO_B	1
#define MGATVO_C	2

static const struct maven_gamma {
  unsigned char reg83;
  unsigned char reg84;
  unsigned char reg85;
  unsigned char reg86;
  unsigned char reg87;
  unsigned char reg88;
  unsigned char reg89;
  unsigned char reg8a;
  unsigned char reg8b;
} maven_gamma[] = {
  { 131, 57, 223, 15, 117, 212, 251, 91, 156},
  { 133, 61, 128, 63, 180, 147, 195, 100, 180},
  { 131, 19, 63, 31, 50, 66, 171, 64, 176},
  { 0, 0, 0, 31, 16, 16, 16, 100, 200},
  { 8, 23, 47, 73, 147, 244, 220, 80, 195},
  { 22, 43, 64, 80, 147, 115, 58, 85, 168},
  { 34, 60, 80, 214, 147, 212, 188, 85, 167},
  { 45, 77, 96, 216, 147, 99, 91, 85, 159},
  { 56, 76, 112, 107, 147, 212, 148, 64, 144},
  { 65, 91, 128, 137, 147, 196, 17, 69, 148},
  { 72, 104, 136, 138, 147, 180, 245, 73, 147},
  { 87, 116, 143, 126, 16, 83, 229, 77, 144},
  { 95, 119, 152, 254, 244, 83, 221, 77, 151},
  { 100, 129, 159, 156, 244, 148, 197, 77, 160},
  { 105, 141, 167, 247, 244, 132, 181, 84, 166},
  { 105, 147, 168, 247, 244, 245, 181, 90, 170},
  { 120, 153, 175, 248, 212, 229, 165, 90, 180},
  { 119, 156, 176, 248, 244, 229, 84, 74, 160},
  { 119, 158, 183, 248, 244, 229, 149, 78, 165}
};

/* Definition of the various controls */
struct mctl {
	struct v4l2_queryctrl desc;
	size_t control;
};

#define BLMIN	0x0FF
#define WLMAX	0x3FF

static const struct mctl maven_controls[] =
{	{ { V4L2_CID_BRIGHTNESS, V4L2_CTRL_TYPE_INTEGER,
	  "brightness",
	  0, WLMAX - BLMIN, 1, 379 - BLMIN, 
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.brightness) },
	{ { V4L2_CID_CONTRAST, V4L2_CTRL_TYPE_INTEGER,
	  "contrast",
	  0, 1023, 1, 127,
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.contrast) },
	{ { V4L2_CID_SATURATION, V4L2_CTRL_TYPE_INTEGER,
	  "saturation",
	  0, 255, 1, 155,
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.saturation) },
	{ { V4L2_CID_HUE, V4L2_CTRL_TYPE_INTEGER,
	  "hue",
	  0, 255, 1, 0,
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.hue) },
	{ { V4L2_CID_GAMMA, V4L2_CTRL_TYPE_INTEGER,
	  "gamma",
	  0, ARRAY_SIZE(maven_gamma) - 1, 1, 3,
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.gamma) },
	{ { MATROXFB_CID_TESTOUT, V4L2_CTRL_TYPE_BOOLEAN,
	  "test output",
	  0, 1, 1, 0,
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.testout) },
	{ { MATROXFB_CID_DEFLICKER, V4L2_CTRL_TYPE_INTEGER,
	  "deflicker mode",
	  0, 2, 1, 0,
	  0,
	}, offsetof(struct matrox_fb_info, altout.tvo_params.deflicker) },

};

#define MAVCTRLS ARRAY_SIZE(maven_controls)

/* Return: positive number: id found
           -EINVAL:         id not found, return failure
	   -ENOENT:         id not found, create fake disabled control */
static int get_ctrl_id(__u32 v4l2_id) {
	int i;

	for (i = 0; i < MAVCTRLS; i++) {
		if (v4l2_id < maven_controls[i].desc.id) {
			if (maven_controls[i].desc.id == 0x08000000) {
				return -EINVAL;
			}
			return -ENOENT;
		}
		if (v4l2_id == maven_controls[i].desc.id) {
			return i;
		}
	}
	return -EINVAL;
}

struct maven_data {
	struct matrox_fb_info*		primary_head;
	struct i2c_client		*client;
	int				version;
};

static int* get_ctrl_ptr(struct maven_data* md, int idx) {
	return (int*)((char*)(md->primary_head) + maven_controls[idx].control);
}

static int maven_get_reg(struct i2c_client* c, char reg) {
	char dst;
	struct i2c_msg msgs[] = {
		{
			.addr = c->addr,
			.flags = I2C_M_REV_DIR_ADDR,
			.len = sizeof(reg),
			.buf = &reg
		},
		{
			.addr = c->addr,
			.flags = I2C_M_RD | I2C_M_NOSTART,
			.len = sizeof(dst),
			.buf = &dst
		}
	};
	s32 err;

	err = i2c_transfer(c->adapter, msgs, 2);
	if (err < 0)
		printk(KERN_INFO "ReadReg(%d) failed\n", reg);
	return dst & 0xFF;
}

static int maven_set_reg(struct i2c_client* c, int reg, int val) {
	s32 err;

	err = i2c_smbus_write_byte_data(c, reg, val);
	if (err)
		printk(KERN_INFO "WriteReg(%d) failed\n", reg);
	return err;
}

static int maven_set_reg_pair(struct i2c_client* c, int reg, int val) {
	s32 err;

	err = i2c_smbus_write_word_data(c, reg, val);
	if (err)
		printk(KERN_INFO "WriteRegPair(%d) failed\n", reg);
	return err;
}

static const struct matrox_pll_features maven_pll = {
	50000,
	27000,
	4, 127,
	2, 31,
	3
};

struct matrox_pll_features2 {
	unsigned int	vco_freq_min;
	unsigned int	vco_freq_max;
	unsigned int	feed_div_min;
	unsigned int	feed_div_max;
	unsigned int	in_div_min;
	unsigned int	in_div_max;
	unsigned int	post_shift_max;
};

struct matrox_pll_ctl {
	unsigned int	ref_freq;
	unsigned int	den;
};

static const struct matrox_pll_features2 maven1000_pll = {
	.vco_freq_min = 50000000,
	.vco_freq_max = 300000000,
	.feed_div_min = 5,
	.feed_div_max = 128,
	.in_div_min = 3,
	.in_div_max = 32,
	.post_shift_max = 3
};

static const struct matrox_pll_ctl maven_PAL = {
	.ref_freq = 540000,
	.den = 50
};

static const struct matrox_pll_ctl maven_NTSC = {
	.ref_freq = 450450,	/* 27027000/60 == 27000000/59.94005994 */
	.den = 60
};

static int matroxfb_PLL_mavenclock(const struct matrox_pll_features2* pll,
		const struct matrox_pll_ctl* ctl,
		unsigned int htotal, unsigned int vtotal,
		unsigned int* in, unsigned int* feed, unsigned int* post,
		unsigned int* h2) {
	unsigned int besth2 = 0;
	unsigned int fxtal = ctl->ref_freq;
	unsigned int fmin = pll->vco_freq_min / ctl->den;
	unsigned int fwant;
	unsigned int p;
	unsigned int scrlen;
	unsigned int fmax;

	DBG(__func__)

	scrlen = htotal * (vtotal - 1);
	fwant = htotal * vtotal;
	fmax = pll->vco_freq_max / ctl->den;

	dprintk(KERN_DEBUG "want: %u, xtal: %u, h: %u, v: %u, fmax: %u\n",
		fwant, fxtal, htotal, vtotal, fmax);
	for (p = 1; p <= pll->post_shift_max; p++) {
		if (fwant * 2 > fmax)
			break;
		fwant *= 2;
	}
	if (fwant > fmax)
		return 0;
	for (; p-- > 0; fwant >>= 1) {
		unsigned int m;

		if (fwant < fmin) break;
		for (m = pll->in_div_min; m <= pll->in_div_max; m++) {
			unsigned int n;
			unsigned int dvd;
			unsigned int ln;

			n = (fwant * m) / fxtal;
			if (n < pll->feed_div_min)
				continue;
			if (n > pll->feed_div_max)
				break;

			ln = fxtal * n;
			dvd = m << p;

			if (ln % dvd)
				continue;
			ln = ln / dvd;

			if (ln < scrlen + 2)
				continue;
			ln = ln - scrlen;
			if (ln > htotal)
				continue;
			dprintk(KERN_DEBUG "Match: %u / %u / %u / %u\n", n, m, p, ln);
			if (ln > besth2) {
				dprintk(KERN_DEBUG "Better...\n");
				*h2 = besth2 = ln;
				*post = p;
				*in = m;
				*feed = n;
			}
		}
	}

	/* if h2/post/in/feed have not been assigned, return zero (error) */
	if (besth2 < 2)
		return 0;

	dprintk(KERN_ERR "clk: %02X %02X %02X %d %d\n", *in, *feed, *post, fxtal, fwant);
	return fxtal * (*feed) / (*in) * ctl->den;
}

static int matroxfb_mavenclock(const struct matrox_pll_ctl *ctl,
		unsigned int htotal, unsigned int vtotal,
		unsigned int* in, unsigned int* feed, unsigned int* post,
		unsigned int* htotal2) {
	unsigned int fvco;
	unsigned int p;

	fvco = matroxfb_PLL_mavenclock(&maven1000_pll, ctl, htotal, vtotal, in, feed, &p, htotal2);
	if (!fvco)
		return -EINVAL;
	p = (1 << p) - 1;
	if (fvco <= 100000000)
		;
	else if (fvco <= 140000000)
		p |= 0x08;
	else if (fvco <= 180000000)
		p |= 0x10;
	else
		p |= 0x18;
	*post = p;
	return 0;
}

static void DAC1064_calcclock(unsigned int freq, unsigned int fmax,
		unsigned int* in, unsigned int* feed, unsigned int* post) {
	unsigned int fvco;
	unsigned int p;

	fvco = matroxfb_PLL_calcclock(&maven_pll, freq, fmax, in, feed, &p);
	p = (1 << p) - 1;
	if (fvco <= 100000)
		;
	else if (fvco <= 140000)
		p |= 0x08;
	else if (fvco <= 180000)
		p |= 0x10;
	else
		p |= 0x18;
	*post = p;
	return;
}

static unsigned char maven_compute_deflicker (const struct maven_data* md) {
	unsigned char df;
	
	df = (md->version == MGATVO_B?0x40:0x00);
	switch (md->primary_head->altout.tvo_params.deflicker) {
		case 0:
/*			df |= 0x00; */
			break;
		case 1:
			df |= 0xB1;
			break;
		case 2:
			df |= 0xA2;
			break;
	}
	return df;
}

static void maven_compute_bwlevel (const struct maven_data* md,
				   int *bl, int *wl) {
	const int b = md->primary_head->altout.tvo_params.brightness + BLMIN;
	const int c = md->primary_head->altout.tvo_params.contrast;

	*bl = max(b - c, BLMIN);
	*wl = min(b + c, WLMAX);
}

static const struct maven_gamma* maven_compute_gamma (const struct maven_data* md) {
 	return maven_gamma + md->primary_head->altout.tvo_params.gamma;
}


static void maven_init_TVdata(const struct maven_data* md, struct mavenregs* data) {
	static struct mavenregs palregs = { {
		0x2A, 0x09, 0x8A, 0xCB,	/* 00: chroma subcarrier */
		0x00,
		0x00,	/* ? not written */
		0x00,	/* modified by code (F9 written...) */
		0x00,	/* ? not written */
		0x7E,	/* 08 */
		0x44,	/* 09 */
		0x9C,	/* 0A */
		0x2E,	/* 0B */
		0x21,	/* 0C */
		0x00,	/* ? not written */
		0x3F, 0x03, /* 0E-0F */
		0x3F, 0x03, /* 10-11 */
		0x1A,	/* 12 */
		0x2A,	/* 13 */
		0x1C, 0x3D, 0x14, /* 14-16 */
		0x9C, 0x01, /* 17-18 */
		0x00,	/* 19 */
		0xFE,	/* 1A */
		0x7E,	/* 1B */
		0x60,	/* 1C */
		0x05,	/* 1D */
		0x89, 0x03, /* 1E-1F */
		0x72,	/* 20 */
		0x07,	/* 21 */
		0x72,	/* 22 */
		0x00,	/* 23 */
		0x00,	/* 24 */
		0x00,	/* 25 */
		0x08,	/* 26 */
		0x04,	/* 27 */
		0x00,	/* 28 */
		0x1A,	/* 29 */
		0x55, 0x01, /* 2A-2B */
		0x26,	/* 2C */
		0x07, 0x7E, /* 2D-2E */
		0x02, 0x54, /* 2F-30 */
		0xB0, 0x00, /* 31-32 */
		0x14,	/* 33 */
		0x49,	/* 34 */
		0x00,	/* 35 written multiple times */
		0x00,	/* 36 not written */
		0xA3,	/* 37 */
		0xC8,	/* 38 */
		0x22,	/* 39 */
		0x02,	/* 3A */
		0x22,	/* 3B */
		0x3F, 0x03, /* 3C-3D */
		0x00,	/* 3E written multiple times */
		0x00,	/* 3F not written */
	}, MATROXFB_OUTPUT_MODE_PAL, 625, 50 };
	static struct mavenregs ntscregs = { {
		0x21, 0xF0, 0x7C, 0x1F,	/* 00: chroma subcarrier */
		0x00,
		0x00,	/* ? not written */
		0x00,	/* modified by code (F9 written...) */
		0x00,	/* ? not written */
		0x7E,	/* 08 */
		0x43,	/* 09 */
		0x7E,	/* 0A */
		0x3D,	/* 0B */
		0x00,	/* 0C */
		0x00,	/* ? not written */
		0x41, 0x00, /* 0E-0F */
		0x3C, 0x00, /* 10-11 */
		0x17,	/* 12 */
		0x21,	/* 13 */
		0x1B, 0x1B, 0x24, /* 14-16 */
		0x83, 0x01, /* 17-18 */
		0x00,	/* 19 */
		0x0F,	/* 1A */
		0x0F,	/* 1B */
		0x60,	/* 1C */
		0x05,	/* 1D */
		0x89, 0x02, /* 1E-1F */
		0x5F,	/* 20 */
		0x04,	/* 21 */
		0x5F,	/* 22 */
		0x01,	/* 23 */
		0x02,	/* 24 */
		0x00,	/* 25 */
		0x0A,	/* 26 */
		0x05,	/* 27 */
		0x00,	/* 28 */
		0x10,	/* 29 */
		0xFF, 0x03, /* 2A-2B */
		0x24,	/* 2C */
		0x0F, 0x78, /* 2D-2E */
		0x00, 0x00, /* 2F-30 */
		0xB2, 0x04, /* 31-32 */
		0x14,	/* 33 */
		0x02,	/* 34 */
		0x00,	/* 35 written multiple times */
		0x00,	/* 36 not written */
		0xA3,	/* 37 */
		0xC8,	/* 38 */
		0x15,	/* 39 */
		0x05,	/* 3A */
		0x3B,	/* 3B */
		0x3C, 0x00, /* 3C-3D */
		0x00,	/* 3E written multiple times */
		0x00,	/* never written */
	}, MATROXFB_OUTPUT_MODE_NTSC, 525, 60 };
	struct matrox_fb_info *minfo = md->primary_head;

	if (minfo->outputs[1].mode == MATROXFB_OUTPUT_MODE_PAL)
		*data = palregs;
	else
		*data = ntscregs;

	/* Set deflicker */
	data->regs[0x93] = maven_compute_deflicker(md);
 
	/* set gamma */
	{
		const struct maven_gamma* g;
		g = maven_compute_gamma(md);
		data->regs[0x83] = g->reg83;
		data->regs[0x84] = g->reg84;
		data->regs[0x85] = g->reg85;
		data->regs[0x86] = g->reg86;
		data->regs[0x87] = g->reg87;
		data->regs[0x88] = g->reg88;
		data->regs[0x89] = g->reg89;
		data->regs[0x8A] = g->reg8a;
		data->regs[0x8B] = g->reg8b;
	}
 
	/* Set contrast / brightness */
	{
		int bl, wl;
		maven_compute_bwlevel (md, &bl, &wl);
		data->regs[0x0e] = bl >> 2;
		data->regs[0x0f] = bl & 3;
		data->regs[0x1e] = wl >> 2;
		data->regs[0x1f] = wl & 3;
	}

	/* Set saturation */
	{
		data->regs[0x20] =
		data->regs[0x22] = minfo->altout.tvo_params.saturation;
	}
 
	/* Set HUE */
	data->regs[0x25] = minfo->altout.tvo_params.hue;
	return;
}

#define LR(x) maven_set_reg(c, (x), m->regs[(x)])
#define LRP(x) maven_set_reg_pair(c, (x), m->regs[(x)] | (m->regs[(x)+1] << 8))
static void maven_init_TV(struct i2c_client* c, const struct mavenregs* m) {
	int val;


	maven_set_reg(c, 0x3E, 0x01);
	maven_get_reg(c, 0x82);	/* fetch oscillator state? */
	maven_set_reg(c, 0x8C, 0x00);
	maven_get_reg(c, 0x94);	/* get 0x82 */
	maven_set_reg(c, 0x94, 0xA2);
	/* xmiscctrl */

	maven_set_reg_pair(c, 0x8E, 0x1EFF);
	maven_set_reg(c, 0xC6, 0x01);

	/* removed code... */

	maven_get_reg(c, 0x06);
	maven_set_reg(c, 0x06, 0xF9);	/* or read |= 0xF0 ? */

	/* removed code here... */

	/* real code begins here? */
	/* chroma subcarrier */
	LR(0x00); LR(0x01); LR(0x02); LR(0x03);

	LR(0x04);

	LR(0x2C);
	LR(0x08);
	LR(0x0A);
	LR(0x09);
	LR(0x29);
	LRP(0x31);
	LRP(0x17);
	LR(0x0B);
	LR(0x0C);
	if (m->mode == MATROXFB_OUTPUT_MODE_PAL) {
		maven_set_reg(c, 0x35, 0x10); /* ... */
	} else {
		maven_set_reg(c, 0x35, 0x0F); /* ... */
	}

	LRP(0x10);

	LRP(0x0E);
	LRP(0x1E);

	LR(0x20);	/* saturation #1 */
	LR(0x22);	/* saturation #2 */
	LR(0x25);	/* hue */
	LR(0x34);
	LR(0x33);
	LR(0x19);
	LR(0x12);
	LR(0x3B);
	LR(0x13);
	LR(0x39);
	LR(0x1D);
	LR(0x3A);
	LR(0x24);
	LR(0x14);
	LR(0x15);
	LR(0x16);
	LRP(0x2D);
	LRP(0x2F);
	LR(0x1A);
	LR(0x1B);
	LR(0x1C);
	LR(0x23);
	LR(0x26);
	LR(0x28);
	LR(0x27);
	LR(0x21);
	LRP(0x2A);
	if (m->mode == MATROXFB_OUTPUT_MODE_PAL)
		maven_set_reg(c, 0x35, 0x1D);	/* ... */
	else
		maven_set_reg(c, 0x35, 0x1C);

	LRP(0x3C);
	LR(0x37);
	LR(0x38);
	maven_set_reg(c, 0xB3, 0x01);

	maven_get_reg(c, 0xB0);	/* read 0x80 */
	maven_set_reg(c, 0xB0, 0x08);	/* ugh... */
	maven_get_reg(c, 0xB9);	/* read 0x7C */
	maven_set_reg(c, 0xB9, 0x78);
	maven_get_reg(c, 0xBF);	/* read 0x00 */
	maven_set_reg(c, 0xBF, 0x02);
	maven_get_reg(c, 0x94);	/* read 0x82 */
	maven_set_reg(c, 0x94, 0xB3);

	LR(0x80); /* 04 1A 91 or 05 21 91 */
	LR(0x81);
	LR(0x82);

	maven_set_reg(c, 0x8C, 0x20);
	maven_get_reg(c, 0x8D);
	maven_set_reg(c, 0x8D, 0x10);

	LR(0x90); /* 4D 50 52 or 4E 05 45 */
	LR(0x91);
	LR(0x92);

	LRP(0x9A); /* 0049 or 004F */
	LRP(0x9C); /* 0004 or 0004 */
	LRP(0x9E); /* 0458 or 045E */
	LRP(0xA0); /* 05DA or 051B */
	LRP(0xA2); /* 00CC or 00CF */
	LRP(0xA4); /* 007D or 007F */
	LRP(0xA6); /* 007C or 007E */
	LRP(0xA8); /* 03CB or 03CE */
	LRP(0x98); /* 0000 or 0000 */
	LRP(0xAE); /* 0044 or 003A */
	LRP(0x96); /* 05DA or 051B */
	LRP(0xAA); /* 04BC or 046A */
	LRP(0xAC); /* 004D or 004E */

	LR(0xBE);
	LR(0xC2);

	maven_get_reg(c, 0x8D);
	maven_set_reg(c, 0x8D, 0x04);

	LR(0x20);	/* saturation #1 */
	LR(0x22);	/* saturation #2 */
	LR(0x93);	/* whoops */
	LR(0x20);	/* oh, saturation #1 again */
	LR(0x22);	/* oh, saturation #2 again */
	LR(0x25);	/* hue */
	LRP(0x0E);
	LRP(0x1E);
	LRP(0x0E);	/* problems with memory? */
	LRP(0x1E);	/* yes, matrox must have problems in memory area... */

	/* load gamma correction stuff */
	LR(0x83);
	LR(0x84);
	LR(0x85);
	LR(0x86);
	LR(0x87);
	LR(0x88);
	LR(0x89);
	LR(0x8A);
	LR(0x8B);

	val = maven_get_reg(c, 0x8D);
	val &= 0x14;			/* 0x10 or anything ored with it */
	maven_set_reg(c, 0x8D, val);

	LR(0x33);
	LR(0x19);
	LR(0x12);
	LR(0x3B);
	LR(0x13);
	LR(0x39);
	LR(0x1D);
	LR(0x3A);
	LR(0x24);
	LR(0x14);
	LR(0x15);
	LR(0x16);
	LRP(0x2D);
	LRP(0x2F);
	LR(0x1A);
	LR(0x1B);
	LR(0x1C);
	LR(0x23);
	LR(0x26);
	LR(0x28);
	LR(0x27);
	LR(0x21);
	LRP(0x2A);
	if (m->mode == MATROXFB_OUTPUT_MODE_PAL)
		maven_set_reg(c, 0x35, 0x1D);
	else
		maven_set_reg(c, 0x35, 0x1C);
	LRP(0x3C);
	LR(0x37);
	LR(0x38);

	maven_get_reg(c, 0xB0);
	LR(0xB0);	/* output mode */
	LR(0x90);
	LR(0xBE);
	LR(0xC2);

	LRP(0x9A);
	LRP(0xA2);
	LRP(0x9E);
	LRP(0xA6);
	LRP(0xAA);
	LRP(0xAC);
	maven_set_reg(c, 0x3E, 0x00);
	maven_set_reg(c, 0x95, 0x20);
}

static int maven_find_exact_clocks(unsigned int ht, unsigned int vt,
		struct mavenregs* m) {
	unsigned int x;
	unsigned int err = ~0;

	/* 1:1 */
	m->regs[0x80] = 0x0F;
	m->regs[0x81] = 0x07;
	m->regs[0x82] = 0x81;

	for (x = 0; x < 8; x++) {
		unsigned int c;
		unsigned int a, b,
			     h2;
		unsigned int h = ht + 2 + x;

		if (!matroxfb_mavenclock((m->mode == MATROXFB_OUTPUT_MODE_PAL) ? &maven_PAL : &maven_NTSC, h, vt, &a, &b, &c, &h2)) {
			unsigned int diff = h - h2;

			if (diff < err) {
				err = diff;
				m->regs[0x80] = a - 1;
				m->regs[0x81] = b - 1;
				m->regs[0x82] = c | 0x80;
				m->hcorr = h2 - 2;
				m->htotal = h - 2;
			}
		}
	}
	return err != ~0U;
}

static inline int maven_compute_timming(struct maven_data* md,
		struct my_timming* mt,
		struct mavenregs* m) {
	unsigned int tmpi;
	unsigned int a, bv, c;
	struct matrox_fb_info *minfo = md->primary_head;

	m->mode = minfo->outputs[1].mode;
	if (m->mode != MATROXFB_OUTPUT_MODE_MONITOR) {
		unsigned int lmargin;
		unsigned int umargin;
		unsigned int vslen;
		unsigned int hcrt;
		unsigned int slen;

		maven_init_TVdata(md, m);

		if (maven_find_exact_clocks(mt->HTotal, mt->VTotal, m) == 0)
			return -EINVAL;

		lmargin = mt->HTotal - mt->HSyncEnd;
		slen = mt->HSyncEnd - mt->HSyncStart;
		hcrt = mt->HTotal - slen - mt->delay;
		umargin = mt->VTotal - mt->VSyncEnd;
		vslen = mt->VSyncEnd - mt->VSyncStart;

		if (m->hcorr < mt->HTotal)
			hcrt += m->hcorr;
		if (hcrt > mt->HTotal)
			hcrt -= mt->HTotal;
		if (hcrt + 2 > mt->HTotal)
			hcrt = 0;	/* or issue warning? */

		/* last (first? middle?) line in picture can have different length */
		/* hlen - 2 */
		m->regs[0x96] = m->hcorr;
		m->regs[0x97] = m->hcorr >> 8;
		/* ... */
		m->regs[0x98] = 0x00; m->regs[0x99] = 0x00;
		/* hblanking end */
		m->regs[0x9A] = lmargin;	/* 100% */
		m->regs[0x9B] = lmargin >> 8;	/* 100% */
		/* who knows */
		m->regs[0x9C] = 0x04;
		m->regs[0x9D] = 0x00;
		/* htotal - 2 */
		m->regs[0xA0] = m->htotal;
		m->regs[0xA1] = m->htotal >> 8;
		/* vblanking end */
		m->regs[0xA2] = mt->VTotal - mt->VSyncStart - 1;	/* stop vblanking */
		m->regs[0xA3] = (mt->VTotal - mt->VSyncStart - 1) >> 8;
		/* something end... [A6]+1..[A8] */
		if (md->version == MGATVO_B) {
			m->regs[0xA4] = 0x04;
			m->regs[0xA5] = 0x00;
		} else {
			m->regs[0xA4] = 0x01;
			m->regs[0xA5] = 0x00;
		}
		/* something start... 0..[A4]-1 */
		m->regs[0xA6] = 0x00;
		m->regs[0xA7] = 0x00;
		/* vertical line count - 1 */
		m->regs[0xA8] = mt->VTotal - 1;
		m->regs[0xA9] = (mt->VTotal - 1) >> 8;
		/* horizontal vidrst pos */
		m->regs[0xAA] = hcrt;		/* 0 <= hcrt <= htotal - 2 */
		m->regs[0xAB] = hcrt >> 8;
		/* vertical vidrst pos */
		m->regs[0xAC] = mt->VTotal - 2;
		m->regs[0xAD] = (mt->VTotal - 2) >> 8;
		/* moves picture up/down and so on... */
		m->regs[0xAE] = 0x01; /* Fix this... 0..VTotal */
		m->regs[0xAF] = 0x00;
		{
			int hdec;
			int hlen;
			unsigned int ibmin = 4 + lmargin + mt->HDisplay;
			unsigned int ib;
			int i;

			/* Verify! */
			/* Where 94208 came from? */
			if (mt->HTotal)
				hdec = 94208 / (mt->HTotal);
			else
				hdec = 0x81;
			if (hdec > 0x81)
				hdec = 0x81;
			if (hdec < 0x41)
				hdec = 0x41;
			hdec--;
			hlen = 98304 - 128 - ((lmargin + mt->HDisplay - 8) * hdec);
			if (hlen < 0)
				hlen = 0;
			hlen = hlen >> 8;
			if (hlen > 0xFF)
				hlen = 0xFF;
			/* Now we have to compute input buffer length.
			   If you want any picture, it must be between
			     4 + lmargin + xres
			   and
			     94208 / hdec
			   If you want perfect picture even on the top
			   of screen, it must be also
			     0x3C0000 * i / hdec + Q - R / hdec
			   where
			        R      Qmin   Qmax
			     0x07000   0x5AE  0x5BF
			     0x08000   0x5CF  0x5FF
			     0x0C000   0x653  0x67F
			     0x10000   0x6F8  0x6FF
			 */
			i = 1;
			do {
				ib = ((0x3C0000 * i - 0x8000)/ hdec + 0x05E7) >> 8;
				i++;
			} while (ib < ibmin);
			if (ib >= m->htotal + 2) {
				ib = ibmin;
			}

			m->regs[0x90] = hdec;	/* < 0x40 || > 0x80 is bad... 0x80 is questionable */
			m->regs[0xC2] = hlen;
			/* 'valid' input line length */
			m->regs[0x9E] = ib;
			m->regs[0x9F] = ib >> 8;
		}
		{
			int vdec;
			int vlen;

#define MATROX_USE64BIT_DIVIDE
			if (mt->VTotal) {
#ifdef MATROX_USE64BIT_DIVIDE
				u64 f1;
				u32 a;
				u32 b;

				a = m->vlines * (m->htotal + 2);
				b = (mt->VTotal - 1) * (m->htotal + 2) + m->hcorr + 2;

				f1 = ((u64)a) << 15;	/* *32768 */
				do_div(f1, b);
				vdec = f1;
#else
				vdec = m->vlines * 32768 / mt->VTotal;
#endif
			} else
				vdec = 0x8000;
			if (vdec > 0x8000)
				vdec = 0x8000;
			vlen = (vslen + umargin + mt->VDisplay) * vdec;
			vlen = (vlen >> 16) - 146; /* FIXME: 146?! */
			if (vlen < 0)
				vlen = 0;
			if (vlen > 0xFF)
				vlen = 0xFF;
			vdec--;
			m->regs[0x91] = vdec;
			m->regs[0x92] = vdec >> 8;
			m->regs[0xBE] = vlen;
		}
		m->regs[0xB0] = 0x08;	/* output: SVideo/Composite */
		return 0;
	}

	DAC1064_calcclock(mt->pixclock, 450000, &a, &bv, &c);
	m->regs[0x80] = a;
	m->regs[0x81] = bv;
	m->regs[0x82] = c | 0x80;

	m->regs[0xB3] = 0x01;
	m->regs[0x94] = 0xB2;

	/* htotal... */
	m->regs[0x96] = mt->HTotal;
	m->regs[0x97] = mt->HTotal >> 8;
	/* ?? */
	m->regs[0x98] = 0x00;
	m->regs[0x99] = 0x00;
	/* hsync len */
	tmpi = mt->HSyncEnd - mt->HSyncStart;
	m->regs[0x9A] = tmpi;
	m->regs[0x9B] = tmpi >> 8;
	/* hblank end */
	tmpi = mt->HTotal - mt->HSyncStart;
	m->regs[0x9C] = tmpi;
	m->regs[0x9D] = tmpi >> 8;
	/* hblank start */
	tmpi += mt->HDisplay;
	m->regs[0x9E] = tmpi;
	m->regs[0x9F] = tmpi >> 8;
	/* htotal + 1 */
	tmpi = mt->HTotal + 1;
	m->regs[0xA0] = tmpi;
	m->regs[0xA1] = tmpi >> 8;
	/* vsync?! */
	tmpi = mt->VSyncEnd - mt->VSyncStart - 1;
	m->regs[0xA2] = tmpi;
	m->regs[0xA3] = tmpi >> 8;
	/* ignored? */
	tmpi = mt->VTotal - mt->VSyncStart;
	m->regs[0xA4] = tmpi;
	m->regs[0xA5] = tmpi >> 8;
	/* ignored? */
	tmpi = mt->VTotal - 1;
	m->regs[0xA6] = tmpi;
	m->regs[0xA7] = tmpi >> 8;
	/* vtotal - 1 */
	m->regs[0xA8] = tmpi;
	m->regs[0xA9] = tmpi >> 8;
	/* hor vidrst */
	tmpi = mt->HTotal - mt->delay;
	m->regs[0xAA] = tmpi;
	m->regs[0xAB] = tmpi >> 8;
	/* vert vidrst */
	tmpi = mt->VTotal - 2;
	m->regs[0xAC] = tmpi;
	m->regs[0xAD] = tmpi >> 8;
	/* ignored? */
	m->regs[0xAE] = 0x00;
	m->regs[0xAF] = 0x00;

	m->regs[0xB0] = 0x03;	/* output: monitor */
	m->regs[0xB1] = 0xA0;	/* ??? */
	m->regs[0x8C] = 0x20;	/* must be set... */
	m->regs[0x8D] = 0x04;	/* defaults to 0x10: test signal */
	m->regs[0xB9] = 0x1A;	/* defaults to 0x2C: too bright */
	m->regs[0xBF] = 0x22;	/* makes picture stable */

	return 0;
}

static int maven_program_timming(struct maven_data* md,
		const struct mavenregs* m) {
	struct i2c_client *c = md->client;

	if (m->mode == MATROXFB_OUTPUT_MODE_MONITOR) {
		LR(0x80);
		LR(0x81);
		LR(0x82);

		LR(0xB3);
		LR(0x94);

		LRP(0x96);
		LRP(0x98);
		LRP(0x9A);
		LRP(0x9C);
		LRP(0x9E);
		LRP(0xA0);
		LRP(0xA2);
		LRP(0xA4);
		LRP(0xA6);
		LRP(0xA8);
		LRP(0xAA);
		LRP(0xAC);
		LRP(0xAE);

		LR(0xB0);	/* output: monitor */
		LR(0xB1);	/* ??? */
		LR(0x8C);	/* must be set... */
		LR(0x8D);	/* defaults to 0x10: test signal */
		LR(0xB9);	/* defaults to 0x2C: too bright */
		LR(0xBF);	/* makes picture stable */
	} else {
		maven_init_TV(c, m);
	}
	return 0;
}

static inline int maven_resync(struct maven_data* md) {
	struct i2c_client *c = md->client;
	maven_set_reg(c, 0x95, 0x20);	/* start whole thing */
	return 0;
}

static int maven_get_queryctrl (struct maven_data* md, 
				struct v4l2_queryctrl *p) {
	int i;
	
	i = get_ctrl_id(p->id);
	if (i >= 0) {
		*p = maven_controls[i].desc;
		return 0;
	}
	if (i == -ENOENT) {
		static const struct v4l2_queryctrl disctrl = 
			{ .flags = V4L2_CTRL_FLAG_DISABLED };
			
		i = p->id;
		*p = disctrl;
		p->id = i;
		sprintf(p->name, "Ctrl #%08X", i);
		return 0;
	}
	return -EINVAL;
}

static int maven_set_control (struct maven_data* md, 
			      struct v4l2_control *p) {
	int i;
	
	i = get_ctrl_id(p->id);
	if (i < 0) return -EINVAL;

	/*
	 * Check if changed.
	 */
	if (p->value == *get_ctrl_ptr(md, i)) return 0;

	/*
	 * Check limits.
	 */
	if (p->value > maven_controls[i].desc.maximum) return -EINVAL;
	if (p->value < maven_controls[i].desc.minimum) return -EINVAL;

	/*
	 * Store new value.
	 */
	*get_ctrl_ptr(md, i) = p->value;

	switch (p->id) {
		case V4L2_CID_BRIGHTNESS:
		case V4L2_CID_CONTRAST:
		{
		  int blacklevel, whitelevel;
		  maven_compute_bwlevel(md, &blacklevel, &whitelevel);
		  blacklevel = (blacklevel >> 2) | ((blacklevel & 3) << 8);
		  whitelevel = (whitelevel >> 2) | ((whitelevel & 3) << 8);
		  maven_set_reg_pair(md->client, 0x0e, blacklevel);
		  maven_set_reg_pair(md->client, 0x1e, whitelevel);
		}
		break;
		case V4L2_CID_SATURATION:
		{
		  maven_set_reg(md->client, 0x20, p->value);
		  maven_set_reg(md->client, 0x22, p->value);
		}
		break;
		case V4L2_CID_HUE:
		{
		  maven_set_reg(md->client, 0x25, p->value);
		}
		break;
		case V4L2_CID_GAMMA:
		{
		  const struct maven_gamma* g;
		  g = maven_compute_gamma(md);
		  maven_set_reg(md->client, 0x83, g->reg83);
		  maven_set_reg(md->client, 0x84, g->reg84);
		  maven_set_reg(md->client, 0x85, g->reg85);
		  maven_set_reg(md->client, 0x86, g->reg86);
		  maven_set_reg(md->client, 0x87, g->reg87);
		  maven_set_reg(md->client, 0x88, g->reg88);
		  maven_set_reg(md->client, 0x89, g->reg89);
		  maven_set_reg(md->client, 0x8a, g->reg8a);
		  maven_set_reg(md->client, 0x8b, g->reg8b);
		}
		break;
		case MATROXFB_CID_TESTOUT:
		{
			unsigned char val 
			  = maven_get_reg(md->client, 0x8d);
			if (p->value) val |= 0x10;
			else          val &= ~0x10;
			maven_set_reg(md->client, 0x8d, val);
		}
		break;
		case MATROXFB_CID_DEFLICKER:
		{
		  maven_set_reg(md->client, 0x93, maven_compute_deflicker(md));
		}
		break;
	}
	

	return 0;
}

static int maven_get_control (struct maven_data* md, 
			      struct v4l2_control *p) {
	int i;
	
	i = get_ctrl_id(p->id);
	if (i < 0) return -EINVAL;
	p->value = *get_ctrl_ptr(md, i);
	return 0;
}

/******************************************************/

static int maven_out_compute(void* md, struct my_timming* mt) {
#define mdinfo ((struct maven_data*)md)
#define minfo (mdinfo->primary_head)
	return maven_compute_timming(md, mt, &minfo->hw.maven);
#undef minfo
#undef mdinfo
}

static int maven_out_program(void* md) {
#define mdinfo ((struct maven_data*)md)
#define minfo (mdinfo->primary_head)
	return maven_program_timming(md, &minfo->hw.maven);
#undef minfo
#undef mdinfo
}

static int maven_out_start(void* md) {
	return maven_resync(md);
}

static int maven_out_verify_mode(void* md, u_int32_t arg) {
	switch (arg) {
		case MATROXFB_OUTPUT_MODE_PAL:
		case MATROXFB_OUTPUT_MODE_NTSC:
		case MATROXFB_OUTPUT_MODE_MONITOR:
			return 0;
	}
	return -EINVAL;
}

static int maven_out_get_queryctrl(void* md, struct v4l2_queryctrl* p) {
        return maven_get_queryctrl(md, p);
}

static int maven_out_get_ctrl(void* md, struct v4l2_control* p) {
	return maven_get_control(md, p);
}

static int maven_out_set_ctrl(void* md, struct v4l2_control* p) {
	return maven_set_control(md, p);
}

static struct matrox_altout maven_altout = {
	.name		= "Secondary output",
	.compute	= maven_out_compute,
	.program	= maven_out_program,
	.start		= maven_out_start,
	.verifymode	= maven_out_verify_mode,
	.getqueryctrl	= maven_out_get_queryctrl,
	.getctrl	= maven_out_get_ctrl,
	.setctrl	= maven_out_set_ctrl,
};

static int maven_init_client(struct i2c_client* clnt) {
	struct maven_data* md = i2c_get_clientdata(clnt);
	struct matrox_fb_info *minfo = container_of(clnt->adapter,
						    struct i2c_bit_adapter,
						    adapter)->minfo;

	md->primary_head = minfo;
	md->client = clnt;
	down_write(&minfo->altout.lock);
	minfo->outputs[1].output = &maven_altout;
	minfo->outputs[1].src = minfo->outputs[1].default_src;
	minfo->outputs[1].data = md;
	minfo->outputs[1].mode = MATROXFB_OUTPUT_MODE_MONITOR;
	up_write(&minfo->altout.lock);
	if (maven_get_reg(clnt, 0xB2) < 0x14) {
		md->version = MGATVO_B;
		/* Tweak some things for this old chip */
	} else {
		md->version = MGATVO_C;
	}
	/*
	 * Set all parameters to its initial values.
	 */
	{
		unsigned int i;

		for (i = 0; i < MAVCTRLS; ++i) {
			*get_ctrl_ptr(md, i) = maven_controls[i].desc.default_value;
		}
	}

	return 0;
}

static int maven_shutdown_client(struct i2c_client* clnt) {
	struct maven_data* md = i2c_get_clientdata(clnt);

	if (md->primary_head) {
		struct matrox_fb_info *minfo = md->primary_head;

		down_write(&minfo->altout.lock);
		minfo->outputs[1].src = MATROXFB_SRC_NONE;
		minfo->outputs[1].output = NULL;
		minfo->outputs[1].data = NULL;
		minfo->outputs[1].mode = MATROXFB_OUTPUT_MODE_MONITOR;
		up_write(&minfo->altout.lock);
		md->primary_head = NULL;
	}
	return 0;
}

static int maven_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	int err = -ENODEV;
	struct maven_data* data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_WORD_DATA |
					      I2C_FUNC_SMBUS_BYTE_DATA |
					      I2C_FUNC_NOSTART |
					      I2C_FUNC_PROTOCOL_MANGLING))
		goto ERROR0;
	if (!(data = kzalloc(sizeof(*data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR0;
	}
	i2c_set_clientdata(client, data);
	err = maven_init_client(client);
	if (err)
		goto ERROR4;
	return 0;
ERROR4:;
	kfree(data);
ERROR0:;
	return err;
}

static int maven_remove(struct i2c_client *client)
{
	maven_shutdown_client(client);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id maven_id[] = {
	{ "maven", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, maven_id);

static struct i2c_driver maven_driver={
	.driver = {
		.name	= "maven",
	},
	.probe		= maven_probe,
	.remove		= maven_remove,
	.id_table	= maven_id,
};

module_i2c_driver(maven_driver);
MODULE_AUTHOR("(c) 1999-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G200/G400 Matrox MGA-TVO driver");
MODULE_LICENSE("GPL");
