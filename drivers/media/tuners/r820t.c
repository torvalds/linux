/*
 * Rafael Micro R820T driver
 *
 * Copyright (C) 2013 Mauro Carvalho Chehab
 *
 * This driver was written from scratch, based on an existing driver
 * that it is part of rtl-sdr git tree, released under GPLv2:
 *	https://groups.google.com/forum/#!topic/ultra-cheap-sdr/Y3rBEOFtHug
 *	https://github.com/n1gp/gr-baz
 *
 * From what I understood from the threads, the original driver was converted
 * to userspace from a Realtek tree. I couldn't find the original tree.
 * However, the original driver look awkward on my eyes. So, I decided to
 * write a new version from it from the scratch, while trying to reproduce
 * everything found there.
 *
 * TODO:
 *	After locking, the original driver seems to have some routines to
 *		improve reception. This was not implemented here yet.
 *
 *	RF Gain set/get is not implemented.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 */

#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/bitrev.h>

#include "tuner-i2c.h"
#include "r820t.h"

/*
 * FIXME: I think that there are only 32 registers, but better safe than
 *	  sorry. After finishing the driver, we may review it.
 */
#define REG_SHADOW_START	5
#define NUM_REGS		27
#define NUM_IMR			5
#define IMR_TRIAL		9

#define VER_NUM  49

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

static int no_imr_cal;
module_param(no_imr_cal, int, 0444);
MODULE_PARM_DESC(no_imr_cal, "Disable IMR calibration at module init");


/*
 * enums and structures
 */

enum xtal_cap_value {
	XTAL_LOW_CAP_30P = 0,
	XTAL_LOW_CAP_20P,
	XTAL_LOW_CAP_10P,
	XTAL_LOW_CAP_0P,
	XTAL_HIGH_CAP_0P
};

struct r820t_sect_type {
	u8	phase_y;
	u8	gain_x;
	u16	value;
};

struct r820t_priv {
	struct list_head		hybrid_tuner_instance_list;
	const struct r820t_config	*cfg;
	struct tuner_i2c_props		i2c_props;
	struct mutex			lock;

	u8				regs[NUM_REGS];
	u8				buf[NUM_REGS + 1];
	enum xtal_cap_value		xtal_cap_sel;
	u16				pll;	/* kHz */
	u32				int_freq;
	u8				fil_cal_code;
	bool				imr_done;
	bool				has_lock;
	bool				init_done;
	struct r820t_sect_type		imr_data[NUM_IMR];

	/* Store current mode */
	u32				delsys;
	enum v4l2_tuner_type		type;
	v4l2_std_id			std;
	u32				bw;	/* in MHz */
};

struct r820t_freq_range {
	u32	freq;
	u8	open_d;
	u8	rf_mux_ploy;
	u8	tf_c;
	u8	xtal_cap20p;
	u8	xtal_cap10p;
	u8	xtal_cap0p;
	u8	imr_mem;		/* Not used, currently */
};

#define VCO_POWER_REF   0x02
#define DIP_FREQ	32000000

/*
 * Static constants
 */

static LIST_HEAD(hybrid_tuner_instance_list);
static DEFINE_MUTEX(r820t_list_mutex);

/* Those initial values start from REG_SHADOW_START */
static const u8 r820t_init_array[NUM_REGS] = {
	0x83, 0x32, 0x75,			/* 05 to 07 */
	0xc0, 0x40, 0xd6, 0x6c,			/* 08 to 0b */
	0xf5, 0x63, 0x75, 0x68,			/* 0c to 0f */
	0x6c, 0x83, 0x80, 0x00,			/* 10 to 13 */
	0x0f, 0x00, 0xc0, 0x30,			/* 14 to 17 */
	0x48, 0xcc, 0x60, 0x00,			/* 18 to 1b */
	0x54, 0xae, 0x4a, 0xc0			/* 1c to 1f */
};

/* Tuner frequency ranges */
static const struct r820t_freq_range freq_ranges[] = {
	{
		.freq = 0,
		.open_d = 0x08,		/* low */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0xdf,		/* R27[7:0]  band2,band0 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 50,		/* Start freq, in MHz */
		.open_d = 0x08,		/* low */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0xbe,		/* R27[7:0]  band4,band1  */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 55,		/* Start freq, in MHz */
		.open_d = 0x08,		/* low */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x8b,		/* R27[7:0]  band7,band4 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 60,		/* Start freq, in MHz */
		.open_d = 0x08,		/* low */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x7b,		/* R27[7:0]  band8,band4 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 65,		/* Start freq, in MHz */
		.open_d = 0x08,		/* low */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x69,		/* R27[7:0]  band9,band6 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 70,		/* Start freq, in MHz */
		.open_d = 0x08,		/* low */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x58,		/* R27[7:0]  band10,band7 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 75,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x44,		/* R27[7:0]  band11,band11 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 80,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x44,		/* R27[7:0]  band11,band11 */
		.xtal_cap20p = 0x02,	/* R16[1:0]  20pF (10)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 90,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x34,		/* R27[7:0]  band12,band11 */
		.xtal_cap20p = 0x01,	/* R16[1:0]  10pF (01)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 100,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x34,		/* R27[7:0]  band12,band11 */
		.xtal_cap20p = 0x01,	/* R16[1:0]  10pF (01)    */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 0,
	}, {
		.freq = 110,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x24,		/* R27[7:0]  band13,band11 */
		.xtal_cap20p = 0x01,	/* R16[1:0]  10pF (01)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 1,
	}, {
		.freq = 120,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x24,		/* R27[7:0]  band13,band11 */
		.xtal_cap20p = 0x01,	/* R16[1:0]  10pF (01)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 1,
	}, {
		.freq = 140,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x14,		/* R27[7:0]  band14,band11 */
		.xtal_cap20p = 0x01,	/* R16[1:0]  10pF (01)   */
		.xtal_cap10p = 0x01,
		.xtal_cap0p = 0x00,
		.imr_mem = 1,
	}, {
		.freq = 180,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x13,		/* R27[7:0]  band14,band12 */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 1,
	}, {
		.freq = 220,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x13,		/* R27[7:0]  band14,band12 */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 2,
	}, {
		.freq = 250,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x11,		/* R27[7:0]  highest,highest */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 2,
	}, {
		.freq = 280,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
		.tf_c = 0x00,		/* R27[7:0]  highest,highest */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 2,
	}, {
		.freq = 310,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x41,	/* R26[7:6]=1 (bypass)  R26[1:0]=1 (middle) */
		.tf_c = 0x00,		/* R27[7:0]  highest,highest */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 2,
	}, {
		.freq = 450,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x41,	/* R26[7:6]=1 (bypass)  R26[1:0]=1 (middle) */
		.tf_c = 0x00,		/* R27[7:0]  highest,highest */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 3,
	}, {
		.freq = 588,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x40,	/* R26[7:6]=1 (bypass)  R26[1:0]=0 (highest) */
		.tf_c = 0x00,		/* R27[7:0]  highest,highest */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 3,
	}, {
		.freq = 650,		/* Start freq, in MHz */
		.open_d = 0x00,		/* high */
		.rf_mux_ploy = 0x40,	/* R26[7:6]=1 (bypass)  R26[1:0]=0 (highest) */
		.tf_c = 0x00,		/* R27[7:0]  highest,highest */
		.xtal_cap20p = 0x00,	/* R16[1:0]  0pF (00)   */
		.xtal_cap10p = 0x00,
		.xtal_cap0p = 0x00,
		.imr_mem = 4,
	}
};

static int r820t_xtal_capacitor[][2] = {
	{ 0x0b, XTAL_LOW_CAP_30P },
	{ 0x02, XTAL_LOW_CAP_20P },
	{ 0x01, XTAL_LOW_CAP_10P },
	{ 0x00, XTAL_LOW_CAP_0P  },
	{ 0x10, XTAL_HIGH_CAP_0P },
};

/*
 * measured with a Racal 6103E GSM test set at 928 MHz with -60 dBm
 * input power, for raw results see:
 *	http://steve-m.de/projects/rtl-sdr/gain_measurement/r820t/
 */

static const int r820t_lna_gain_steps[]  = {
	0, 9, 13, 40, 38, 13, 31, 22, 26, 31, 26, 14, 19, 5, 35, 13
};

static const int r820t_mixer_gain_steps[]  = {
	0, 5, 10, 10, 19, 9, 10, 25, 17, 10, 8, 16, 13, 6, 3, -8
};

/*
 * I2C read/write code and shadow registers logic
 */
static void shadow_store(struct r820t_priv *priv, u8 reg, const u8 *val,
			 int len)
{
	int r = reg - REG_SHADOW_START;

	if (r < 0) {
		len += r;
		r = 0;
	}
	if (len <= 0)
		return;
	if (len > NUM_REGS - r)
		len = NUM_REGS - r;

	tuner_dbg("%s: prev  reg=%02x len=%d: %*ph\n",
		  __func__, r + REG_SHADOW_START, len, len, val);

	memcpy(&priv->regs[r], val, len);
}

static int r820t_write(struct r820t_priv *priv, u8 reg, const u8 *val,
		       int len)
{
	int rc, size, pos = 0;

	/* Store the shadow registers */
	shadow_store(priv, reg, val, len);

	do {
		if (len > priv->cfg->max_i2c_msg_len - 1)
			size = priv->cfg->max_i2c_msg_len - 1;
		else
			size = len;

		/* Fill I2C buffer */
		priv->buf[0] = reg;
		memcpy(&priv->buf[1], &val[pos], size);

		rc = tuner_i2c_xfer_send(&priv->i2c_props, priv->buf, size + 1);
		if (rc != size + 1) {
			tuner_info("%s: i2c wr failed=%d reg=%02x len=%d: %*ph\n",
				   __func__, rc, reg, size, size, &priv->buf[1]);
			if (rc < 0)
				return rc;
			return -EREMOTEIO;
		}
		tuner_dbg("%s: i2c wr reg=%02x len=%d: %*ph\n",
			  __func__, reg, size, size, &priv->buf[1]);

		reg += size;
		len -= size;
		pos += size;
	} while (len > 0);

	return 0;
}

static int r820t_write_reg(struct r820t_priv *priv, u8 reg, u8 val)
{
	return r820t_write(priv, reg, &val, 1);
}

static int r820t_read_cache_reg(struct r820t_priv *priv, int reg)
{
	reg -= REG_SHADOW_START;

	if (reg >= 0 && reg < NUM_REGS)
		return priv->regs[reg];
	else
		return -EINVAL;
}

static int r820t_write_reg_mask(struct r820t_priv *priv, u8 reg, u8 val,
				u8 bit_mask)
{
	int rc = r820t_read_cache_reg(priv, reg);

	if (rc < 0)
		return rc;

	val = (rc & ~bit_mask) | (val & bit_mask);

	return r820t_write(priv, reg, &val, 1);
}

static int r820t_read(struct r820t_priv *priv, u8 reg, u8 *val, int len)
{
	int rc, i;
	u8 *p = &priv->buf[1];

	priv->buf[0] = reg;

	rc = tuner_i2c_xfer_send_recv(&priv->i2c_props, priv->buf, 1, p, len);
	if (rc != len) {
		tuner_info("%s: i2c rd failed=%d reg=%02x len=%d: %*ph\n",
			   __func__, rc, reg, len, len, p);
		if (rc < 0)
			return rc;
		return -EREMOTEIO;
	}

	/* Copy data to the output buffer */
	for (i = 0; i < len; i++)
		val[i] = bitrev8(p[i]);

	tuner_dbg("%s: i2c rd reg=%02x len=%d: %*ph\n",
		  __func__, reg, len, len, val);

	return 0;
}

/*
 * r820t tuning logic
 */

static int r820t_set_mux(struct r820t_priv *priv, u32 freq)
{
	const struct r820t_freq_range *range;
	int i, rc;
	u8 val, reg08, reg09;

	/* Get the proper frequency range */
	freq = freq / 1000000;
	for (i = 0; i < ARRAY_SIZE(freq_ranges) - 1; i++) {
		if (freq < freq_ranges[i + 1].freq)
			break;
	}
	range = &freq_ranges[i];

	tuner_dbg("set r820t range#%d for frequency %d MHz\n", i, freq);

	/* Open Drain */
	rc = r820t_write_reg_mask(priv, 0x17, range->open_d, 0x08);
	if (rc < 0)
		return rc;

	/* RF_MUX,Polymux */
	rc = r820t_write_reg_mask(priv, 0x1a, range->rf_mux_ploy, 0xc3);
	if (rc < 0)
		return rc;

	/* TF BAND */
	rc = r820t_write_reg(priv, 0x1b, range->tf_c);
	if (rc < 0)
		return rc;

	/* XTAL CAP & Drive */
	switch (priv->xtal_cap_sel) {
	case XTAL_LOW_CAP_30P:
	case XTAL_LOW_CAP_20P:
		val = range->xtal_cap20p | 0x08;
		break;
	case XTAL_LOW_CAP_10P:
		val = range->xtal_cap10p | 0x08;
		break;
	case XTAL_HIGH_CAP_0P:
		val = range->xtal_cap0p | 0x00;
		break;
	default:
	case XTAL_LOW_CAP_0P:
		val = range->xtal_cap0p | 0x08;
		break;
	}
	rc = r820t_write_reg_mask(priv, 0x10, val, 0x0b);
	if (rc < 0)
		return rc;

	if (priv->imr_done) {
		reg08 = priv->imr_data[range->imr_mem].gain_x;
		reg09 = priv->imr_data[range->imr_mem].phase_y;
	} else {
		reg08 = 0;
		reg09 = 0;
	}
	rc = r820t_write_reg_mask(priv, 0x08, reg08, 0x3f);
	if (rc < 0)
		return rc;

	rc = r820t_write_reg_mask(priv, 0x09, reg09, 0x3f);

	return rc;
}

static int r820t_set_pll(struct r820t_priv *priv, enum v4l2_tuner_type type,
			 u32 freq)
{
	u32 vco_freq;
	int rc, i;
	unsigned sleep_time = 10000;
	u32 vco_fra;		/* VCO contribution by SDM (kHz) */
	u32 vco_min  = 1770000;
	u32 vco_max  = vco_min * 2;
	u32 pll_ref;
	u16 n_sdm = 2;
	u16 sdm = 0;
	u8 mix_div = 2;
	u8 div_buf = 0;
	u8 div_num = 0;
	u8 refdiv2 = 0;
	u8 ni, si, nint, vco_fine_tune, val;
	u8 data[5];

	/* Frequency in kHz */
	freq = freq / 1000;
	pll_ref = priv->cfg->xtal / 1000;

#if 0
	/* Doesn't exist on rtl-sdk, and on field tests, caused troubles */
	if ((priv->cfg->rafael_chip == CHIP_R620D) ||
	   (priv->cfg->rafael_chip == CHIP_R828D) ||
	   (priv->cfg->rafael_chip == CHIP_R828)) {
		/* ref set refdiv2, reffreq = Xtal/2 on ATV application */
		if (type != V4L2_TUNER_DIGITAL_TV) {
			pll_ref /= 2;
			refdiv2 = 0x10;
			sleep_time = 20000;
		}
	} else {
		if (priv->cfg->xtal > 24000000) {
			pll_ref /= 2;
			refdiv2 = 0x10;
		}
	}
#endif

	rc = r820t_write_reg_mask(priv, 0x10, refdiv2, 0x10);
	if (rc < 0)
		return rc;

	/* set pll autotune = 128kHz */
	rc = r820t_write_reg_mask(priv, 0x1a, 0x00, 0x0c);
	if (rc < 0)
		return rc;

	/* set VCO current = 100 */
	rc = r820t_write_reg_mask(priv, 0x12, 0x80, 0xe0);
	if (rc < 0)
		return rc;

	/* Calculate divider */
	while (mix_div <= 64) {
		if (((freq * mix_div) >= vco_min) &&
		   ((freq * mix_div) < vco_max)) {
			div_buf = mix_div;
			while (div_buf > 2) {
				div_buf = div_buf >> 1;
				div_num++;
			}
			break;
		}
		mix_div = mix_div << 1;
	}

	rc = r820t_read(priv, 0x00, data, sizeof(data));
	if (rc < 0)
		return rc;

	vco_fine_tune = (data[4] & 0x30) >> 4;

	tuner_dbg("mix_div=%d div_num=%d vco_fine_tune=%d\n",
			mix_div, div_num, vco_fine_tune);

	/*
	 * XXX: R828D/16MHz seems to have always vco_fine_tune=1.
	 * Due to that, this calculation goes wrong.
	 */
	if (priv->cfg->rafael_chip != CHIP_R828D) {
		if (vco_fine_tune > VCO_POWER_REF)
			div_num = div_num - 1;
		else if (vco_fine_tune < VCO_POWER_REF)
			div_num = div_num + 1;
	}

	rc = r820t_write_reg_mask(priv, 0x10, div_num << 5, 0xe0);
	if (rc < 0)
		return rc;

	vco_freq = freq * mix_div;
	nint = vco_freq / (2 * pll_ref);
	vco_fra = vco_freq - 2 * pll_ref * nint;

	/* boundary spur prevention */
	if (vco_fra < pll_ref / 64) {
		vco_fra = 0;
	} else if (vco_fra > pll_ref * 127 / 64) {
		vco_fra = 0;
		nint++;
	} else if ((vco_fra > pll_ref * 127 / 128) && (vco_fra < pll_ref)) {
		vco_fra = pll_ref * 127 / 128;
	} else if ((vco_fra > pll_ref) && (vco_fra < pll_ref * 129 / 128)) {
		vco_fra = pll_ref * 129 / 128;
	}

	ni = (nint - 13) / 4;
	si = nint - 4 * ni - 13;

	rc = r820t_write_reg(priv, 0x14, ni + (si << 6));
	if (rc < 0)
		return rc;

	/* pw_sdm */
	if (!vco_fra)
		val = 0x08;
	else
		val = 0x00;

	rc = r820t_write_reg_mask(priv, 0x12, val, 0x08);
	if (rc < 0)
		return rc;

	/* sdm calculator */
	while (vco_fra > 1) {
		if (vco_fra > (2 * pll_ref / n_sdm)) {
			sdm = sdm + 32768 / (n_sdm / 2);
			vco_fra = vco_fra - 2 * pll_ref / n_sdm;
			if (n_sdm >= 0x8000)
				break;
		}
		n_sdm = n_sdm << 1;
	}

	tuner_dbg("freq %d kHz, pll ref %d%s, sdm=0x%04x\n",
		  freq, pll_ref, refdiv2 ? " / 2" : "", sdm);

	rc = r820t_write_reg(priv, 0x16, sdm >> 8);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x15, sdm & 0xff);
	if (rc < 0)
		return rc;

	for (i = 0; i < 2; i++) {
		usleep_range(sleep_time, sleep_time + 1000);

		/* Check if PLL has locked */
		rc = r820t_read(priv, 0x00, data, 3);
		if (rc < 0)
			return rc;
		if (data[2] & 0x40)
			break;

		if (!i) {
			/* Didn't lock. Increase VCO current */
			rc = r820t_write_reg_mask(priv, 0x12, 0x60, 0xe0);
			if (rc < 0)
				return rc;
		}
	}

	if (!(data[2] & 0x40)) {
		priv->has_lock = false;
		return 0;
	}

	priv->has_lock = true;
	tuner_dbg("tuner has lock at frequency %d kHz\n", freq);

	/* set pll autotune = 8kHz */
	rc = r820t_write_reg_mask(priv, 0x1a, 0x08, 0x08);

	return rc;
}

static int r820t_sysfreq_sel(struct r820t_priv *priv, u32 freq,
			     enum v4l2_tuner_type type,
			     v4l2_std_id std,
			     u32 delsys)
{
	int rc;
	u8 mixer_top, lna_top, cp_cur, div_buf_cur, lna_vth_l, mixer_vth_l;
	u8 air_cable1_in, cable2_in, pre_dect, lna_discharge, filter_cur;

	tuner_dbg("adjusting tuner parameters for the standard\n");

	switch (delsys) {
	case SYS_DVBT:
		if ((freq == 506000000) || (freq == 666000000) ||
		   (freq == 818000000)) {
			mixer_top = 0x14;	/* mixer top:14 , top-1, low-discharge */
			lna_top = 0xe5;		/* detect bw 3, lna top:4, predet top:2 */
			cp_cur = 0x28;		/* 101, 0.2 */
			div_buf_cur = 0x20;	/* 10, 200u */
		} else {
			mixer_top = 0x24;	/* mixer top:13 , top-1, low-discharge */
			lna_top = 0xe5;		/* detect bw 3, lna top:4, predet top:2 */
			cp_cur = 0x38;		/* 111, auto */
			div_buf_cur = 0x30;	/* 11, 150u */
		}
		lna_vth_l = 0x53;		/* lna vth 0.84	,  vtl 0.64 */
		mixer_vth_l = 0x75;		/* mixer vth 1.04, vtl 0.84 */
		air_cable1_in = 0x00;
		cable2_in = 0x00;
		pre_dect = 0x40;
		lna_discharge = 14;
		filter_cur = 0x40;		/* 10, low */
		break;
	case SYS_DVBT2:
		mixer_top = 0x24;	/* mixer top:13 , top-1, low-discharge */
		lna_top = 0xe5;		/* detect bw 3, lna top:4, predet top:2 */
		lna_vth_l = 0x53;	/* lna vth 0.84	,  vtl 0.64 */
		mixer_vth_l = 0x75;	/* mixer vth 1.04, vtl 0.84 */
		air_cable1_in = 0x00;
		cable2_in = 0x00;
		pre_dect = 0x40;
		lna_discharge = 14;
		cp_cur = 0x38;		/* 111, auto */
		div_buf_cur = 0x30;	/* 11, 150u */
		filter_cur = 0x40;	/* 10, low */
		break;
	case SYS_ISDBT:
		mixer_top = 0x24;	/* mixer top:13 , top-1, low-discharge */
		lna_top = 0xe5;		/* detect bw 3, lna top:4, predet top:2 */
		lna_vth_l = 0x75;	/* lna vth 1.04	,  vtl 0.84 */
		mixer_vth_l = 0x75;	/* mixer vth 1.04, vtl 0.84 */
		air_cable1_in = 0x00;
		cable2_in = 0x00;
		pre_dect = 0x40;
		lna_discharge = 14;
		cp_cur = 0x38;		/* 111, auto */
		div_buf_cur = 0x30;	/* 11, 150u */
		filter_cur = 0x40;	/* 10, low */
		break;
	default: /* DVB-T 8M */
		mixer_top = 0x24;	/* mixer top:13 , top-1, low-discharge */
		lna_top = 0xe5;		/* detect bw 3, lna top:4, predet top:2 */
		lna_vth_l = 0x53;	/* lna vth 0.84	,  vtl 0.64 */
		mixer_vth_l = 0x75;	/* mixer vth 1.04, vtl 0.84 */
		air_cable1_in = 0x00;
		cable2_in = 0x00;
		pre_dect = 0x40;
		lna_discharge = 14;
		cp_cur = 0x38;		/* 111, auto */
		div_buf_cur = 0x30;	/* 11, 150u */
		filter_cur = 0x40;	/* 10, low */
		break;
	}

	if (priv->cfg->use_diplexer &&
	   ((priv->cfg->rafael_chip == CHIP_R820T) ||
	   (priv->cfg->rafael_chip == CHIP_R828S) ||
	   (priv->cfg->rafael_chip == CHIP_R820C))) {
		if (freq > DIP_FREQ)
			air_cable1_in = 0x00;
		else
			air_cable1_in = 0x60;
		cable2_in = 0x00;
	}


	if (priv->cfg->use_predetect) {
		rc = r820t_write_reg_mask(priv, 0x06, pre_dect, 0x40);
		if (rc < 0)
			return rc;
	}

	rc = r820t_write_reg_mask(priv, 0x1d, lna_top, 0xc7);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg_mask(priv, 0x1c, mixer_top, 0xf8);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x0d, lna_vth_l);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x0e, mixer_vth_l);
	if (rc < 0)
		return rc;

	/* Air-IN only for Astrometa */
	rc = r820t_write_reg_mask(priv, 0x05, air_cable1_in, 0x60);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg_mask(priv, 0x06, cable2_in, 0x08);
	if (rc < 0)
		return rc;

	rc = r820t_write_reg_mask(priv, 0x11, cp_cur, 0x38);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg_mask(priv, 0x17, div_buf_cur, 0x30);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg_mask(priv, 0x0a, filter_cur, 0x60);
	if (rc < 0)
		return rc;
	/*
	 * Original driver initializes regs 0x05 and 0x06 with the
	 * same value again on this point. Probably, it is just an
	 * error there
	 */

	/*
	 * Set LNA
	 */

	tuner_dbg("adjusting LNA parameters\n");
	if (type != V4L2_TUNER_ANALOG_TV) {
		/* LNA TOP: lowest */
		rc = r820t_write_reg_mask(priv, 0x1d, 0, 0x38);
		if (rc < 0)
			return rc;

		/* 0: normal mode */
		rc = r820t_write_reg_mask(priv, 0x1c, 0, 0x04);
		if (rc < 0)
			return rc;

		/* 0: PRE_DECT off */
		rc = r820t_write_reg_mask(priv, 0x06, 0, 0x40);
		if (rc < 0)
			return rc;

		/* agc clk 250hz */
		rc = r820t_write_reg_mask(priv, 0x1a, 0x30, 0x30);
		if (rc < 0)
			return rc;

		msleep(250);

		/* write LNA TOP = 3 */
		rc = r820t_write_reg_mask(priv, 0x1d, 0x18, 0x38);
		if (rc < 0)
			return rc;

		/*
		 * write discharge mode
		 * FIXME: IMHO, the mask here is wrong, but it matches
		 * what's there at the original driver
		 */
		rc = r820t_write_reg_mask(priv, 0x1c, mixer_top, 0x04);
		if (rc < 0)
			return rc;

		/* LNA discharge current */
		rc = r820t_write_reg_mask(priv, 0x1e, lna_discharge, 0x1f);
		if (rc < 0)
			return rc;

		/* agc clk 60hz */
		rc = r820t_write_reg_mask(priv, 0x1a, 0x20, 0x30);
		if (rc < 0)
			return rc;
	} else {
		/* PRE_DECT off */
		rc = r820t_write_reg_mask(priv, 0x06, 0, 0x40);
		if (rc < 0)
			return rc;

		/* write LNA TOP */
		rc = r820t_write_reg_mask(priv, 0x1d, lna_top, 0x38);
		if (rc < 0)
			return rc;

		/*
		 * write discharge mode
		 * FIXME: IMHO, the mask here is wrong, but it matches
		 * what's there at the original driver
		 */
		rc = r820t_write_reg_mask(priv, 0x1c, mixer_top, 0x04);
		if (rc < 0)
			return rc;

		/* LNA discharge current */
		rc = r820t_write_reg_mask(priv, 0x1e, lna_discharge, 0x1f);
		if (rc < 0)
			return rc;

		/* agc clk 1Khz, external det1 cap 1u */
		rc = r820t_write_reg_mask(priv, 0x1a, 0x00, 0x30);
		if (rc < 0)
			return rc;

		rc = r820t_write_reg_mask(priv, 0x10, 0x00, 0x04);
		if (rc < 0)
			return rc;
	 }
	 return 0;
}

static int r820t_set_tv_standard(struct r820t_priv *priv,
				 unsigned bw,
				 enum v4l2_tuner_type type,
				 v4l2_std_id std, u32 delsys)

{
	int rc, i;
	u32 if_khz, filt_cal_lo;
	u8 data[5], val;
	u8 filt_gain, img_r, filt_q, hp_cor, ext_enable, loop_through;
	u8 lt_att, flt_ext_widest, polyfil_cur;
	bool need_calibration;

	tuner_dbg("selecting the delivery system\n");

	if (delsys == SYS_ISDBT) {
		if_khz = 4063;
		filt_cal_lo = 59000;
		filt_gain = 0x10;	/* +3db, 6mhz on */
		img_r = 0x00;		/* image negative */
		filt_q = 0x10;		/* r10[4]:low q(1'b1) */
		hp_cor = 0x6a;		/* 1.7m disable, +2cap, 1.25mhz */
		ext_enable = 0x40;	/* r30[6], ext enable; r30[5]:0 ext at lna max */
		loop_through = 0x00;	/* r5[7], lt on */
		lt_att = 0x00;		/* r31[7], lt att enable */
		flt_ext_widest = 0x00;	/* r15[7]: flt_ext_wide off */
		polyfil_cur = 0x60;	/* r25[6:5]:min */
	} else {
		if (bw <= 6) {
			if_khz = 3570;
			filt_cal_lo = 56000;	/* 52000->56000 */
			filt_gain = 0x10;	/* +3db, 6mhz on */
			img_r = 0x00;		/* image negative */
			filt_q = 0x10;		/* r10[4]:low q(1'b1) */
			hp_cor = 0x6b;		/* 1.7m disable, +2cap, 1.0mhz */
			ext_enable = 0x60;	/* r30[6]=1 ext enable; r30[5]:1 ext at lna max-1 */
			loop_through = 0x00;	/* r5[7], lt on */
			lt_att = 0x00;		/* r31[7], lt att enable */
			flt_ext_widest = 0x00;	/* r15[7]: flt_ext_wide off */
			polyfil_cur = 0x60;	/* r25[6:5]:min */
		} else if (bw == 7) {
#if 0
			/*
			 * There are two 7 MHz tables defined on the original
			 * driver, but just the second one seems to be visible
			 * by rtl2832. Keep this one here commented, as it
			 * might be needed in the future
			 */

			if_khz = 4070;
			filt_cal_lo = 60000;
			filt_gain = 0x10;	/* +3db, 6mhz on */
			img_r = 0x00;		/* image negative */
			filt_q = 0x10;		/* r10[4]:low q(1'b1) */
			hp_cor = 0x2b;		/* 1.7m disable, +1cap, 1.0mhz */
			ext_enable = 0x60;	/* r30[6]=1 ext enable; r30[5]:1 ext at lna max-1 */
			loop_through = 0x00;	/* r5[7], lt on */
			lt_att = 0x00;		/* r31[7], lt att enable */
			flt_ext_widest = 0x00;	/* r15[7]: flt_ext_wide off */
			polyfil_cur = 0x60;	/* r25[6:5]:min */
#endif
			/* 7 MHz, second table */
			if_khz = 4570;
			filt_cal_lo = 63000;
			filt_gain = 0x10;	/* +3db, 6mhz on */
			img_r = 0x00;		/* image negative */
			filt_q = 0x10;		/* r10[4]:low q(1'b1) */
			hp_cor = 0x2a;		/* 1.7m disable, +1cap, 1.25mhz */
			ext_enable = 0x60;	/* r30[6]=1 ext enable; r30[5]:1 ext at lna max-1 */
			loop_through = 0x00;	/* r5[7], lt on */
			lt_att = 0x00;		/* r31[7], lt att enable */
			flt_ext_widest = 0x00;	/* r15[7]: flt_ext_wide off */
			polyfil_cur = 0x60;	/* r25[6:5]:min */
		} else {
			if_khz = 4570;
			filt_cal_lo = 68500;
			filt_gain = 0x10;	/* +3db, 6mhz on */
			img_r = 0x00;		/* image negative */
			filt_q = 0x10;		/* r10[4]:low q(1'b1) */
			hp_cor = 0x0b;		/* 1.7m disable, +0cap, 1.0mhz */
			ext_enable = 0x60;	/* r30[6]=1 ext enable; r30[5]:1 ext at lna max-1 */
			loop_through = 0x00;	/* r5[7], lt on */
			lt_att = 0x00;		/* r31[7], lt att enable */
			flt_ext_widest = 0x00;	/* r15[7]: flt_ext_wide off */
			polyfil_cur = 0x60;	/* r25[6:5]:min */
		}
	}

	/* Initialize the shadow registers */
	memcpy(priv->regs, r820t_init_array, sizeof(r820t_init_array));

	/* Init Flag & Xtal_check Result */
	if (priv->imr_done)
		val = 1 | priv->xtal_cap_sel << 1;
	else
		val = 0;
	rc = r820t_write_reg_mask(priv, 0x0c, val, 0x0f);
	if (rc < 0)
		return rc;

	/* version */
	rc = r820t_write_reg_mask(priv, 0x13, VER_NUM, 0x3f);
	if (rc < 0)
		return rc;

	/* for LT Gain test */
	if (type != V4L2_TUNER_ANALOG_TV) {
		rc = r820t_write_reg_mask(priv, 0x1d, 0x00, 0x38);
		if (rc < 0)
			return rc;
		usleep_range(1000, 2000);
	}
	priv->int_freq = if_khz * 1000;

	/* Check if standard changed. If so, filter calibration is needed */
	if (type != priv->type)
		need_calibration = true;
	else if ((type == V4L2_TUNER_ANALOG_TV) && (std != priv->std))
		need_calibration = true;
	else if ((type == V4L2_TUNER_DIGITAL_TV) &&
		 ((delsys != priv->delsys) || bw != priv->bw))
		need_calibration = true;
	else
		need_calibration = false;

	if (need_calibration) {
		tuner_dbg("calibrating the tuner\n");
		for (i = 0; i < 2; i++) {
			/* Set filt_cap */
			rc = r820t_write_reg_mask(priv, 0x0b, hp_cor, 0x60);
			if (rc < 0)
				return rc;

			/* set cali clk =on */
			rc = r820t_write_reg_mask(priv, 0x0f, 0x04, 0x04);
			if (rc < 0)
				return rc;

			/* X'tal cap 0pF for PLL */
			rc = r820t_write_reg_mask(priv, 0x10, 0x00, 0x03);
			if (rc < 0)
				return rc;

			rc = r820t_set_pll(priv, type, filt_cal_lo * 1000);
			if (rc < 0 || !priv->has_lock)
				return rc;

			/* Start Trigger */
			rc = r820t_write_reg_mask(priv, 0x0b, 0x10, 0x10);
			if (rc < 0)
				return rc;

			usleep_range(1000, 2000);

			/* Stop Trigger */
			rc = r820t_write_reg_mask(priv, 0x0b, 0x00, 0x10);
			if (rc < 0)
				return rc;

			/* set cali clk =off */
			rc = r820t_write_reg_mask(priv, 0x0f, 0x00, 0x04);
			if (rc < 0)
				return rc;

			/* Check if calibration worked */
			rc = r820t_read(priv, 0x00, data, sizeof(data));
			if (rc < 0)
				return rc;

			priv->fil_cal_code = data[4] & 0x0f;
			if (priv->fil_cal_code && priv->fil_cal_code != 0x0f)
				break;
		}
		/* narrowest */
		if (priv->fil_cal_code == 0x0f)
			priv->fil_cal_code = 0;
	}

	rc = r820t_write_reg_mask(priv, 0x0a,
				  filt_q | priv->fil_cal_code, 0x1f);
	if (rc < 0)
		return rc;

	/* Set BW, Filter_gain, & HP corner */
	rc = r820t_write_reg_mask(priv, 0x0b, hp_cor, 0xef);
	if (rc < 0)
		return rc;


	/* Set Img_R */
	rc = r820t_write_reg_mask(priv, 0x07, img_r, 0x80);
	if (rc < 0)
		return rc;

	/* Set filt_3dB, V6MHz */
	rc = r820t_write_reg_mask(priv, 0x06, filt_gain, 0x30);
	if (rc < 0)
		return rc;

	/* channel filter extension */
	rc = r820t_write_reg_mask(priv, 0x1e, ext_enable, 0x60);
	if (rc < 0)
		return rc;

	/* Loop through */
	rc = r820t_write_reg_mask(priv, 0x05, loop_through, 0x80);
	if (rc < 0)
		return rc;

	/* Loop through attenuation */
	rc = r820t_write_reg_mask(priv, 0x1f, lt_att, 0x80);
	if (rc < 0)
		return rc;

	/* filter extension widest */
	rc = r820t_write_reg_mask(priv, 0x0f, flt_ext_widest, 0x80);
	if (rc < 0)
		return rc;

	/* RF poly filter current */
	rc = r820t_write_reg_mask(priv, 0x19, polyfil_cur, 0x60);
	if (rc < 0)
		return rc;

	/* Store current standard. If it changes, re-calibrate the tuner */
	priv->delsys = delsys;
	priv->type = type;
	priv->std = std;
	priv->bw = bw;

	return 0;
}

static int r820t_read_gain(struct r820t_priv *priv)
{
	u8 data[4];
	int rc;

	rc = r820t_read(priv, 0x00, data, sizeof(data));
	if (rc < 0)
		return rc;

	return ((data[3] & 0x0f) << 1) + ((data[3] & 0xf0) >> 4);
}

#if 0
/* FIXME: This routine requires more testing */
static int r820t_set_gain_mode(struct r820t_priv *priv,
			       bool set_manual_gain,
			       int gain)
{
	int rc;

	if (set_manual_gain) {
		int i, total_gain = 0;
		uint8_t mix_index = 0, lna_index = 0;
		u8 data[4];

		/* LNA auto off */
		rc = r820t_write_reg_mask(priv, 0x05, 0x10, 0x10);
		if (rc < 0)
			return rc;

		 /* Mixer auto off */
		rc = r820t_write_reg_mask(priv, 0x07, 0, 0x10);
		if (rc < 0)
			return rc;

		rc = r820t_read(priv, 0x00, data, sizeof(data));
		if (rc < 0)
			return rc;

		/* set fixed VGA gain for now (16.3 dB) */
		rc = r820t_write_reg_mask(priv, 0x0c, 0x08, 0x9f);
		if (rc < 0)
			return rc;

		for (i = 0; i < 15; i++) {
			if (total_gain >= gain)
				break;

			total_gain += r820t_lna_gain_steps[++lna_index];

			if (total_gain >= gain)
				break;

			total_gain += r820t_mixer_gain_steps[++mix_index];
		}

		/* set LNA gain */
		rc = r820t_write_reg_mask(priv, 0x05, lna_index, 0x0f);
		if (rc < 0)
			return rc;

		/* set Mixer gain */
		rc = r820t_write_reg_mask(priv, 0x07, mix_index, 0x0f);
		if (rc < 0)
			return rc;
	} else {
		/* LNA */
		rc = r820t_write_reg_mask(priv, 0x05, 0, 0x10);
		if (rc < 0)
			return rc;

		/* Mixer */
		rc = r820t_write_reg_mask(priv, 0x07, 0x10, 0x10);
		if (rc < 0)
			return rc;

		/* set fixed VGA gain for now (26.5 dB) */
		rc = r820t_write_reg_mask(priv, 0x0c, 0x0b, 0x9f);
		if (rc < 0)
			return rc;
	}

	return 0;
}
#endif

static int generic_set_freq(struct dvb_frontend *fe,
			    u32 freq /* in HZ */,
			    unsigned bw,
			    enum v4l2_tuner_type type,
			    v4l2_std_id std, u32 delsys)
{
	struct r820t_priv		*priv = fe->tuner_priv;
	int				rc = -EINVAL;
	u32				lo_freq;

	tuner_dbg("should set frequency to %d kHz, bw %d MHz\n",
		  freq / 1000, bw);

	rc = r820t_set_tv_standard(priv, bw, type, std, delsys);
	if (rc < 0)
		goto err;

	if ((type == V4L2_TUNER_ANALOG_TV) && (std == V4L2_STD_SECAM_LC))
		lo_freq = freq - priv->int_freq;
	 else
		lo_freq = freq + priv->int_freq;

	rc = r820t_set_mux(priv, lo_freq);
	if (rc < 0)
		goto err;

	rc = r820t_set_pll(priv, type, lo_freq);
	if (rc < 0 || !priv->has_lock)
		goto err;

	rc = r820t_sysfreq_sel(priv, freq, type, std, delsys);
	if (rc < 0)
		goto err;

	tuner_dbg("%s: PLL locked on frequency %d Hz, gain=%d\n",
		  __func__, freq, r820t_read_gain(priv));

err:

	if (rc < 0)
		tuner_dbg("%s: failed=%d\n", __func__, rc);
	return rc;
}

/*
 * r820t standby logic
 */

static int r820t_standby(struct r820t_priv *priv)
{
	int rc;

	/* If device was not initialized yet, don't need to standby */
	if (!priv->init_done)
		return 0;

	rc = r820t_write_reg(priv, 0x06, 0xb1);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x05, 0x03);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x07, 0x3a);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x08, 0x40);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x09, 0xc0);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x0a, 0x36);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x0c, 0x35);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x0f, 0x68);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x11, 0x03);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x17, 0xf4);
	if (rc < 0)
		return rc;
	rc = r820t_write_reg(priv, 0x19, 0x0c);

	/* Force initial calibration */
	priv->type = -1;

	return rc;
}

/*
 * r820t device init logic
 */

static int r820t_xtal_check(struct r820t_priv *priv)
{
	int rc, i;
	u8 data[3], val;

	/* Initialize the shadow registers */
	memcpy(priv->regs, r820t_init_array, sizeof(r820t_init_array));

	/* cap 30pF & Drive Low */
	rc = r820t_write_reg_mask(priv, 0x10, 0x0b, 0x0b);
	if (rc < 0)
		return rc;

	/* set pll autotune = 128kHz */
	rc = r820t_write_reg_mask(priv, 0x1a, 0x00, 0x0c);
	if (rc < 0)
		return rc;

	/* set manual initial reg = 111111;  */
	rc = r820t_write_reg_mask(priv, 0x13, 0x7f, 0x7f);
	if (rc < 0)
		return rc;

	/* set auto */
	rc = r820t_write_reg_mask(priv, 0x13, 0x00, 0x40);
	if (rc < 0)
		return rc;

	/* Try several xtal capacitor alternatives */
	for (i = 0; i < ARRAY_SIZE(r820t_xtal_capacitor); i++) {
		rc = r820t_write_reg_mask(priv, 0x10,
					  r820t_xtal_capacitor[i][0], 0x1b);
		if (rc < 0)
			return rc;

		usleep_range(5000, 6000);

		rc = r820t_read(priv, 0x00, data, sizeof(data));
		if (rc < 0)
			return rc;
		if (!(data[2] & 0x40))
			continue;

		val = data[2] & 0x3f;

		if (priv->cfg->xtal == 16000000 && (val > 29 || val < 23))
			break;

		if (val != 0x3f)
			break;
	}

	if (i == ARRAY_SIZE(r820t_xtal_capacitor))
		return -EINVAL;

	return r820t_xtal_capacitor[i][1];
}

static int r820t_imr_prepare(struct r820t_priv *priv)
{
	int rc;

	/* Initialize the shadow registers */
	memcpy(priv->regs, r820t_init_array, sizeof(r820t_init_array));

	/* lna off (air-in off) */
	rc = r820t_write_reg_mask(priv, 0x05, 0x20, 0x20);
	if (rc < 0)
		return rc;

	/* mixer gain mode = manual */
	rc = r820t_write_reg_mask(priv, 0x07, 0, 0x10);
	if (rc < 0)
		return rc;

	/* filter corner = lowest */
	rc = r820t_write_reg_mask(priv, 0x0a, 0x0f, 0x0f);
	if (rc < 0)
		return rc;

	/* filter bw=+2cap, hp=5M */
	rc = r820t_write_reg_mask(priv, 0x0b, 0x60, 0x6f);
	if (rc < 0)
		return rc;

	/* adc=on, vga code mode, gain = 26.5dB   */
	rc = r820t_write_reg_mask(priv, 0x0c, 0x0b, 0x9f);
	if (rc < 0)
		return rc;

	/* ring clk = on */
	rc = r820t_write_reg_mask(priv, 0x0f, 0, 0x08);
	if (rc < 0)
		return rc;

	/* ring power = on */
	rc = r820t_write_reg_mask(priv, 0x18, 0x10, 0x10);
	if (rc < 0)
		return rc;

	/* from ring = ring pll in */
	rc = r820t_write_reg_mask(priv, 0x1c, 0x02, 0x02);
	if (rc < 0)
		return rc;

	/* sw_pdect = det3 */
	rc = r820t_write_reg_mask(priv, 0x1e, 0x80, 0x80);
	if (rc < 0)
		return rc;

	/* Set filt_3dB */
	rc = r820t_write_reg_mask(priv, 0x06, 0x20, 0x20);

	return rc;
}

static int r820t_multi_read(struct r820t_priv *priv)
{
	int rc, i;
	u16 sum = 0;
	u8 data[2], min = 255, max = 0;

	usleep_range(5000, 6000);

	for (i = 0; i < 6; i++) {
		rc = r820t_read(priv, 0x00, data, sizeof(data));
		if (rc < 0)
			return rc;

		sum += data[1];

		if (data[1] < min)
			min = data[1];

		if (data[1] > max)
			max = data[1];
	}
	rc = sum - max - min;

	return rc;
}

static int r820t_imr_cross(struct r820t_priv *priv,
			   struct r820t_sect_type iq_point[3],
			   u8 *x_direct)
{
	struct r820t_sect_type cross[5]; /* (0,0)(0,Q-1)(0,I-1)(Q-1,0)(I-1,0) */
	struct r820t_sect_type tmp;
	int i, rc;
	u8 reg08, reg09;

	reg08 = r820t_read_cache_reg(priv, 8) & 0xc0;
	reg09 = r820t_read_cache_reg(priv, 9) & 0xc0;

	tmp.gain_x = 0;
	tmp.phase_y = 0;
	tmp.value = 255;

	for (i = 0; i < 5; i++) {
		switch (i) {
		case 0:
			cross[i].gain_x  = reg08;
			cross[i].phase_y = reg09;
			break;
		case 1:
			cross[i].gain_x  = reg08;		/* 0 */
			cross[i].phase_y = reg09 + 1;		/* Q-1 */
			break;
		case 2:
			cross[i].gain_x  = reg08;		/* 0 */
			cross[i].phase_y = (reg09 | 0x20) + 1;	/* I-1 */
			break;
		case 3:
			cross[i].gain_x  = reg08 + 1;		/* Q-1 */
			cross[i].phase_y = reg09;
			break;
		default:
			cross[i].gain_x  = (reg08 | 0x20) + 1;	/* I-1 */
			cross[i].phase_y = reg09;
		}

		rc = r820t_write_reg(priv, 0x08, cross[i].gain_x);
		if (rc < 0)
			return rc;

		rc = r820t_write_reg(priv, 0x09, cross[i].phase_y);
		if (rc < 0)
			return rc;

		rc = r820t_multi_read(priv);
		if (rc < 0)
			return rc;

		cross[i].value = rc;

		if (cross[i].value < tmp.value)
			tmp = cross[i];
	}

	if ((tmp.phase_y & 0x1f) == 1) {	/* y-direction */
		*x_direct = 0;

		iq_point[0] = cross[0];
		iq_point[1] = cross[1];
		iq_point[2] = cross[2];
	} else {				/* (0,0) or x-direction */
		*x_direct = 1;

		iq_point[0] = cross[0];
		iq_point[1] = cross[3];
		iq_point[2] = cross[4];
	}
	return 0;
}

static void r820t_compre_cor(struct r820t_sect_type iq[3])
{
	int i;

	for (i = 3; i > 0; i--) {
		if (iq[0].value > iq[i - 1].value)
			swap(iq[0], iq[i - 1]);
	}
}

static int r820t_compre_step(struct r820t_priv *priv,
			     struct r820t_sect_type iq[3], u8 reg)
{
	int rc;
	struct r820t_sect_type tmp;

	/*
	 * Purpose: if (Gain<9 or Phase<9), Gain+1 or Phase+1 and compare
	 * with min value:
	 *  new < min => update to min and continue
	 *  new > min => Exit
	 */

	/* min value already saved in iq[0] */
	tmp.phase_y = iq[0].phase_y;
	tmp.gain_x  = iq[0].gain_x;

	while (((tmp.gain_x & 0x1f) < IMR_TRIAL) &&
	      ((tmp.phase_y & 0x1f) < IMR_TRIAL)) {
		if (reg == 0x08)
			tmp.gain_x++;
		else
			tmp.phase_y++;

		rc = r820t_write_reg(priv, 0x08, tmp.gain_x);
		if (rc < 0)
			return rc;

		rc = r820t_write_reg(priv, 0x09, tmp.phase_y);
		if (rc < 0)
			return rc;

		rc = r820t_multi_read(priv);
		if (rc < 0)
			return rc;
		tmp.value = rc;

		if (tmp.value <= iq[0].value) {
			iq[0].gain_x  = tmp.gain_x;
			iq[0].phase_y = tmp.phase_y;
			iq[0].value   = tmp.value;
		} else {
			return 0;
		}

	}

	return 0;
}

static int r820t_iq_tree(struct r820t_priv *priv,
			 struct r820t_sect_type iq[3],
			 u8 fix_val, u8 var_val, u8 fix_reg)
{
	int rc, i;
	u8 tmp, var_reg;

	/*
	 * record IMC results by input gain/phase location then adjust
	 * gain or phase positive 1 step and negtive 1 step,
	 * both record results
	 */

	if (fix_reg == 0x08)
		var_reg = 0x09;
	else
		var_reg = 0x08;

	for (i = 0; i < 3; i++) {
		rc = r820t_write_reg(priv, fix_reg, fix_val);
		if (rc < 0)
			return rc;

		rc = r820t_write_reg(priv, var_reg, var_val);
		if (rc < 0)
			return rc;

		rc = r820t_multi_read(priv);
		if (rc < 0)
			return rc;
		iq[i].value = rc;

		if (fix_reg == 0x08) {
			iq[i].gain_x  = fix_val;
			iq[i].phase_y = var_val;
		} else {
			iq[i].phase_y = fix_val;
			iq[i].gain_x  = var_val;
		}

		if (i == 0) {  /* try right-side point */
			var_val++;
		} else if (i == 1) { /* try left-side point */
			 /* if absolute location is 1, change I/Q direction */
			if ((var_val & 0x1f) < 0x02) {
				tmp = 2 - (var_val & 0x1f);

				/* b[5]:I/Q selection. 0:Q-path, 1:I-path */
				if (var_val & 0x20) {
					var_val &= 0xc0;
					var_val |= tmp;
				} else {
					var_val |= 0x20 | tmp;
				}
			} else {
				var_val -= 2;
			}
		}
	}

	return 0;
}

static int r820t_section(struct r820t_priv *priv,
			 struct r820t_sect_type *iq_point)
{
	int rc;
	struct r820t_sect_type compare_iq[3], compare_bet[3];

	/* Try X-1 column and save min result to compare_bet[0] */
	if (!(iq_point->gain_x & 0x1f))
		compare_iq[0].gain_x = ((iq_point->gain_x) & 0xdf) + 1;  /* Q-path, Gain=1 */
	else
		compare_iq[0].gain_x  = iq_point->gain_x - 1;  /* left point */
	compare_iq[0].phase_y = iq_point->phase_y;

	/* y-direction */
	rc = r820t_iq_tree(priv, compare_iq,  compare_iq[0].gain_x,
			compare_iq[0].phase_y, 0x08);
	if (rc < 0)
		return rc;

	r820t_compre_cor(compare_iq);

	compare_bet[0] = compare_iq[0];

	/* Try X column and save min result to compare_bet[1] */
	compare_iq[0].gain_x  = iq_point->gain_x;
	compare_iq[0].phase_y = iq_point->phase_y;

	rc = r820t_iq_tree(priv, compare_iq,  compare_iq[0].gain_x,
			   compare_iq[0].phase_y, 0x08);
	if (rc < 0)
		return rc;

	r820t_compre_cor(compare_iq);

	compare_bet[1] = compare_iq[0];

	/* Try X+1 column and save min result to compare_bet[2] */
	if ((iq_point->gain_x & 0x1f) == 0x00)
		compare_iq[0].gain_x = ((iq_point->gain_x) | 0x20) + 1;  /* I-path, Gain=1 */
	else
		compare_iq[0].gain_x = iq_point->gain_x + 1;
	compare_iq[0].phase_y = iq_point->phase_y;

	rc = r820t_iq_tree(priv, compare_iq,  compare_iq[0].gain_x,
			   compare_iq[0].phase_y, 0x08);
	if (rc < 0)
		return rc;

	r820t_compre_cor(compare_iq);

	compare_bet[2] = compare_iq[0];

	r820t_compre_cor(compare_bet);

	*iq_point = compare_bet[0];

	return 0;
}

static int r820t_vga_adjust(struct r820t_priv *priv)
{
	int rc;
	u8 vga_count;

	/* increase vga power to let image significant */
	for (vga_count = 12; vga_count < 16; vga_count++) {
		rc = r820t_write_reg_mask(priv, 0x0c, vga_count, 0x0f);
		if (rc < 0)
			return rc;

		usleep_range(10000, 11000);

		rc = r820t_multi_read(priv);
		if (rc < 0)
			return rc;

		if (rc > 40 * 4)
			break;
	}

	return 0;
}

static int r820t_iq(struct r820t_priv *priv, struct r820t_sect_type *iq_pont)
{
	struct r820t_sect_type compare_iq[3];
	int rc;
	u8 x_direction = 0;  /* 1:x, 0:y */
	u8 dir_reg, other_reg;

	r820t_vga_adjust(priv);

	rc = r820t_imr_cross(priv, compare_iq, &x_direction);
	if (rc < 0)
		return rc;

	if (x_direction == 1) {
		dir_reg   = 0x08;
		other_reg = 0x09;
	} else {
		dir_reg   = 0x09;
		other_reg = 0x08;
	}

	/* compare and find min of 3 points. determine i/q direction */
	r820t_compre_cor(compare_iq);

	/* increase step to find min value of this direction */
	rc = r820t_compre_step(priv, compare_iq, dir_reg);
	if (rc < 0)
		return rc;

	/* the other direction */
	rc = r820t_iq_tree(priv, compare_iq,  compare_iq[0].gain_x,
				compare_iq[0].phase_y, dir_reg);
	if (rc < 0)
		return rc;

	/* compare and find min of 3 points. determine i/q direction */
	r820t_compre_cor(compare_iq);

	/* increase step to find min value on this direction */
	rc = r820t_compre_step(priv, compare_iq, other_reg);
	if (rc < 0)
		return rc;

	/* check 3 points again */
	rc = r820t_iq_tree(priv, compare_iq,  compare_iq[0].gain_x,
				compare_iq[0].phase_y, other_reg);
	if (rc < 0)
		return rc;

	r820t_compre_cor(compare_iq);

	/* section-9 check */
	rc = r820t_section(priv, compare_iq);

	*iq_pont = compare_iq[0];

	/* reset gain/phase control setting */
	rc = r820t_write_reg_mask(priv, 0x08, 0, 0x3f);
	if (rc < 0)
		return rc;

	rc = r820t_write_reg_mask(priv, 0x09, 0, 0x3f);

	return rc;
}

static int r820t_f_imr(struct r820t_priv *priv, struct r820t_sect_type *iq_pont)
{
	int rc;

	r820t_vga_adjust(priv);

	/*
	 * search surrounding points from previous point
	 * try (x-1), (x), (x+1) columns, and find min IMR result point
	 */
	rc = r820t_section(priv, iq_pont);
	if (rc < 0)
		return rc;

	return 0;
}

static int r820t_imr(struct r820t_priv *priv, unsigned imr_mem, bool im_flag)
{
	struct r820t_sect_type imr_point;
	int rc;
	u32 ring_vco, ring_freq, ring_ref;
	u8 n_ring, n;
	int reg18, reg19, reg1f;

	if (priv->cfg->xtal > 24000000)
		ring_ref = priv->cfg->xtal / 2000;
	else
		ring_ref = priv->cfg->xtal / 1000;

	n_ring = 15;
	for (n = 0; n < 16; n++) {
		if ((16 + n) * 8 * ring_ref >= 3100000) {
			n_ring = n;
			break;
		}
	}

	reg18 = r820t_read_cache_reg(priv, 0x18);
	reg19 = r820t_read_cache_reg(priv, 0x19);
	reg1f = r820t_read_cache_reg(priv, 0x1f);

	reg18 &= 0xf0;      /* set ring[3:0] */
	reg18 |= n_ring;

	ring_vco = (16 + n_ring) * 8 * ring_ref;

	reg18 &= 0xdf;   /* clear ring_se23 */
	reg19 &= 0xfc;   /* clear ring_seldiv */
	reg1f &= 0xfc;   /* clear ring_att */

	switch (imr_mem) {
	case 0:
		ring_freq = ring_vco / 48;
		reg18 |= 0x20;  /* ring_se23 = 1 */
		reg19 |= 0x03;  /* ring_seldiv = 3 */
		reg1f |= 0x02;  /* ring_att 10 */
		break;
	case 1:
		ring_freq = ring_vco / 16;
		reg18 |= 0x00;  /* ring_se23 = 0 */
		reg19 |= 0x02;  /* ring_seldiv = 2 */
		reg1f |= 0x00;  /* pw_ring 00 */
		break;
	case 2:
		ring_freq = ring_vco / 8;
		reg18 |= 0x00;  /* ring_se23 = 0 */
		reg19 |= 0x01;  /* ring_seldiv = 1 */
		reg1f |= 0x03;  /* pw_ring 11 */
		break;
	case 3:
		ring_freq = ring_vco / 6;
		reg18 |= 0x20;  /* ring_se23 = 1 */
		reg19 |= 0x00;  /* ring_seldiv = 0 */
		reg1f |= 0x03;  /* pw_ring 11 */
		break;
	case 4:
		ring_freq = ring_vco / 4;
		reg18 |= 0x00;  /* ring_se23 = 0 */
		reg19 |= 0x00;  /* ring_seldiv = 0 */
		reg1f |= 0x01;  /* pw_ring 01 */
		break;
	default:
		ring_freq = ring_vco / 4;
		reg18 |= 0x00;  /* ring_se23 = 0 */
		reg19 |= 0x00;  /* ring_seldiv = 0 */
		reg1f |= 0x01;  /* pw_ring 01 */
		break;
	}


	/* write pw_ring, n_ring, ringdiv2 registers */

	/* n_ring, ring_se23 */
	rc = r820t_write_reg(priv, 0x18, reg18);
	if (rc < 0)
		return rc;

	/* ring_sediv */
	rc = r820t_write_reg(priv, 0x19, reg19);
	if (rc < 0)
		return rc;

	/* pw_ring */
	rc = r820t_write_reg(priv, 0x1f, reg1f);
	if (rc < 0)
		return rc;

	/* mux input freq ~ rf_in freq */
	rc = r820t_set_mux(priv, (ring_freq - 5300) * 1000);
	if (rc < 0)
		return rc;

	rc = r820t_set_pll(priv, V4L2_TUNER_DIGITAL_TV,
			   (ring_freq - 5300) * 1000);
	if (!priv->has_lock)
		rc = -EINVAL;
	if (rc < 0)
		return rc;

	if (im_flag) {
		rc = r820t_iq(priv, &imr_point);
	} else {
		imr_point.gain_x  = priv->imr_data[3].gain_x;
		imr_point.phase_y = priv->imr_data[3].phase_y;
		imr_point.value   = priv->imr_data[3].value;

		rc = r820t_f_imr(priv, &imr_point);
	}
	if (rc < 0)
		return rc;

	/* save IMR value */
	switch (imr_mem) {
	case 0:
		priv->imr_data[0].gain_x  = imr_point.gain_x;
		priv->imr_data[0].phase_y = imr_point.phase_y;
		priv->imr_data[0].value   = imr_point.value;
		break;
	case 1:
		priv->imr_data[1].gain_x  = imr_point.gain_x;
		priv->imr_data[1].phase_y = imr_point.phase_y;
		priv->imr_data[1].value   = imr_point.value;
		break;
	case 2:
		priv->imr_data[2].gain_x  = imr_point.gain_x;
		priv->imr_data[2].phase_y = imr_point.phase_y;
		priv->imr_data[2].value   = imr_point.value;
		break;
	case 3:
		priv->imr_data[3].gain_x  = imr_point.gain_x;
		priv->imr_data[3].phase_y = imr_point.phase_y;
		priv->imr_data[3].value   = imr_point.value;
		break;
	case 4:
		priv->imr_data[4].gain_x  = imr_point.gain_x;
		priv->imr_data[4].phase_y = imr_point.phase_y;
		priv->imr_data[4].value   = imr_point.value;
		break;
	default:
		priv->imr_data[4].gain_x  = imr_point.gain_x;
		priv->imr_data[4].phase_y = imr_point.phase_y;
		priv->imr_data[4].value   = imr_point.value;
		break;
	}

	return 0;
}

static int r820t_imr_callibrate(struct r820t_priv *priv)
{
	int rc, i;
	int xtal_cap = 0;

	if (priv->init_done)
		return 0;

	/* Detect Xtal capacitance */
	if ((priv->cfg->rafael_chip == CHIP_R820T) ||
	    (priv->cfg->rafael_chip == CHIP_R828S) ||
	    (priv->cfg->rafael_chip == CHIP_R820C)) {
		priv->xtal_cap_sel = XTAL_HIGH_CAP_0P;
	} else {
		/* Initialize registers */
		rc = r820t_write(priv, 0x05,
				r820t_init_array, sizeof(r820t_init_array));
		if (rc < 0)
			return rc;
		for (i = 0; i < 3; i++) {
			rc = r820t_xtal_check(priv);
			if (rc < 0)
				return rc;
			if (!i || rc > xtal_cap)
				xtal_cap = rc;
		}
		priv->xtal_cap_sel = xtal_cap;
	}

	/*
	 * Disables IMR callibration. That emulates the same behaviour
	 * as what is done by rtl-sdr userspace library. Useful for testing
	 */
	if (no_imr_cal) {
		priv->init_done = true;

		return 0;
	}

	/* Initialize registers */
	rc = r820t_write(priv, 0x05,
			 r820t_init_array, sizeof(r820t_init_array));
	if (rc < 0)
		return rc;

	rc = r820t_imr_prepare(priv);
	if (rc < 0)
		return rc;

	rc = r820t_imr(priv, 3, true);
	if (rc < 0)
		return rc;
	rc = r820t_imr(priv, 1, false);
	if (rc < 0)
		return rc;
	rc = r820t_imr(priv, 0, false);
	if (rc < 0)
		return rc;
	rc = r820t_imr(priv, 2, false);
	if (rc < 0)
		return rc;
	rc = r820t_imr(priv, 4, false);
	if (rc < 0)
		return rc;

	priv->init_done = true;
	priv->imr_done = true;

	return 0;
}

#if 0
/* Not used, for now */
static int r820t_gpio(struct r820t_priv *priv, bool enable)
{
	return r820t_write_reg_mask(priv, 0x0f, enable ? 1 : 0, 0x01);
}
#endif

/*
 *  r820t frontend operations and tuner attach code
 *
 * All driver locks and i2c control are only in this part of the code
 */

static int r820t_init(struct dvb_frontend *fe)
{
	struct r820t_priv *priv = fe->tuner_priv;
	int rc;

	tuner_dbg("%s:\n", __func__);

	mutex_lock(&priv->lock);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	rc = r820t_imr_callibrate(priv);
	if (rc < 0)
		goto err;

	/* Initialize registers */
	rc = r820t_write(priv, 0x05,
			 r820t_init_array, sizeof(r820t_init_array));

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	mutex_unlock(&priv->lock);

	if (rc < 0)
		tuner_dbg("%s: failed=%d\n", __func__, rc);
	return rc;
}

static int r820t_sleep(struct dvb_frontend *fe)
{
	struct r820t_priv *priv = fe->tuner_priv;
	int rc;

	tuner_dbg("%s:\n", __func__);

	mutex_lock(&priv->lock);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	rc = r820t_standby(priv);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	mutex_unlock(&priv->lock);

	tuner_dbg("%s: failed=%d\n", __func__, rc);
	return rc;
}

static int r820t_set_analog_freq(struct dvb_frontend *fe,
				 struct analog_parameters *p)
{
	struct r820t_priv *priv = fe->tuner_priv;
	unsigned bw;
	int rc;

	tuner_dbg("%s called\n", __func__);

	/* if std is not defined, choose one */
	if (!p->std)
		p->std = V4L2_STD_MN;

	if ((p->std == V4L2_STD_PAL_M) || (p->std == V4L2_STD_NTSC))
		bw = 6;
	else
		bw = 8;

	mutex_lock(&priv->lock);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	rc = generic_set_freq(fe, 62500l * p->frequency, bw,
			      V4L2_TUNER_ANALOG_TV, p->std, SYS_UNDEFINED);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	mutex_unlock(&priv->lock);

	return rc;
}

static int r820t_set_params(struct dvb_frontend *fe)
{
	struct r820t_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int rc;
	unsigned bw;

	tuner_dbg("%s: delivery_system=%d frequency=%d bandwidth_hz=%d\n",
		__func__, c->delivery_system, c->frequency, c->bandwidth_hz);

	mutex_lock(&priv->lock);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	bw = (c->bandwidth_hz + 500000) / 1000000;
	if (!bw)
		bw = 8;

	rc = generic_set_freq(fe, c->frequency, bw,
			      V4L2_TUNER_DIGITAL_TV, 0, c->delivery_system);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	mutex_unlock(&priv->lock);

	if (rc)
		tuner_dbg("%s: failed=%d\n", __func__, rc);
	return rc;
}

static int r820t_signal(struct dvb_frontend *fe, u16 *strength)
{
	struct r820t_priv *priv = fe->tuner_priv;
	int rc = 0;

	mutex_lock(&priv->lock);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (priv->has_lock) {
		rc = r820t_read_gain(priv);
		if (rc < 0)
			goto err;

		/* A higher gain at LNA means a lower signal strength */
		*strength = (45 - rc) << 4 | 0xff;
		if (*strength == 0xff)
			*strength = 0;
	} else {
		*strength = 0;
	}

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	mutex_unlock(&priv->lock);

	tuner_dbg("%s: %s, gain=%d strength=%d\n",
		  __func__,
		  priv->has_lock ? "PLL locked" : "no signal",
		  rc, *strength);

	return 0;
}

static int r820t_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct r820t_priv *priv = fe->tuner_priv;

	tuner_dbg("%s:\n", __func__);

	*frequency = priv->int_freq;

	return 0;
}

static int r820t_release(struct dvb_frontend *fe)
{
	struct r820t_priv *priv = fe->tuner_priv;

	tuner_dbg("%s:\n", __func__);

	mutex_lock(&r820t_list_mutex);

	if (priv)
		hybrid_tuner_release_state(priv);

	mutex_unlock(&r820t_list_mutex);

	fe->tuner_priv = NULL;

	return 0;
}

static const struct dvb_tuner_ops r820t_tuner_ops = {
	.info = {
		.name           = "Rafael Micro R820T",
		.frequency_min  =   42000000,
		.frequency_max  = 1002000000,
	},
	.init = r820t_init,
	.release = r820t_release,
	.sleep = r820t_sleep,
	.set_params = r820t_set_params,
	.set_analog_params = r820t_set_analog_freq,
	.get_if_frequency = r820t_get_if_frequency,
	.get_rf_strength = r820t_signal,
};

struct dvb_frontend *r820t_attach(struct dvb_frontend *fe,
				  struct i2c_adapter *i2c,
				  const struct r820t_config *cfg)
{
	struct r820t_priv *priv;
	int rc = -ENODEV;
	u8 data[5];
	int instance;

	mutex_lock(&r820t_list_mutex);

	instance = hybrid_tuner_request_state(struct r820t_priv, priv,
					      hybrid_tuner_instance_list,
					      i2c, cfg->i2c_addr,
					      "r820t");
	switch (instance) {
	case 0:
		/* memory allocation failure */
		goto err_no_gate;
	case 1:
		/* new tuner instance */
		priv->cfg = cfg;

		mutex_init(&priv->lock);

		fe->tuner_priv = priv;
		break;
	case 2:
		/* existing tuner instance */
		fe->tuner_priv = priv;
		break;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* check if the tuner is there */
	rc = r820t_read(priv, 0x00, data, sizeof(data));
	if (rc < 0)
		goto err;

	rc = r820t_sleep(fe);
	if (rc < 0)
		goto err;

	tuner_info("Rafael Micro r820t successfully identified\n");

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	mutex_unlock(&r820t_list_mutex);

	memcpy(&fe->ops.tuner_ops, &r820t_tuner_ops,
			sizeof(struct dvb_tuner_ops));

	return fe;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

err_no_gate:
	mutex_unlock(&r820t_list_mutex);

	tuner_info("%s: failed=%d\n", __func__, rc);
	r820t_release(fe);
	return NULL;
}
EXPORT_SYMBOL_GPL(r820t_attach);

MODULE_DESCRIPTION("Rafael Micro r820t silicon tuner driver");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");
