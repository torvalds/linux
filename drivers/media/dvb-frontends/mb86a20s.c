/*
 *   Fujitu mb86a20s ISDB-T/ISDB-Tsb Module driver
 *
 *   Copyright (C) 2010-2013 Mauro Carvalho Chehab <mchehab@redhat.com>
 *   Copyright (C) 2009-2010 Douglas Landgraf <dougsland@redhat.com>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 */

#include <linux/kernel.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "mb86a20s.h"

static int debug = 1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activates frontend debugging (default:0)");

struct mb86a20s_state {
	struct i2c_adapter *i2c;
	const struct mb86a20s_config *config;
	u32 last_frequency;

	struct dvb_frontend frontend;

	u32 if_freq;

	u32 estimated_rate[3];
	unsigned long get_strength_time;

	bool need_init;
};

struct regdata {
	u8 reg;
	u8 data;
};

#define BER_SAMPLING_RATE	1	/* Seconds */

/*
 * Initialization sequence: Use whatevere default values that PV SBTVD
 * does on its initialisation, obtained via USB snoop
 */
static struct regdata mb86a20s_init1[] = {
	{ 0x70, 0x0f },
	{ 0x70, 0xff },
	{ 0x08, 0x01 },
	{ 0x09, 0x3e },
	{ 0x50, 0xd1 }, { 0x51, 0x20 },
	{ 0x39, 0x01 },
	{ 0x71, 0x00 },
	{ 0x28, 0x2a }, { 0x29, 0x00 }, { 0x2a, 0xff }, { 0x2b, 0x80 },
};

static struct regdata mb86a20s_init2[] = {
	{ 0x28, 0x22 }, { 0x29, 0x00 }, { 0x2a, 0x1f }, { 0x2b, 0xf0 },
	{ 0x3b, 0x21 },
	{ 0x3c, 0x38 },
	{ 0x01, 0x0d },
	{ 0x04, 0x08 }, { 0x05, 0x03 },
	{ 0x04, 0x0e }, { 0x05, 0x00 },
	{ 0x04, 0x0f }, { 0x05, 0x37 },
	{ 0x04, 0x0b }, { 0x05, 0x78 },
	{ 0x04, 0x00 }, { 0x05, 0x00 },
	{ 0x04, 0x01 }, { 0x05, 0x1e },
	{ 0x04, 0x02 }, { 0x05, 0x07 },
	{ 0x04, 0x03 }, { 0x05, 0xd0 },
	{ 0x04, 0x09 }, { 0x05, 0x00 },
	{ 0x04, 0x0a }, { 0x05, 0xff },
	{ 0x04, 0x27 }, { 0x05, 0x00 },
	{ 0x04, 0x28 }, { 0x05, 0x00 },
	{ 0x04, 0x1e }, { 0x05, 0x00 },
	{ 0x04, 0x29 }, { 0x05, 0x64 },
	{ 0x04, 0x32 }, { 0x05, 0x02 },
	{ 0x04, 0x14 }, { 0x05, 0x02 },
	{ 0x04, 0x04 }, { 0x05, 0x00 },
	{ 0x04, 0x05 }, { 0x05, 0x22 },
	{ 0x04, 0x06 }, { 0x05, 0x0e },
	{ 0x04, 0x07 }, { 0x05, 0xd8 },
	{ 0x04, 0x12 }, { 0x05, 0x00 },
	{ 0x04, 0x13 }, { 0x05, 0xff },
	{ 0x04, 0x15 }, { 0x05, 0x4e },
	{ 0x04, 0x16 }, { 0x05, 0x20 },

	/*
	 * On this demod, when the bit count reaches the count below,
	 * it collects the bit error count. The bit counters are initialized
	 * to 65535 here. This warrants that all of them will be quickly
	 * calculated when device gets locked. As TMCC is parsed, the values
	 * will be adjusted later in the driver's code.
	 */
	{ 0x52, 0x01 },				/* Turn on BER before Viterbi */
	{ 0x50, 0xa7 }, { 0x51, 0x00 },
	{ 0x50, 0xa8 }, { 0x51, 0xff },
	{ 0x50, 0xa9 }, { 0x51, 0xff },
	{ 0x50, 0xaa }, { 0x51, 0x00 },
	{ 0x50, 0xab }, { 0x51, 0xff },
	{ 0x50, 0xac }, { 0x51, 0xff },
	{ 0x50, 0xad }, { 0x51, 0x00 },
	{ 0x50, 0xae }, { 0x51, 0xff },
	{ 0x50, 0xaf }, { 0x51, 0xff },

	/*
	 * On this demod, post BER counts blocks. When the count reaches the
	 * value below, it collects the block error count. The block counters
	 * are initialized to 127 here. This warrants that all of them will be
	 * quickly calculated when device gets locked. As TMCC is parsed, the
	 * values will be adjusted later in the driver's code.
	 */
	{ 0x5e, 0x07 },				/* Turn on BER after Viterbi */
	{ 0x50, 0xdc }, { 0x51, 0x00 },
	{ 0x50, 0xdd }, { 0x51, 0x7f },
	{ 0x50, 0xde }, { 0x51, 0x00 },
	{ 0x50, 0xdf }, { 0x51, 0x7f },
	{ 0x50, 0xe0 }, { 0x51, 0x00 },
	{ 0x50, 0xe1 }, { 0x51, 0x7f },

	/*
	 * On this demod, when the block count reaches the count below,
	 * it collects the block error count. The block counters are initialized
	 * to 127 here. This warrants that all of them will be quickly
	 * calculated when device gets locked. As TMCC is parsed, the values
	 * will be adjusted later in the driver's code.
	 */
	{ 0x50, 0xb0 }, { 0x51, 0x07 },		/* Enable PER */
	{ 0x50, 0xb2 }, { 0x51, 0x00 },
	{ 0x50, 0xb3 }, { 0x51, 0x7f },
	{ 0x50, 0xb4 }, { 0x51, 0x00 },
	{ 0x50, 0xb5 }, { 0x51, 0x7f },
	{ 0x50, 0xb6 }, { 0x51, 0x00 },
	{ 0x50, 0xb7 }, { 0x51, 0x7f },

	{ 0x50, 0x50 }, { 0x51, 0x02 },		/* MER manual mode */
	{ 0x50, 0x51 }, { 0x51, 0x04 },		/* MER symbol 4 */
	{ 0x45, 0x04 },				/* CN symbol 4 */
	{ 0x48, 0x04 },				/* CN manual mode */

	{ 0x50, 0xd5 }, { 0x51, 0x01 },		/* Serial */
	{ 0x50, 0xd6 }, { 0x51, 0x1f },
	{ 0x50, 0xd2 }, { 0x51, 0x03 },
	{ 0x50, 0xd7 }, { 0x51, 0xbf },
	{ 0x28, 0x74 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0xff },
	{ 0x28, 0x46 }, { 0x29, 0x00 }, { 0x2a, 0x1a }, { 0x2b, 0x0c },

	{ 0x04, 0x40 }, { 0x05, 0x00 },
	{ 0x28, 0x00 }, { 0x2b, 0x08 },
	{ 0x28, 0x05 }, { 0x2b, 0x00 },
	{ 0x1c, 0x01 },
	{ 0x28, 0x06 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x1f },
	{ 0x28, 0x07 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x18 },
	{ 0x28, 0x08 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x12 },
	{ 0x28, 0x09 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x30 },
	{ 0x28, 0x0a }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x37 },
	{ 0x28, 0x0b }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x02 },
	{ 0x28, 0x0c }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x09 },
	{ 0x28, 0x0d }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x06 },
	{ 0x28, 0x0e }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x7b },
	{ 0x28, 0x0f }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x76 },
	{ 0x28, 0x10 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x7d },
	{ 0x28, 0x11 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x08 },
	{ 0x28, 0x12 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x0b },
	{ 0x28, 0x13 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x00 },
	{ 0x28, 0x14 }, { 0x29, 0x00 }, { 0x2a, 0x01 }, { 0x2b, 0xf2 },
	{ 0x28, 0x15 }, { 0x29, 0x00 }, { 0x2a, 0x01 }, { 0x2b, 0xf3 },
	{ 0x28, 0x16 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x05 },
	{ 0x28, 0x17 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x16 },
	{ 0x28, 0x18 }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x0f },
	{ 0x28, 0x19 }, { 0x29, 0x00 }, { 0x2a, 0x07 }, { 0x2b, 0xef },
	{ 0x28, 0x1a }, { 0x29, 0x00 }, { 0x2a, 0x07 }, { 0x2b, 0xd8 },
	{ 0x28, 0x1b }, { 0x29, 0x00 }, { 0x2a, 0x07 }, { 0x2b, 0xf1 },
	{ 0x28, 0x1c }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x3d },
	{ 0x28, 0x1d }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x94 },
	{ 0x28, 0x1e }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0xba },
	{ 0x50, 0x1e }, { 0x51, 0x5d },
	{ 0x50, 0x22 }, { 0x51, 0x00 },
	{ 0x50, 0x23 }, { 0x51, 0xc8 },
	{ 0x50, 0x24 }, { 0x51, 0x00 },
	{ 0x50, 0x25 }, { 0x51, 0xf0 },
	{ 0x50, 0x26 }, { 0x51, 0x00 },
	{ 0x50, 0x27 }, { 0x51, 0xc3 },
	{ 0x50, 0x39 }, { 0x51, 0x02 },
	{ 0xec, 0x0f },
	{ 0xeb, 0x1f },
	{ 0x28, 0x6a }, { 0x29, 0x00 }, { 0x2a, 0x00 }, { 0x2b, 0x00 },
	{ 0xd0, 0x00 },
};

static struct regdata mb86a20s_reset_reception[] = {
	{ 0x70, 0xf0 },
	{ 0x70, 0xff },
	{ 0x08, 0x01 },
	{ 0x08, 0x00 },
};

static struct regdata mb86a20s_per_ber_reset[] = {
	{ 0x53, 0x00 },	/* pre BER Counter reset */
	{ 0x53, 0x07 },

	{ 0x5f, 0x00 },	/* post BER Counter reset */
	{ 0x5f, 0x07 },

	{ 0x50, 0xb1 },	/* PER Counter reset */
	{ 0x51, 0x07 },
	{ 0x51, 0x00 },
};

/*
 * I2C read/write functions and macros
 */

static int mb86a20s_i2c_writereg(struct mb86a20s_state *state,
			     u8 i2c_addr, u8 reg, u8 data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = i2c_addr, .flags = 0, .buf = buf, .len = 2
	};
	int rc;

	rc = i2c_transfer(state->i2c, &msg, 1);
	if (rc != 1) {
		dev_err(&state->i2c->dev,
			"%s: writereg error (rc == %i, reg == 0x%02x, data == 0x%02x)\n",
			__func__, rc, reg, data);
		return rc;
	}

	return 0;
}

static int mb86a20s_i2c_writeregdata(struct mb86a20s_state *state,
				     u8 i2c_addr, struct regdata *rd, int size)
{
	int i, rc;

	for (i = 0; i < size; i++) {
		rc = mb86a20s_i2c_writereg(state, i2c_addr, rd[i].reg,
					   rd[i].data);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static int mb86a20s_i2c_readreg(struct mb86a20s_state *state,
				u8 i2c_addr, u8 reg)
{
	u8 val;
	int rc;
	struct i2c_msg msg[] = {
		{ .addr = i2c_addr, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = i2c_addr, .flags = I2C_M_RD, .buf = &val, .len = 1 }
	};

	rc = i2c_transfer(state->i2c, msg, 2);

	if (rc != 2) {
		dev_err(&state->i2c->dev, "%s: reg=0x%x (error=%d)\n",
			__func__, reg, rc);
		return (rc < 0) ? rc : -EIO;
	}

	return val;
}

#define mb86a20s_readreg(state, reg) \
	mb86a20s_i2c_readreg(state, state->config->demod_address, reg)
#define mb86a20s_writereg(state, reg, val) \
	mb86a20s_i2c_writereg(state, state->config->demod_address, reg, val)
#define mb86a20s_writeregdata(state, regdata) \
	mb86a20s_i2c_writeregdata(state, state->config->demod_address, \
	regdata, ARRAY_SIZE(regdata))

/*
 * Ancillary internal routines (likely compiled inlined)
 *
 * The functions below assume that gateway lock has already obtained
 */

static int mb86a20s_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	int val;

	*status = 0;

	val = mb86a20s_readreg(state, 0x0a) & 0xf;
	if (val < 0)
		return val;

	if (val >= 2)
		*status |= FE_HAS_SIGNAL;

	if (val >= 4)
		*status |= FE_HAS_CARRIER;

	if (val >= 5)
		*status |= FE_HAS_VITERBI;

	if (val >= 7)
		*status |= FE_HAS_SYNC;

	if (val >= 8)				/* Maybe 9? */
		*status |= FE_HAS_LOCK;

	dev_dbg(&state->i2c->dev, "%s: Status = 0x%02x (state = %d)\n",
		 __func__, *status, val);

	return val;
}

static int mb86a20s_read_signal_strength(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int rc;
	unsigned rf_max, rf_min, rf;

	if (state->get_strength_time &&
	   (!time_after(jiffies, state->get_strength_time)))
		return c->strength.stat[0].uvalue;

	/* Reset its value if an error happen */
	c->strength.stat[0].uvalue = 0;

	/* Does a binary search to get RF strength */
	rf_max = 0xfff;
	rf_min = 0;
	do {
		rf = (rf_max + rf_min) / 2;
		rc = mb86a20s_writereg(state, 0x04, 0x1f);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x05, rf >> 8);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x04, 0x20);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x05, rf);
		if (rc < 0)
			return rc;

		rc = mb86a20s_readreg(state, 0x02);
		if (rc < 0)
			return rc;
		if (rc & 0x08)
			rf_min = (rf_max + rf_min) / 2;
		else
			rf_max = (rf_max + rf_min) / 2;
		if (rf_max - rf_min < 4) {
			rf = (rf_max + rf_min) / 2;

			/* Rescale it from 2^12 (4096) to 2^16 */
			rf = rf << (16 - 12);
			if (rf)
				rf |= (1 << 12) - 1;

			dev_dbg(&state->i2c->dev,
				"%s: signal strength = %d (%d < RF=%d < %d)\n",
				__func__, rf, rf_min, rf >> 4, rf_max);
			c->strength.stat[0].uvalue = rf;
			state->get_strength_time = jiffies +
						   msecs_to_jiffies(1000);
			return 0;
		}
	} while (1);
}

static int mb86a20s_get_modulation(struct mb86a20s_state *state,
				   unsigned layer)
{
	int rc;
	static unsigned char reg[] = {
		[0] = 0x86,	/* Layer A */
		[1] = 0x8a,	/* Layer B */
		[2] = 0x8e,	/* Layer C */
	};

	if (layer >= ARRAY_SIZE(reg))
		return -EINVAL;
	rc = mb86a20s_writereg(state, 0x6d, reg[layer]);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x6e);
	if (rc < 0)
		return rc;
	switch ((rc >> 4) & 0x07) {
	case 0:
		return DQPSK;
	case 1:
		return QPSK;
	case 2:
		return QAM_16;
	case 3:
		return QAM_64;
	default:
		return QAM_AUTO;
	}
}

static int mb86a20s_get_fec(struct mb86a20s_state *state,
			    unsigned layer)
{
	int rc;

	static unsigned char reg[] = {
		[0] = 0x87,	/* Layer A */
		[1] = 0x8b,	/* Layer B */
		[2] = 0x8f,	/* Layer C */
	};

	if (layer >= ARRAY_SIZE(reg))
		return -EINVAL;
	rc = mb86a20s_writereg(state, 0x6d, reg[layer]);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x6e);
	if (rc < 0)
		return rc;
	switch ((rc >> 4) & 0x07) {
	case 0:
		return FEC_1_2;
	case 1:
		return FEC_2_3;
	case 2:
		return FEC_3_4;
	case 3:
		return FEC_5_6;
	case 4:
		return FEC_7_8;
	default:
		return FEC_AUTO;
	}
}

static int mb86a20s_get_interleaving(struct mb86a20s_state *state,
				     unsigned layer)
{
	int rc;

	static unsigned char reg[] = {
		[0] = 0x88,	/* Layer A */
		[1] = 0x8c,	/* Layer B */
		[2] = 0x90,	/* Layer C */
	};

	if (layer >= ARRAY_SIZE(reg))
		return -EINVAL;
	rc = mb86a20s_writereg(state, 0x6d, reg[layer]);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x6e);
	if (rc < 0)
		return rc;

	switch ((rc >> 4) & 0x07) {
	case 1:
		return GUARD_INTERVAL_1_4;
	case 2:
		return GUARD_INTERVAL_1_8;
	case 3:
		return GUARD_INTERVAL_1_16;
	case 4:
		return GUARD_INTERVAL_1_32;

	default:
	case 0:
		return GUARD_INTERVAL_AUTO;
	}
}

static int mb86a20s_get_segment_count(struct mb86a20s_state *state,
				      unsigned layer)
{
	int rc, count;
	static unsigned char reg[] = {
		[0] = 0x89,	/* Layer A */
		[1] = 0x8d,	/* Layer B */
		[2] = 0x91,	/* Layer C */
	};

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (layer >= ARRAY_SIZE(reg))
		return -EINVAL;

	rc = mb86a20s_writereg(state, 0x6d, reg[layer]);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x6e);
	if (rc < 0)
		return rc;
	count = (rc >> 4) & 0x0f;

	dev_dbg(&state->i2c->dev, "%s: segments: %d.\n", __func__, count);

	return count;
}

static void mb86a20s_reset_frontend_cache(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	/* Fixed parameters */
	c->delivery_system = SYS_ISDBT;
	c->bandwidth_hz = 6000000;

	/* Initialize values that will be later autodetected */
	c->isdbt_layer_enabled = 0;
	c->transmission_mode = TRANSMISSION_MODE_AUTO;
	c->guard_interval = GUARD_INTERVAL_AUTO;
	c->isdbt_sb_mode = 0;
	c->isdbt_sb_segment_count = 0;
}

/*
 * Estimates the bit rate using the per-segment bit rate given by
 * ABNT/NBR 15601 spec (table 4).
 */
static u32 isdbt_rate[3][5][4] = {
	{	/* DQPSK/QPSK */
		{  280850,  312060,  330420,  340430 },	/* 1/2 */
		{  374470,  416080,  440560,  453910 },	/* 2/3 */
		{  421280,  468090,  495630,  510650 },	/* 3/4 */
		{  468090,  520100,  550700,  567390 },	/* 5/6 */
		{  491500,  546110,  578230,  595760 },	/* 7/8 */
	}, {	/* QAM16 */
		{  561710,  624130,  660840,  680870 },	/* 1/2 */
		{  748950,  832170,  881120,  907820 },	/* 2/3 */
		{  842570,  936190,  991260, 1021300 },	/* 3/4 */
		{  936190, 1040210, 1101400, 1134780 },	/* 5/6 */
		{  983000, 1092220, 1156470, 1191520 },	/* 7/8 */
	}, {	/* QAM64 */
		{  842570,  936190,  991260, 1021300 },	/* 1/2 */
		{ 1123430, 1248260, 1321680, 1361740 },	/* 2/3 */
		{ 1263860, 1404290, 1486900, 1531950 },	/* 3/4 */
		{ 1404290, 1560320, 1652110, 1702170 },	/* 5/6 */
		{ 1474500, 1638340, 1734710, 1787280 },	/* 7/8 */
	}
};

static void mb86a20s_layer_bitrate(struct dvb_frontend *fe, u32 layer,
				   u32 modulation, u32 fec, u32 interleaving,
				   u32 segment)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	u32 rate;
	int m, f, i;

	/*
	 * If modulation/fec/interleaving is not detected, the default is
	 * to consider the lowest bit rate, to avoid taking too long time
	 * to get BER.
	 */
	switch (modulation) {
	case DQPSK:
	case QPSK:
	default:
		m = 0;
		break;
	case QAM_16:
		m = 1;
		break;
	case QAM_64:
		m = 2;
		break;
	}

	switch (fec) {
	default:
	case FEC_1_2:
	case FEC_AUTO:
		f = 0;
		break;
	case FEC_2_3:
		f = 1;
		break;
	case FEC_3_4:
		f = 2;
		break;
	case FEC_5_6:
		f = 3;
		break;
	case FEC_7_8:
		f = 4;
		break;
	}

	switch (interleaving) {
	default:
	case GUARD_INTERVAL_1_4:
		i = 0;
		break;
	case GUARD_INTERVAL_1_8:
		i = 1;
		break;
	case GUARD_INTERVAL_1_16:
		i = 2;
		break;
	case GUARD_INTERVAL_1_32:
		i = 3;
		break;
	}

	/* Samples BER at BER_SAMPLING_RATE seconds */
	rate = isdbt_rate[m][f][i] * segment * BER_SAMPLING_RATE;

	/* Avoids sampling too quickly or to overflow the register */
	if (rate < 256)
		rate = 256;
	else if (rate > (1 << 24) - 1)
		rate = (1 << 24) - 1;

	dev_dbg(&state->i2c->dev,
		"%s: layer %c bitrate: %d kbps; counter = %d (0x%06x)\n",
	       __func__, 'A' + layer, segment * isdbt_rate[m][f][i]/1000,
		rate, rate);

	state->estimated_rate[i] = rate;
}


static int mb86a20s_get_frontend(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int i, rc;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	/* Reset frontend cache to default values */
	mb86a20s_reset_frontend_cache(fe);

	/* Check for partial reception */
	rc = mb86a20s_writereg(state, 0x6d, 0x85);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x6e);
	if (rc < 0)
		return rc;
	c->isdbt_partial_reception = (rc & 0x10) ? 1 : 0;

	/* Get per-layer data */

	for (i = 0; i < 3; i++) {
		dev_dbg(&state->i2c->dev, "%s: getting data for layer %c.\n",
			__func__, 'A' + i);

		rc = mb86a20s_get_segment_count(state, i);
		if (rc < 0)
			goto noperlayer_error;
		if (rc >= 0 && rc < 14) {
			c->layer[i].segment_count = rc;
		} else {
			c->layer[i].segment_count = 0;
			state->estimated_rate[i] = 0;
			continue;
		}
		c->isdbt_layer_enabled |= 1 << i;
		rc = mb86a20s_get_modulation(state, i);
		if (rc < 0)
			goto noperlayer_error;
		dev_dbg(&state->i2c->dev, "%s: modulation %d.\n",
			__func__, rc);
		c->layer[i].modulation = rc;
		rc = mb86a20s_get_fec(state, i);
		if (rc < 0)
			goto noperlayer_error;
		dev_dbg(&state->i2c->dev, "%s: FEC %d.\n",
			__func__, rc);
		c->layer[i].fec = rc;
		rc = mb86a20s_get_interleaving(state, i);
		if (rc < 0)
			goto noperlayer_error;
		dev_dbg(&state->i2c->dev, "%s: interleaving %d.\n",
			__func__, rc);
		c->layer[i].interleaving = rc;
		mb86a20s_layer_bitrate(fe, i, c->layer[i].modulation,
				       c->layer[i].fec,
				       c->layer[i].interleaving,
				       c->layer[i].segment_count);
	}

	rc = mb86a20s_writereg(state, 0x6d, 0x84);
	if (rc < 0)
		return rc;
	if ((rc & 0x60) == 0x20) {
		c->isdbt_sb_mode = 1;
		/* At least, one segment should exist */
		if (!c->isdbt_sb_segment_count)
			c->isdbt_sb_segment_count = 1;
	}

	/* Get transmission mode and guard interval */
	rc = mb86a20s_readreg(state, 0x07);
	if (rc < 0)
		return rc;
	if ((rc & 0x60) == 0x20) {
		switch (rc & 0x0c >> 2) {
		case 0:
			c->transmission_mode = TRANSMISSION_MODE_2K;
			break;
		case 1:
			c->transmission_mode = TRANSMISSION_MODE_4K;
			break;
		case 2:
			c->transmission_mode = TRANSMISSION_MODE_8K;
			break;
		}
	}
	if (!(rc & 0x10)) {
		switch (rc & 0x3) {
		case 0:
			c->guard_interval = GUARD_INTERVAL_1_4;
			break;
		case 1:
			c->guard_interval = GUARD_INTERVAL_1_8;
			break;
		case 2:
			c->guard_interval = GUARD_INTERVAL_1_16;
			break;
		}
	}
	return 0;

noperlayer_error:

	/* per-layer info is incomplete; discard all per-layer */
	c->isdbt_layer_enabled = 0;

	return rc;
}

static int mb86a20s_reset_counters(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int rc, val;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	/* Reset the counters, if the channel changed */
	if (state->last_frequency != c->frequency) {
		memset(&c->cnr, 0, sizeof(c->cnr));
		memset(&c->pre_bit_error, 0, sizeof(c->pre_bit_error));
		memset(&c->pre_bit_count, 0, sizeof(c->pre_bit_count));
		memset(&c->post_bit_error, 0, sizeof(c->post_bit_error));
		memset(&c->post_bit_count, 0, sizeof(c->post_bit_count));
		memset(&c->block_error, 0, sizeof(c->block_error));
		memset(&c->block_count, 0, sizeof(c->block_count));

		state->last_frequency = c->frequency;
	}

	/* Clear status for most stats */

	/* BER/PER counter reset */
	rc = mb86a20s_writeregdata(state, mb86a20s_per_ber_reset);
	if (rc < 0)
		goto err;

	/* CNR counter reset */
	rc = mb86a20s_readreg(state, 0x45);
	if (rc < 0)
		goto err;
	val = rc;
	rc = mb86a20s_writereg(state, 0x45, val | 0x10);
	if (rc < 0)
		goto err;
	rc = mb86a20s_writereg(state, 0x45, val & 0x6f);
	if (rc < 0)
		goto err;

	/* MER counter reset */
	rc = mb86a20s_writereg(state, 0x50, 0x50);
	if (rc < 0)
		goto err;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		goto err;
	val = rc;
	rc = mb86a20s_writereg(state, 0x51, val | 0x01);
	if (rc < 0)
		goto err;
	rc = mb86a20s_writereg(state, 0x51, val & 0x06);
	if (rc < 0)
		goto err;

	goto ok;
err:
	dev_err(&state->i2c->dev,
		"%s: Can't reset FE statistics (error %d).\n",
		__func__, rc);
ok:
	return rc;
}

static int mb86a20s_get_pre_ber(struct dvb_frontend *fe,
				unsigned layer,
				u32 *error, u32 *count)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	int rc, val;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (layer >= 3)
		return -EINVAL;

	/* Check if the BER measures are already available */
	rc = mb86a20s_readreg(state, 0x54);
	if (rc < 0)
		return rc;

	/* Check if data is available for that layer */
	if (!(rc & (1 << layer))) {
		dev_dbg(&state->i2c->dev,
			"%s: preBER for layer %c is not available yet.\n",
			__func__, 'A' + layer);
		return -EBUSY;
	}

	/* Read Bit Error Count */
	rc = mb86a20s_readreg(state, 0x55 + layer * 3);
	if (rc < 0)
		return rc;
	*error = rc << 16;
	rc = mb86a20s_readreg(state, 0x56 + layer * 3);
	if (rc < 0)
		return rc;
	*error |= rc << 8;
	rc = mb86a20s_readreg(state, 0x57 + layer * 3);
	if (rc < 0)
		return rc;
	*error |= rc;

	dev_dbg(&state->i2c->dev,
		"%s: bit error before Viterbi for layer %c: %d.\n",
		__func__, 'A' + layer, *error);

	/* Read Bit Count */
	rc = mb86a20s_writereg(state, 0x50, 0xa7 + layer * 3);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*count = rc << 16;
	rc = mb86a20s_writereg(state, 0x50, 0xa8 + layer * 3);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*count |= rc << 8;
	rc = mb86a20s_writereg(state, 0x50, 0xa9 + layer * 3);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*count |= rc;

	dev_dbg(&state->i2c->dev,
		"%s: bit count before Viterbi for layer %c: %d.\n",
		__func__, 'A' + layer, *count);


	/*
	 * As we get TMCC data from the frontend, we can better estimate the
	 * BER bit counters, in order to do the BER measure during a longer
	 * time. Use those data, if available, to update the bit count
	 * measure.
	 */

	if (state->estimated_rate[layer]
	    && state->estimated_rate[layer] != *count) {
		dev_dbg(&state->i2c->dev,
			"%s: updating layer %c preBER counter to %d.\n",
			__func__, 'A' + layer, state->estimated_rate[layer]);

		/* Turn off BER before Viterbi */
		rc = mb86a20s_writereg(state, 0x52, 0x00);

		/* Update counter for this layer */
		rc = mb86a20s_writereg(state, 0x50, 0xa7 + layer * 3);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51,
				       state->estimated_rate[layer] >> 16);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x50, 0xa8 + layer * 3);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51,
				       state->estimated_rate[layer] >> 8);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x50, 0xa9 + layer * 3);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51,
				       state->estimated_rate[layer]);
		if (rc < 0)
			return rc;

		/* Turn on BER before Viterbi */
		rc = mb86a20s_writereg(state, 0x52, 0x01);

		/* Reset all preBER counters */
		rc = mb86a20s_writereg(state, 0x53, 0x00);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x53, 0x07);
	} else {
		/* Reset counter to collect new data */
		rc = mb86a20s_readreg(state, 0x53);
		if (rc < 0)
			return rc;
		val = rc;
		rc = mb86a20s_writereg(state, 0x53, val & ~(1 << layer));
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x53, val | (1 << layer));
	}

	return rc;
}

static int mb86a20s_get_post_ber(struct dvb_frontend *fe,
				 unsigned layer,
				  u32 *error, u32 *count)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	u32 counter, collect_rate;
	int rc, val;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (layer >= 3)
		return -EINVAL;

	/* Check if the BER measures are already available */
	rc = mb86a20s_readreg(state, 0x60);
	if (rc < 0)
		return rc;

	/* Check if data is available for that layer */
	if (!(rc & (1 << layer))) {
		dev_dbg(&state->i2c->dev,
			"%s: post BER for layer %c is not available yet.\n",
			__func__, 'A' + layer);
		return -EBUSY;
	}

	/* Read Bit Error Count */
	rc = mb86a20s_readreg(state, 0x64 + layer * 3);
	if (rc < 0)
		return rc;
	*error = rc << 16;
	rc = mb86a20s_readreg(state, 0x65 + layer * 3);
	if (rc < 0)
		return rc;
	*error |= rc << 8;
	rc = mb86a20s_readreg(state, 0x66 + layer * 3);
	if (rc < 0)
		return rc;
	*error |= rc;

	dev_dbg(&state->i2c->dev,
		"%s: post bit error for layer %c: %d.\n",
		__func__, 'A' + layer, *error);

	/* Read Bit Count */
	rc = mb86a20s_writereg(state, 0x50, 0xdc + layer * 2);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	counter = rc << 8;
	rc = mb86a20s_writereg(state, 0x50, 0xdd + layer * 2);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	counter |= rc;
	*count = counter * 204 * 8;

	dev_dbg(&state->i2c->dev,
		"%s: post bit count for layer %c: %d.\n",
		__func__, 'A' + layer, *count);

	/*
	 * As we get TMCC data from the frontend, we can better estimate the
	 * BER bit counters, in order to do the BER measure during a longer
	 * time. Use those data, if available, to update the bit count
	 * measure.
	 */

	if (!state->estimated_rate[layer])
		goto reset_measurement;

	collect_rate = state->estimated_rate[layer] / 204 / 8;
	if (collect_rate < 32)
		collect_rate = 32;
	if (collect_rate > 65535)
		collect_rate = 65535;
	if (collect_rate != counter) {
		dev_dbg(&state->i2c->dev,
			"%s: updating postBER counter on layer %c to %d.\n",
			__func__, 'A' + layer, collect_rate);

		/* Turn off BER after Viterbi */
		rc = mb86a20s_writereg(state, 0x5e, 0x00);

		/* Update counter for this layer */
		rc = mb86a20s_writereg(state, 0x50, 0xdc + layer * 2);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, collect_rate >> 8);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x50, 0xdd + layer * 2);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, collect_rate & 0xff);
		if (rc < 0)
			return rc;

		/* Turn on BER after Viterbi */
		rc = mb86a20s_writereg(state, 0x5e, 0x07);

		/* Reset all preBER counters */
		rc = mb86a20s_writereg(state, 0x5f, 0x00);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x5f, 0x07);

		return rc;
	}

reset_measurement:
	/* Reset counter to collect new data */
	rc = mb86a20s_readreg(state, 0x5f);
	if (rc < 0)
		return rc;
	val = rc;
	rc = mb86a20s_writereg(state, 0x5f, val & ~(1 << layer));
	if (rc < 0)
		return rc;
	rc = mb86a20s_writereg(state, 0x5f, val | (1 << layer));

	return rc;
}

static int mb86a20s_get_blk_error(struct dvb_frontend *fe,
			    unsigned layer,
			    u32 *error, u32 *count)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	int rc, val;
	u32 collect_rate;
	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (layer >= 3)
		return -EINVAL;

	/* Check if the PER measures are already available */
	rc = mb86a20s_writereg(state, 0x50, 0xb8);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;

	/* Check if data is available for that layer */

	if (!(rc & (1 << layer))) {
		dev_dbg(&state->i2c->dev,
			"%s: block counts for layer %c aren't available yet.\n",
			__func__, 'A' + layer);
		return -EBUSY;
	}

	/* Read Packet error Count */
	rc = mb86a20s_writereg(state, 0x50, 0xb9 + layer * 2);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*error = rc << 8;
	rc = mb86a20s_writereg(state, 0x50, 0xba + layer * 2);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*error |= rc;
	dev_dbg(&state->i2c->dev, "%s: block error for layer %c: %d.\n",
		__func__, 'A' + layer, *error);

	/* Read Bit Count */
	rc = mb86a20s_writereg(state, 0x50, 0xb2 + layer * 2);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*count = rc << 8;
	rc = mb86a20s_writereg(state, 0x50, 0xb3 + layer * 2);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	*count |= rc;

	dev_dbg(&state->i2c->dev,
		"%s: block count for layer %c: %d.\n",
		__func__, 'A' + layer, *count);

	/*
	 * As we get TMCC data from the frontend, we can better estimate the
	 * BER bit counters, in order to do the BER measure during a longer
	 * time. Use those data, if available, to update the bit count
	 * measure.
	 */

	if (!state->estimated_rate[layer])
		goto reset_measurement;

	collect_rate = state->estimated_rate[layer] / 204 / 8;
	if (collect_rate < 32)
		collect_rate = 32;
	if (collect_rate > 65535)
		collect_rate = 65535;

	if (collect_rate != *count) {
		dev_dbg(&state->i2c->dev,
			"%s: updating PER counter on layer %c to %d.\n",
			__func__, 'A' + layer, collect_rate);

		/* Stop PER measurement */
		rc = mb86a20s_writereg(state, 0x50, 0xb0);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, 0x00);
		if (rc < 0)
			return rc;

		/* Update this layer's counter */
		rc = mb86a20s_writereg(state, 0x50, 0xb2 + layer * 2);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, collect_rate >> 8);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x50, 0xb3 + layer * 2);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, collect_rate & 0xff);
		if (rc < 0)
			return rc;

		/* start PER measurement */
		rc = mb86a20s_writereg(state, 0x50, 0xb0);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, 0x07);
		if (rc < 0)
			return rc;

		/* Reset all counters to collect new data */
		rc = mb86a20s_writereg(state, 0x50, 0xb1);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, 0x07);
		if (rc < 0)
			return rc;
		rc = mb86a20s_writereg(state, 0x51, 0x00);

		return rc;
	}

reset_measurement:
	/* Reset counter to collect new data */
	rc = mb86a20s_writereg(state, 0x50, 0xb1);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	val = rc;
	rc = mb86a20s_writereg(state, 0x51, val | (1 << layer));
	if (rc < 0)
		return rc;
	rc = mb86a20s_writereg(state, 0x51, val & ~(1 << layer));

	return rc;
}

struct linear_segments {
	unsigned x, y;
};

/*
 * All tables below return a dB/1000 measurement
 */

static struct linear_segments cnr_to_db_table[] = {
	{ 19648,     0},
	{ 18187,  1000},
	{ 16534,  2000},
	{ 14823,  3000},
	{ 13161,  4000},
	{ 11622,  5000},
	{ 10279,  6000},
	{  9089,  7000},
	{  8042,  8000},
	{  7137,  9000},
	{  6342, 10000},
	{  5641, 11000},
	{  5030, 12000},
	{  4474, 13000},
	{  3988, 14000},
	{  3556, 15000},
	{  3180, 16000},
	{  2841, 17000},
	{  2541, 18000},
	{  2276, 19000},
	{  2038, 20000},
	{  1800, 21000},
	{  1625, 22000},
	{  1462, 23000},
	{  1324, 24000},
	{  1175, 25000},
	{  1063, 26000},
	{   980, 27000},
	{   907, 28000},
	{   840, 29000},
	{   788, 30000},
};

static struct linear_segments cnr_64qam_table[] = {
	{ 3922688,     0},
	{ 3920384,  1000},
	{ 3902720,  2000},
	{ 3894784,  3000},
	{ 3882496,  4000},
	{ 3872768,  5000},
	{ 3858944,  6000},
	{ 3851520,  7000},
	{ 3838976,  8000},
	{ 3829248,  9000},
	{ 3818240, 10000},
	{ 3806976, 11000},
	{ 3791872, 12000},
	{ 3767040, 13000},
	{ 3720960, 14000},
	{ 3637504, 15000},
	{ 3498496, 16000},
	{ 3296000, 17000},
	{ 3031040, 18000},
	{ 2715392, 19000},
	{ 2362624, 20000},
	{ 1963264, 21000},
	{ 1649664, 22000},
	{ 1366784, 23000},
	{ 1120768, 24000},
	{  890880, 25000},
	{  723456, 26000},
	{  612096, 27000},
	{  518912, 28000},
	{  448256, 29000},
	{  388864, 30000},
};

static struct linear_segments cnr_16qam_table[] = {
	{ 5314816,     0},
	{ 5219072,  1000},
	{ 5118720,  2000},
	{ 4998912,  3000},
	{ 4875520,  4000},
	{ 4736000,  5000},
	{ 4604160,  6000},
	{ 4458752,  7000},
	{ 4300288,  8000},
	{ 4092928,  9000},
	{ 3836160, 10000},
	{ 3521024, 11000},
	{ 3155968, 12000},
	{ 2756864, 13000},
	{ 2347008, 14000},
	{ 1955072, 15000},
	{ 1593600, 16000},
	{ 1297920, 17000},
	{ 1043968, 18000},
	{  839680, 19000},
	{  672256, 20000},
	{  523008, 21000},
	{  424704, 22000},
	{  345088, 23000},
	{  280064, 24000},
	{  221440, 25000},
	{  179712, 26000},
	{  151040, 27000},
	{  128512, 28000},
	{  110080, 29000},
	{   95744, 30000},
};

struct linear_segments cnr_qpsk_table[] = {
	{ 2834176,     0},
	{ 2683648,  1000},
	{ 2536960,  2000},
	{ 2391808,  3000},
	{ 2133248,  4000},
	{ 1906176,  5000},
	{ 1666560,  6000},
	{ 1422080,  7000},
	{ 1189632,  8000},
	{  976384,  9000},
	{  790272, 10000},
	{  633344, 11000},
	{  505600, 12000},
	{  402944, 13000},
	{  320768, 14000},
	{  255488, 15000},
	{  204032, 16000},
	{  163072, 17000},
	{  130304, 18000},
	{  105216, 19000},
	{   83456, 20000},
	{   65024, 21000},
	{   52480, 22000},
	{   42752, 23000},
	{   34560, 24000},
	{   27136, 25000},
	{   22016, 26000},
	{   18432, 27000},
	{   15616, 28000},
	{   13312, 29000},
	{   11520, 30000},
};

static u32 interpolate_value(u32 value, struct linear_segments *segments,
			     unsigned len)
{
	u64 tmp64;
	u32 dx, dy;
	int i, ret;

	if (value >= segments[0].x)
		return segments[0].y;
	if (value < segments[len-1].x)
		return segments[len-1].y;

	for (i = 1; i < len - 1; i++) {
		/* If value is identical, no need to interpolate */
		if (value == segments[i].x)
			return segments[i].y;
		if (value > segments[i].x)
			break;
	}

	/* Linear interpolation between the two (x,y) points */
	dy = segments[i].y - segments[i - 1].y;
	dx = segments[i - 1].x - segments[i].x;
	tmp64 = value - segments[i].x;
	tmp64 *= dy;
	do_div(tmp64, dx);
	ret = segments[i].y - tmp64;

	return ret;
}

static int mb86a20s_get_main_CNR(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 cnr_linear, cnr;
	int rc, val;

	/* Check if CNR is available */
	rc = mb86a20s_readreg(state, 0x45);
	if (rc < 0)
		return rc;

	if (!(rc & 0x40)) {
		dev_dbg(&state->i2c->dev, "%s: CNR is not available yet.\n",
			 __func__);
		return -EBUSY;
	}
	val = rc;

	rc = mb86a20s_readreg(state, 0x46);
	if (rc < 0)
		return rc;
	cnr_linear = rc << 8;

	rc = mb86a20s_readreg(state, 0x46);
	if (rc < 0)
		return rc;
	cnr_linear |= rc;

	cnr = interpolate_value(cnr_linear,
				cnr_to_db_table, ARRAY_SIZE(cnr_to_db_table));

	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	c->cnr.stat[0].svalue = cnr;

	dev_dbg(&state->i2c->dev, "%s: CNR is %d.%03d dB (%d)\n",
		__func__, cnr / 1000, cnr % 1000, cnr_linear);

	/* CNR counter reset */
	rc = mb86a20s_writereg(state, 0x45, val | 0x10);
	if (rc < 0)
		return rc;
	rc = mb86a20s_writereg(state, 0x45, val & 0x6f);

	return rc;
}

static int mb86a20s_get_blk_error_layer_CNR(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 mer, cnr;
	int rc, val, i;
	struct linear_segments *segs;
	unsigned segs_len;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	/* Check if the measures are already available */
	rc = mb86a20s_writereg(state, 0x50, 0x5b);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;

	/* Check if data is available */
	if (!(rc & 0x01)) {
		dev_dbg(&state->i2c->dev,
			"%s: MER measures aren't available yet.\n", __func__);
		return -EBUSY;
	}

	/* Read all layers */
	for (i = 0; i < 3; i++) {
		if (!(c->isdbt_layer_enabled & (1 << i))) {
			c->cnr.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
			continue;
		}

		rc = mb86a20s_writereg(state, 0x50, 0x52 + i * 3);
		if (rc < 0)
			return rc;
		rc = mb86a20s_readreg(state, 0x51);
		if (rc < 0)
			return rc;
		mer = rc << 16;
		rc = mb86a20s_writereg(state, 0x50, 0x53 + i * 3);
		if (rc < 0)
			return rc;
		rc = mb86a20s_readreg(state, 0x51);
		if (rc < 0)
			return rc;
		mer |= rc << 8;
		rc = mb86a20s_writereg(state, 0x50, 0x54 + i * 3);
		if (rc < 0)
			return rc;
		rc = mb86a20s_readreg(state, 0x51);
		if (rc < 0)
			return rc;
		mer |= rc;

		switch (c->layer[i].modulation) {
		case DQPSK:
		case QPSK:
			segs = cnr_qpsk_table;
			segs_len = ARRAY_SIZE(cnr_qpsk_table);
			break;
		case QAM_16:
			segs = cnr_16qam_table;
			segs_len = ARRAY_SIZE(cnr_16qam_table);
			break;
		default:
		case QAM_64:
			segs = cnr_64qam_table;
			segs_len = ARRAY_SIZE(cnr_64qam_table);
			break;
		}
		cnr = interpolate_value(mer, segs, segs_len);

		c->cnr.stat[1 + i].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[1 + i].svalue = cnr;

		dev_dbg(&state->i2c->dev,
			"%s: CNR for layer %c is %d.%03d dB (MER = %d).\n",
			__func__, 'A' + i, cnr / 1000, cnr % 1000, mer);

	}

	/* Start a new MER measurement */
	/* MER counter reset */
	rc = mb86a20s_writereg(state, 0x50, 0x50);
	if (rc < 0)
		return rc;
	rc = mb86a20s_readreg(state, 0x51);
	if (rc < 0)
		return rc;
	val = rc;

	rc = mb86a20s_writereg(state, 0x51, val | 0x01);
	if (rc < 0)
		return rc;
	rc = mb86a20s_writereg(state, 0x51, val & 0x06);
	if (rc < 0)
		return rc;

	return 0;
}

static void mb86a20s_stats_not_ready(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int i;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	/* Fill the length of each status counter */

	/* Only global stats */
	c->strength.len = 1;

	/* Per-layer stats - 3 layers + global */
	c->cnr.len = 4;
	c->pre_bit_error.len = 4;
	c->pre_bit_count.len = 4;
	c->post_bit_error.len = 4;
	c->post_bit_count.len = 4;
	c->block_error.len = 4;
	c->block_count.len = 4;

	/* Signal is always available */
	c->strength.stat[0].scale = FE_SCALE_RELATIVE;
	c->strength.stat[0].uvalue = 0;

	/* Put all of them at FE_SCALE_NOT_AVAILABLE */
	for (i = 0; i < 4; i++) {
		c->cnr.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->pre_bit_error.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->pre_bit_count.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_error.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_error.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
	}
}

static int mb86a20s_get_stats(struct dvb_frontend *fe, int status_nr)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int rc = 0, i;
	u32 bit_error = 0, bit_count = 0;
	u32 t_pre_bit_error = 0, t_pre_bit_count = 0;
	u32 t_post_bit_error = 0, t_post_bit_count = 0;
	u32 block_error = 0, block_count = 0;
	u32 t_block_error = 0, t_block_count = 0;
	int active_layers = 0, pre_ber_layers = 0, post_ber_layers = 0;
	int per_layers = 0;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	mb86a20s_get_main_CNR(fe);

	/* Get per-layer stats */
	mb86a20s_get_blk_error_layer_CNR(fe);

	/*
	 * At state 7, only CNR is available
	 * For BER measures, state=9 is required
	 * FIXME: we may get MER measures with state=8
	 */
	if (status_nr < 9)
		return 0;

	for (i = 0; i < 3; i++) {
		if (c->isdbt_layer_enabled & (1 << i)) {
			/* Layer is active and has rc segments */
			active_layers++;

			/* Handle BER before vterbi */
			rc = mb86a20s_get_pre_ber(fe, i,
						  &bit_error, &bit_count);
			if (rc >= 0) {
				c->pre_bit_error.stat[1 + i].scale = FE_SCALE_COUNTER;
				c->pre_bit_error.stat[1 + i].uvalue += bit_error;
				c->pre_bit_count.stat[1 + i].scale = FE_SCALE_COUNTER;
				c->pre_bit_count.stat[1 + i].uvalue += bit_count;
			} else if (rc != -EBUSY) {
				/*
					* If an I/O error happened,
					* measures are now unavailable
					*/
				c->pre_bit_error.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
				c->pre_bit_count.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
				dev_err(&state->i2c->dev,
					"%s: Can't get BER for layer %c (error %d).\n",
					__func__, 'A' + i, rc);
			}
			if (c->block_error.stat[1 + i].scale != FE_SCALE_NOT_AVAILABLE)
				pre_ber_layers++;

			/* Handle BER post vterbi */
			rc = mb86a20s_get_post_ber(fe, i,
						   &bit_error, &bit_count);
			if (rc >= 0) {
				c->post_bit_error.stat[1 + i].scale = FE_SCALE_COUNTER;
				c->post_bit_error.stat[1 + i].uvalue += bit_error;
				c->post_bit_count.stat[1 + i].scale = FE_SCALE_COUNTER;
				c->post_bit_count.stat[1 + i].uvalue += bit_count;
			} else if (rc != -EBUSY) {
				/*
					* If an I/O error happened,
					* measures are now unavailable
					*/
				c->post_bit_error.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
				c->post_bit_count.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
				dev_err(&state->i2c->dev,
					"%s: Can't get BER for layer %c (error %d).\n",
					__func__, 'A' + i, rc);
			}
			if (c->block_error.stat[1 + i].scale != FE_SCALE_NOT_AVAILABLE)
				post_ber_layers++;

			/* Handle Block errors for PER/UCB reports */
			rc = mb86a20s_get_blk_error(fe, i,
						&block_error,
						&block_count);
			if (rc >= 0) {
				c->block_error.stat[1 + i].scale = FE_SCALE_COUNTER;
				c->block_error.stat[1 + i].uvalue += block_error;
				c->block_count.stat[1 + i].scale = FE_SCALE_COUNTER;
				c->block_count.stat[1 + i].uvalue += block_count;
			} else if (rc != -EBUSY) {
				/*
					* If an I/O error happened,
					* measures are now unavailable
					*/
				c->block_error.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
				c->block_count.stat[1 + i].scale = FE_SCALE_NOT_AVAILABLE;
				dev_err(&state->i2c->dev,
					"%s: Can't get PER for layer %c (error %d).\n",
					__func__, 'A' + i, rc);

			}
			if (c->block_error.stat[1 + i].scale != FE_SCALE_NOT_AVAILABLE)
				per_layers++;

			/* Update total preBER */
			t_pre_bit_error += c->pre_bit_error.stat[1 + i].uvalue;
			t_pre_bit_count += c->pre_bit_count.stat[1 + i].uvalue;

			/* Update total postBER */
			t_post_bit_error += c->post_bit_error.stat[1 + i].uvalue;
			t_post_bit_count += c->post_bit_count.stat[1 + i].uvalue;

			/* Update total PER */
			t_block_error += c->block_error.stat[1 + i].uvalue;
			t_block_count += c->block_count.stat[1 + i].uvalue;
		}
	}

	/*
	 * Start showing global count if at least one error count is
	 * available.
	 */
	if (pre_ber_layers) {
		/*
		 * At least one per-layer BER measure was read. We can now
		 * calculate the total BER
		 *
		 * Total Bit Error/Count is calculated as the sum of the
		 * bit errors on all active layers.
		 */
		c->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->pre_bit_error.stat[0].uvalue = t_pre_bit_error;
		c->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->pre_bit_count.stat[0].uvalue = t_pre_bit_count;
	} else {
		c->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	}

	/*
	 * Start showing global count if at least one error count is
	 * available.
	 */
	if (post_ber_layers) {
		/*
		 * At least one per-layer BER measure was read. We can now
		 * calculate the total BER
		 *
		 * Total Bit Error/Count is calculated as the sum of the
		 * bit errors on all active layers.
		 */
		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue = t_post_bit_error;
		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue = t_post_bit_count;
	} else {
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	}

	if (per_layers) {
		/*
		 * At least one per-layer UCB measure was read. We can now
		 * calculate the total UCB
		 *
		 * Total block Error/Count is calculated as the sum of the
		 * block errors on all active layers.
		 */
		c->block_error.stat[0].scale = FE_SCALE_COUNTER;
		c->block_error.stat[0].uvalue = t_block_error;
		c->block_count.stat[0].scale = FE_SCALE_COUNTER;
		c->block_count.stat[0].uvalue = t_block_count;
	} else {
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	}

	return rc;
}

/*
 * The functions below are called via DVB callbacks, so they need to
 * properly use the I2C gate control
 */

static int mb86a20s_initfe(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	u64 pll;
	int rc;
	u8  regD5 = 1;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	/* Initialize the frontend */
	rc = mb86a20s_writeregdata(state, mb86a20s_init1);
	if (rc < 0)
		goto err;

	/* Adjust IF frequency to match tuner */
	if (fe->ops.tuner_ops.get_if_frequency)
		fe->ops.tuner_ops.get_if_frequency(fe, &state->if_freq);

	if (!state->if_freq)
		state->if_freq = 3300000;

	/* pll = freq[Hz] * 2^24/10^6 / 16.285714286 */
	pll = state->if_freq * 1677721600L;
	do_div(pll, 1628571429L);
	rc = mb86a20s_writereg(state, 0x28, 0x20);
	if (rc < 0)
		goto err;
	rc = mb86a20s_writereg(state, 0x29, (pll >> 16) & 0xff);
	if (rc < 0)
		goto err;
	rc = mb86a20s_writereg(state, 0x2a, (pll >> 8) & 0xff);
	if (rc < 0)
		goto err;
	rc = mb86a20s_writereg(state, 0x2b, pll & 0xff);
	if (rc < 0)
		goto err;
	dev_dbg(&state->i2c->dev, "%s: IF=%d, PLL=0x%06llx\n",
		__func__, state->if_freq, (long long)pll);

	if (!state->config->is_serial) {
		regD5 &= ~1;

		rc = mb86a20s_writereg(state, 0x50, 0xd5);
		if (rc < 0)
			goto err;
		rc = mb86a20s_writereg(state, 0x51, regD5);
		if (rc < 0)
			goto err;
	}

	rc = mb86a20s_writeregdata(state, mb86a20s_init2);
	if (rc < 0)
		goto err;


err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (rc < 0) {
		state->need_init = true;
		dev_info(&state->i2c->dev,
			 "mb86a20s: Init failed. Will try again later\n");
	} else {
		state->need_init = false;
		dev_dbg(&state->i2c->dev, "Initialization succeeded.\n");
	}
	return rc;
}

static int mb86a20s_set_frontend(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	int rc, if_freq;
#if 0
	/*
	 * FIXME: Properly implement the set frontend properties
	 */
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
#endif
	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	/*
	 * Gate should already be opened, but it doesn't hurt to
	 * double-check
	 */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	fe->ops.tuner_ops.set_params(fe);

	if (fe->ops.tuner_ops.get_if_frequency)
		fe->ops.tuner_ops.get_if_frequency(fe, &if_freq);

	/*
	 * Make it more reliable: if, for some reason, the initial
	 * device initialization doesn't happen, initialize it when
	 * a SBTVD parameters are adjusted.
	 *
	 * Unfortunately, due to a hard to track bug at tda829x/tda18271,
	 * the agc callback logic is not called during DVB attach time,
	 * causing mb86a20s to not be initialized with Kworld SBTVD.
	 * So, this hack is needed, in order to make Kworld SBTVD to work.
	 *
	 * It is also needed to change the IF after the initial init.
	 *
	 * HACK: Always init the frontend when set_frontend is called:
	 * it was noticed that, on some devices, it fails to lock on a
	 * different channel. So, it is better to reset everything, even
	 * wasting some time, than to loose channel lock.
	 */
	mb86a20s_initfe(fe);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	rc = mb86a20s_writeregdata(state, mb86a20s_reset_reception);
	mb86a20s_reset_counters(fe);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	return rc;
}

static int mb86a20s_read_status_and_stats(struct dvb_frontend *fe,
					  fe_status_t *status)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	int rc, status_nr;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	/* Get lock */
	status_nr = mb86a20s_read_status(fe, status);
	if (status_nr < 7) {
		mb86a20s_stats_not_ready(fe);
		mb86a20s_reset_frontend_cache(fe);
	}
	if (status_nr < 0) {
		dev_err(&state->i2c->dev,
			"%s: Can't read frontend lock status\n", __func__);
		goto error;
	}

	/* Get signal strength */
	rc = mb86a20s_read_signal_strength(fe);
	if (rc < 0) {
		dev_err(&state->i2c->dev,
			"%s: Can't reset VBER registers.\n", __func__);
		mb86a20s_stats_not_ready(fe);
		mb86a20s_reset_frontend_cache(fe);

		rc = 0;		/* Status is OK */
		goto error;
	}

	if (status_nr >= 7) {
		/* Get TMCC info*/
		rc = mb86a20s_get_frontend(fe);
		if (rc < 0) {
			dev_err(&state->i2c->dev,
				"%s: Can't get FE TMCC data.\n", __func__);
			rc = 0;		/* Status is OK */
			goto error;
		}

		/* Get statistics */
		rc = mb86a20s_get_stats(fe, status_nr);
		if (rc < 0 && rc != -EBUSY) {
			dev_err(&state->i2c->dev,
				"%s: Can't get FE statistics.\n", __func__);
			rc = 0;
			goto error;
		}
		rc = 0;	/* Don't return EBUSY to userspace */
	}
	goto ok;

error:
	mb86a20s_stats_not_ready(fe);

ok:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	return rc;
}

static int mb86a20s_read_signal_strength_from_cache(struct dvb_frontend *fe,
						    u16 *strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;


	*strength = c->strength.stat[0].uvalue;

	return 0;
}

static int mb86a20s_get_frontend_dummy(struct dvb_frontend *fe)
{
	/*
	 * get_frontend is now handled together with other stats
	 * retrival, when read_status() is called, as some statistics
	 * will depend on the layers detection.
	 */
	return 0;
};

static int mb86a20s_tune(struct dvb_frontend *fe,
			bool re_tune,
			unsigned int mode_flags,
			unsigned int *delay,
			fe_status_t *status)
{
	struct mb86a20s_state *state = fe->demodulator_priv;
	int rc = 0;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	if (re_tune)
		rc = mb86a20s_set_frontend(fe);

	if (!(mode_flags & FE_TUNE_MODE_ONESHOT))
		mb86a20s_read_status_and_stats(fe, status);

	return rc;
}

static void mb86a20s_release(struct dvb_frontend *fe)
{
	struct mb86a20s_state *state = fe->demodulator_priv;

	dev_dbg(&state->i2c->dev, "%s called.\n", __func__);

	kfree(state);
}

static struct dvb_frontend_ops mb86a20s_ops;

struct dvb_frontend *mb86a20s_attach(const struct mb86a20s_config *config,
				    struct i2c_adapter *i2c)
{
	struct mb86a20s_state *state;
	u8	rev;

	dev_dbg(&i2c->dev, "%s called.\n", __func__);

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct mb86a20s_state), GFP_KERNEL);
	if (state == NULL) {
		dev_err(&i2c->dev,
			"%s: unable to allocate memory for state\n", __func__);
		goto error;
	}

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &mb86a20s_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	/* Check if it is a mb86a20s frontend */
	rev = mb86a20s_readreg(state, 0);

	if (rev == 0x13) {
		dev_info(&i2c->dev,
			 "Detected a Fujitsu mb86a20s frontend\n");
	} else {
		dev_dbg(&i2c->dev,
			"Frontend revision %d is unknown - aborting.\n",
		       rev);
		goto error;
	}

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(mb86a20s_attach);

static struct dvb_frontend_ops mb86a20s_ops = {
	.delsys = { SYS_ISDBT },
	/* Use dib8000 values per default */
	.info = {
		.name = "Fujitsu mb86A20s",
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_RECOVER |
			FE_CAN_FEC_1_2  | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6  | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK     | FE_CAN_QAM_16  | FE_CAN_QAM_64 |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_QAM_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO    | FE_CAN_HIERARCHY_AUTO,
		/* Actually, those values depend on the used tuner */
		.frequency_min = 45000000,
		.frequency_max = 864000000,
		.frequency_stepsize = 62500,
	},

	.release = mb86a20s_release,

	.init = mb86a20s_initfe,
	.set_frontend = mb86a20s_set_frontend,
	.get_frontend = mb86a20s_get_frontend_dummy,
	.read_status = mb86a20s_read_status_and_stats,
	.read_signal_strength = mb86a20s_read_signal_strength_from_cache,
	.tune = mb86a20s_tune,
};

MODULE_DESCRIPTION("DVB Frontend module for Fujitsu mb86A20s hardware");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_LICENSE("GPL");
