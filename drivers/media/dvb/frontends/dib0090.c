/*
 * Linux-DVB Driver for DiBcom's DiB0090 base-band RF Tuner.
 *
 * Copyright (C) 2005-9 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * This code is more or less generated from another driver, please
 * excuse some codingstyle oddities.
 *
 */

#include <linux/kernel.h>
#include <linux/i2c.h>

#include "dvb_frontend.h"

#include "dib0090.h"
#include "dibx000_common.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { \
	if (debug) { \
		printk(KERN_DEBUG "DiB0090: "); \
		printk(args); \
		printk("\n"); \
	} \
} while (0)

#define CONFIG_SYS_ISDBT
#define CONFIG_BAND_CBAND
#define CONFIG_BAND_VHF
#define CONFIG_BAND_UHF
#define CONFIG_DIB0090_USE_PWM_AGC

#define EN_LNA0      0x8000
#define EN_LNA1      0x4000
#define EN_LNA2      0x2000
#define EN_LNA3      0x1000
#define EN_MIX0      0x0800
#define EN_MIX1      0x0400
#define EN_MIX2      0x0200
#define EN_MIX3      0x0100
#define EN_IQADC     0x0040
#define EN_PLL       0x0020
#define EN_TX        0x0010
#define EN_BB        0x0008
#define EN_LO        0x0004
#define EN_BIAS      0x0001

#define EN_IQANA     0x0002
#define EN_DIGCLK    0x0080	/* not in the 0x24 reg, only in 0x1b */
#define EN_CRYSTAL   0x0002

#define EN_UHF		 0x22E9
#define EN_VHF		 0x44E9
#define EN_LBD		 0x11E9
#define EN_SBD		 0x44E9
#define EN_CAB		 0x88E9

#define pgm_read_word(w) (*w)

struct dc_calibration;

struct dib0090_tuning {
	u32 max_freq;		/* for every frequency less than or equal to that field: this information is correct */
	u8 switch_trim;
	u8 lna_tune;
	u8 lna_bias;
	u16 v2i;
	u16 mix;
	u16 load;
	u16 tuner_enable;
};

struct dib0090_pll {
	u32 max_freq;		/* for every frequency less than or equal to that field: this information is correct */
	u8 vco_band;
	u8 hfdiv_code;
	u8 hfdiv;
	u8 topresc;
};

struct dib0090_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend *fe;
	const struct dib0090_config *config;

	u8 current_band;
	u16 revision;
	enum frontend_tune_state tune_state;
	u32 current_rf;

	u16 wbd_offset;
	s16 wbd_target;		/* in dB */

	s16 rf_gain_limit;	/* take-over-point: where to split between bb and rf gain */
	s16 current_gain;	/* keeps the currently programmed gain */
	u8 agc_step;		/* new binary search */

	u16 gain[2];		/* for channel monitoring */

	const u16 *rf_ramp;
	const u16 *bb_ramp;

	/* for the software AGC ramps */
	u16 bb_1_def;
	u16 rf_lt_def;
	u16 gain_reg[4];

	/* for the captrim/dc-offset search */
	s8 step;
	s16 adc_diff;
	s16 min_adc_diff;

	s8 captrim;
	s8 fcaptrim;

	const struct dc_calibration *dc;
	u16 bb6, bb7;

	const struct dib0090_tuning *current_tune_table_index;
	const struct dib0090_pll *current_pll_table_index;

	u8 tuner_is_tuned;
	u8 agc_freeze;

	u8 reset;
};

static u16 dib0090_read_reg(struct dib0090_state *state, u8 reg)
{
	u8 b[2];
	struct i2c_msg msg[2] = {
		{.addr = state->config->i2c_address, .flags = 0, .buf = &reg, .len = 1},
		{.addr = state->config->i2c_address, .flags = I2C_M_RD, .buf = b, .len = 2},
	};
	if (i2c_transfer(state->i2c, msg, 2) != 2) {
		printk(KERN_WARNING "DiB0090 I2C read failed\n");
		return 0;
	}
	return (b[0] << 8) | b[1];
}

static int dib0090_write_reg(struct dib0090_state *state, u32 reg, u16 val)
{
	u8 b[3] = { reg & 0xff, val >> 8, val & 0xff };
	struct i2c_msg msg = {.addr = state->config->i2c_address, .flags = 0, .buf = b, .len = 3 };
	if (i2c_transfer(state->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "DiB0090 I2C write failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

#define HARD_RESET(state) do {  if (cfg->reset) {  if (cfg->sleep) cfg->sleep(fe, 0); msleep(10);  cfg->reset(fe, 1); msleep(10);  cfg->reset(fe, 0); msleep(10);  }  } while (0)
#define ADC_TARGET -220
#define GAIN_ALPHA 5
#define WBD_ALPHA 6
#define LPF	100
static void dib0090_write_regs(struct dib0090_state *state, u8 r, const u16 * b, u8 c)
{
	do {
		dib0090_write_reg(state, r++, *b++);
	} while (--c);
}

static u16 dib0090_identify(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	u16 v;

	v = dib0090_read_reg(state, 0x1a);

#ifdef FIRMWARE_FIREFLY
	/* pll is not locked locked */
	if (!(v & 0x800))
		dprintk("FE%d : Identification : pll is not yet locked", fe->id);
#endif

	/* without PLL lock info */
	v &= 0x3ff;
	dprintk("P/V: %04x:", v);

	if ((v >> 8) & 0xf)
		dprintk("FE%d : Product ID = 0x%x : KROSUS", fe->id, (v >> 8) & 0xf);
	else
		return 0xff;

	v &= 0xff;
	if (((v >> 5) & 0x7) == 0x1)
		dprintk("FE%d : MP001 : 9090/8096", fe->id);
	else if (((v >> 5) & 0x7) == 0x4)
		dprintk("FE%d : MP005 : Single Sband", fe->id);
	else if (((v >> 5) & 0x7) == 0x6)
		dprintk("FE%d : MP008 : diversity VHF-UHF-LBAND", fe->id);
	else if (((v >> 5) & 0x7) == 0x7)
		dprintk("FE%d : MP009 : diversity 29098 CBAND-UHF-LBAND-SBAND", fe->id);
	else
		return 0xff;

	/* revision only */
	if ((v & 0x1f) == 0x3)
		dprintk("FE%d : P1-D/E/F detected", fe->id);
	else if ((v & 0x1f) == 0x1)
		dprintk("FE%d : P1C detected", fe->id);
	else if ((v & 0x1f) == 0x0) {
#ifdef CONFIG_TUNER_DIB0090_P1B_SUPPORT
		dprintk("FE%d : P1-A/B detected: using previous driver - support will be removed soon", fe->id);
		dib0090_p1b_register(fe);
#else
		dprintk("FE%d : P1-A/B detected: driver is deactivated - not available", fe->id);
		return 0xff;
#endif
	}

	return v;
}

static void dib0090_reset_digital(struct dvb_frontend *fe, const struct dib0090_config *cfg)
{
	struct dib0090_state *state = fe->tuner_priv;

	HARD_RESET(state);

	dib0090_write_reg(state, 0x24, EN_PLL);
	dib0090_write_reg(state, 0x1b, EN_DIGCLK | EN_PLL | EN_CRYSTAL);	/* PLL, DIG_CLK and CRYSTAL remain */

	/* adcClkOutRatio=8->7, release reset */
	dib0090_write_reg(state, 0x20, ((cfg->io.adc_clock_ratio - 1) << 11) | (0 << 10) | (1 << 9) | (1 << 8) | (0 << 4) | 0);
	if (cfg->clkoutdrive != 0)
		dib0090_write_reg(state, 0x23,
				  (0 << 15) | ((!cfg->analog_output) << 14) | (1 << 10) | (1 << 9) | (0 << 8) | (cfg->clkoutdrive << 5) | (cfg->
																	   clkouttobamse
																	   << 4) | (0
																		    <<
																		    2)
				  | (0));
	else
		dib0090_write_reg(state, 0x23,
				  (0 << 15) | ((!cfg->analog_output) << 14) | (1 << 10) | (1 << 9) | (0 << 8) | (7 << 5) | (cfg->
															    clkouttobamse << 4) | (0
																		   <<
																		   2)
				  | (0));

	/* enable pll, de-activate reset, ratio: 2/1 = 60MHz */
	dib0090_write_reg(state, 0x21,
			  (cfg->io.pll_bypass << 15) | (1 << 13) | (cfg->io.pll_range << 12) | (cfg->io.pll_loopdiv << 6) | (cfg->io.pll_prediv));

}

static int dib0090_wakeup(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	if (state->config->sleep)
		state->config->sleep(fe, 0);
	return 0;
}

static int dib0090_sleep(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	if (state->config->sleep)
		state->config->sleep(fe, 1);
	return 0;
}

extern void dib0090_dcc_freq(struct dvb_frontend *fe, u8 fast)
{
	struct dib0090_state *state = fe->tuner_priv;
	if (fast)
		dib0090_write_reg(state, 0x04, 0);
	else
		dib0090_write_reg(state, 0x04, 1);
}
EXPORT_SYMBOL(dib0090_dcc_freq);

static const u16 rf_ramp_pwm_cband[] = {
	0,			/* max RF gain in 10th of dB */
	0,			/* ramp_slope = 1dB of gain -> clock_ticks_per_db = clk_khz / ramp_slope -> 0x2b */
	0,			/* ramp_max = maximum X used on the ramp */
	(0 << 10) | 0,		/* 0x2c, LNA 1 = 0dB */
	(0 << 10) | 0,		/* 0x2d, LNA 1 */
	(0 << 10) | 0,		/* 0x2e, LNA 2 = 0dB */
	(0 << 10) | 0,		/* 0x2f, LNA 2 */
	(0 << 10) | 0,		/* 0x30, LNA 3 = 0dB */
	(0 << 10) | 0,		/* 0x31, LNA 3 */
	(0 << 10) | 0,		/* GAIN_4_1, LNA 4 = 0dB */
	(0 << 10) | 0,		/* GAIN_4_2, LNA 4 */
};

static const u16 rf_ramp_vhf[] = {
	412,			/* max RF gain in 10th of dB */
	132, 307, 127,		/* LNA1,  13.2dB */
	105, 412, 255,		/* LNA2,  10.5dB */
	50, 50, 127,		/* LNA3,  5dB */
	125, 175, 127,		/* LNA4,  12.5dB */
	0, 0, 127,		/* CBAND, 0dB */
};

static const u16 rf_ramp_uhf[] = {
	412,			/* max RF gain in 10th of dB */
	132, 307, 127,		/* LNA1  : total gain = 13.2dB, point on the ramp where this amp is full gain, value to write to get full gain */
	105, 412, 255,		/* LNA2  : 10.5 dB */
	50, 50, 127,		/* LNA3  :  5.0 dB */
	125, 175, 127,		/* LNA4  : 12.5 dB */
	0, 0, 127,		/* CBAND :  0.0 dB */
};

static const u16 rf_ramp_cband[] = {
	332,			/* max RF gain in 10th of dB */
	132, 252, 127,		/* LNA1,  dB */
	80, 332, 255,		/* LNA2,  dB */
	0, 0, 127,		/* LNA3,  dB */
	0, 0, 127,		/* LNA4,  dB */
	120, 120, 127,		/* LT1 CBAND */
};

static const u16 rf_ramp_pwm_vhf[] = {
	404,			/* max RF gain in 10th of dB */
	25,			/* ramp_slope = 1dB of gain -> clock_ticks_per_db = clk_khz / ramp_slope -> 0x2b */
	1011,			/* ramp_max = maximum X used on the ramp */
	(6 << 10) | 417,	/* 0x2c, LNA 1 = 13.2dB */
	(0 << 10) | 756,	/* 0x2d, LNA 1 */
	(16 << 10) | 756,	/* 0x2e, LNA 2 = 10.5dB */
	(0 << 10) | 1011,	/* 0x2f, LNA 2 */
	(16 << 10) | 290,	/* 0x30, LNA 3 = 5dB */
	(0 << 10) | 417,	/* 0x31, LNA 3 */
	(7 << 10) | 0,		/* GAIN_4_1, LNA 4 = 12.5dB */
	(0 << 10) | 290,	/* GAIN_4_2, LNA 4 */
};

static const u16 rf_ramp_pwm_uhf[] = {
	404,			/* max RF gain in 10th of dB */
	25,			/* ramp_slope = 1dB of gain -> clock_ticks_per_db = clk_khz / ramp_slope -> 0x2b */
	1011,			/* ramp_max = maximum X used on the ramp */
	(6 << 10) | 417,	/* 0x2c, LNA 1 = 13.2dB */
	(0 << 10) | 756,	/* 0x2d, LNA 1 */
	(16 << 10) | 756,	/* 0x2e, LNA 2 = 10.5dB */
	(0 << 10) | 1011,	/* 0x2f, LNA 2 */
	(16 << 10) | 0,		/* 0x30, LNA 3 = 5dB */
	(0 << 10) | 127,	/* 0x31, LNA 3 */
	(7 << 10) | 127,	/* GAIN_4_1, LNA 4 = 12.5dB */
	(0 << 10) | 417,	/* GAIN_4_2, LNA 4 */
};

static const u16 bb_ramp_boost[] = {
	550,			/* max BB gain in 10th of dB */
	260, 260, 26,		/* BB1, 26dB */
	290, 550, 29,		/* BB2, 29dB */
};

static const u16 bb_ramp_pwm_normal[] = {
	500,			/* max RF gain in 10th of dB */
	8,			/* ramp_slope = 1dB of gain -> clock_ticks_per_db = clk_khz / ramp_slope -> 0x34 */
	400,
	(2 << 9) | 0,		/* 0x35 = 21dB */
	(0 << 9) | 168,		/* 0x36 */
	(2 << 9) | 168,		/* 0x37 = 29dB */
	(0 << 9) | 400,		/* 0x38 */
};

struct slope {
	int16_t range;
	int16_t slope;
};
static u16 slopes_to_scale(const struct slope *slopes, u8 num, s16 val)
{
	u8 i;
	u16 rest;
	u16 ret = 0;
	for (i = 0; i < num; i++) {
		if (val > slopes[i].range)
			rest = slopes[i].range;
		else
			rest = val;
		ret += (rest * slopes[i].slope) / slopes[i].range;
		val -= rest;
	}
	return ret;
}

static const struct slope dib0090_wbd_slopes[3] = {
	{66, 120},		/* -64,-52: offset -   65 */
	{600, 170},		/* -52,-35: 65     -  665 */
	{170, 250},		/* -45,-10: 665    - 835 */
};

static s16 dib0090_wbd_to_db(struct dib0090_state *state, u16 wbd)
{
	wbd &= 0x3ff;
	if (wbd < state->wbd_offset)
		wbd = 0;
	else
		wbd -= state->wbd_offset;
	/* -64dB is the floor */
	return -640 + (s16) slopes_to_scale(dib0090_wbd_slopes, ARRAY_SIZE(dib0090_wbd_slopes), wbd);
}

static void dib0090_wbd_target(struct dib0090_state *state, u32 rf)
{
	u16 offset = 250;

	/* TODO : DAB digital N+/-1 interferer perfs : offset = 10 */

	if (state->current_band == BAND_VHF)
		offset = 650;
#ifndef FIRMWARE_FIREFLY
	if (state->current_band == BAND_VHF)
		offset = state->config->wbd_vhf_offset;
	if (state->current_band == BAND_CBAND)
		offset = state->config->wbd_cband_offset;
#endif

	state->wbd_target = dib0090_wbd_to_db(state, state->wbd_offset + offset);
	dprintk("wbd-target: %d dB", (u32) state->wbd_target);
}

static const int gain_reg_addr[4] = {
	0x08, 0x0a, 0x0f, 0x01
};

static void dib0090_gain_apply(struct dib0090_state *state, s16 gain_delta, s16 top_delta, u8 force)
{
	u16 rf, bb, ref;
	u16 i, v, gain_reg[4] = { 0 }, gain;
	const u16 *g;

	if (top_delta < -511)
		top_delta = -511;
	if (top_delta > 511)
		top_delta = 511;

	if (force) {
		top_delta *= (1 << WBD_ALPHA);
		gain_delta *= (1 << GAIN_ALPHA);
	}

	if (top_delta >= ((s16) (state->rf_ramp[0] << WBD_ALPHA) - state->rf_gain_limit))	/* overflow */
		state->rf_gain_limit = state->rf_ramp[0] << WBD_ALPHA;
	else
		state->rf_gain_limit += top_delta;

	if (state->rf_gain_limit < 0)	/*underflow */
		state->rf_gain_limit = 0;

	/* use gain as a temporary variable and correct current_gain */
	gain = ((state->rf_gain_limit >> WBD_ALPHA) + state->bb_ramp[0]) << GAIN_ALPHA;
	if (gain_delta >= ((s16) gain - state->current_gain))	/* overflow */
		state->current_gain = gain;
	else
		state->current_gain += gain_delta;
	/* cannot be less than 0 (only if gain_delta is less than 0 we can have current_gain < 0) */
	if (state->current_gain < 0)
		state->current_gain = 0;

	/* now split total gain to rf and bb gain */
	gain = state->current_gain >> GAIN_ALPHA;

	/* requested gain is bigger than rf gain limit - ACI/WBD adjustment */
	if (gain > (state->rf_gain_limit >> WBD_ALPHA)) {
		rf = state->rf_gain_limit >> WBD_ALPHA;
		bb = gain - rf;
		if (bb > state->bb_ramp[0])
			bb = state->bb_ramp[0];
	} else {		/* high signal level -> all gains put on RF */
		rf = gain;
		bb = 0;
	}

	state->gain[0] = rf;
	state->gain[1] = bb;

	/* software ramp */
	/* Start with RF gains */
	g = state->rf_ramp + 1;	/* point on RF LNA1 max gain */
	ref = rf;
	for (i = 0; i < 7; i++) {	/* Go over all amplifiers => 5RF amps + 2 BB amps = 7 amps */
		if (g[0] == 0 || ref < (g[1] - g[0]))	/* if total gain of the current amp is null or this amp is not concerned because it starts to work from an higher gain value */
			v = 0;	/* force the gain to write for the current amp to be null */
		else if (ref >= g[1])	/* Gain to set is higher than the high working point of this amp */
			v = g[2];	/* force this amp to be full gain */
		else		/* compute the value to set to this amp because we are somewhere in his range */
			v = ((ref - (g[1] - g[0])) * g[2]) / g[0];

		if (i == 0)	/* LNA 1 reg mapping */
			gain_reg[0] = v;
		else if (i == 1)	/* LNA 2 reg mapping */
			gain_reg[0] |= v << 7;
		else if (i == 2)	/* LNA 3 reg mapping */
			gain_reg[1] = v;
		else if (i == 3)	/* LNA 4 reg mapping */
			gain_reg[1] |= v << 7;
		else if (i == 4)	/* CBAND LNA reg mapping */
			gain_reg[2] = v | state->rf_lt_def;
		else if (i == 5)	/* BB gain 1 reg mapping */
			gain_reg[3] = v << 3;
		else if (i == 6)	/* BB gain 2 reg mapping */
			gain_reg[3] |= v << 8;

		g += 3;		/* go to next gain bloc */

		/* When RF is finished, start with BB */
		if (i == 4) {
			g = state->bb_ramp + 1;	/* point on BB gain 1 max gain */
			ref = bb;
		}
	}
	gain_reg[3] |= state->bb_1_def;
	gain_reg[3] |= ((bb % 10) * 100) / 125;

#ifdef DEBUG_AGC
	dprintk("GA CALC: DB: %3d(rf) + %3d(bb) = %3d gain_reg[0]=%04x gain_reg[1]=%04x gain_reg[2]=%04x gain_reg[0]=%04x", rf, bb, rf + bb,
		gain_reg[0], gain_reg[1], gain_reg[2], gain_reg[3]);
#endif

	/* Write the amplifier regs */
	for (i = 0; i < 4; i++) {
		v = gain_reg[i];
		if (force || state->gain_reg[i] != v) {
			state->gain_reg[i] = v;
			dib0090_write_reg(state, gain_reg_addr[i], v);
		}
	}
}

static void dib0090_set_boost(struct dib0090_state *state, int onoff)
{
	state->bb_1_def &= 0xdfff;
	state->bb_1_def |= onoff << 13;
}

static void dib0090_set_rframp(struct dib0090_state *state, const u16 * cfg)
{
	state->rf_ramp = cfg;
}

static void dib0090_set_rframp_pwm(struct dib0090_state *state, const u16 * cfg)
{
	state->rf_ramp = cfg;

	dib0090_write_reg(state, 0x2a, 0xffff);

	dprintk("total RF gain: %ddB, step: %d", (u32) cfg[0], dib0090_read_reg(state, 0x2a));

	dib0090_write_regs(state, 0x2c, cfg + 3, 6);
	dib0090_write_regs(state, 0x3e, cfg + 9, 2);
}

static void dib0090_set_bbramp(struct dib0090_state *state, const u16 * cfg)
{
	state->bb_ramp = cfg;
	dib0090_set_boost(state, cfg[0] > 500);	/* we want the boost if the gain is higher that 50dB */
}

static void dib0090_set_bbramp_pwm(struct dib0090_state *state, const u16 * cfg)
{
	state->bb_ramp = cfg;

	dib0090_set_boost(state, cfg[0] > 500);	/* we want the boost if the gain is higher that 50dB */

	dib0090_write_reg(state, 0x33, 0xffff);
	dprintk("total BB gain: %ddB, step: %d", (u32) cfg[0], dib0090_read_reg(state, 0x33));
	dib0090_write_regs(state, 0x35, cfg + 3, 4);
}

void dib0090_pwm_gain_reset(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	/* reset the AGC */

	if (state->config->use_pwm_agc) {
#ifdef CONFIG_BAND_SBAND
		if (state->current_band == BAND_SBAND) {
			dib0090_set_rframp_pwm(state, rf_ramp_pwm_sband);
			dib0090_set_bbramp_pwm(state, bb_ramp_pwm_boost);
		} else
#endif
#ifdef CONFIG_BAND_CBAND
		if (state->current_band == BAND_CBAND) {
			dib0090_set_rframp_pwm(state, rf_ramp_pwm_cband);
			dib0090_set_bbramp_pwm(state, bb_ramp_pwm_normal);
		} else
#endif
#ifdef CONFIG_BAND_VHF
		if (state->current_band == BAND_VHF) {
			dib0090_set_rframp_pwm(state, rf_ramp_pwm_vhf);
			dib0090_set_bbramp_pwm(state, bb_ramp_pwm_normal);
		} else
#endif
		{
			dib0090_set_rframp_pwm(state, rf_ramp_pwm_uhf);
			dib0090_set_bbramp_pwm(state, bb_ramp_pwm_normal);
		}

		if (state->rf_ramp[0] != 0)
			dib0090_write_reg(state, 0x32, (3 << 11));
		else
			dib0090_write_reg(state, 0x32, (0 << 11));

		dib0090_write_reg(state, 0x39, (1 << 10));
	}
}
EXPORT_SYMBOL(dib0090_pwm_gain_reset);

int dib0090_gain_control(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	enum frontend_tune_state *tune_state = &state->tune_state;
	int ret = 10;

	u16 wbd_val = 0;
	u8 apply_gain_immediatly = 1;
	s16 wbd_error = 0, adc_error = 0;

	if (*tune_state == CT_AGC_START) {
		state->agc_freeze = 0;
		dib0090_write_reg(state, 0x04, 0x0);

#ifdef CONFIG_BAND_SBAND
		if (state->current_band == BAND_SBAND) {
			dib0090_set_rframp(state, rf_ramp_sband);
			dib0090_set_bbramp(state, bb_ramp_boost);
		} else
#endif
#ifdef CONFIG_BAND_VHF
		if (state->current_band == BAND_VHF) {
			dib0090_set_rframp(state, rf_ramp_vhf);
			dib0090_set_bbramp(state, bb_ramp_boost);
		} else
#endif
#ifdef CONFIG_BAND_CBAND
		if (state->current_band == BAND_CBAND) {
			dib0090_set_rframp(state, rf_ramp_cband);
			dib0090_set_bbramp(state, bb_ramp_boost);
		} else
#endif
		{
			dib0090_set_rframp(state, rf_ramp_uhf);
			dib0090_set_bbramp(state, bb_ramp_boost);
		}

		dib0090_write_reg(state, 0x32, 0);
		dib0090_write_reg(state, 0x39, 0);

		dib0090_wbd_target(state, state->current_rf);

		state->rf_gain_limit = state->rf_ramp[0] << WBD_ALPHA;
		state->current_gain = ((state->rf_ramp[0] + state->bb_ramp[0]) / 2) << GAIN_ALPHA;

		*tune_state = CT_AGC_STEP_0;
	} else if (!state->agc_freeze) {
		s16 wbd;

		int adc;
		wbd_val = dib0090_read_reg(state, 0x1d);

		/* read and calc the wbd power */
		wbd = dib0090_wbd_to_db(state, wbd_val);
		wbd_error = state->wbd_target - wbd;

		if (*tune_state == CT_AGC_STEP_0) {
			if (wbd_error < 0 && state->rf_gain_limit > 0) {
#ifdef CONFIG_BAND_CBAND
				/* in case of CBAND tune reduce first the lt_gain2 before adjusting the RF gain */
				u8 ltg2 = (state->rf_lt_def >> 10) & 0x7;
				if (state->current_band == BAND_CBAND && ltg2) {
					ltg2 >>= 1;
					state->rf_lt_def &= ltg2 << 10;	/* reduce in 3 steps from 7 to 0 */
				}
#endif
			} else {
				state->agc_step = 0;
				*tune_state = CT_AGC_STEP_1;
			}
		} else {
			/* calc the adc power */
			adc = state->config->get_adc_power(fe);
			adc = (adc * ((s32) 355774) + (((s32) 1) << 20)) >> 21;	/* included in [0:-700] */

			adc_error = (s16) (((s32) ADC_TARGET) - adc);
#ifdef CONFIG_STANDARD_DAB
			if (state->fe->dtv_property_cache.delivery_system == STANDARD_DAB)
				adc_error += 130;
#endif
#ifdef CONFIG_STANDARD_DVBT
			if (state->fe->dtv_property_cache.delivery_system == STANDARD_DVBT &&
			    (state->fe->dtv_property_cache.modulation == QAM_64 || state->fe->dtv_property_cache.modulation == QAM_16))
				adc_error += 60;
#endif
#ifdef CONFIG_SYS_ISDBT
			if ((state->fe->dtv_property_cache.delivery_system == SYS_ISDBT) && (((state->fe->dtv_property_cache.layer[0].segment_count >
											       0)
											      &&
											      ((state->fe->dtv_property_cache.layer[0].modulation ==
												QAM_64)
											       || (state->fe->dtv_property_cache.layer[0].
												   modulation == QAM_16)))
											     ||
											     ((state->fe->dtv_property_cache.layer[1].segment_count >
											       0)
											      &&
											      ((state->fe->dtv_property_cache.layer[1].modulation ==
												QAM_64)
											       || (state->fe->dtv_property_cache.layer[1].
												   modulation == QAM_16)))
											     ||
											     ((state->fe->dtv_property_cache.layer[2].segment_count >
											       0)
											      &&
											      ((state->fe->dtv_property_cache.layer[2].modulation ==
												QAM_64)
											       || (state->fe->dtv_property_cache.layer[2].
												   modulation == QAM_16)))
			    )
			    )
				adc_error += 60;
#endif

			if (*tune_state == CT_AGC_STEP_1) {	/* quickly go to the correct range of the ADC power */
				if (ABS(adc_error) < 50 || state->agc_step++ > 5) {

#ifdef CONFIG_STANDARD_DAB
					if (state->fe->dtv_property_cache.delivery_system == STANDARD_DAB) {
						dib0090_write_reg(state, 0x02, (1 << 15) | (15 << 11) | (31 << 6) | (63));	/* cap value = 63 : narrow BB filter : Fc = 1.8MHz */
						dib0090_write_reg(state, 0x04, 0x0);
					} else
#endif
					{
						dib0090_write_reg(state, 0x02, (1 << 15) | (3 << 11) | (6 << 6) | (32));
						dib0090_write_reg(state, 0x04, 0x01);	/*0 = 1KHz ; 1 = 150Hz ; 2 = 50Hz ; 3 = 50KHz ; 4 = servo fast */
					}

					*tune_state = CT_AGC_STOP;
				}
			} else {
				/* everything higher than or equal to CT_AGC_STOP means tracking */
				ret = 100;	/* 10ms interval */
				apply_gain_immediatly = 0;
			}
		}
#ifdef DEBUG_AGC
		dprintk
		    ("FE: %d, tune state %d, ADC = %3ddB (ADC err %3d) WBD %3ddB (WBD err %3d, WBD val SADC: %4d), RFGainLimit (TOP): %3d, signal: %3ddBm",
		     (u32) fe->id, (u32) *tune_state, (u32) adc, (u32) adc_error, (u32) wbd, (u32) wbd_error, (u32) wbd_val,
		     (u32) state->rf_gain_limit >> WBD_ALPHA, (s32) 200 + adc - (state->current_gain >> GAIN_ALPHA));
#endif
	}

	/* apply gain */
	if (!state->agc_freeze)
		dib0090_gain_apply(state, adc_error, wbd_error, apply_gain_immediatly);
	return ret;
}
EXPORT_SYMBOL(dib0090_gain_control);

void dib0090_get_current_gain(struct dvb_frontend *fe, u16 * rf, u16 * bb, u16 * rf_gain_limit, u16 * rflt)
{
	struct dib0090_state *state = fe->tuner_priv;
	if (rf)
		*rf = state->gain[0];
	if (bb)
		*bb = state->gain[1];
	if (rf_gain_limit)
		*rf_gain_limit = state->rf_gain_limit;
	if (rflt)
		*rflt = (state->rf_lt_def >> 10) & 0x7;
}
EXPORT_SYMBOL(dib0090_get_current_gain);

u16 dib0090_get_wbd_offset(struct dvb_frontend *tuner)
{
	struct dib0090_state *st = tuner->tuner_priv;
	return st->wbd_offset;
}
EXPORT_SYMBOL(dib0090_get_wbd_offset);

static const u16 dib0090_defaults[] = {

	25, 0x01,
	0x0000,
	0x99a0,
	0x6008,
	0x0000,
	0x8acb,
	0x0000,
	0x0405,
	0x0000,
	0x0000,
	0x0000,
	0xb802,
	0x0300,
	0x2d12,
	0xbac0,
	0x7c00,
	0xdbb9,
	0x0954,
	0x0743,
	0x8000,
	0x0001,
	0x0040,
	0x0100,
	0x0000,
	0xe910,
	0x149e,

	1, 0x1c,
	0xff2d,

	1, 0x39,
	0x0000,

	1, 0x1b,
	EN_IQADC | EN_BB | EN_BIAS | EN_DIGCLK | EN_PLL | EN_CRYSTAL,
	2, 0x1e,
	0x07FF,
	0x0007,

	1, 0x24,
	EN_UHF | EN_CRYSTAL,

	2, 0x3c,
	0x3ff,
	0x111,
	0
};

static int dib0090_reset(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	u16 l, r, *n;

	dib0090_reset_digital(fe, state->config);
	state->revision = dib0090_identify(fe);

	/* Revision definition */
	if (state->revision == 0xff)
		return -EINVAL;
#ifdef EFUSE
	else if ((state->revision & 0x1f) >= 3)	/* Update the efuse : Only available for KROSUS > P1C */
		dib0090_set_EFUSE(state);
#endif

#ifdef CONFIG_TUNER_DIB0090_P1B_SUPPORT
	if (!(state->revision & 0x1))	/* it is P1B - reset is already done */
		return 0;
#endif

	/* Upload the default values */
	n = (u16 *) dib0090_defaults;
	l = pgm_read_word(n++);
	while (l) {
		r = pgm_read_word(n++);
		do {
			/* DEBUG_TUNER */
			/* dprintk("%d, %d, %d", l, r, pgm_read_word(n)); */
			dib0090_write_reg(state, r, pgm_read_word(n++));
			r++;
		} while (--l);
		l = pgm_read_word(n++);
	}

	/* Congigure in function of the crystal */
	if (state->config->io.clock_khz >= 24000)
		l = 1;
	else
		l = 2;
	dib0090_write_reg(state, 0x14, l);
	dprintk("Pll lock : %d", (dib0090_read_reg(state, 0x1a) >> 11) & 0x1);

	state->reset = 3;	/* enable iq-offset-calibration and wbd-calibration when tuning next time */

	return 0;
}

#define steps(u) (((u) > 15) ? ((u)-16) : (u))
#define INTERN_WAIT 10
static int dib0090_get_offset(struct dib0090_state *state, enum frontend_tune_state *tune_state)
{
	int ret = INTERN_WAIT * 10;

	switch (*tune_state) {
	case CT_TUNER_STEP_2:
		/* Turns to positive */
		dib0090_write_reg(state, 0x1f, 0x7);
		*tune_state = CT_TUNER_STEP_3;
		break;

	case CT_TUNER_STEP_3:
		state->adc_diff = dib0090_read_reg(state, 0x1d);

		/* Turns to negative */
		dib0090_write_reg(state, 0x1f, 0x4);
		*tune_state = CT_TUNER_STEP_4;
		break;

	case CT_TUNER_STEP_4:
		state->adc_diff -= dib0090_read_reg(state, 0x1d);
		*tune_state = CT_TUNER_STEP_5;
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

struct dc_calibration {
	uint8_t addr;
	uint8_t offset;
	uint8_t pga:1;
	uint16_t bb1;
	uint8_t i:1;
};

static const struct dc_calibration dc_table[] = {
	/* Step1 BB gain1= 26 with boost 1, gain 2 = 0 */
	{0x06, 5, 1, (1 << 13) | (0 << 8) | (26 << 3), 1},
	{0x07, 11, 1, (1 << 13) | (0 << 8) | (26 << 3), 0},
	/* Step 2 BB gain 1 = 26 with boost = 1 & gain 2 = 29 */
	{0x06, 0, 0, (1 << 13) | (29 << 8) | (26 << 3), 1},
	{0x06, 10, 0, (1 << 13) | (29 << 8) | (26 << 3), 0},
	{0},
};

static void dib0090_set_trim(struct dib0090_state *state)
{
	u16 *val;

	if (state->dc->addr == 0x07)
		val = &state->bb7;
	else
		val = &state->bb6;

	*val &= ~(0x1f << state->dc->offset);
	*val |= state->step << state->dc->offset;

	dib0090_write_reg(state, state->dc->addr, *val);
}

static int dib0090_dc_offset_calibration(struct dib0090_state *state, enum frontend_tune_state *tune_state)
{
	int ret = 0;

	switch (*tune_state) {

	case CT_TUNER_START:
		/* init */
		dprintk("Internal DC calibration");

		/* the LNA is off */
		dib0090_write_reg(state, 0x24, 0x02ed);

		/* force vcm2 = 0.8V */
		state->bb6 = 0;
		state->bb7 = 0x040d;

		state->dc = dc_table;

		*tune_state = CT_TUNER_STEP_0;

		/* fall through */

	case CT_TUNER_STEP_0:
		dib0090_write_reg(state, 0x01, state->dc->bb1);
		dib0090_write_reg(state, 0x07, state->bb7 | (state->dc->i << 7));

		state->step = 0;

		state->min_adc_diff = 1023;

		*tune_state = CT_TUNER_STEP_1;
		ret = 50;
		break;

	case CT_TUNER_STEP_1:
		dib0090_set_trim(state);

		*tune_state = CT_TUNER_STEP_2;
		break;

	case CT_TUNER_STEP_2:
	case CT_TUNER_STEP_3:
	case CT_TUNER_STEP_4:
		ret = dib0090_get_offset(state, tune_state);
		break;

	case CT_TUNER_STEP_5:	/* found an offset */
		dprintk("FE%d: IQC read=%d, current=%x", state->fe->id, (u32) state->adc_diff, state->step);

		/* first turn for this frequency */
		if (state->step == 0) {
			if (state->dc->pga && state->adc_diff < 0)
				state->step = 0x10;
			if (state->dc->pga == 0 && state->adc_diff > 0)
				state->step = 0x10;
		}

		state->adc_diff = ABS(state->adc_diff);

		if (state->adc_diff < state->min_adc_diff && steps(state->step) < 15) {	/* stop search when the delta to 0 is increasing */
			state->step++;
			state->min_adc_diff = state->adc_diff;
			*tune_state = CT_TUNER_STEP_1;
		} else {

			/* the minimum was what we have seen in the step before */
			state->step--;
			dib0090_set_trim(state);

			dprintk("FE%d: BB Offset Cal, BBreg=%hd,Offset=%hd,Value Set=%hd", state->fe->id, state->dc->addr, state->adc_diff,
				state->step);

			state->dc++;
			if (state->dc->addr == 0)	/* done */
				*tune_state = CT_TUNER_STEP_6;
			else
				*tune_state = CT_TUNER_STEP_0;

		}
		break;

	case CT_TUNER_STEP_6:
		dib0090_write_reg(state, 0x07, state->bb7 & ~0x0008);
		dib0090_write_reg(state, 0x1f, 0x7);
		*tune_state = CT_TUNER_START;	/* reset done -> real tuning can now begin */
		state->reset &= ~0x1;
	default:
		break;
	}
	return ret;
}

static int dib0090_wbd_calibration(struct dib0090_state *state, enum frontend_tune_state *tune_state)
{
	switch (*tune_state) {
	case CT_TUNER_START:
		/* WBD-mode=log, Bias=2, Gain=6, Testmode=1, en=1, WBDMUX=1 */
		dib0090_write_reg(state, 0x10, 0xdb09 | (1 << 10));
		dib0090_write_reg(state, 0x24, EN_UHF & 0x0fff);

		*tune_state = CT_TUNER_STEP_0;
		return 90;	/* wait for the WBDMUX to switch and for the ADC to sample */
	case CT_TUNER_STEP_0:
		state->wbd_offset = dib0090_read_reg(state, 0x1d);
		dprintk("WBD calibration offset = %d", state->wbd_offset);

		*tune_state = CT_TUNER_START;	/* reset done -> real tuning can now begin */
		state->reset &= ~0x2;
		break;
	default:
		break;
	}
	return 0;
}

static void dib0090_set_bandwidth(struct dib0090_state *state)
{
	u16 tmp;

	if (state->fe->dtv_property_cache.bandwidth_hz / 1000 <= 5000)
		tmp = (3 << 14);
	else if (state->fe->dtv_property_cache.bandwidth_hz / 1000 <= 6000)
		tmp = (2 << 14);
	else if (state->fe->dtv_property_cache.bandwidth_hz / 1000 <= 7000)
		tmp = (1 << 14);
	else
		tmp = (0 << 14);

	state->bb_1_def &= 0x3fff;
	state->bb_1_def |= tmp;

	dib0090_write_reg(state, 0x01, state->bb_1_def);	/* be sure that we have the right bb-filter */
}

static const struct dib0090_pll dib0090_pll_table[] = {
#ifdef CONFIG_BAND_CBAND
	{56000, 0, 9, 48, 6},
	{70000, 1, 9, 48, 6},
	{87000, 0, 8, 32, 4},
	{105000, 1, 8, 32, 4},
	{115000, 0, 7, 24, 6},
	{140000, 1, 7, 24, 6},
	{170000, 0, 6, 16, 4},
#endif
#ifdef CONFIG_BAND_VHF
	{200000, 1, 6, 16, 4},
	{230000, 0, 5, 12, 6},
	{280000, 1, 5, 12, 6},
	{340000, 0, 4, 8, 4},
	{380000, 1, 4, 8, 4},
	{450000, 0, 3, 6, 6},
#endif
#ifdef CONFIG_BAND_UHF
	{580000, 1, 3, 6, 6},
	{700000, 0, 2, 4, 4},
	{860000, 1, 2, 4, 4},
#endif
#ifdef CONFIG_BAND_LBAND
	{1800000, 1, 0, 2, 4},
#endif
#ifdef CONFIG_BAND_SBAND
	{2900000, 0, 14, 1, 4},
#endif
};

static const struct dib0090_tuning dib0090_tuning_table_fm_vhf_on_cband[] = {

#ifdef CONFIG_BAND_CBAND
	{184000, 4, 1, 15, 0x280, 0x2912, 0xb94e, EN_CAB},
	{227000, 4, 3, 15, 0x280, 0x2912, 0xb94e, EN_CAB},
	{380000, 4, 7, 15, 0x280, 0x2912, 0xb94e, EN_CAB},
#endif
#ifdef CONFIG_BAND_UHF
	{520000, 2, 0, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{550000, 2, 2, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{650000, 2, 3, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{750000, 2, 5, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{850000, 2, 6, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{900000, 2, 7, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
#endif
#ifdef CONFIG_BAND_LBAND
	{1500000, 4, 0, 20, 0x300, 0x1912, 0x82c9, EN_LBD},
	{1600000, 4, 1, 20, 0x300, 0x1912, 0x82c9, EN_LBD},
	{1800000, 4, 3, 20, 0x300, 0x1912, 0x82c9, EN_LBD},
#endif
#ifdef CONFIG_BAND_SBAND
	{2300000, 1, 4, 20, 0x300, 0x2d2A, 0x82c7, EN_SBD},
	{2900000, 1, 7, 20, 0x280, 0x2deb, 0x8347, EN_SBD},
#endif
};

static const struct dib0090_tuning dib0090_tuning_table[] = {

#ifdef CONFIG_BAND_CBAND
	{170000, 4, 1, 15, 0x280, 0x2912, 0xb94e, EN_CAB},
#endif
#ifdef CONFIG_BAND_VHF
	{184000, 1, 1, 15, 0x300, 0x4d12, 0xb94e, EN_VHF},
	{227000, 1, 3, 15, 0x300, 0x4d12, 0xb94e, EN_VHF},
	{380000, 1, 7, 15, 0x300, 0x4d12, 0xb94e, EN_VHF},
#endif
#ifdef CONFIG_BAND_UHF
	{520000, 2, 0, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{550000, 2, 2, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{650000, 2, 3, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{750000, 2, 5, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{850000, 2, 6, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
	{900000, 2, 7, 15, 0x300, 0x1d12, 0xb9ce, EN_UHF},
#endif
#ifdef CONFIG_BAND_LBAND
	{1500000, 4, 0, 20, 0x300, 0x1912, 0x82c9, EN_LBD},
	{1600000, 4, 1, 20, 0x300, 0x1912, 0x82c9, EN_LBD},
	{1800000, 4, 3, 20, 0x300, 0x1912, 0x82c9, EN_LBD},
#endif
#ifdef CONFIG_BAND_SBAND
	{2300000, 1, 4, 20, 0x300, 0x2d2A, 0x82c7, EN_SBD},
	{2900000, 1, 7, 20, 0x280, 0x2deb, 0x8347, EN_SBD},
#endif
};

#define WBD     0x781		/* 1 1 1 1 0000 0 0 1 */
static int dib0090_tune(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;
	const struct dib0090_tuning *tune = state->current_tune_table_index;
	const struct dib0090_pll *pll = state->current_pll_table_index;
	enum frontend_tune_state *tune_state = &state->tune_state;

	u32 rf;
	u16 lo4 = 0xe900, lo5, lo6, Den;
	u32 FBDiv, Rest, FREF, VCOF_kHz = 0;
	u16 tmp, adc;
	int8_t step_sign;
	int ret = 10;		/* 1ms is the default delay most of the time */
	u8 c, i;

	state->current_band = (u8) BAND_OF_FREQUENCY(fe->dtv_property_cache.frequency / 1000);
	rf = fe->dtv_property_cache.frequency / 1000 + (state->current_band ==
							BAND_UHF ? state->config->freq_offset_khz_uhf : state->config->freq_offset_khz_vhf);
	/* in any case we first need to do a reset if needed */
	if (state->reset & 0x1)
		return dib0090_dc_offset_calibration(state, tune_state);
	else if (state->reset & 0x2)
		return dib0090_wbd_calibration(state, tune_state);

    /************************* VCO ***************************/
	/* Default values for FG                                 */
	/* from these are needed :                               */
	/* Cp,HFdiv,VCOband,SD,Num,Den,FB and REFDiv             */

#ifdef CONFIG_SYS_ISDBT
	if (state->fe->dtv_property_cache.delivery_system == SYS_ISDBT && state->fe->dtv_property_cache.isdbt_sb_mode == 1)
		rf += 850;
#endif

	if (state->current_rf != rf) {
		state->tuner_is_tuned = 0;

		tune = dib0090_tuning_table;

		tmp = (state->revision >> 5) & 0x7;
		if (tmp == 0x4 || tmp == 0x7) {
			/* CBAND tuner version for VHF */
			if (state->current_band == BAND_FM || state->current_band == BAND_VHF) {
				/* Force CBAND */
				state->current_band = BAND_CBAND;
				tune = dib0090_tuning_table_fm_vhf_on_cband;
			}
		}

		pll = dib0090_pll_table;
		/* Look for the interval */
		while (rf > tune->max_freq)
			tune++;
		while (rf > pll->max_freq)
			pll++;
		state->current_tune_table_index = tune;
		state->current_pll_table_index = pll;
	}

	if (*tune_state == CT_TUNER_START) {

		if (state->tuner_is_tuned == 0)
			state->current_rf = 0;

		if (state->current_rf != rf) {

			dib0090_write_reg(state, 0x0b, 0xb800 | (tune->switch_trim));

			/* external loop filter, otherwise:
			 * lo5 = (0 << 15) | (0 << 12) | (0 << 11) | (3 << 9) | (4 << 6) | (3 << 4) | 4;
			 * lo6 = 0x0e34 */
			if (pll->vco_band)
				lo5 = 0x049e;
			else if (state->config->analog_output)
				lo5 = 0x041d;
			else
				lo5 = 0x041c;

			lo5 |= (pll->hfdiv_code << 11) | (pll->vco_band << 7);	/* bit 15 is the split to the slave, we do not do it here */

			if (!state->config->io.pll_int_loop_filt)
				lo6 = 0xff28;
			else
				lo6 = (state->config->io.pll_int_loop_filt << 3);

			VCOF_kHz = (pll->hfdiv * rf) * 2;

			FREF = state->config->io.clock_khz;

			FBDiv = (VCOF_kHz / pll->topresc / FREF);
			Rest = (VCOF_kHz / pll->topresc) - FBDiv * FREF;

			if (Rest < LPF)
				Rest = 0;
			else if (Rest < 2 * LPF)
				Rest = 2 * LPF;
			else if (Rest > (FREF - LPF)) {
				Rest = 0;
				FBDiv += 1;
			} else if (Rest > (FREF - 2 * LPF))
				Rest = FREF - 2 * LPF;
			Rest = (Rest * 6528) / (FREF / 10);

			Den = 1;

			dprintk(" *****  ******* Rest value = %d", Rest);

			if (Rest > 0) {
				if (state->config->analog_output)
					lo6 |= (1 << 2) | 2;
				else
					lo6 |= (1 << 2) | 1;
				Den = 255;
			}
#ifdef CONFIG_BAND_SBAND
			if (state->current_band == BAND_SBAND)
				lo6 &= 0xfffb;
#endif

			dib0090_write_reg(state, 0x15, (u16) FBDiv);

			dib0090_write_reg(state, 0x16, (Den << 8) | 1);

			dib0090_write_reg(state, 0x17, (u16) Rest);

			dib0090_write_reg(state, 0x19, lo5);

			dib0090_write_reg(state, 0x1c, lo6);

			lo6 = tune->tuner_enable;
			if (state->config->analog_output)
				lo6 = (lo6 & 0xff9f) | 0x2;

			dib0090_write_reg(state, 0x24, lo6 | EN_LO
#ifdef CONFIG_DIB0090_USE_PWM_AGC
					  | state->config->use_pwm_agc * EN_CRYSTAL
#endif
			    );

			state->current_rf = rf;

			/* prepare a complete captrim */
			state->step = state->captrim = state->fcaptrim = 64;

		} else {	/* we are already tuned to this frequency - the configuration is correct  */

			/* do a minimal captrim even if the frequency has not changed */
			state->step = 4;
			state->captrim = state->fcaptrim = dib0090_read_reg(state, 0x18) & 0x7f;
		}
		state->adc_diff = 3000;

		dib0090_write_reg(state, 0x10, 0x2B1);

		dib0090_write_reg(state, 0x1e, 0x0032);

		ret = 20;
		*tune_state = CT_TUNER_STEP_1;
	} else if (*tune_state == CT_TUNER_STEP_0) {
		/* nothing */
	} else if (*tune_state == CT_TUNER_STEP_1) {
		state->step /= 2;
		dib0090_write_reg(state, 0x18, lo4 | state->captrim);
		*tune_state = CT_TUNER_STEP_2;
	} else if (*tune_state == CT_TUNER_STEP_2) {

		adc = dib0090_read_reg(state, 0x1d);
		dprintk("FE %d CAPTRIM=%d; ADC = %d (ADC) & %dmV", (u32) fe->id, (u32) state->captrim, (u32) adc,
			(u32) (adc) * (u32) 1800 / (u32) 1024);

		if (adc >= 400) {
			adc -= 400;
			step_sign = -1;
		} else {
			adc = 400 - adc;
			step_sign = 1;
		}

		if (adc < state->adc_diff) {
			dprintk("FE %d CAPTRIM=%d is closer to target (%d/%d)", (u32) fe->id, (u32) state->captrim, (u32) adc, (u32) state->adc_diff);
			state->adc_diff = adc;
			state->fcaptrim = state->captrim;

		}

		state->captrim += step_sign * state->step;
		if (state->step >= 1)
			*tune_state = CT_TUNER_STEP_1;
		else
			*tune_state = CT_TUNER_STEP_3;

		ret = 15;
	} else if (*tune_state == CT_TUNER_STEP_3) {
		/*write the final cptrim config */
		dib0090_write_reg(state, 0x18, lo4 | state->fcaptrim);

#ifdef CONFIG_TUNER_DIB0090_CAPTRIM_MEMORY
		state->memory[state->memory_index].cap = state->fcaptrim;
#endif

		*tune_state = CT_TUNER_STEP_4;
	} else if (*tune_state == CT_TUNER_STEP_4) {
		dib0090_write_reg(state, 0x1e, 0x07ff);

		dprintk("FE %d Final Captrim: %d", (u32) fe->id, (u32) state->fcaptrim);
		dprintk("FE %d HFDIV code: %d", (u32) fe->id, (u32) pll->hfdiv_code);
		dprintk("FE %d VCO = %d", (u32) fe->id, (u32) pll->vco_band);
		dprintk("FE %d VCOF in kHz: %d ((%d*%d) << 1))", (u32) fe->id, (u32) ((pll->hfdiv * rf) * 2), (u32) pll->hfdiv, (u32) rf);
		dprintk("FE %d REFDIV: %d, FREF: %d", (u32) fe->id, (u32) 1, (u32) state->config->io.clock_khz);
		dprintk("FE %d FBDIV: %d, Rest: %d", (u32) fe->id, (u32) dib0090_read_reg(state, 0x15), (u32) dib0090_read_reg(state, 0x17));
		dprintk("FE %d Num: %d, Den: %d, SD: %d", (u32) fe->id, (u32) dib0090_read_reg(state, 0x17),
			(u32) (dib0090_read_reg(state, 0x16) >> 8), (u32) dib0090_read_reg(state, 0x1c) & 0x3);

		c = 4;
		i = 3;
#if defined(CONFIG_BAND_LBAND) || defined(CONFIG_BAND_SBAND)
		if ((state->current_band == BAND_LBAND) || (state->current_band == BAND_SBAND)) {
			c = 2;
			i = 2;
		}
#endif
		dib0090_write_reg(state, 0x10, (c << 13) | (i << 11) | (WBD
#ifdef CONFIG_DIB0090_USE_PWM_AGC
									| (state->config->use_pwm_agc << 1)
#endif
				  ));
		dib0090_write_reg(state, 0x09, (tune->lna_tune << 5) | (tune->lna_bias << 0));
		dib0090_write_reg(state, 0x0c, tune->v2i);
		dib0090_write_reg(state, 0x0d, tune->mix);
		dib0090_write_reg(state, 0x0e, tune->load);

		*tune_state = CT_TUNER_STEP_5;
	} else if (*tune_state == CT_TUNER_STEP_5) {

		/* initialize the lt gain register */
		state->rf_lt_def = 0x7c00;
		dib0090_write_reg(state, 0x0f, state->rf_lt_def);

		dib0090_set_bandwidth(state);
		state->tuner_is_tuned = 1;
		*tune_state = CT_TUNER_STOP;
	} else
		ret = FE_CALLBACK_TIME_NEVER;
	return ret;
}

static int dib0090_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

enum frontend_tune_state dib0090_get_tune_state(struct dvb_frontend *fe)
{
	struct dib0090_state *state = fe->tuner_priv;

	return state->tune_state;
}
EXPORT_SYMBOL(dib0090_get_tune_state);

int dib0090_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state)
{
	struct dib0090_state *state = fe->tuner_priv;

	state->tune_state = tune_state;
	return 0;
}
EXPORT_SYMBOL(dib0090_set_tune_state);

static int dib0090_get_frequency(struct dvb_frontend *fe, u32 * frequency)
{
	struct dib0090_state *state = fe->tuner_priv;

	*frequency = 1000 * state->current_rf;
	return 0;
}

static int dib0090_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	struct dib0090_state *state = fe->tuner_priv;
	uint32_t ret;

	state->tune_state = CT_TUNER_START;

	do {
		ret = dib0090_tune(fe);
		if (ret != FE_CALLBACK_TIME_NEVER)
			msleep(ret / 10);
		else
			break;
	} while (state->tune_state != CT_TUNER_STOP);

	return 0;
}

static const struct dvb_tuner_ops dib0090_ops = {
	.info = {
		 .name = "DiBcom DiB0090",
		 .frequency_min = 45000000,
		 .frequency_max = 860000000,
		 .frequency_step = 1000,
		 },
	.release = dib0090_release,

	.init = dib0090_wakeup,
	.sleep = dib0090_sleep,
	.set_params = dib0090_set_params,
	.get_frequency = dib0090_get_frequency,
};

struct dvb_frontend *dib0090_register(struct dvb_frontend *fe, struct i2c_adapter *i2c, const struct dib0090_config *config)
{
	struct dib0090_state *st = kzalloc(sizeof(struct dib0090_state), GFP_KERNEL);
	if (st == NULL)
		return NULL;

	st->config = config;
	st->i2c = i2c;
	st->fe = fe;
	fe->tuner_priv = st;

	if (dib0090_reset(fe) != 0)
		goto free_mem;

	printk(KERN_INFO "DiB0090: successfully identified\n");
	memcpy(&fe->ops.tuner_ops, &dib0090_ops, sizeof(struct dvb_tuner_ops));

	return fe;
 free_mem:
	kfree(st);
	fe->tuner_priv = NULL;
	return NULL;
}
EXPORT_SYMBOL(dib0090_register);

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_AUTHOR("Olivier Grenie <olivier.grenie@dibcom.fr>");
MODULE_DESCRIPTION("Driver for the DiBcom 0090 base-band RF Tuner");
MODULE_LICENSE("GPL");
