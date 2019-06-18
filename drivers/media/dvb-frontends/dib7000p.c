// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux-DVB Driver for DiBcom's second generation DiB7000P (PC).
 *
 * Copyright (C) 2005-7 DiBcom (http://www.dibcom.fr/)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <asm/div64.h>

#include <media/dvb_math.h>
#include <media/dvb_frontend.h>

#include "dib7000p.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

static int buggy_sfn_workaround;
module_param(buggy_sfn_workaround, int, 0644);
MODULE_PARM_DESC(buggy_sfn_workaround, "Enable work-around for buggy SFNs (default: 0)");

#define dprintk(fmt, arg...) do {					\
	if (debug)							\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

struct i2c_device {
	struct i2c_adapter *i2c_adap;
	u8 i2c_addr;
};

struct dib7000p_state {
	struct dvb_frontend demod;
	struct dib7000p_config cfg;

	u8 i2c_addr;
	struct i2c_adapter *i2c_adap;

	struct dibx000_i2c_master i2c_master;

	u16 wbd_ref;

	u8 current_band;
	u32 current_bandwidth;
	struct dibx000_agc_config *current_agc;
	u32 timf;

	u8 div_force_off:1;
	u8 div_state:1;
	u16 div_sync_wait;

	u8 agc_state;

	u16 gpio_dir;
	u16 gpio_val;

	u8 sfn_workaround_active:1;

#define SOC7090 0x7090
	u16 version;

	u16 tuner_enable;
	struct i2c_adapter dib7090_tuner_adap;

	/* for the I2C transfer */
	struct i2c_msg msg[2];
	u8 i2c_write_buffer[4];
	u8 i2c_read_buffer[2];
	struct mutex i2c_buffer_lock;

	u8 input_mode_mpeg;

	/* for DVBv5 stats */
	s64 old_ucb;
	unsigned long per_jiffies_stats;
	unsigned long ber_jiffies_stats;
	unsigned long get_stats_time;
};

enum dib7000p_power_mode {
	DIB7000P_POWER_ALL = 0,
	DIB7000P_POWER_ANALOG_ADC,
	DIB7000P_POWER_INTERFACE_ONLY,
};

/* dib7090 specific functions */
static int dib7090_set_output_mode(struct dvb_frontend *fe, int mode);
static int dib7090_set_diversity_in(struct dvb_frontend *fe, int onoff);
static void dib7090_setDibTxMux(struct dib7000p_state *state, int mode);
static void dib7090_setHostBusMux(struct dib7000p_state *state, int mode);

static u16 dib7000p_read_word(struct dib7000p_state *state, u16 reg)
{
	u16 ret;

	if (mutex_lock_interruptible(&state->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return 0;
	}

	state->i2c_write_buffer[0] = reg >> 8;
	state->i2c_write_buffer[1] = reg & 0xff;

	memset(state->msg, 0, 2 * sizeof(struct i2c_msg));
	state->msg[0].addr = state->i2c_addr >> 1;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 2;
	state->msg[1].addr = state->i2c_addr >> 1;
	state->msg[1].flags = I2C_M_RD;
	state->msg[1].buf = state->i2c_read_buffer;
	state->msg[1].len = 2;

	if (i2c_transfer(state->i2c_adap, state->msg, 2) != 2)
		dprintk("i2c read error on %d\n", reg);

	ret = (state->i2c_read_buffer[0] << 8) | state->i2c_read_buffer[1];
	mutex_unlock(&state->i2c_buffer_lock);
	return ret;
}

static int dib7000p_write_word(struct dib7000p_state *state, u16 reg, u16 val)
{
	int ret;

	if (mutex_lock_interruptible(&state->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}

	state->i2c_write_buffer[0] = (reg >> 8) & 0xff;
	state->i2c_write_buffer[1] = reg & 0xff;
	state->i2c_write_buffer[2] = (val >> 8) & 0xff;
	state->i2c_write_buffer[3] = val & 0xff;

	memset(&state->msg[0], 0, sizeof(struct i2c_msg));
	state->msg[0].addr = state->i2c_addr >> 1;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 4;

	ret = (i2c_transfer(state->i2c_adap, state->msg, 1) != 1 ?
			-EREMOTEIO : 0);
	mutex_unlock(&state->i2c_buffer_lock);
	return ret;
}

static void dib7000p_write_tab(struct dib7000p_state *state, u16 * buf)
{
	u16 l = 0, r, *n;
	n = buf;
	l = *n++;
	while (l) {
		r = *n++;

		do {
			dib7000p_write_word(state, r, *n++);
			r++;
		} while (--l);
		l = *n++;
	}
}

static int dib7000p_set_output_mode(struct dib7000p_state *state, int mode)
{
	int ret = 0;
	u16 outreg, fifo_threshold, smo_mode;

	outreg = 0;
	fifo_threshold = 1792;
	smo_mode = (dib7000p_read_word(state, 235) & 0x0050) | (1 << 1);

	dprintk("setting output mode for demod %p to %d\n", &state->demod, mode);

	switch (mode) {
	case OUTMODE_MPEG2_PAR_GATED_CLK:
		outreg = (1 << 10);	/* 0x0400 */
		break;
	case OUTMODE_MPEG2_PAR_CONT_CLK:
		outreg = (1 << 10) | (1 << 6);	/* 0x0440 */
		break;
	case OUTMODE_MPEG2_SERIAL:
		outreg = (1 << 10) | (2 << 6) | (0 << 1);	/* 0x0480 */
		break;
	case OUTMODE_DIVERSITY:
		if (state->cfg.hostbus_diversity)
			outreg = (1 << 10) | (4 << 6);	/* 0x0500 */
		else
			outreg = (1 << 11);
		break;
	case OUTMODE_MPEG2_FIFO:
		smo_mode |= (3 << 1);
		fifo_threshold = 512;
		outreg = (1 << 10) | (5 << 6);
		break;
	case OUTMODE_ANALOG_ADC:
		outreg = (1 << 10) | (3 << 6);
		break;
	case OUTMODE_HIGH_Z:
		outreg = 0;
		break;
	default:
		dprintk("Unhandled output_mode passed to be set for demod %p\n", &state->demod);
		break;
	}

	if (state->cfg.output_mpeg2_in_188_bytes)
		smo_mode |= (1 << 5);

	ret |= dib7000p_write_word(state, 235, smo_mode);
	ret |= dib7000p_write_word(state, 236, fifo_threshold);	/* synchronous fread */
	if (state->version != SOC7090)
		ret |= dib7000p_write_word(state, 1286, outreg);	/* P_Div_active */

	return ret;
}

static int dib7000p_set_diversity_in(struct dvb_frontend *demod, int onoff)
{
	struct dib7000p_state *state = demod->demodulator_priv;

	if (state->div_force_off) {
		dprintk("diversity combination deactivated - forced by COFDM parameters\n");
		onoff = 0;
		dib7000p_write_word(state, 207, 0);
	} else
		dib7000p_write_word(state, 207, (state->div_sync_wait << 4) | (1 << 2) | (2 << 0));

	state->div_state = (u8) onoff;

	if (onoff) {
		dib7000p_write_word(state, 204, 6);
		dib7000p_write_word(state, 205, 16);
		/* P_dvsy_sync_mode = 0, P_dvsy_sync_enable=1, P_dvcb_comb_mode=2 */
	} else {
		dib7000p_write_word(state, 204, 1);
		dib7000p_write_word(state, 205, 0);
	}

	return 0;
}

static int dib7000p_set_power_mode(struct dib7000p_state *state, enum dib7000p_power_mode mode)
{
	/* by default everything is powered off */
	u16 reg_774 = 0x3fff, reg_775 = 0xffff, reg_776 = 0x0007, reg_899 = 0x0003, reg_1280 = (0xfe00) | (dib7000p_read_word(state, 1280) & 0x01ff);

	/* now, depending on the requested mode, we power on */
	switch (mode) {
		/* power up everything in the demod */
	case DIB7000P_POWER_ALL:
		reg_774 = 0x0000;
		reg_775 = 0x0000;
		reg_776 = 0x0;
		reg_899 = 0x0;
		if (state->version == SOC7090)
			reg_1280 &= 0x001f;
		else
			reg_1280 &= 0x01ff;
		break;

	case DIB7000P_POWER_ANALOG_ADC:
		/* dem, cfg, iqc, sad, agc */
		reg_774 &= ~((1 << 15) | (1 << 14) | (1 << 11) | (1 << 10) | (1 << 9));
		/* nud */
		reg_776 &= ~((1 << 0));
		/* Dout */
		if (state->version != SOC7090)
			reg_1280 &= ~((1 << 11));
		reg_1280 &= ~(1 << 6);
		/* fall-through */
	case DIB7000P_POWER_INTERFACE_ONLY:
		/* just leave power on the control-interfaces: GPIO and (I2C or SDIO) */
		/* TODO power up either SDIO or I2C */
		if (state->version == SOC7090)
			reg_1280 &= ~((1 << 7) | (1 << 5));
		else
			reg_1280 &= ~((1 << 14) | (1 << 13) | (1 << 12) | (1 << 10));
		break;

/* TODO following stuff is just converted from the dib7000-driver - check when is used what */
	}

	dib7000p_write_word(state, 774, reg_774);
	dib7000p_write_word(state, 775, reg_775);
	dib7000p_write_word(state, 776, reg_776);
	dib7000p_write_word(state, 1280, reg_1280);
	if (state->version != SOC7090)
		dib7000p_write_word(state, 899, reg_899);

	return 0;
}

static void dib7000p_set_adc_state(struct dib7000p_state *state, enum dibx000_adc_states no)
{
	u16 reg_908 = 0, reg_909 = 0;
	u16 reg;

	if (state->version != SOC7090) {
		reg_908 = dib7000p_read_word(state, 908);
		reg_909 = dib7000p_read_word(state, 909);
	}

	switch (no) {
	case DIBX000_SLOW_ADC_ON:
		if (state->version == SOC7090) {
			reg = dib7000p_read_word(state, 1925);

			dib7000p_write_word(state, 1925, reg | (1 << 4) | (1 << 2));	/* en_slowAdc = 1 & reset_sladc = 1 */

			reg = dib7000p_read_word(state, 1925);	/* read access to make it works... strange ... */
			msleep(200);
			dib7000p_write_word(state, 1925, reg & ~(1 << 4));	/* en_slowAdc = 1 & reset_sladc = 0 */

			reg = dib7000p_read_word(state, 72) & ~((0x3 << 14) | (0x3 << 12));
			dib7000p_write_word(state, 72, reg | (1 << 14) | (3 << 12) | 524);	/* ref = Vin1 => Vbg ; sel = Vin0 or Vin3 ; (Vin2 = Vcm) */
		} else {
			reg_909 |= (1 << 1) | (1 << 0);
			dib7000p_write_word(state, 909, reg_909);
			reg_909 &= ~(1 << 1);
		}
		break;

	case DIBX000_SLOW_ADC_OFF:
		if (state->version == SOC7090) {
			reg = dib7000p_read_word(state, 1925);
			dib7000p_write_word(state, 1925, (reg & ~(1 << 2)) | (1 << 4));	/* reset_sladc = 1 en_slowAdc = 0 */
		} else
			reg_909 |= (1 << 1) | (1 << 0);
		break;

	case DIBX000_ADC_ON:
		reg_908 &= 0x0fff;
		reg_909 &= 0x0003;
		break;

	case DIBX000_ADC_OFF:
		reg_908 |= (1 << 14) | (1 << 13) | (1 << 12);
		reg_909 |= (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2);
		break;

	case DIBX000_VBG_ENABLE:
		reg_908 &= ~(1 << 15);
		break;

	case DIBX000_VBG_DISABLE:
		reg_908 |= (1 << 15);
		break;

	default:
		break;
	}

//	dprintk( "908: %x, 909: %x\n", reg_908, reg_909);

	reg_909 |= (state->cfg.disable_sample_and_hold & 1) << 4;
	reg_908 |= (state->cfg.enable_current_mirror & 1) << 7;

	if (state->version != SOC7090) {
		dib7000p_write_word(state, 908, reg_908);
		dib7000p_write_word(state, 909, reg_909);
	}
}

static int dib7000p_set_bandwidth(struct dib7000p_state *state, u32 bw)
{
	u32 timf;

	// store the current bandwidth for later use
	state->current_bandwidth = bw;

	if (state->timf == 0) {
		dprintk("using default timf\n");
		timf = state->cfg.bw->timf;
	} else {
		dprintk("using updated timf\n");
		timf = state->timf;
	}

	timf = timf * (bw / 50) / 160;

	dib7000p_write_word(state, 23, (u16) ((timf >> 16) & 0xffff));
	dib7000p_write_word(state, 24, (u16) ((timf) & 0xffff));

	return 0;
}

static int dib7000p_sad_calib(struct dib7000p_state *state)
{
/* internal */
	dib7000p_write_word(state, 73, (0 << 1) | (0 << 0));

	if (state->version == SOC7090)
		dib7000p_write_word(state, 74, 2048);
	else
		dib7000p_write_word(state, 74, 776);

	/* do the calibration */
	dib7000p_write_word(state, 73, (1 << 0));
	dib7000p_write_word(state, 73, (0 << 0));

	msleep(1);

	return 0;
}

static int dib7000p_set_wbd_ref(struct dvb_frontend *demod, u16 value)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	if (value > 4095)
		value = 4095;
	state->wbd_ref = value;
	return dib7000p_write_word(state, 105, (dib7000p_read_word(state, 105) & 0xf000) | value);
}

static int dib7000p_get_agc_values(struct dvb_frontend *fe,
		u16 *agc_global, u16 *agc1, u16 *agc2, u16 *wbd)
{
	struct dib7000p_state *state = fe->demodulator_priv;

	if (agc_global != NULL)
		*agc_global = dib7000p_read_word(state, 394);
	if (agc1 != NULL)
		*agc1 = dib7000p_read_word(state, 392);
	if (agc2 != NULL)
		*agc2 = dib7000p_read_word(state, 393);
	if (wbd != NULL)
		*wbd = dib7000p_read_word(state, 397);

	return 0;
}

static int dib7000p_set_agc1_min(struct dvb_frontend *fe, u16 v)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	return dib7000p_write_word(state, 108,  v);
}

static void dib7000p_reset_pll(struct dib7000p_state *state)
{
	struct dibx000_bandwidth_config *bw = &state->cfg.bw[0];
	u16 clk_cfg0;

	if (state->version == SOC7090) {
		dib7000p_write_word(state, 1856, (!bw->pll_reset << 13) | (bw->pll_range << 12) | (bw->pll_ratio << 6) | (bw->pll_prediv));

		while (((dib7000p_read_word(state, 1856) >> 15) & 0x1) != 1)
			;

		dib7000p_write_word(state, 1857, dib7000p_read_word(state, 1857) | (!bw->pll_bypass << 15));
	} else {
		/* force PLL bypass */
		clk_cfg0 = (1 << 15) | ((bw->pll_ratio & 0x3f) << 9) |
			(bw->modulo << 7) | (bw->ADClkSrc << 6) | (bw->IO_CLK_en_core << 5) | (bw->bypclk_div << 2) | (bw->enable_refdiv << 1) | (0 << 0);

		dib7000p_write_word(state, 900, clk_cfg0);

		/* P_pll_cfg */
		dib7000p_write_word(state, 903, (bw->pll_prediv << 5) | (((bw->pll_ratio >> 6) & 0x3) << 3) | (bw->pll_range << 1) | bw->pll_reset);
		clk_cfg0 = (bw->pll_bypass << 15) | (clk_cfg0 & 0x7fff);
		dib7000p_write_word(state, 900, clk_cfg0);
	}

	dib7000p_write_word(state, 18, (u16) (((bw->internal * 1000) >> 16) & 0xffff));
	dib7000p_write_word(state, 19, (u16) ((bw->internal * 1000) & 0xffff));
	dib7000p_write_word(state, 21, (u16) ((bw->ifreq >> 16) & 0xffff));
	dib7000p_write_word(state, 22, (u16) ((bw->ifreq) & 0xffff));

	dib7000p_write_word(state, 72, bw->sad_cfg);
}

static u32 dib7000p_get_internal_freq(struct dib7000p_state *state)
{
	u32 internal = (u32) dib7000p_read_word(state, 18) << 16;
	internal |= (u32) dib7000p_read_word(state, 19);
	internal /= 1000;

	return internal;
}

static int dib7000p_update_pll(struct dvb_frontend *fe, struct dibx000_bandwidth_config *bw)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 reg_1857, reg_1856 = dib7000p_read_word(state, 1856);
	u8 loopdiv, prediv;
	u32 internal, xtal;

	/* get back old values */
	prediv = reg_1856 & 0x3f;
	loopdiv = (reg_1856 >> 6) & 0x3f;

	if ((bw != NULL) && (bw->pll_prediv != prediv || bw->pll_ratio != loopdiv)) {
		dprintk("Updating pll (prediv: old =  %d new = %d ; loopdiv : old = %d new = %d)\n", prediv, bw->pll_prediv, loopdiv, bw->pll_ratio);
		reg_1856 &= 0xf000;
		reg_1857 = dib7000p_read_word(state, 1857);
		dib7000p_write_word(state, 1857, reg_1857 & ~(1 << 15));

		dib7000p_write_word(state, 1856, reg_1856 | ((bw->pll_ratio & 0x3f) << 6) | (bw->pll_prediv & 0x3f));

		/* write new system clk into P_sec_len */
		internal = dib7000p_get_internal_freq(state);
		xtal = (internal / loopdiv) * prediv;
		internal = 1000 * (xtal / bw->pll_prediv) * bw->pll_ratio;	/* new internal */
		dib7000p_write_word(state, 18, (u16) ((internal >> 16) & 0xffff));
		dib7000p_write_word(state, 19, (u16) (internal & 0xffff));

		dib7000p_write_word(state, 1857, reg_1857 | (1 << 15));

		while (((dib7000p_read_word(state, 1856) >> 15) & 0x1) != 1)
			dprintk("Waiting for PLL to lock\n");

		return 0;
	}
	return -EIO;
}

static int dib7000p_reset_gpio(struct dib7000p_state *st)
{
	/* reset the GPIOs */
	dprintk("gpio dir: %x: val: %x, pwm_pos: %x\n", st->gpio_dir, st->gpio_val, st->cfg.gpio_pwm_pos);

	dib7000p_write_word(st, 1029, st->gpio_dir);
	dib7000p_write_word(st, 1030, st->gpio_val);

	/* TODO 1031 is P_gpio_od */

	dib7000p_write_word(st, 1032, st->cfg.gpio_pwm_pos);

	dib7000p_write_word(st, 1037, st->cfg.pwm_freq_div);
	return 0;
}

static int dib7000p_cfg_gpio(struct dib7000p_state *st, u8 num, u8 dir, u8 val)
{
	st->gpio_dir = dib7000p_read_word(st, 1029);
	st->gpio_dir &= ~(1 << num);	/* reset the direction bit */
	st->gpio_dir |= (dir & 0x1) << num;	/* set the new direction */
	dib7000p_write_word(st, 1029, st->gpio_dir);

	st->gpio_val = dib7000p_read_word(st, 1030);
	st->gpio_val &= ~(1 << num);	/* reset the direction bit */
	st->gpio_val |= (val & 0x01) << num;	/* set the new value */
	dib7000p_write_word(st, 1030, st->gpio_val);

	return 0;
}

static int dib7000p_set_gpio(struct dvb_frontend *demod, u8 num, u8 dir, u8 val)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	return dib7000p_cfg_gpio(state, num, dir, val);
}

static u16 dib7000p_defaults[] = {
	// auto search configuration
	3, 2,
	0x0004,
	(1<<3)|(1<<11)|(1<<12)|(1<<13),
	0x0814,			/* Equal Lock */

	12, 6,
	0x001b,
	0x7740,
	0x005b,
	0x8d80,
	0x01c9,
	0xc380,
	0x0000,
	0x0080,
	0x0000,
	0x0090,
	0x0001,
	0xd4c0,

	1, 26,
	0x6680,

	/* set ADC level to -16 */
	11, 79,
	(1 << 13) - 825 - 117,
	(1 << 13) - 837 - 117,
	(1 << 13) - 811 - 117,
	(1 << 13) - 766 - 117,
	(1 << 13) - 737 - 117,
	(1 << 13) - 693 - 117,
	(1 << 13) - 648 - 117,
	(1 << 13) - 619 - 117,
	(1 << 13) - 575 - 117,
	(1 << 13) - 531 - 117,
	(1 << 13) - 501 - 117,

	1, 142,
	0x0410,

	/* disable power smoothing */
	8, 145,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,

	1, 154,
	1 << 13,

	1, 168,
	0x0ccd,

	1, 183,
	0x200f,

	1, 212,
		0x169,

	5, 187,
	0x023d,
	0x00a4,
	0x00a4,
	0x7ff0,
	0x3ccc,

	1, 198,
	0x800,

	1, 222,
	0x0010,

	1, 235,
	0x0062,

	0,
};

static void dib7000p_reset_stats(struct dvb_frontend *fe);

static int dib7000p_demod_reset(struct dib7000p_state *state)
{
	dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);

	if (state->version == SOC7090)
		dibx000_reset_i2c_master(&state->i2c_master);

	dib7000p_set_adc_state(state, DIBX000_VBG_ENABLE);

	/* restart all parts */
	dib7000p_write_word(state, 770, 0xffff);
	dib7000p_write_word(state, 771, 0xffff);
	dib7000p_write_word(state, 772, 0x001f);
	dib7000p_write_word(state, 1280, 0x001f - ((1 << 4) | (1 << 3)));

	dib7000p_write_word(state, 770, 0);
	dib7000p_write_word(state, 771, 0);
	dib7000p_write_word(state, 772, 0);
	dib7000p_write_word(state, 1280, 0);

	if (state->version != SOC7090) {
		dib7000p_write_word(state,  898, 0x0003);
		dib7000p_write_word(state,  898, 0);
	}

	/* default */
	dib7000p_reset_pll(state);

	if (dib7000p_reset_gpio(state) != 0)
		dprintk("GPIO reset was not successful.\n");

	if (state->version == SOC7090) {
		dib7000p_write_word(state, 899, 0);

		/* impulse noise */
		dib7000p_write_word(state, 42, (1<<5) | 3); /* P_iqc_thsat_ipc = 1 ; P_iqc_win2 = 3 */
		dib7000p_write_word(state, 43, 0x2d4); /*-300 fag P_iqc_dect_min = -280 */
		dib7000p_write_word(state, 44, 300); /* 300 fag P_iqc_dect_min = +280 */
		dib7000p_write_word(state, 273, (0<<6) | 30);
	}
	if (dib7000p_set_output_mode(state, OUTMODE_HIGH_Z) != 0)
		dprintk("OUTPUT_MODE could not be reset.\n");

	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_ON);
	dib7000p_sad_calib(state);
	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_OFF);

	/* unforce divstr regardless whether i2c enumeration was done or not */
	dib7000p_write_word(state, 1285, dib7000p_read_word(state, 1285) & ~(1 << 1));

	dib7000p_set_bandwidth(state, 8000);

	if (state->version == SOC7090) {
		dib7000p_write_word(state, 36, 0x0755);/* P_iqc_impnc_on =1 & P_iqc_corr_inh = 1 for impulsive noise */
	} else {
		if (state->cfg.tuner_is_baseband)
			dib7000p_write_word(state, 36, 0x0755);
		else
			dib7000p_write_word(state, 36, 0x1f55);
	}

	dib7000p_write_tab(state, dib7000p_defaults);
	if (state->version != SOC7090) {
		dib7000p_write_word(state, 901, 0x0006);
		dib7000p_write_word(state, 902, (3 << 10) | (1 << 6));
		dib7000p_write_word(state, 905, 0x2c8e);
	}

	dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);

	return 0;
}

static void dib7000p_pll_clk_cfg(struct dib7000p_state *state)
{
	u16 tmp = 0;
	tmp = dib7000p_read_word(state, 903);
	dib7000p_write_word(state, 903, (tmp | 0x1));
	tmp = dib7000p_read_word(state, 900);
	dib7000p_write_word(state, 900, (tmp & 0x7fff) | (1 << 6));
}

static void dib7000p_restart_agc(struct dib7000p_state *state)
{
	// P_restart_iqc & P_restart_agc
	dib7000p_write_word(state, 770, (1 << 11) | (1 << 9));
	dib7000p_write_word(state, 770, 0x0000);
}

static int dib7000p_update_lna(struct dib7000p_state *state)
{
	u16 dyn_gain;

	if (state->cfg.update_lna) {
		dyn_gain = dib7000p_read_word(state, 394);
		if (state->cfg.update_lna(&state->demod, dyn_gain)) {
			dib7000p_restart_agc(state);
			return 1;
		}
	}

	return 0;
}

static int dib7000p_set_agc_config(struct dib7000p_state *state, u8 band)
{
	struct dibx000_agc_config *agc = NULL;
	int i;
	if (state->current_band == band && state->current_agc != NULL)
		return 0;
	state->current_band = band;

	for (i = 0; i < state->cfg.agc_config_count; i++)
		if (state->cfg.agc[i].band_caps & band) {
			agc = &state->cfg.agc[i];
			break;
		}

	if (agc == NULL) {
		dprintk("no valid AGC configuration found for band 0x%02x\n", band);
		return -EINVAL;
	}

	state->current_agc = agc;

	/* AGC */
	dib7000p_write_word(state, 75, agc->setup);
	dib7000p_write_word(state, 76, agc->inv_gain);
	dib7000p_write_word(state, 77, agc->time_stabiliz);
	dib7000p_write_word(state, 100, (agc->alpha_level << 12) | agc->thlock);

	// Demod AGC loop configuration
	dib7000p_write_word(state, 101, (agc->alpha_mant << 5) | agc->alpha_exp);
	dib7000p_write_word(state, 102, (agc->beta_mant << 6) | agc->beta_exp);

	/* AGC continued */
	dprintk("WBD: ref: %d, sel: %d, active: %d, alpha: %d\n",
		state->wbd_ref != 0 ? state->wbd_ref : agc->wbd_ref, agc->wbd_sel, !agc->perform_agc_softsplit, agc->wbd_sel);

	if (state->wbd_ref != 0)
		dib7000p_write_word(state, 105, (agc->wbd_inv << 12) | state->wbd_ref);
	else
		dib7000p_write_word(state, 105, (agc->wbd_inv << 12) | agc->wbd_ref);

	dib7000p_write_word(state, 106, (agc->wbd_sel << 13) | (agc->wbd_alpha << 9) | (agc->perform_agc_softsplit << 8));

	dib7000p_write_word(state, 107, agc->agc1_max);
	dib7000p_write_word(state, 108, agc->agc1_min);
	dib7000p_write_word(state, 109, agc->agc2_max);
	dib7000p_write_word(state, 110, agc->agc2_min);
	dib7000p_write_word(state, 111, (agc->agc1_pt1 << 8) | agc->agc1_pt2);
	dib7000p_write_word(state, 112, agc->agc1_pt3);
	dib7000p_write_word(state, 113, (agc->agc1_slope1 << 8) | agc->agc1_slope2);
	dib7000p_write_word(state, 114, (agc->agc2_pt1 << 8) | agc->agc2_pt2);
	dib7000p_write_word(state, 115, (agc->agc2_slope1 << 8) | agc->agc2_slope2);
	return 0;
}

static int dib7000p_set_dds(struct dib7000p_state *state, s32 offset_khz)
{
	u32 internal = dib7000p_get_internal_freq(state);
	s32 unit_khz_dds_val;
	u32 abs_offset_khz = abs(offset_khz);
	u32 dds = state->cfg.bw->ifreq & 0x1ffffff;
	u8 invert = !!(state->cfg.bw->ifreq & (1 << 25));
	if (internal == 0) {
		pr_warn("DIB7000P: dib7000p_get_internal_freq returned 0\n");
		return -1;
	}
	/* 2**26 / Fsampling is the unit 1KHz offset */
	unit_khz_dds_val = 67108864 / (internal);

	dprintk("setting a frequency offset of %dkHz internal freq = %d invert = %d\n", offset_khz, internal, invert);

	if (offset_khz < 0)
		unit_khz_dds_val *= -1;

	/* IF tuner */
	if (invert)
		dds -= (abs_offset_khz * unit_khz_dds_val);	/* /100 because of /100 on the unit_khz_dds_val line calc for better accuracy */
	else
		dds += (abs_offset_khz * unit_khz_dds_val);

	if (abs_offset_khz <= (internal / 2)) {	/* Max dds offset is the half of the demod freq */
		dib7000p_write_word(state, 21, (u16) (((dds >> 16) & 0x1ff) | (0 << 10) | (invert << 9)));
		dib7000p_write_word(state, 22, (u16) (dds & 0xffff));
	}
	return 0;
}

static int dib7000p_agc_startup(struct dvb_frontend *demod)
{
	struct dtv_frontend_properties *ch = &demod->dtv_property_cache;
	struct dib7000p_state *state = demod->demodulator_priv;
	int ret = -1;
	u8 *agc_state = &state->agc_state;
	u8 agc_split;
	u16 reg;
	u32 upd_demod_gain_period = 0x1000;
	s32 frequency_offset = 0;

	switch (state->agc_state) {
	case 0:
		dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);
		if (state->version == SOC7090) {
			reg = dib7000p_read_word(state, 0x79b) & 0xff00;
			dib7000p_write_word(state, 0x79a, upd_demod_gain_period & 0xFFFF);	/* lsb */
			dib7000p_write_word(state, 0x79b, reg | (1 << 14) | ((upd_demod_gain_period >> 16) & 0xFF));

			/* enable adc i & q */
			reg = dib7000p_read_word(state, 0x780);
			dib7000p_write_word(state, 0x780, (reg | (0x3)) & (~(1 << 7)));
		} else {
			dib7000p_set_adc_state(state, DIBX000_ADC_ON);
			dib7000p_pll_clk_cfg(state);
		}

		if (dib7000p_set_agc_config(state, BAND_OF_FREQUENCY(ch->frequency / 1000)) != 0)
			return -1;

		if (demod->ops.tuner_ops.get_frequency) {
			u32 frequency_tuner;

			demod->ops.tuner_ops.get_frequency(demod, &frequency_tuner);
			frequency_offset = (s32)frequency_tuner / 1000 - ch->frequency / 1000;
		}

		if (dib7000p_set_dds(state, frequency_offset) < 0)
			return -1;

		ret = 7;
		(*agc_state)++;
		break;

	case 1:
		if (state->cfg.agc_control)
			state->cfg.agc_control(&state->demod, 1);

		dib7000p_write_word(state, 78, 32768);
		if (!state->current_agc->perform_agc_softsplit) {
			/* we are using the wbd - so slow AGC startup */
			/* force 0 split on WBD and restart AGC */
			dib7000p_write_word(state, 106, (state->current_agc->wbd_sel << 13) | (state->current_agc->wbd_alpha << 9) | (1 << 8));
			(*agc_state)++;
			ret = 5;
		} else {
			/* default AGC startup */
			(*agc_state) = 4;
			/* wait AGC rough lock time */
			ret = 7;
		}

		dib7000p_restart_agc(state);
		break;

	case 2:		/* fast split search path after 5sec */
		dib7000p_write_word(state, 75, state->current_agc->setup | (1 << 4));	/* freeze AGC loop */
		dib7000p_write_word(state, 106, (state->current_agc->wbd_sel << 13) | (2 << 9) | (0 << 8));	/* fast split search 0.25kHz */
		(*agc_state)++;
		ret = 14;
		break;

	case 3:		/* split search ended */
		agc_split = (u8) dib7000p_read_word(state, 396);	/* store the split value for the next time */
		dib7000p_write_word(state, 78, dib7000p_read_word(state, 394));	/* set AGC gain start value */

		dib7000p_write_word(state, 75, state->current_agc->setup);	/* std AGC loop */
		dib7000p_write_word(state, 106, (state->current_agc->wbd_sel << 13) | (state->current_agc->wbd_alpha << 9) | agc_split);	/* standard split search */

		dib7000p_restart_agc(state);

		dprintk("SPLIT %p: %hd\n", demod, agc_split);

		(*agc_state)++;
		ret = 5;
		break;

	case 4:		/* LNA startup */
		ret = 7;

		if (dib7000p_update_lna(state))
			ret = 5;
		else
			(*agc_state)++;
		break;

	case 5:
		if (state->cfg.agc_control)
			state->cfg.agc_control(&state->demod, 0);
		(*agc_state)++;
		break;
	default:
		break;
	}
	return ret;
}

static void dib7000p_update_timf(struct dib7000p_state *state)
{
	u32 timf = (dib7000p_read_word(state, 427) << 16) | dib7000p_read_word(state, 428);
	state->timf = timf * 160 / (state->current_bandwidth / 50);
	dib7000p_write_word(state, 23, (u16) (timf >> 16));
	dib7000p_write_word(state, 24, (u16) (timf & 0xffff));
	dprintk("updated timf_frequency: %d (default: %d)\n", state->timf, state->cfg.bw->timf);

}

static u32 dib7000p_ctrl_timf(struct dvb_frontend *fe, u8 op, u32 timf)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	switch (op) {
	case DEMOD_TIMF_SET:
		state->timf = timf;
		break;
	case DEMOD_TIMF_UPDATE:
		dib7000p_update_timf(state);
		break;
	case DEMOD_TIMF_GET:
		break;
	}
	dib7000p_set_bandwidth(state, state->current_bandwidth);
	return state->timf;
}

static void dib7000p_set_channel(struct dib7000p_state *state,
				 struct dtv_frontend_properties *ch, u8 seq)
{
	u16 value, est[4];

	dib7000p_set_bandwidth(state, BANDWIDTH_TO_KHZ(ch->bandwidth_hz));

	/* nfft, guard, qam, alpha */
	value = 0;
	switch (ch->transmission_mode) {
	case TRANSMISSION_MODE_2K:
		value |= (0 << 7);
		break;
	case TRANSMISSION_MODE_4K:
		value |= (2 << 7);
		break;
	default:
	case TRANSMISSION_MODE_8K:
		value |= (1 << 7);
		break;
	}
	switch (ch->guard_interval) {
	case GUARD_INTERVAL_1_32:
		value |= (0 << 5);
		break;
	case GUARD_INTERVAL_1_16:
		value |= (1 << 5);
		break;
	case GUARD_INTERVAL_1_4:
		value |= (3 << 5);
		break;
	default:
	case GUARD_INTERVAL_1_8:
		value |= (2 << 5);
		break;
	}
	switch (ch->modulation) {
	case QPSK:
		value |= (0 << 3);
		break;
	case QAM_16:
		value |= (1 << 3);
		break;
	default:
	case QAM_64:
		value |= (2 << 3);
		break;
	}
	switch (HIERARCHY_1) {
	case HIERARCHY_2:
		value |= 2;
		break;
	case HIERARCHY_4:
		value |= 4;
		break;
	default:
	case HIERARCHY_1:
		value |= 1;
		break;
	}
	dib7000p_write_word(state, 0, value);
	dib7000p_write_word(state, 5, (seq << 4) | 1);	/* do not force tps, search list 0 */

	/* P_dintl_native, P_dintlv_inv, P_hrch, P_code_rate, P_select_hp */
	value = 0;
	if (1 != 0)
		value |= (1 << 6);
	if (ch->hierarchy == 1)
		value |= (1 << 4);
	if (1 == 1)
		value |= 1;
	switch ((ch->hierarchy == 0 || 1 == 1) ? ch->code_rate_HP : ch->code_rate_LP) {
	case FEC_2_3:
		value |= (2 << 1);
		break;
	case FEC_3_4:
		value |= (3 << 1);
		break;
	case FEC_5_6:
		value |= (5 << 1);
		break;
	case FEC_7_8:
		value |= (7 << 1);
		break;
	default:
	case FEC_1_2:
		value |= (1 << 1);
		break;
	}
	dib7000p_write_word(state, 208, value);

	/* offset loop parameters */
	dib7000p_write_word(state, 26, 0x6680);
	dib7000p_write_word(state, 32, 0x0003);
	dib7000p_write_word(state, 29, 0x1273);
	dib7000p_write_word(state, 33, 0x0005);

	/* P_dvsy_sync_wait */
	switch (ch->transmission_mode) {
	case TRANSMISSION_MODE_8K:
		value = 256;
		break;
	case TRANSMISSION_MODE_4K:
		value = 128;
		break;
	case TRANSMISSION_MODE_2K:
	default:
		value = 64;
		break;
	}
	switch (ch->guard_interval) {
	case GUARD_INTERVAL_1_16:
		value *= 2;
		break;
	case GUARD_INTERVAL_1_8:
		value *= 4;
		break;
	case GUARD_INTERVAL_1_4:
		value *= 8;
		break;
	default:
	case GUARD_INTERVAL_1_32:
		value *= 1;
		break;
	}
	if (state->cfg.diversity_delay == 0)
		state->div_sync_wait = (value * 3) / 2 + 48;
	else
		state->div_sync_wait = (value * 3) / 2 + state->cfg.diversity_delay;

	/* deactivate the possibility of diversity reception if extended interleaver */
	state->div_force_off = !1 && ch->transmission_mode != TRANSMISSION_MODE_8K;
	dib7000p_set_diversity_in(&state->demod, state->div_state);

	/* channel estimation fine configuration */
	switch (ch->modulation) {
	case QAM_64:
		est[0] = 0x0148;	/* P_adp_regul_cnt 0.04 */
		est[1] = 0xfff0;	/* P_adp_noise_cnt -0.002 */
		est[2] = 0x00a4;	/* P_adp_regul_ext 0.02 */
		est[3] = 0xfff8;	/* P_adp_noise_ext -0.001 */
		break;
	case QAM_16:
		est[0] = 0x023d;	/* P_adp_regul_cnt 0.07 */
		est[1] = 0xffdf;	/* P_adp_noise_cnt -0.004 */
		est[2] = 0x00a4;	/* P_adp_regul_ext 0.02 */
		est[3] = 0xfff0;	/* P_adp_noise_ext -0.002 */
		break;
	default:
		est[0] = 0x099a;	/* P_adp_regul_cnt 0.3 */
		est[1] = 0xffae;	/* P_adp_noise_cnt -0.01 */
		est[2] = 0x0333;	/* P_adp_regul_ext 0.1 */
		est[3] = 0xfff8;	/* P_adp_noise_ext -0.002 */
		break;
	}
	for (value = 0; value < 4; value++)
		dib7000p_write_word(state, 187 + value, est[value]);
}

static int dib7000p_autosearch_start(struct dvb_frontend *demod)
{
	struct dtv_frontend_properties *ch = &demod->dtv_property_cache;
	struct dib7000p_state *state = demod->demodulator_priv;
	struct dtv_frontend_properties schan;
	u32 value, factor;
	u32 internal = dib7000p_get_internal_freq(state);

	schan = *ch;
	schan.modulation = QAM_64;
	schan.guard_interval = GUARD_INTERVAL_1_32;
	schan.transmission_mode = TRANSMISSION_MODE_8K;
	schan.code_rate_HP = FEC_2_3;
	schan.code_rate_LP = FEC_3_4;
	schan.hierarchy = 0;

	dib7000p_set_channel(state, &schan, 7);

	factor = BANDWIDTH_TO_KHZ(ch->bandwidth_hz);
	if (factor >= 5000) {
		if (state->version == SOC7090)
			factor = 2;
		else
			factor = 1;
	} else
		factor = 6;

	value = 30 * internal * factor;
	dib7000p_write_word(state, 6, (u16) ((value >> 16) & 0xffff));
	dib7000p_write_word(state, 7, (u16) (value & 0xffff));
	value = 100 * internal * factor;
	dib7000p_write_word(state, 8, (u16) ((value >> 16) & 0xffff));
	dib7000p_write_word(state, 9, (u16) (value & 0xffff));
	value = 500 * internal * factor;
	dib7000p_write_word(state, 10, (u16) ((value >> 16) & 0xffff));
	dib7000p_write_word(state, 11, (u16) (value & 0xffff));

	value = dib7000p_read_word(state, 0);
	dib7000p_write_word(state, 0, (u16) ((1 << 9) | value));
	dib7000p_read_word(state, 1284);
	dib7000p_write_word(state, 0, (u16) value);

	return 0;
}

static int dib7000p_autosearch_is_irq(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	u16 irq_pending = dib7000p_read_word(state, 1284);

	if (irq_pending & 0x1)
		return 1;

	if (irq_pending & 0x2)
		return 2;

	return 0;
}

static void dib7000p_spur_protect(struct dib7000p_state *state, u32 rf_khz, u32 bw)
{
	static s16 notch[] = { 16143, 14402, 12238, 9713, 6902, 3888, 759, -2392 };
	static u8 sine[] = { 0, 2, 3, 5, 6, 8, 9, 11, 13, 14, 16, 17, 19, 20, 22,
		24, 25, 27, 28, 30, 31, 33, 34, 36, 38, 39, 41, 42, 44, 45, 47, 48, 50, 51,
		53, 55, 56, 58, 59, 61, 62, 64, 65, 67, 68, 70, 71, 73, 74, 76, 77, 79, 80,
		82, 83, 85, 86, 88, 89, 91, 92, 94, 95, 97, 98, 99, 101, 102, 104, 105,
		107, 108, 109, 111, 112, 114, 115, 117, 118, 119, 121, 122, 123, 125, 126,
		128, 129, 130, 132, 133, 134, 136, 137, 138, 140, 141, 142, 144, 145, 146,
		147, 149, 150, 151, 152, 154, 155, 156, 157, 159, 160, 161, 162, 164, 165,
		166, 167, 168, 170, 171, 172, 173, 174, 175, 177, 178, 179, 180, 181, 182,
		183, 184, 185, 186, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198,
		199, 200, 201, 202, 203, 204, 205, 206, 207, 207, 208, 209, 210, 211, 212,
		213, 214, 215, 215, 216, 217, 218, 219, 220, 220, 221, 222, 223, 224, 224,
		225, 226, 227, 227, 228, 229, 229, 230, 231, 231, 232, 233, 233, 234, 235,
		235, 236, 237, 237, 238, 238, 239, 239, 240, 241, 241, 242, 242, 243, 243,
		244, 244, 245, 245, 245, 246, 246, 247, 247, 248, 248, 248, 249, 249, 249,
		250, 250, 250, 251, 251, 251, 252, 252, 252, 252, 253, 253, 253, 253, 254,
		254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255
	};

	u32 xtal = state->cfg.bw->xtal_hz / 1000;
	int f_rel = DIV_ROUND_CLOSEST(rf_khz, xtal) * xtal - rf_khz;
	int k;
	int coef_re[8], coef_im[8];
	int bw_khz = bw;
	u32 pha;

	dprintk("relative position of the Spur: %dk (RF: %dk, XTAL: %dk)\n", f_rel, rf_khz, xtal);

	if (f_rel < -bw_khz / 2 || f_rel > bw_khz / 2)
		return;

	bw_khz /= 100;

	dib7000p_write_word(state, 142, 0x0610);

	for (k = 0; k < 8; k++) {
		pha = ((f_rel * (k + 1) * 112 * 80 / bw_khz) / 1000) & 0x3ff;

		if (pha == 0) {
			coef_re[k] = 256;
			coef_im[k] = 0;
		} else if (pha < 256) {
			coef_re[k] = sine[256 - (pha & 0xff)];
			coef_im[k] = sine[pha & 0xff];
		} else if (pha == 256) {
			coef_re[k] = 0;
			coef_im[k] = 256;
		} else if (pha < 512) {
			coef_re[k] = -sine[pha & 0xff];
			coef_im[k] = sine[256 - (pha & 0xff)];
		} else if (pha == 512) {
			coef_re[k] = -256;
			coef_im[k] = 0;
		} else if (pha < 768) {
			coef_re[k] = -sine[256 - (pha & 0xff)];
			coef_im[k] = -sine[pha & 0xff];
		} else if (pha == 768) {
			coef_re[k] = 0;
			coef_im[k] = -256;
		} else {
			coef_re[k] = sine[pha & 0xff];
			coef_im[k] = -sine[256 - (pha & 0xff)];
		}

		coef_re[k] *= notch[k];
		coef_re[k] += (1 << 14);
		if (coef_re[k] >= (1 << 24))
			coef_re[k] = (1 << 24) - 1;
		coef_re[k] /= (1 << 15);

		coef_im[k] *= notch[k];
		coef_im[k] += (1 << 14);
		if (coef_im[k] >= (1 << 24))
			coef_im[k] = (1 << 24) - 1;
		coef_im[k] /= (1 << 15);

		dprintk("PALF COEF: %d re: %d im: %d\n", k, coef_re[k], coef_im[k]);

		dib7000p_write_word(state, 143, (0 << 14) | (k << 10) | (coef_re[k] & 0x3ff));
		dib7000p_write_word(state, 144, coef_im[k] & 0x3ff);
		dib7000p_write_word(state, 143, (1 << 14) | (k << 10) | (coef_re[k] & 0x3ff));
	}
	dib7000p_write_word(state, 143, 0);
}

static int dib7000p_tune(struct dvb_frontend *demod)
{
	struct dtv_frontend_properties *ch = &demod->dtv_property_cache;
	struct dib7000p_state *state = demod->demodulator_priv;
	u16 tmp = 0;

	if (ch != NULL)
		dib7000p_set_channel(state, ch, 0);
	else
		return -EINVAL;

	// restart demod
	dib7000p_write_word(state, 770, 0x4000);
	dib7000p_write_word(state, 770, 0x0000);
	msleep(45);

	/* P_ctrl_inh_cor=0, P_ctrl_alpha_cor=4, P_ctrl_inh_isi=0, P_ctrl_alpha_isi=3, P_ctrl_inh_cor4=1, P_ctrl_alpha_cor4=3 */
	tmp = (0 << 14) | (4 << 10) | (0 << 9) | (3 << 5) | (1 << 4) | (0x3);
	if (state->sfn_workaround_active) {
		dprintk("SFN workaround is active\n");
		tmp |= (1 << 9);
		dib7000p_write_word(state, 166, 0x4000);
	} else {
		dib7000p_write_word(state, 166, 0x0000);
	}
	dib7000p_write_word(state, 29, tmp);

	// never achieved a lock with that bandwidth so far - wait for osc-freq to update
	if (state->timf == 0)
		msleep(200);

	/* offset loop parameters */

	/* P_timf_alpha, P_corm_alpha=6, P_corm_thres=0x80 */
	tmp = (6 << 8) | 0x80;
	switch (ch->transmission_mode) {
	case TRANSMISSION_MODE_2K:
		tmp |= (2 << 12);
		break;
	case TRANSMISSION_MODE_4K:
		tmp |= (3 << 12);
		break;
	default:
	case TRANSMISSION_MODE_8K:
		tmp |= (4 << 12);
		break;
	}
	dib7000p_write_word(state, 26, tmp);	/* timf_a(6xxx) */

	/* P_ctrl_freeze_pha_shift=0, P_ctrl_pha_off_max */
	tmp = (0 << 4);
	switch (ch->transmission_mode) {
	case TRANSMISSION_MODE_2K:
		tmp |= 0x6;
		break;
	case TRANSMISSION_MODE_4K:
		tmp |= 0x7;
		break;
	default:
	case TRANSMISSION_MODE_8K:
		tmp |= 0x8;
		break;
	}
	dib7000p_write_word(state, 32, tmp);

	/* P_ctrl_sfreq_inh=0, P_ctrl_sfreq_step */
	tmp = (0 << 4);
	switch (ch->transmission_mode) {
	case TRANSMISSION_MODE_2K:
		tmp |= 0x6;
		break;
	case TRANSMISSION_MODE_4K:
		tmp |= 0x7;
		break;
	default:
	case TRANSMISSION_MODE_8K:
		tmp |= 0x8;
		break;
	}
	dib7000p_write_word(state, 33, tmp);

	tmp = dib7000p_read_word(state, 509);
	if (!((tmp >> 6) & 0x1)) {
		/* restart the fec */
		tmp = dib7000p_read_word(state, 771);
		dib7000p_write_word(state, 771, tmp | (1 << 1));
		dib7000p_write_word(state, 771, tmp);
		msleep(40);
		tmp = dib7000p_read_word(state, 509);
	}
	// we achieved a lock - it's time to update the osc freq
	if ((tmp >> 6) & 0x1) {
		dib7000p_update_timf(state);
		/* P_timf_alpha += 2 */
		tmp = dib7000p_read_word(state, 26);
		dib7000p_write_word(state, 26, (tmp & ~(0xf << 12)) | ((((tmp >> 12) & 0xf) + 5) << 12));
	}

	if (state->cfg.spur_protect)
		dib7000p_spur_protect(state, ch->frequency / 1000, BANDWIDTH_TO_KHZ(ch->bandwidth_hz));

	dib7000p_set_bandwidth(state, BANDWIDTH_TO_KHZ(ch->bandwidth_hz));

	dib7000p_reset_stats(demod);

	return 0;
}

static int dib7000p_wakeup(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);
	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_ON);
	if (state->version == SOC7090)
		dib7000p_sad_calib(state);
	return 0;
}

static int dib7000p_sleep(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	if (state->version == SOC7090)
		return dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);
	return dib7000p_set_output_mode(state, OUTMODE_HIGH_Z) | dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);
}

static int dib7000p_identify(struct dib7000p_state *st)
{
	u16 value;
	dprintk("checking demod on I2C address: %d (%x)\n", st->i2c_addr, st->i2c_addr);

	if ((value = dib7000p_read_word(st, 768)) != 0x01b3) {
		dprintk("wrong Vendor ID (read=0x%x)\n", value);
		return -EREMOTEIO;
	}

	if ((value = dib7000p_read_word(st, 769)) != 0x4000) {
		dprintk("wrong Device ID (%x)\n", value);
		return -EREMOTEIO;
	}

	return 0;
}

static int dib7000p_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *fep)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 tps = dib7000p_read_word(state, 463);

	fep->inversion = INVERSION_AUTO;

	fep->bandwidth_hz = BANDWIDTH_TO_HZ(state->current_bandwidth);

	switch ((tps >> 8) & 0x3) {
	case 0:
		fep->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		fep->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	/* case 2: fep->transmission_mode = TRANSMISSION_MODE_4K; break; */
	}

	switch (tps & 0x3) {
	case 0:
		fep->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		fep->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		fep->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		fep->guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	switch ((tps >> 14) & 0x3) {
	case 0:
		fep->modulation = QPSK;
		break;
	case 1:
		fep->modulation = QAM_16;
		break;
	case 2:
	default:
		fep->modulation = QAM_64;
		break;
	}

	/* as long as the frontend_param structure is fixed for hierarchical transmission I refuse to use it */
	/* (tps >> 13) & 0x1 == hrch is used, (tps >> 10) & 0x7 == alpha */

	fep->hierarchy = HIERARCHY_NONE;
	switch ((tps >> 5) & 0x7) {
	case 1:
		fep->code_rate_HP = FEC_1_2;
		break;
	case 2:
		fep->code_rate_HP = FEC_2_3;
		break;
	case 3:
		fep->code_rate_HP = FEC_3_4;
		break;
	case 5:
		fep->code_rate_HP = FEC_5_6;
		break;
	case 7:
	default:
		fep->code_rate_HP = FEC_7_8;
		break;

	}

	switch ((tps >> 2) & 0x7) {
	case 1:
		fep->code_rate_LP = FEC_1_2;
		break;
	case 2:
		fep->code_rate_LP = FEC_2_3;
		break;
	case 3:
		fep->code_rate_LP = FEC_3_4;
		break;
	case 5:
		fep->code_rate_LP = FEC_5_6;
		break;
	case 7:
	default:
		fep->code_rate_LP = FEC_7_8;
		break;
	}

	/* native interleaver: (dib7000p_read_word(state, 464) >>  5) & 0x1 */

	return 0;
}

static int dib7000p_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fep = &fe->dtv_property_cache;
	struct dib7000p_state *state = fe->demodulator_priv;
	int time, ret;

	if (state->version == SOC7090)
		dib7090_set_diversity_in(fe, 0);
	else
		dib7000p_set_output_mode(state, OUTMODE_HIGH_Z);

	/* maybe the parameter has been changed */
	state->sfn_workaround_active = buggy_sfn_workaround;

	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	/* start up the AGC */
	state->agc_state = 0;
	do {
		time = dib7000p_agc_startup(fe);
		if (time != -1)
			msleep(time);
	} while (time != -1);

	if (fep->transmission_mode == TRANSMISSION_MODE_AUTO ||
		fep->guard_interval == GUARD_INTERVAL_AUTO || fep->modulation == QAM_AUTO || fep->code_rate_HP == FEC_AUTO) {
		int i = 800, found;

		dib7000p_autosearch_start(fe);
		do {
			msleep(1);
			found = dib7000p_autosearch_is_irq(fe);
		} while (found == 0 && i--);

		dprintk("autosearch returns: %d\n", found);
		if (found == 0 || found == 1)
			return 0;

		dib7000p_get_frontend(fe, fep);
	}

	ret = dib7000p_tune(fe);

	/* make this a config parameter */
	if (state->version == SOC7090) {
		dib7090_set_output_mode(fe, state->cfg.output_mode);
		if (state->cfg.enMpegOutput == 0) {
			dib7090_setDibTxMux(state, MPEG_ON_DIBTX);
			dib7090_setHostBusMux(state, DIBTX_ON_HOSTBUS);
		}
	} else
		dib7000p_set_output_mode(state, state->cfg.output_mode);

	return ret;
}

static int dib7000p_get_stats(struct dvb_frontend *fe, enum fe_status stat);

static int dib7000p_read_status(struct dvb_frontend *fe, enum fe_status *stat)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 lock = dib7000p_read_word(state, 509);

	*stat = 0;

	if (lock & 0x8000)
		*stat |= FE_HAS_SIGNAL;
	if (lock & 0x3000)
		*stat |= FE_HAS_CARRIER;
	if (lock & 0x0100)
		*stat |= FE_HAS_VITERBI;
	if (lock & 0x0010)
		*stat |= FE_HAS_SYNC;
	if ((lock & 0x0038) == 0x38)
		*stat |= FE_HAS_LOCK;

	dib7000p_get_stats(fe, *stat);

	return 0;
}

static int dib7000p_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	*ber = (dib7000p_read_word(state, 500) << 16) | dib7000p_read_word(state, 501);
	return 0;
}

static int dib7000p_read_unc_blocks(struct dvb_frontend *fe, u32 * unc)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	*unc = dib7000p_read_word(state, 506);
	return 0;
}

static int dib7000p_read_signal_strength(struct dvb_frontend *fe, u16 * strength)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 val = dib7000p_read_word(state, 394);
	*strength = 65535 - val;
	return 0;
}

static u32 dib7000p_get_snr(struct dvb_frontend *fe)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 val;
	s32 signal_mant, signal_exp, noise_mant, noise_exp;
	u32 result = 0;

	val = dib7000p_read_word(state, 479);
	noise_mant = (val >> 4) & 0xff;
	noise_exp = ((val & 0xf) << 2);
	val = dib7000p_read_word(state, 480);
	noise_exp += ((val >> 14) & 0x3);
	if ((noise_exp & 0x20) != 0)
		noise_exp -= 0x40;

	signal_mant = (val >> 6) & 0xFF;
	signal_exp = (val & 0x3F);
	if ((signal_exp & 0x20) != 0)
		signal_exp -= 0x40;

	if (signal_mant != 0)
		result = intlog10(2) * 10 * signal_exp + 10 * intlog10(signal_mant);
	else
		result = intlog10(2) * 10 * signal_exp - 100;

	if (noise_mant != 0)
		result -= intlog10(2) * 10 * noise_exp + 10 * intlog10(noise_mant);
	else
		result -= intlog10(2) * 10 * noise_exp - 100;

	return result;
}

static int dib7000p_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	u32 result;

	result = dib7000p_get_snr(fe);

	*snr = result / ((1 << 24) / 10);
	return 0;
}

static void dib7000p_reset_stats(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	struct dtv_frontend_properties *c = &demod->dtv_property_cache;
	u32 ucb;

	memset(&c->strength, 0, sizeof(c->strength));
	memset(&c->cnr, 0, sizeof(c->cnr));
	memset(&c->post_bit_error, 0, sizeof(c->post_bit_error));
	memset(&c->post_bit_count, 0, sizeof(c->post_bit_count));
	memset(&c->block_error, 0, sizeof(c->block_error));

	c->strength.len = 1;
	c->cnr.len = 1;
	c->block_error.len = 1;
	c->block_count.len = 1;
	c->post_bit_error.len = 1;
	c->post_bit_count.len = 1;

	c->strength.stat[0].scale = FE_SCALE_DECIBEL;
	c->strength.stat[0].uvalue = 0;

	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	dib7000p_read_unc_blocks(demod, &ucb);

	state->old_ucb = ucb;
	state->ber_jiffies_stats = 0;
	state->per_jiffies_stats = 0;
}

struct linear_segments {
	unsigned x;
	signed y;
};

/*
 * Table to estimate signal strength in dBm.
 * This table should be empirically determinated by measuring the signal
 * strength generated by a RF generator directly connected into
 * a device.
 * This table was determinated by measuring the signal strength generated
 * by a DTA-2111 RF generator directly connected into a dib7000p device
 * (a Hauppauge Nova-TD stick), using a good quality 3 meters length
 * RC6 cable and good RC6 connectors, connected directly to antenna 1.
 * As the minimum output power of DTA-2111 is -31dBm, a 16 dBm attenuator
 * were used, for the lower power values.
 * The real value can actually be on other devices, or even at the
 * second antena input, depending on several factors, like if LNA
 * is enabled or not, if diversity is enabled, type of connectors, etc.
 * Yet, it is better to use this measure in dB than a random non-linear
 * percentage value, especially for antenna adjustments.
 * On my tests, the precision of the measure using this table is about
 * 0.5 dB, with sounds reasonable enough to adjust antennas.
 */
#define DB_OFFSET 131000

static struct linear_segments strength_to_db_table[] = {
	{ 63630, DB_OFFSET - 20500},
	{ 62273, DB_OFFSET - 21000},
	{ 60162, DB_OFFSET - 22000},
	{ 58730, DB_OFFSET - 23000},
	{ 58294, DB_OFFSET - 24000},
	{ 57778, DB_OFFSET - 25000},
	{ 57320, DB_OFFSET - 26000},
	{ 56779, DB_OFFSET - 27000},
	{ 56293, DB_OFFSET - 28000},
	{ 55724, DB_OFFSET - 29000},
	{ 55145, DB_OFFSET - 30000},
	{ 54680, DB_OFFSET - 31000},
	{ 54293, DB_OFFSET - 32000},
	{ 53813, DB_OFFSET - 33000},
	{ 53427, DB_OFFSET - 34000},
	{ 52981, DB_OFFSET - 35000},

	{ 52636, DB_OFFSET - 36000},
	{ 52014, DB_OFFSET - 37000},
	{ 51674, DB_OFFSET - 38000},
	{ 50692, DB_OFFSET - 39000},
	{ 49824, DB_OFFSET - 40000},
	{ 49052, DB_OFFSET - 41000},
	{ 48436, DB_OFFSET - 42000},
	{ 47836, DB_OFFSET - 43000},
	{ 47368, DB_OFFSET - 44000},
	{ 46468, DB_OFFSET - 45000},
	{ 45597, DB_OFFSET - 46000},
	{ 44586, DB_OFFSET - 47000},
	{ 43667, DB_OFFSET - 48000},
	{ 42673, DB_OFFSET - 49000},
	{ 41816, DB_OFFSET - 50000},
	{ 40876, DB_OFFSET - 51000},
	{     0,      0},
};

static u32 interpolate_value(u32 value, struct linear_segments *segments,
			     unsigned len)
{
	u64 tmp64;
	u32 dx;
	s32 dy;
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
	dy = segments[i - 1].y - segments[i].y;
	dx = segments[i - 1].x - segments[i].x;

	tmp64 = value - segments[i].x;
	tmp64 *= dy;
	do_div(tmp64, dx);
	ret = segments[i].y + tmp64;

	return ret;
}

/* FIXME: may require changes - this one was borrowed from dib8000 */
static u32 dib7000p_get_time_us(struct dvb_frontend *demod)
{
	struct dtv_frontend_properties *c = &demod->dtv_property_cache;
	u64 time_us, tmp64;
	u32 tmp, denom;
	int guard, rate_num, rate_denum = 1, bits_per_symbol;
	int interleaving = 0, fft_div;

	switch (c->guard_interval) {
	case GUARD_INTERVAL_1_4:
		guard = 4;
		break;
	case GUARD_INTERVAL_1_8:
		guard = 8;
		break;
	case GUARD_INTERVAL_1_16:
		guard = 16;
		break;
	default:
	case GUARD_INTERVAL_1_32:
		guard = 32;
		break;
	}

	switch (c->transmission_mode) {
	case TRANSMISSION_MODE_2K:
		fft_div = 4;
		break;
	case TRANSMISSION_MODE_4K:
		fft_div = 2;
		break;
	default:
	case TRANSMISSION_MODE_8K:
		fft_div = 1;
		break;
	}

	switch (c->modulation) {
	case DQPSK:
	case QPSK:
		bits_per_symbol = 2;
		break;
	case QAM_16:
		bits_per_symbol = 4;
		break;
	default:
	case QAM_64:
		bits_per_symbol = 6;
		break;
	}

	switch ((c->hierarchy == 0 || 1 == 1) ? c->code_rate_HP : c->code_rate_LP) {
	case FEC_1_2:
		rate_num = 1;
		rate_denum = 2;
		break;
	case FEC_2_3:
		rate_num = 2;
		rate_denum = 3;
		break;
	case FEC_3_4:
		rate_num = 3;
		rate_denum = 4;
		break;
	case FEC_5_6:
		rate_num = 5;
		rate_denum = 6;
		break;
	default:
	case FEC_7_8:
		rate_num = 7;
		rate_denum = 8;
		break;
	}

	denom = bits_per_symbol * rate_num * fft_div * 384;

	/*
	 * FIXME: check if the math makes sense. If so, fill the
	 * interleaving var.
	 */

	/* If calculus gets wrong, wait for 1s for the next stats */
	if (!denom)
		return 0;

	/* Estimate the period for the total bit rate */
	time_us = rate_denum * (1008 * 1562500L);
	tmp64 = time_us;
	do_div(tmp64, guard);
	time_us = time_us + tmp64;
	time_us += denom / 2;
	do_div(time_us, denom);

	tmp = 1008 * 96 * interleaving;
	time_us += tmp + tmp / guard;

	return time_us;
}

static int dib7000p_get_stats(struct dvb_frontend *demod, enum fe_status stat)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	struct dtv_frontend_properties *c = &demod->dtv_property_cache;
	int show_per_stats = 0;
	u32 time_us = 0, val, snr;
	u64 blocks, ucb;
	s32 db;
	u16 strength;

	/* Get Signal strength */
	dib7000p_read_signal_strength(demod, &strength);
	val = strength;
	db = interpolate_value(val,
			       strength_to_db_table,
			       ARRAY_SIZE(strength_to_db_table)) - DB_OFFSET;
	c->strength.stat[0].svalue = db;

	/* UCB/BER/CNR measures require lock */
	if (!(stat & FE_HAS_LOCK)) {
		c->cnr.len = 1;
		c->block_count.len = 1;
		c->block_error.len = 1;
		c->post_bit_error.len = 1;
		c->post_bit_count.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return 0;
	}

	/* Check if time for stats was elapsed */
	if (time_after(jiffies, state->per_jiffies_stats)) {
		state->per_jiffies_stats = jiffies + msecs_to_jiffies(1000);

		/* Get SNR */
		snr = dib7000p_get_snr(demod);
		if (snr)
			snr = (1000L * snr) >> 24;
		else
			snr = 0;
		c->cnr.stat[0].svalue = snr;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;

		/* Get UCB measures */
		dib7000p_read_unc_blocks(demod, &val);
		ucb = val - state->old_ucb;
		if (val < state->old_ucb)
			ucb += 0x100000000LL;

		c->block_error.stat[0].scale = FE_SCALE_COUNTER;
		c->block_error.stat[0].uvalue = ucb;

		/* Estimate the number of packets based on bitrate */
		if (!time_us)
			time_us = dib7000p_get_time_us(demod);

		if (time_us) {
			blocks = 1250000ULL * 1000000ULL;
			do_div(blocks, time_us * 8 * 204);
			c->block_count.stat[0].scale = FE_SCALE_COUNTER;
			c->block_count.stat[0].uvalue += blocks;
		}

		show_per_stats = 1;
	}

	/* Get post-BER measures */
	if (time_after(jiffies, state->ber_jiffies_stats)) {
		time_us = dib7000p_get_time_us(demod);
		state->ber_jiffies_stats = jiffies + msecs_to_jiffies((time_us + 500) / 1000);

		dprintk("Next all layers stats available in %u us.\n", time_us);

		dib7000p_read_ber(demod, &val);
		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue += val;

		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue += 100000000;
	}

	/* Get PER measures */
	if (show_per_stats) {
		dib7000p_read_unc_blocks(demod, &val);

		c->block_error.stat[0].scale = FE_SCALE_COUNTER;
		c->block_error.stat[0].uvalue += val;

		time_us = dib7000p_get_time_us(demod);
		if (time_us) {
			blocks = 1250000ULL * 1000000ULL;
			do_div(blocks, time_us * 8 * 204);
			c->block_count.stat[0].scale = FE_SCALE_COUNTER;
			c->block_count.stat[0].uvalue += blocks;
		}
	}
	return 0;
}

static int dib7000p_fe_get_tune_settings(struct dvb_frontend *fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void dib7000p_release(struct dvb_frontend *demod)
{
	struct dib7000p_state *st = demod->demodulator_priv;
	dibx000_exit_i2c_master(&st->i2c_master);
	i2c_del_adapter(&st->dib7090_tuner_adap);
	kfree(st);
}

static int dib7000pc_detection(struct i2c_adapter *i2c_adap)
{
	u8 *tx, *rx;
	struct i2c_msg msg[2] = {
		{.addr = 18 >> 1, .flags = 0, .len = 2},
		{.addr = 18 >> 1, .flags = I2C_M_RD, .len = 2},
	};
	int ret = 0;

	tx = kzalloc(2, GFP_KERNEL);
	if (!tx)
		return -ENOMEM;
	rx = kzalloc(2, GFP_KERNEL);
	if (!rx) {
		ret = -ENOMEM;
		goto rx_memory_error;
	}

	msg[0].buf = tx;
	msg[1].buf = rx;

	tx[0] = 0x03;
	tx[1] = 0x00;

	if (i2c_transfer(i2c_adap, msg, 2) == 2)
		if (rx[0] == 0x01 && rx[1] == 0xb3) {
			dprintk("-D-  DiB7000PC detected\n");
			return 1;
		}

	msg[0].addr = msg[1].addr = 0x40;

	if (i2c_transfer(i2c_adap, msg, 2) == 2)
		if (rx[0] == 0x01 && rx[1] == 0xb3) {
			dprintk("-D-  DiB7000PC detected\n");
			return 1;
		}

	dprintk("-D-  DiB7000PC not detected\n");

	kfree(rx);
rx_memory_error:
	kfree(tx);
	return ret;
}

static struct i2c_adapter *dib7000p_get_i2c_master(struct dvb_frontend *demod, enum dibx000_i2c_interface intf, int gating)
{
	struct dib7000p_state *st = demod->demodulator_priv;
	return dibx000_get_i2c_adapter(&st->i2c_master, intf, gating);
}

static int dib7000p_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 val = dib7000p_read_word(state, 235) & 0xffef;
	val |= (onoff & 0x1) << 4;
	dprintk("PID filter enabled %d\n", onoff);
	return dib7000p_write_word(state, 235, val);
}

static int dib7000p_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	dprintk("PID filter: index %x, PID %d, OnOff %d\n", id, pid, onoff);
	return dib7000p_write_word(state, 241 + id, onoff ? (1 << 13) | pid : 0);
}

static int dib7000p_i2c_enumeration(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, struct dib7000p_config cfg[])
{
	struct dib7000p_state *dpst;
	int k = 0;
	u8 new_addr = 0;

	dpst = kzalloc(sizeof(struct dib7000p_state), GFP_KERNEL);
	if (!dpst)
		return -ENOMEM;

	dpst->i2c_adap = i2c;
	mutex_init(&dpst->i2c_buffer_lock);

	for (k = no_of_demods - 1; k >= 0; k--) {
		dpst->cfg = cfg[k];

		/* designated i2c address */
		if (cfg[k].default_i2c_addr != 0)
			new_addr = cfg[k].default_i2c_addr + (k << 1);
		else
			new_addr = (0x40 + k) << 1;
		dpst->i2c_addr = new_addr;
		dib7000p_write_word(dpst, 1287, 0x0003);	/* sram lead in, rdy */
		if (dib7000p_identify(dpst) != 0) {
			dpst->i2c_addr = default_addr;
			dib7000p_write_word(dpst, 1287, 0x0003);	/* sram lead in, rdy */
			if (dib7000p_identify(dpst) != 0) {
				dprintk("DiB7000P #%d: not identified\n", k);
				kfree(dpst);
				return -EIO;
			}
		}

		/* start diversity to pull_down div_str - just for i2c-enumeration */
		dib7000p_set_output_mode(dpst, OUTMODE_DIVERSITY);

		/* set new i2c address and force divstart */
		dib7000p_write_word(dpst, 1285, (new_addr << 2) | 0x2);

		dprintk("IC %d initialized (to i2c_address 0x%x)\n", k, new_addr);
	}

	for (k = 0; k < no_of_demods; k++) {
		dpst->cfg = cfg[k];
		if (cfg[k].default_i2c_addr != 0)
			dpst->i2c_addr = (cfg[k].default_i2c_addr + k) << 1;
		else
			dpst->i2c_addr = (0x40 + k) << 1;

		// unforce divstr
		dib7000p_write_word(dpst, 1285, dpst->i2c_addr << 2);

		/* deactivate div - it was just for i2c-enumeration */
		dib7000p_set_output_mode(dpst, OUTMODE_HIGH_Z);
	}

	kfree(dpst);
	return 0;
}

static const s32 lut_1000ln_mant[] = {
	6908, 6956, 7003, 7047, 7090, 7131, 7170, 7208, 7244, 7279, 7313, 7346, 7377, 7408, 7438, 7467, 7495, 7523, 7549, 7575, 7600
};

static s32 dib7000p_get_adc_power(struct dvb_frontend *fe)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u32 tmp_val = 0, exp = 0, mant = 0;
	s32 pow_i;
	u16 buf[2];
	u8 ix = 0;

	buf[0] = dib7000p_read_word(state, 0x184);
	buf[1] = dib7000p_read_word(state, 0x185);
	pow_i = (buf[0] << 16) | buf[1];
	dprintk("raw pow_i = %d\n", pow_i);

	tmp_val = pow_i;
	while (tmp_val >>= 1)
		exp++;

	mant = (pow_i * 1000 / (1 << exp));
	dprintk(" mant = %d exp = %d\n", mant / 1000, exp);

	ix = (u8) ((mant - 1000) / 100);	/* index of the LUT */
	dprintk(" ix = %d\n", ix);

	pow_i = (lut_1000ln_mant[ix] + 693 * (exp - 20) - 6908);
	pow_i = (pow_i << 8) / 1000;
	dprintk(" pow_i = %d\n", pow_i);

	return pow_i;
}

static int map_addr_to_serpar_number(struct i2c_msg *msg)
{
	if ((msg->buf[0] <= 15))
		msg->buf[0] -= 1;
	else if (msg->buf[0] == 17)
		msg->buf[0] = 15;
	else if (msg->buf[0] == 16)
		msg->buf[0] = 17;
	else if (msg->buf[0] == 19)
		msg->buf[0] = 16;
	else if (msg->buf[0] >= 21 && msg->buf[0] <= 25)
		msg->buf[0] -= 3;
	else if (msg->buf[0] == 28)
		msg->buf[0] = 23;
	else
		return -EINVAL;
	return 0;
}

static int w7090p_tuner_write_serpar(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dib7000p_state *state = i2c_get_adapdata(i2c_adap);
	u8 n_overflow = 1;
	u16 i = 1000;
	u16 serpar_num = msg[0].buf[0];

	while (n_overflow == 1 && i) {
		n_overflow = (dib7000p_read_word(state, 1984) >> 1) & 0x1;
		i--;
		if (i == 0)
			dprintk("Tuner ITF: write busy (overflow)\n");
	}
	dib7000p_write_word(state, 1985, (1 << 6) | (serpar_num & 0x3f));
	dib7000p_write_word(state, 1986, (msg[0].buf[1] << 8) | msg[0].buf[2]);

	return num;
}

static int w7090p_tuner_read_serpar(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dib7000p_state *state = i2c_get_adapdata(i2c_adap);
	u8 n_overflow = 1, n_empty = 1;
	u16 i = 1000;
	u16 serpar_num = msg[0].buf[0];
	u16 read_word;

	while (n_overflow == 1 && i) {
		n_overflow = (dib7000p_read_word(state, 1984) >> 1) & 0x1;
		i--;
		if (i == 0)
			dprintk("TunerITF: read busy (overflow)\n");
	}
	dib7000p_write_word(state, 1985, (0 << 6) | (serpar_num & 0x3f));

	i = 1000;
	while (n_empty == 1 && i) {
		n_empty = dib7000p_read_word(state, 1984) & 0x1;
		i--;
		if (i == 0)
			dprintk("TunerITF: read busy (empty)\n");
	}
	read_word = dib7000p_read_word(state, 1987);
	msg[1].buf[0] = (read_word >> 8) & 0xff;
	msg[1].buf[1] = (read_word) & 0xff;

	return num;
}

static int w7090p_tuner_rw_serpar(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	if (map_addr_to_serpar_number(&msg[0]) == 0) {	/* else = Tuner regs to ignore : DIG_CFG, CTRL_RF_LT, PLL_CFG, PWM1_REG, ADCCLK, DIG_CFG_3; SLEEP_EN... */
		if (num == 1) {	/* write */
			return w7090p_tuner_write_serpar(i2c_adap, msg, 1);
		} else {	/* read */
			return w7090p_tuner_read_serpar(i2c_adap, msg, 2);
		}
	}
	return num;
}

static int dib7090p_rw_on_apb(struct i2c_adapter *i2c_adap,
		struct i2c_msg msg[], int num, u16 apb_address)
{
	struct dib7000p_state *state = i2c_get_adapdata(i2c_adap);
	u16 word;

	if (num == 1) {		/* write */
		dib7000p_write_word(state, apb_address, ((msg[0].buf[1] << 8) | (msg[0].buf[2])));
	} else {
		word = dib7000p_read_word(state, apb_address);
		msg[1].buf[0] = (word >> 8) & 0xff;
		msg[1].buf[1] = (word) & 0xff;
	}

	return num;
}

static int dib7090_tuner_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dib7000p_state *state = i2c_get_adapdata(i2c_adap);

	u16 apb_address = 0, word;
	int i = 0;
	switch (msg[0].buf[0]) {
	case 0x12:
		apb_address = 1920;
		break;
	case 0x14:
		apb_address = 1921;
		break;
	case 0x24:
		apb_address = 1922;
		break;
	case 0x1a:
		apb_address = 1923;
		break;
	case 0x22:
		apb_address = 1924;
		break;
	case 0x33:
		apb_address = 1926;
		break;
	case 0x34:
		apb_address = 1927;
		break;
	case 0x35:
		apb_address = 1928;
		break;
	case 0x36:
		apb_address = 1929;
		break;
	case 0x37:
		apb_address = 1930;
		break;
	case 0x38:
		apb_address = 1931;
		break;
	case 0x39:
		apb_address = 1932;
		break;
	case 0x2a:
		apb_address = 1935;
		break;
	case 0x2b:
		apb_address = 1936;
		break;
	case 0x2c:
		apb_address = 1937;
		break;
	case 0x2d:
		apb_address = 1938;
		break;
	case 0x2e:
		apb_address = 1939;
		break;
	case 0x2f:
		apb_address = 1940;
		break;
	case 0x30:
		apb_address = 1941;
		break;
	case 0x31:
		apb_address = 1942;
		break;
	case 0x32:
		apb_address = 1943;
		break;
	case 0x3e:
		apb_address = 1944;
		break;
	case 0x3f:
		apb_address = 1945;
		break;
	case 0x40:
		apb_address = 1948;
		break;
	case 0x25:
		apb_address = 914;
		break;
	case 0x26:
		apb_address = 915;
		break;
	case 0x27:
		apb_address = 917;
		break;
	case 0x28:
		apb_address = 916;
		break;
	case 0x1d:
		i = ((dib7000p_read_word(state, 72) >> 12) & 0x3);
		word = dib7000p_read_word(state, 384 + i);
		msg[1].buf[0] = (word >> 8) & 0xff;
		msg[1].buf[1] = (word) & 0xff;
		return num;
	case 0x1f:
		if (num == 1) {	/* write */
			word = (u16) ((msg[0].buf[1] << 8) | msg[0].buf[2]);
			word &= 0x3;
			word = (dib7000p_read_word(state, 72) & ~(3 << 12)) | (word << 12);
			dib7000p_write_word(state, 72, word);	/* Set the proper input */
			return num;
		}
	}

	if (apb_address != 0)	/* R/W access via APB */
		return dib7090p_rw_on_apb(i2c_adap, msg, num, apb_address);
	else			/* R/W access via SERPAR  */
		return w7090p_tuner_rw_serpar(i2c_adap, msg, num);

	return 0;
}

static u32 dib7000p_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm dib7090_tuner_xfer_algo = {
	.master_xfer = dib7090_tuner_xfer,
	.functionality = dib7000p_i2c_func,
};

static struct i2c_adapter *dib7090_get_i2c_tuner(struct dvb_frontend *fe)
{
	struct dib7000p_state *st = fe->demodulator_priv;
	return &st->dib7090_tuner_adap;
}

static int dib7090_host_bus_drive(struct dib7000p_state *state, u8 drive)
{
	u16 reg;

	/* drive host bus 2, 3, 4 */
	reg = dib7000p_read_word(state, 1798) & ~((0x7) | (0x7 << 6) | (0x7 << 12));
	reg |= (drive << 12) | (drive << 6) | drive;
	dib7000p_write_word(state, 1798, reg);

	/* drive host bus 5,6 */
	reg = dib7000p_read_word(state, 1799) & ~((0x7 << 2) | (0x7 << 8));
	reg |= (drive << 8) | (drive << 2);
	dib7000p_write_word(state, 1799, reg);

	/* drive host bus 7, 8, 9 */
	reg = dib7000p_read_word(state, 1800) & ~((0x7) | (0x7 << 6) | (0x7 << 12));
	reg |= (drive << 12) | (drive << 6) | drive;
	dib7000p_write_word(state, 1800, reg);

	/* drive host bus 10, 11 */
	reg = dib7000p_read_word(state, 1801) & ~((0x7 << 2) | (0x7 << 8));
	reg |= (drive << 8) | (drive << 2);
	dib7000p_write_word(state, 1801, reg);

	/* drive host bus 12, 13, 14 */
	reg = dib7000p_read_word(state, 1802) & ~((0x7) | (0x7 << 6) | (0x7 << 12));
	reg |= (drive << 12) | (drive << 6) | drive;
	dib7000p_write_word(state, 1802, reg);

	return 0;
}

static u32 dib7090_calcSyncFreq(u32 P_Kin, u32 P_Kout, u32 insertExtSynchro, u32 syncSize)
{
	u32 quantif = 3;
	u32 nom = (insertExtSynchro * P_Kin + syncSize);
	u32 denom = P_Kout;
	u32 syncFreq = ((nom << quantif) / denom);

	if ((syncFreq & ((1 << quantif) - 1)) != 0)
		syncFreq = (syncFreq >> quantif) + 1;
	else
		syncFreq = (syncFreq >> quantif);

	if (syncFreq != 0)
		syncFreq = syncFreq - 1;

	return syncFreq;
}

static int dib7090_cfg_DibTx(struct dib7000p_state *state, u32 P_Kin, u32 P_Kout, u32 insertExtSynchro, u32 synchroMode, u32 syncWord, u32 syncSize)
{
	dprintk("Configure DibStream Tx\n");

	dib7000p_write_word(state, 1615, 1);
	dib7000p_write_word(state, 1603, P_Kin);
	dib7000p_write_word(state, 1605, P_Kout);
	dib7000p_write_word(state, 1606, insertExtSynchro);
	dib7000p_write_word(state, 1608, synchroMode);
	dib7000p_write_word(state, 1609, (syncWord >> 16) & 0xffff);
	dib7000p_write_word(state, 1610, syncWord & 0xffff);
	dib7000p_write_word(state, 1612, syncSize);
	dib7000p_write_word(state, 1615, 0);

	return 0;
}

static int dib7090_cfg_DibRx(struct dib7000p_state *state, u32 P_Kin, u32 P_Kout, u32 synchroMode, u32 insertExtSynchro, u32 syncWord, u32 syncSize,
		u32 dataOutRate)
{
	u32 syncFreq;

	dprintk("Configure DibStream Rx\n");
	if ((P_Kin != 0) && (P_Kout != 0)) {
		syncFreq = dib7090_calcSyncFreq(P_Kin, P_Kout, insertExtSynchro, syncSize);
		dib7000p_write_word(state, 1542, syncFreq);
	}
	dib7000p_write_word(state, 1554, 1);
	dib7000p_write_word(state, 1536, P_Kin);
	dib7000p_write_word(state, 1537, P_Kout);
	dib7000p_write_word(state, 1539, synchroMode);
	dib7000p_write_word(state, 1540, (syncWord >> 16) & 0xffff);
	dib7000p_write_word(state, 1541, syncWord & 0xffff);
	dib7000p_write_word(state, 1543, syncSize);
	dib7000p_write_word(state, 1544, dataOutRate);
	dib7000p_write_word(state, 1554, 0);

	return 0;
}

static void dib7090_enMpegMux(struct dib7000p_state *state, int onoff)
{
	u16 reg_1287 = dib7000p_read_word(state, 1287);

	switch (onoff) {
	case 1:
			reg_1287 &= ~(1<<7);
			break;
	case 0:
			reg_1287 |= (1<<7);
			break;
	}

	dib7000p_write_word(state, 1287, reg_1287);
}

static void dib7090_configMpegMux(struct dib7000p_state *state,
		u16 pulseWidth, u16 enSerialMode, u16 enSerialClkDiv2)
{
	dprintk("Enable Mpeg mux\n");

	dib7090_enMpegMux(state, 0);

	/* If the input mode is MPEG do not divide the serial clock */
	if ((enSerialMode == 1) && (state->input_mode_mpeg == 1))
		enSerialClkDiv2 = 0;

	dib7000p_write_word(state, 1287, ((pulseWidth & 0x1f) << 2)
			| ((enSerialMode & 0x1) << 1)
			| (enSerialClkDiv2 & 0x1));

	dib7090_enMpegMux(state, 1);
}

static void dib7090_setDibTxMux(struct dib7000p_state *state, int mode)
{
	u16 reg_1288 = dib7000p_read_word(state, 1288) & ~(0x7 << 7);

	switch (mode) {
	case MPEG_ON_DIBTX:
			dprintk("SET MPEG ON DIBSTREAM TX\n");
			dib7090_cfg_DibTx(state, 8, 5, 0, 0, 0, 0);
			reg_1288 |= (1<<9);
			break;
	case DIV_ON_DIBTX:
			dprintk("SET DIV_OUT ON DIBSTREAM TX\n");
			dib7090_cfg_DibTx(state, 5, 5, 0, 0, 0, 0);
			reg_1288 |= (1<<8);
			break;
	case ADC_ON_DIBTX:
			dprintk("SET ADC_OUT ON DIBSTREAM TX\n");
			dib7090_cfg_DibTx(state, 20, 5, 10, 0, 0, 0);
			reg_1288 |= (1<<7);
			break;
	default:
			break;
	}
	dib7000p_write_word(state, 1288, reg_1288);
}

static void dib7090_setHostBusMux(struct dib7000p_state *state, int mode)
{
	u16 reg_1288 = dib7000p_read_word(state, 1288) & ~(0x7 << 4);

	switch (mode) {
	case DEMOUT_ON_HOSTBUS:
			dprintk("SET DEM OUT OLD INTERF ON HOST BUS\n");
			dib7090_enMpegMux(state, 0);
			reg_1288 |= (1<<6);
			break;
	case DIBTX_ON_HOSTBUS:
			dprintk("SET DIBSTREAM TX ON HOST BUS\n");
			dib7090_enMpegMux(state, 0);
			reg_1288 |= (1<<5);
			break;
	case MPEG_ON_HOSTBUS:
			dprintk("SET MPEG MUX ON HOST BUS\n");
			reg_1288 |= (1<<4);
			break;
	default:
			break;
	}
	dib7000p_write_word(state, 1288, reg_1288);
}

static int dib7090_set_diversity_in(struct dvb_frontend *fe, int onoff)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 reg_1287;

	switch (onoff) {
	case 0: /* only use the internal way - not the diversity input */
			dprintk("%s mode OFF : by default Enable Mpeg INPUT\n", __func__);
			dib7090_cfg_DibRx(state, 8, 5, 0, 0, 0, 8, 0);

			/* Do not divide the serial clock of MPEG MUX */
			/* in SERIAL MODE in case input mode MPEG is used */
			reg_1287 = dib7000p_read_word(state, 1287);
			/* enSerialClkDiv2 == 1 ? */
			if ((reg_1287 & 0x1) == 1) {
				/* force enSerialClkDiv2 = 0 */
				reg_1287 &= ~0x1;
				dib7000p_write_word(state, 1287, reg_1287);
			}
			state->input_mode_mpeg = 1;
			break;
	case 1: /* both ways */
	case 2: /* only the diversity input */
			dprintk("%s ON : Enable diversity INPUT\n", __func__);
			dib7090_cfg_DibRx(state, 5, 5, 0, 0, 0, 0, 0);
			state->input_mode_mpeg = 0;
			break;
	}

	dib7000p_set_diversity_in(&state->demod, onoff);
	return 0;
}

static int dib7090_set_output_mode(struct dvb_frontend *fe, int mode)
{
	struct dib7000p_state *state = fe->demodulator_priv;

	u16 outreg, smo_mode, fifo_threshold;
	u8 prefer_mpeg_mux_use = 1;
	int ret = 0;

	dib7090_host_bus_drive(state, 1);

	fifo_threshold = 1792;
	smo_mode = (dib7000p_read_word(state, 235) & 0x0050) | (1 << 1);
	outreg = dib7000p_read_word(state, 1286) & ~((1 << 10) | (0x7 << 6) | (1 << 1));

	switch (mode) {
	case OUTMODE_HIGH_Z:
		outreg = 0;
		break;

	case OUTMODE_MPEG2_SERIAL:
		if (prefer_mpeg_mux_use) {
			dprintk("setting output mode TS_SERIAL using Mpeg Mux\n");
			dib7090_configMpegMux(state, 3, 1, 1);
			dib7090_setHostBusMux(state, MPEG_ON_HOSTBUS);
		} else {/* Use Smooth block */
			dprintk("setting output mode TS_SERIAL using Smooth bloc\n");
			dib7090_setHostBusMux(state, DEMOUT_ON_HOSTBUS);
			outreg |= (2<<6) | (0 << 1);
		}
		break;

	case OUTMODE_MPEG2_PAR_GATED_CLK:
		if (prefer_mpeg_mux_use) {
			dprintk("setting output mode TS_PARALLEL_GATED using Mpeg Mux\n");
			dib7090_configMpegMux(state, 2, 0, 0);
			dib7090_setHostBusMux(state, MPEG_ON_HOSTBUS);
		} else { /* Use Smooth block */
			dprintk("setting output mode TS_PARALLEL_GATED using Smooth block\n");
			dib7090_setHostBusMux(state, DEMOUT_ON_HOSTBUS);
			outreg |= (0<<6);
		}
		break;

	case OUTMODE_MPEG2_PAR_CONT_CLK:	/* Using Smooth block only */
		dprintk("setting output mode TS_PARALLEL_CONT using Smooth block\n");
		dib7090_setHostBusMux(state, DEMOUT_ON_HOSTBUS);
		outreg |= (1<<6);
		break;

	case OUTMODE_MPEG2_FIFO:	/* Using Smooth block because not supported by new Mpeg Mux bloc */
		dprintk("setting output mode TS_FIFO using Smooth block\n");
		dib7090_setHostBusMux(state, DEMOUT_ON_HOSTBUS);
		outreg |= (5<<6);
		smo_mode |= (3 << 1);
		fifo_threshold = 512;
		break;

	case OUTMODE_DIVERSITY:
		dprintk("setting output mode MODE_DIVERSITY\n");
		dib7090_setDibTxMux(state, DIV_ON_DIBTX);
		dib7090_setHostBusMux(state, DIBTX_ON_HOSTBUS);
		break;

	case OUTMODE_ANALOG_ADC:
		dprintk("setting output mode MODE_ANALOG_ADC\n");
		dib7090_setDibTxMux(state, ADC_ON_DIBTX);
		dib7090_setHostBusMux(state, DIBTX_ON_HOSTBUS);
		break;
	}
	if (mode != OUTMODE_HIGH_Z)
		outreg |= (1 << 10);

	if (state->cfg.output_mpeg2_in_188_bytes)
		smo_mode |= (1 << 5);

	ret |= dib7000p_write_word(state, 235, smo_mode);
	ret |= dib7000p_write_word(state, 236, fifo_threshold);	/* synchronous fread */
	ret |= dib7000p_write_word(state, 1286, outreg);

	return ret;
}

static int dib7090_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 en_cur_state;

	dprintk("sleep dib7090: %d\n", onoff);

	en_cur_state = dib7000p_read_word(state, 1922);

	if (en_cur_state > 0xff)
		state->tuner_enable = en_cur_state;

	if (onoff)
		en_cur_state &= 0x00ff;
	else {
		if (state->tuner_enable != 0)
			en_cur_state = state->tuner_enable;
	}

	dib7000p_write_word(state, 1922, en_cur_state);

	return 0;
}

static int dib7090_get_adc_power(struct dvb_frontend *fe)
{
	return dib7000p_get_adc_power(fe);
}

static int dib7090_slave_reset(struct dvb_frontend *fe)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 reg;

	reg = dib7000p_read_word(state, 1794);
	dib7000p_write_word(state, 1794, reg | (4 << 12));

	dib7000p_write_word(state, 1032, 0xffff);
	return 0;
}

static const struct dvb_frontend_ops dib7000p_ops;
static struct dvb_frontend *dib7000p_init(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib7000p_config *cfg)
{
	struct dvb_frontend *demod;
	struct dib7000p_state *st;
	st = kzalloc(sizeof(struct dib7000p_state), GFP_KERNEL);
	if (st == NULL)
		return NULL;

	memcpy(&st->cfg, cfg, sizeof(struct dib7000p_config));
	st->i2c_adap = i2c_adap;
	st->i2c_addr = i2c_addr;
	st->gpio_val = cfg->gpio_val;
	st->gpio_dir = cfg->gpio_dir;

	/* Ensure the output mode remains at the previous default if it's
	 * not specifically set by the caller.
	 */
	if ((st->cfg.output_mode != OUTMODE_MPEG2_SERIAL) && (st->cfg.output_mode != OUTMODE_MPEG2_PAR_GATED_CLK))
		st->cfg.output_mode = OUTMODE_MPEG2_FIFO;

	demod = &st->demod;
	demod->demodulator_priv = st;
	memcpy(&st->demod.ops, &dib7000p_ops, sizeof(struct dvb_frontend_ops));
	mutex_init(&st->i2c_buffer_lock);

	dib7000p_write_word(st, 1287, 0x0003);	/* sram lead in, rdy */

	if (dib7000p_identify(st) != 0)
		goto error;

	st->version = dib7000p_read_word(st, 897);

	/* FIXME: make sure the dev.parent field is initialized, or else
	   request_firmware() will hit an OOPS (this should be moved somewhere
	   more common) */
	st->i2c_master.gated_tuner_i2c_adap.dev.parent = i2c_adap->dev.parent;

	dibx000_init_i2c_master(&st->i2c_master, DIB7000P, st->i2c_adap, st->i2c_addr);

	/* init 7090 tuner adapter */
	strscpy(st->dib7090_tuner_adap.name, "DiB7090 tuner interface",
		sizeof(st->dib7090_tuner_adap.name));
	st->dib7090_tuner_adap.algo = &dib7090_tuner_xfer_algo;
	st->dib7090_tuner_adap.algo_data = NULL;
	st->dib7090_tuner_adap.dev.parent = st->i2c_adap->dev.parent;
	i2c_set_adapdata(&st->dib7090_tuner_adap, st);
	i2c_add_adapter(&st->dib7090_tuner_adap);

	dib7000p_demod_reset(st);

	dib7000p_reset_stats(demod);

	if (st->version == SOC7090) {
		dib7090_set_output_mode(demod, st->cfg.output_mode);
		dib7090_set_diversity_in(demod, 0);
	}

	return demod;

error:
	kfree(st);
	return NULL;
}

void *dib7000p_attach(struct dib7000p_ops *ops)
{
	if (!ops)
		return NULL;

	ops->slave_reset = dib7090_slave_reset;
	ops->get_adc_power = dib7090_get_adc_power;
	ops->dib7000pc_detection = dib7000pc_detection;
	ops->get_i2c_tuner = dib7090_get_i2c_tuner;
	ops->tuner_sleep = dib7090_tuner_sleep;
	ops->init = dib7000p_init;
	ops->set_agc1_min = dib7000p_set_agc1_min;
	ops->set_gpio = dib7000p_set_gpio;
	ops->i2c_enumeration = dib7000p_i2c_enumeration;
	ops->pid_filter = dib7000p_pid_filter;
	ops->pid_filter_ctrl = dib7000p_pid_filter_ctrl;
	ops->get_i2c_master = dib7000p_get_i2c_master;
	ops->update_pll = dib7000p_update_pll;
	ops->ctrl_timf = dib7000p_ctrl_timf;
	ops->get_agc_values = dib7000p_get_agc_values;
	ops->set_wbd_ref = dib7000p_set_wbd_ref;

	return ops;
}
EXPORT_SYMBOL(dib7000p_attach);

static const struct dvb_frontend_ops dib7000p_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		 .name = "DiBcom 7000PC",
		 .frequency_min_hz =  44250 * kHz,
		 .frequency_max_hz = 867250 * kHz,
		 .frequency_stepsize_hz = 62500,
		 .caps = FE_CAN_INVERSION_AUTO |
		 FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		 FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		 FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		 FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_RECOVER | FE_CAN_HIERARCHY_AUTO,
		 },

	.release = dib7000p_release,

	.init = dib7000p_wakeup,
	.sleep = dib7000p_sleep,

	.set_frontend = dib7000p_set_frontend,
	.get_tune_settings = dib7000p_fe_get_tune_settings,
	.get_frontend = dib7000p_get_frontend,

	.read_status = dib7000p_read_status,
	.read_ber = dib7000p_read_ber,
	.read_signal_strength = dib7000p_read_signal_strength,
	.read_snr = dib7000p_read_snr,
	.read_ucblocks = dib7000p_read_unc_blocks,
};

MODULE_AUTHOR("Olivier Grenie <olivie.grenie@parrot.com>");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for the DiBcom 7000PC COFDM demodulator");
MODULE_LICENSE("GPL");
