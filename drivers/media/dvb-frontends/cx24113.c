/*
 *  Driver for Conexant CX24113/CX24128 Tuner (Satellite)
 *
 *  Copyright (C) 2007-8 Patrick Boettcher <pb@linuxtv.org>
 *
 *  Developed for BBTI / Technisat
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "dvb_frontend.h"
#include "cx24113.h"

static int debug;

#define cx_info(args...) do { printk(KERN_INFO "CX24113: " args); } while (0)
#define cx_err(args...)  do { printk(KERN_ERR  "CX24113: " args); } while (0)

#define dprintk(args...) \
	do { \
		if (debug) { \
			printk(KERN_DEBUG "CX24113: %s: ", __func__); \
			printk(args); \
		} \
	} while (0)

struct cx24113_state {
	struct i2c_adapter *i2c;
	const struct cx24113_config *config;

#define REV_CX24113 0x23
	u8 rev;
	u8 ver;

	u8 icp_mode:1;

#define ICP_LEVEL1 0
#define ICP_LEVEL2 1
#define ICP_LEVEL3 2
#define ICP_LEVEL4 3
	u8 icp_man:2;
	u8 icp_auto_low:2;
	u8 icp_auto_mlow:2;
	u8 icp_auto_mhi:2;
	u8 icp_auto_hi:2;
	u8 icp_dig;

#define LNA_MIN_GAIN 0
#define LNA_MID_GAIN 1
#define LNA_MAX_GAIN 2
	u8 lna_gain:2;

	u8 acp_on:1;

	u8 vco_mode:2;
	u8 vco_shift:1;
#define VCOBANDSEL_6 0x80
#define VCOBANDSEL_5 0x01
#define VCOBANDSEL_4 0x02
#define VCOBANDSEL_3 0x04
#define VCOBANDSEL_2 0x08
#define VCOBANDSEL_1 0x10
	u8 vco_band;

#define VCODIV4 4
#define VCODIV2 2
	u8 vcodiv;

	u8 bs_delay:4;
	u16 bs_freqcnt:13;
	u16 bs_rdiv;
	u8 prescaler_mode:1;

	u8 rfvga_bias_ctrl;

	s16 tuner_gain_thres;
	u8  gain_level;

	u32 frequency;

	u8 refdiv;

	u8 Fwindow_enabled;
};

static int cx24113_writereg(struct cx24113_state *state, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->i2c_addr,
		.flags = 0, .buf = buf, .len = 2 };
	int err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk(KERN_DEBUG "%s: writereg error(err == %i, reg == 0x%02x, data == 0x%02x)\n",
		       __func__, err, reg, data);
		return err;
	}

	return 0;
}

static int cx24113_readreg(struct cx24113_state *state, u8 reg)
{
	int ret;
	u8 b;
	struct i2c_msg msg[] = {
		{ .addr = state->config->i2c_addr,
			.flags = 0, .buf = &reg, .len = 1 },
		{ .addr = state->config->i2c_addr,
			.flags = I2C_M_RD, .buf = &b, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_DEBUG "%s: reg=0x%x (error=%d)\n",
			__func__, reg, ret);
		return ret;
	}

	return b;
}

static void cx24113_set_parameters(struct cx24113_state *state)
{
	u8 r;

	r = cx24113_readreg(state, 0x10) & 0x82;
	r |= state->icp_mode;
	r |= state->icp_man << 4;
	r |= state->icp_dig << 2;
	r |= state->prescaler_mode << 5;
	cx24113_writereg(state, 0x10, r);

	r = (state->icp_auto_low  << 0) | (state->icp_auto_mlow << 2)
		| (state->icp_auto_mhi << 4) | (state->icp_auto_hi << 6);
	cx24113_writereg(state, 0x11, r);

	if (state->rev == REV_CX24113) {
		r = cx24113_readreg(state, 0x20) & 0xec;
		r |= state->lna_gain;
		r |= state->rfvga_bias_ctrl << 4;
		cx24113_writereg(state, 0x20, r);
	}

	r = cx24113_readreg(state, 0x12) & 0x03;
	r |= state->acp_on << 2;
	r |= state->bs_delay << 4;
	cx24113_writereg(state, 0x12, r);

	r = cx24113_readreg(state, 0x18) & 0x40;
	r |= state->vco_shift;
	if (state->vco_band == VCOBANDSEL_6)
		r |= (1 << 7);
	else
		r |= (state->vco_band << 1);
	cx24113_writereg(state, 0x18, r);

	r  = cx24113_readreg(state, 0x14) & 0x20;
	r |= (state->vco_mode << 6) | ((state->bs_freqcnt >> 8) & 0x1f);
	cx24113_writereg(state, 0x14, r);
	cx24113_writereg(state, 0x15, (state->bs_freqcnt        & 0xff));

	cx24113_writereg(state, 0x16, (state->bs_rdiv >> 4) & 0xff);
	r = (cx24113_readreg(state, 0x17) & 0x0f) |
		((state->bs_rdiv & 0x0f) << 4);
	cx24113_writereg(state, 0x17, r);
}

#define VGA_0 0x00
#define VGA_1 0x04
#define VGA_2 0x02
#define VGA_3 0x06
#define VGA_4 0x01
#define VGA_5 0x05
#define VGA_6 0x03
#define VGA_7 0x07

#define RFVGA_0 0x00
#define RFVGA_1 0x01
#define RFVGA_2 0x02
#define RFVGA_3 0x03

static int cx24113_set_gain_settings(struct cx24113_state *state,
		s16 power_estimation)
{
	u8 ampout = cx24113_readreg(state, 0x1d) & 0xf0,
	   vga    = cx24113_readreg(state, 0x1f) & 0x3f,
	   rfvga  = cx24113_readreg(state, 0x20) & 0xf3;
	u8 gain_level = power_estimation >= state->tuner_gain_thres;

	dprintk("power estimation: %d, thres: %d, gain_level: %d/%d\n",
			power_estimation, state->tuner_gain_thres,
			state->gain_level, gain_level);

	if (gain_level == state->gain_level)
		return 0; /* nothing to be done */

	ampout |= 0xf;

	if (gain_level) {
		rfvga |= RFVGA_0 << 2;
		vga   |= (VGA_7 << 3) | VGA_7;
	} else {
		rfvga |= RFVGA_2 << 2;
		vga  |= (VGA_6 << 3) | VGA_2;
	}
	state->gain_level = gain_level;

	cx24113_writereg(state, 0x1d, ampout);
	cx24113_writereg(state, 0x1f, vga);
	cx24113_writereg(state, 0x20, rfvga);

	return 1; /* did something */
}

static int cx24113_set_Fref(struct cx24113_state *state, u8 high)
{
	u8 xtal = cx24113_readreg(state, 0x02);
	if (state->rev == 0x43 && state->vcodiv == VCODIV4)
		high = 1;

	xtal &= ~0x2;
	if (high)
		xtal |= high << 1;
	return cx24113_writereg(state, 0x02, xtal);
}

static int cx24113_enable(struct cx24113_state *state, u8 enable)
{
	u8 r21 = (cx24113_readreg(state, 0x21) & 0xc0) | enable;
	if (state->rev == REV_CX24113)
		r21 |= (1 << 1);
	return cx24113_writereg(state, 0x21, r21);
}

static int cx24113_set_bandwidth(struct cx24113_state *state, u32 bandwidth_khz)
{
	u8 r;

	if (bandwidth_khz <= 19000)
		r = 0x03 << 6;
	else if (bandwidth_khz <= 25000)
		r = 0x02 << 6;
	else
		r = 0x01 << 6;

	dprintk("bandwidth to be set: %d\n", bandwidth_khz);
	bandwidth_khz *= 10;
	bandwidth_khz -= 10000;
	bandwidth_khz /= 1000;
	bandwidth_khz += 5;
	bandwidth_khz /= 10;

	dprintk("bandwidth: %d %d\n", r >> 6, bandwidth_khz);

	r |= bandwidth_khz & 0x3f;

	return cx24113_writereg(state, 0x1e, r);
}

static int cx24113_set_clk_inversion(struct cx24113_state *state, u8 on)
{
	u8 r = (cx24113_readreg(state, 0x10) & 0x7f) | ((on & 0x1) << 7);
	return cx24113_writereg(state, 0x10, r);
}

static int cx24113_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct cx24113_state *state = fe->tuner_priv;
	u8 r = (cx24113_readreg(state, 0x10) & 0x02) >> 1;
	if (r)
		*status |= TUNER_STATUS_LOCKED;
	dprintk("PLL locked: %d\n", r);
	return 0;
}

static u8 cx24113_set_ref_div(struct cx24113_state *state, u8 refdiv)
{
	if (state->rev == 0x43 && state->vcodiv == VCODIV4)
		refdiv = 2;
	return state->refdiv = refdiv;
}

static void cx24113_calc_pll_nf(struct cx24113_state *state, u16 *n, s32 *f)
{
	s32 N;
	s64 F;
	u64 dividend;
	u8 R, r;
	u8 vcodiv;
	u8 factor;
	s32 freq_hz = state->frequency * 1000;

	if (state->config->xtal_khz < 20000)
		factor = 1;
	else
		factor = 2;

	if (state->rev == REV_CX24113) {
		if (state->frequency >= 1100000)
			vcodiv = VCODIV2;
		else
			vcodiv = VCODIV4;
	} else {
		if (state->frequency >= 1165000)
			vcodiv = VCODIV2;
		else
			vcodiv = VCODIV4;
	}
	state->vcodiv = vcodiv;

	dprintk("calculating N/F for %dHz with vcodiv %d\n", freq_hz, vcodiv);
	R = 0;
	do {
		R = cx24113_set_ref_div(state, R + 1);

		/* calculate tuner PLL settings: */
		N =  (freq_hz / 100 * vcodiv) * R;
		N /= (state->config->xtal_khz) * factor * 2;
		N += 5;     /* For round up. */
		N /= 10;
		N -= 32;
	} while (N < 6 && R < 3);

	if (N < 6) {
		cx_err("strange frequency: N < 6\n");
		return;
	}
	F = freq_hz;
	F *= (u64) (R * vcodiv * 262144);
	dprintk("1 N: %d, F: %lld, R: %d\n", N, (long long)F, R);
	/* do_div needs an u64 as first argument */
	dividend = F;
	do_div(dividend, state->config->xtal_khz * 1000 * factor * 2);
	F = dividend;
	dprintk("2 N: %d, F: %lld, R: %d\n", N, (long long)F, R);
	F -= (N + 32) * 262144;

	dprintk("3 N: %d, F: %lld, R: %d\n", N, (long long)F, R);

	if (state->Fwindow_enabled) {
		if (F > (262144 / 2 - 1638))
			F = 262144 / 2 - 1638;
		if (F < (-262144 / 2 + 1638))
			F = -262144 / 2 + 1638;
		if ((F < 3277 && F > 0) || (F > -3277 && F < 0)) {
			F = 0;
			r = cx24113_readreg(state, 0x10);
			cx24113_writereg(state, 0x10, r | (1 << 6));
		}
	}
	dprintk("4 N: %d, F: %lld, R: %d\n", N, (long long)F, R);

	*n = (u16) N;
	*f = (s32) F;
}


static void cx24113_set_nfr(struct cx24113_state *state, u16 n, s32 f, u8 r)
{
	u8 reg;
	cx24113_writereg(state, 0x19, (n >> 1) & 0xff);

	reg = ((n & 0x1) << 7) | ((f >> 11) & 0x7f);
	cx24113_writereg(state, 0x1a, reg);

	cx24113_writereg(state, 0x1b, (f >> 3) & 0xff);

	reg = cx24113_readreg(state, 0x1c) & 0x1f;
	cx24113_writereg(state, 0x1c, reg | ((f & 0x7) << 5));

	cx24113_set_Fref(state, r - 1);
}

static int cx24113_set_frequency(struct cx24113_state *state, u32 frequency)
{
	u8 r = 1; /* or 2 */
	u16 n = 6;
	s32 f = 0;

	r = cx24113_readreg(state, 0x14);
	cx24113_writereg(state, 0x14, r & 0x3f);

	r = cx24113_readreg(state, 0x10);
	cx24113_writereg(state, 0x10, r & 0xbf);

	state->frequency = frequency;

	dprintk("tuning to frequency: %d\n", frequency);

	cx24113_calc_pll_nf(state, &n, &f);
	cx24113_set_nfr(state, n, f, state->refdiv);

	r = cx24113_readreg(state, 0x18) & 0xbf;
	if (state->vcodiv != VCODIV2)
		r |= 1 << 6;
	cx24113_writereg(state, 0x18, r);

	/* The need for this sleep is not clear. But helps in some cases */
	msleep(5);

	r = cx24113_readreg(state, 0x1c) & 0xef;
	cx24113_writereg(state, 0x1c, r | (1 << 4));
	return 0;
}

static int cx24113_init(struct dvb_frontend *fe)
{
	struct cx24113_state *state = fe->tuner_priv;
	int ret;

	state->tuner_gain_thres = -50;
	state->gain_level = 255; /* to force a gain-setting initialization */
	state->icp_mode = 0;

	if (state->config->xtal_khz < 11000) {
		state->icp_auto_hi  = ICP_LEVEL4;
		state->icp_auto_mhi  = ICP_LEVEL4;
		state->icp_auto_mlow = ICP_LEVEL3;
		state->icp_auto_low = ICP_LEVEL3;
	} else {
		state->icp_auto_hi  = ICP_LEVEL4;
		state->icp_auto_mhi  = ICP_LEVEL4;
		state->icp_auto_mlow = ICP_LEVEL3;
		state->icp_auto_low = ICP_LEVEL2;
	}

	state->icp_dig = ICP_LEVEL3;
	state->icp_man = ICP_LEVEL1;
	state->acp_on  = 1;
	state->vco_mode = 0;
	state->vco_shift = 0;
	state->vco_band = VCOBANDSEL_1;
	state->bs_delay = 8;
	state->bs_freqcnt = 0x0fff;
	state->bs_rdiv = 0x0fff;
	state->prescaler_mode = 0;
	state->lna_gain = LNA_MAX_GAIN;
	state->rfvga_bias_ctrl = 1;
	state->Fwindow_enabled = 1;

	cx24113_set_Fref(state, 0);
	cx24113_enable(state, 0x3d);
	cx24113_set_parameters(state);

	cx24113_set_gain_settings(state, -30);

	cx24113_set_bandwidth(state, 18025);
	cx24113_set_clk_inversion(state, 1);

	if (state->config->xtal_khz >= 40000)
		ret = cx24113_writereg(state, 0x02,
			(cx24113_readreg(state, 0x02) & 0xfb) | (1 << 2));
	else
		ret = cx24113_writereg(state, 0x02,
			(cx24113_readreg(state, 0x02) & 0xfb) | (0 << 2));

	return ret;
}

static int cx24113_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24113_state *state = fe->tuner_priv;
	/* for a ROLL-OFF factor of 0.35, 0.2: 600, 0.25: 625 */
	u32 roll_off = 675;
	u32 bw;

	bw  = ((c->symbol_rate/100) * roll_off) / 1000;
	bw += (10000000/100) + 5;
	bw /= 10;
	bw += 1000;
	cx24113_set_bandwidth(state, bw);

	cx24113_set_frequency(state, c->frequency);
	msleep(5);
	return cx24113_get_status(fe, &bw);
}

static s8 cx24113_agc_table[2][10] = {
	{-54, -41, -35, -30, -25, -21, -16, -10,  -6,  -2},
	{-39, -35, -30, -25, -19, -15, -11,  -5,   1,   9},
};

void cx24113_agc_callback(struct dvb_frontend *fe)
{
	struct cx24113_state *state = fe->tuner_priv;
	s16 s, i;
	if (!fe->ops.read_signal_strength)
		return;

	do {
		/* this only works with the current CX24123 implementation */
		fe->ops.read_signal_strength(fe, (u16 *) &s);
		s >>= 8;
		dprintk("signal strength: %d\n", s);
		for (i = 0; i < sizeof(cx24113_agc_table[0]); i++)
			if (cx24113_agc_table[state->gain_level][i] > s)
				break;
		s = -25 - i*5;
	} while (cx24113_set_gain_settings(state, s));
}
EXPORT_SYMBOL(cx24113_agc_callback);

static int cx24113_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct cx24113_state *state = fe->tuner_priv;
	*frequency = state->frequency;
	return 0;
}

static int cx24113_release(struct dvb_frontend *fe)
{
	struct cx24113_state *state = fe->tuner_priv;
	dprintk("\n");
	fe->tuner_priv = NULL;
	kfree(state);
	return 0;
}

static const struct dvb_tuner_ops cx24113_tuner_ops = {
	.info = {
		.name           = "Conexant CX24113",
		.frequency_min  = 950000,
		.frequency_max  = 2150000,
		.frequency_step = 125,
	},

	.release       = cx24113_release,

	.init          = cx24113_init,

	.set_params    = cx24113_set_params,
	.get_frequency = cx24113_get_frequency,
	.get_status    = cx24113_get_status,
};

struct dvb_frontend *cx24113_attach(struct dvb_frontend *fe,
		const struct cx24113_config *config, struct i2c_adapter *i2c)
{
	/* allocate memory for the internal state */
	struct cx24113_state *state =
		kzalloc(sizeof(struct cx24113_state), GFP_KERNEL);
	int rc;
	if (state == NULL) {
		cx_err("Unable to kzalloc\n");
		goto error;
	}

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	cx_info("trying to detect myself\n");

	/* making a dummy read, because of some expected troubles
	 * after power on */
	cx24113_readreg(state, 0x00);

	rc = cx24113_readreg(state, 0x00);
	if (rc < 0) {
		cx_info("CX24113 not found.\n");
		goto error;
	}
	state->rev = rc;

	switch (rc) {
	case 0x43:
		cx_info("detected CX24113 variant\n");
		break;
	case REV_CX24113:
		cx_info("successfully detected\n");
		break;
	default:
		cx_err("unsupported device id: %x\n", state->rev);
		goto error;
	}
	state->ver = cx24113_readreg(state, 0x01);
	cx_info("version: %x\n", state->ver);

	/* create dvb_frontend */
	memcpy(&fe->ops.tuner_ops, &cx24113_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = state;
	return fe;

error:
	kfree(state);

	return NULL;
}
EXPORT_SYMBOL(cx24113_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activates frontend debugging (default:0)");

MODULE_AUTHOR("Patrick Boettcher <pb@linuxtv.org>");
MODULE_DESCRIPTION("DVB Frontend module for Conexant CX24113/CX24128hardware");
MODULE_LICENSE("GPL");

