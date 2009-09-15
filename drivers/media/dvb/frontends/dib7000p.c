/*
 * Linux-DVB Driver for DiBcom's second generation DiB7000P (PC).
 *
 * Copyright (C) 2005-7 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/i2c.h>

#include "dvb_math.h"
#include "dvb_frontend.h"

#include "dib7000p.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

static int buggy_sfn_workaround;
module_param(buggy_sfn_workaround, int, 0644);
MODULE_PARM_DESC(buggy_sfn_workaround, "Enable work-around for buggy SFNs (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiB7000P: "); printk(args); printk("\n"); } } while (0)

struct dib7000p_state {
	struct dvb_frontend demod;
    struct dib7000p_config cfg;

	u8 i2c_addr;
	struct i2c_adapter   *i2c_adap;

	struct dibx000_i2c_master i2c_master;

	u16 wbd_ref;

	u8  current_band;
	u32 current_bandwidth;
	struct dibx000_agc_config *current_agc;
	u32 timf;

	u8 div_force_off : 1;
	u8 div_state : 1;
	u16 div_sync_wait;

	u8 agc_state;

	u16 gpio_dir;
	u16 gpio_val;

	u8 sfn_workaround_active :1;
};

enum dib7000p_power_mode {
	DIB7000P_POWER_ALL = 0,
	DIB7000P_POWER_ANALOG_ADC,
	DIB7000P_POWER_INTERFACE_ONLY,
};

static u16 dib7000p_read_word(struct dib7000p_state *state, u16 reg)
{
	u8 wb[2] = { reg >> 8, reg & 0xff };
	u8 rb[2];
	struct i2c_msg msg[2] = {
		{ .addr = state->i2c_addr >> 1, .flags = 0,        .buf = wb, .len = 2 },
		{ .addr = state->i2c_addr >> 1, .flags = I2C_M_RD, .buf = rb, .len = 2 },
	};

	if (i2c_transfer(state->i2c_adap, msg, 2) != 2)
		dprintk("i2c read error on %d",reg);

	return (rb[0] << 8) | rb[1];
}

static int dib7000p_write_word(struct dib7000p_state *state, u16 reg, u16 val)
{
	u8 b[4] = {
		(reg >> 8) & 0xff, reg & 0xff,
		(val >> 8) & 0xff, val & 0xff,
	};
	struct i2c_msg msg = {
		.addr = state->i2c_addr >> 1, .flags = 0, .buf = b, .len = 4
	};
	return i2c_transfer(state->i2c_adap, &msg, 1) != 1 ? -EREMOTEIO : 0;
}
static void dib7000p_write_tab(struct dib7000p_state *state, u16 *buf)
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
	int    ret = 0;
	u16 outreg, fifo_threshold, smo_mode;

	outreg = 0;
	fifo_threshold = 1792;
	smo_mode = (dib7000p_read_word(state, 235) & 0x0010) | (1 << 1);

	dprintk( "setting output mode for demod %p to %d",
			&state->demod, mode);

	switch (mode) {
		case OUTMODE_MPEG2_PAR_GATED_CLK:   // STBs with parallel gated clock
			outreg = (1 << 10);  /* 0x0400 */
			break;
		case OUTMODE_MPEG2_PAR_CONT_CLK:    // STBs with parallel continues clock
			outreg = (1 << 10) | (1 << 6); /* 0x0440 */
			break;
		case OUTMODE_MPEG2_SERIAL:          // STBs with serial input
			outreg = (1 << 10) | (2 << 6) | (0 << 1); /* 0x0480 */
			break;
		case OUTMODE_DIVERSITY:
			if (state->cfg.hostbus_diversity)
				outreg = (1 << 10) | (4 << 6); /* 0x0500 */
			else
				outreg = (1 << 11);
			break;
		case OUTMODE_MPEG2_FIFO:            // e.g. USB feeding
			smo_mode |= (3 << 1);
			fifo_threshold = 512;
			outreg = (1 << 10) | (5 << 6);
			break;
		case OUTMODE_ANALOG_ADC:
			outreg = (1 << 10) | (3 << 6);
			break;
		case OUTMODE_HIGH_Z:  // disable
			outreg = 0;
			break;
		default:
			dprintk( "Unhandled output_mode passed to be set for demod %p",&state->demod);
			break;
	}

	if (state->cfg.output_mpeg2_in_188_bytes)
		smo_mode |= (1 << 5) ;

	ret |= dib7000p_write_word(state,  235, smo_mode);
	ret |= dib7000p_write_word(state,  236, fifo_threshold); /* synchronous fread */
	ret |= dib7000p_write_word(state, 1286, outreg);         /* P_Div_active */

	return ret;
}

static int dib7000p_set_diversity_in(struct dvb_frontend *demod, int onoff)
{
	struct dib7000p_state *state = demod->demodulator_priv;

	if (state->div_force_off) {
		dprintk( "diversity combination deactivated - forced by COFDM parameters");
		onoff = 0;
	}
	state->div_state = (u8)onoff;

	if (onoff) {
		dib7000p_write_word(state, 204, 6);
		dib7000p_write_word(state, 205, 16);
		/* P_dvsy_sync_mode = 0, P_dvsy_sync_enable=1, P_dvcb_comb_mode=2 */
		dib7000p_write_word(state, 207, (state->div_sync_wait << 4) | (1 << 2) | (2 << 0));
	} else {
		dib7000p_write_word(state, 204, 1);
		dib7000p_write_word(state, 205, 0);
		dib7000p_write_word(state, 207, 0);
	}

	return 0;
}

static int dib7000p_set_power_mode(struct dib7000p_state *state, enum dib7000p_power_mode mode)
{
	/* by default everything is powered off */
	u16 reg_774 = 0xffff, reg_775 = 0xffff, reg_776 = 0x0007, reg_899  = 0x0003,
		reg_1280 = (0xfe00) | (dib7000p_read_word(state, 1280) & 0x01ff);

	/* now, depending on the requested mode, we power on */
	switch (mode) {
		/* power up everything in the demod */
		case DIB7000P_POWER_ALL:
			reg_774 = 0x0000; reg_775 = 0x0000; reg_776 = 0x0; reg_899 = 0x0; reg_1280 &= 0x01ff;
			break;

		case DIB7000P_POWER_ANALOG_ADC:
			/* dem, cfg, iqc, sad, agc */
			reg_774 &= ~((1 << 15) | (1 << 14) | (1 << 11) | (1 << 10) | (1 << 9));
			/* nud */
			reg_776 &= ~((1 << 0));
			/* Dout */
			reg_1280 &= ~((1 << 11));
			/* fall through wanted to enable the interfaces */

		/* just leave power on the control-interfaces: GPIO and (I2C or SDIO) */
		case DIB7000P_POWER_INTERFACE_ONLY: /* TODO power up either SDIO or I2C */
			reg_1280 &= ~((1 << 14) | (1 << 13) | (1 << 12) | (1 << 10));
			break;

/* TODO following stuff is just converted from the dib7000-driver - check when is used what */
	}

	dib7000p_write_word(state,  774,  reg_774);
	dib7000p_write_word(state,  775,  reg_775);
	dib7000p_write_word(state,  776,  reg_776);
	dib7000p_write_word(state,  899,  reg_899);
	dib7000p_write_word(state, 1280, reg_1280);

	return 0;
}

static void dib7000p_set_adc_state(struct dib7000p_state *state, enum dibx000_adc_states no)
{
	u16 reg_908 = dib7000p_read_word(state, 908),
	       reg_909 = dib7000p_read_word(state, 909);

	switch (no) {
		case DIBX000_SLOW_ADC_ON:
			reg_909 |= (1 << 1) | (1 << 0);
			dib7000p_write_word(state, 909, reg_909);
			reg_909 &= ~(1 << 1);
			break;

		case DIBX000_SLOW_ADC_OFF:
			reg_909 |=  (1 << 1) | (1 << 0);
			break;

		case DIBX000_ADC_ON:
			reg_908 &= 0x0fff;
			reg_909 &= 0x0003;
			break;

		case DIBX000_ADC_OFF: // leave the VBG voltage on
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

	dib7000p_write_word(state, 908, reg_908);
	dib7000p_write_word(state, 909, reg_909);
}

static int dib7000p_set_bandwidth(struct dib7000p_state *state, u32 bw)
{
	u32 timf;

	// store the current bandwidth for later use
	state->current_bandwidth = bw;

	if (state->timf == 0) {
		dprintk( "using default timf");
		timf = state->cfg.bw->timf;
	} else {
		dprintk( "using updated timf");
		timf = state->timf;
	}

	timf = timf * (bw / 50) / 160;

	dib7000p_write_word(state, 23, (u16) ((timf >> 16) & 0xffff));
	dib7000p_write_word(state, 24, (u16) ((timf      ) & 0xffff));

	return 0;
}

static int dib7000p_sad_calib(struct dib7000p_state *state)
{
/* internal */
//	dib7000p_write_word(state, 72, (3 << 14) | (1 << 12) | (524 << 0)); // sampling clock of the SAD is writting in set_bandwidth
	dib7000p_write_word(state, 73, (0 << 1) | (0 << 0));
	dib7000p_write_word(state, 74, 776); // 0.625*3.3 / 4096

	/* do the calibration */
	dib7000p_write_word(state, 73, (1 << 0));
	dib7000p_write_word(state, 73, (0 << 0));

	msleep(1);

	return 0;
}

int dib7000p_set_wbd_ref(struct dvb_frontend *demod, u16 value)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	if (value > 4095)
		value = 4095;
	state->wbd_ref = value;
	return dib7000p_write_word(state, 105, (dib7000p_read_word(state, 105) & 0xf000) | value);
}

EXPORT_SYMBOL(dib7000p_set_wbd_ref);
static void dib7000p_reset_pll(struct dib7000p_state *state)
{
	struct dibx000_bandwidth_config *bw = &state->cfg.bw[0];
	u16 clk_cfg0;

	/* force PLL bypass */
	clk_cfg0 = (1 << 15) | ((bw->pll_ratio & 0x3f) << 9) |
		(bw->modulo << 7) | (bw->ADClkSrc << 6) | (bw->IO_CLK_en_core << 5) |
		(bw->bypclk_div << 2) | (bw->enable_refdiv << 1) | (0 << 0);

	dib7000p_write_word(state, 900, clk_cfg0);

	/* P_pll_cfg */
	dib7000p_write_word(state, 903, (bw->pll_prediv << 5) | (((bw->pll_ratio >> 6) & 0x3) << 3) | (bw->pll_range << 1) | bw->pll_reset);
	clk_cfg0 = (bw->pll_bypass << 15) | (clk_cfg0 & 0x7fff);
	dib7000p_write_word(state, 900, clk_cfg0);

	dib7000p_write_word(state, 18, (u16) (((bw->internal*1000) >> 16) & 0xffff));
	dib7000p_write_word(state, 19, (u16) ( (bw->internal*1000       ) & 0xffff));
	dib7000p_write_word(state, 21, (u16) ( (bw->ifreq          >> 16) & 0xffff));
	dib7000p_write_word(state, 22, (u16) ( (bw->ifreq               ) & 0xffff));

	dib7000p_write_word(state, 72, bw->sad_cfg);
}

static int dib7000p_reset_gpio(struct dib7000p_state *st)
{
	/* reset the GPIOs */
	dprintk( "gpio dir: %x: val: %x, pwm_pos: %x",st->gpio_dir, st->gpio_val,st->cfg.gpio_pwm_pos);

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
	st->gpio_dir &= ~(1 << num);         /* reset the direction bit */
	st->gpio_dir |=  (dir & 0x1) << num; /* set the new direction */
	dib7000p_write_word(st, 1029, st->gpio_dir);

	st->gpio_val = dib7000p_read_word(st, 1030);
	st->gpio_val &= ~(1 << num);          /* reset the direction bit */
	st->gpio_val |=  (val & 0x01) << num; /* set the new value */
	dib7000p_write_word(st, 1030, st->gpio_val);

	return 0;
}

int dib7000p_set_gpio(struct dvb_frontend *demod, u8 num, u8 dir, u8 val)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	return dib7000p_cfg_gpio(state, num, dir, val);
}

EXPORT_SYMBOL(dib7000p_set_gpio);
static u16 dib7000p_defaults[] =

{
	// auto search configuration
	3, 2,
		0x0004,
		0x1000,
		0x0814, /* Equal Lock */

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
		0x6680, // P_timf_alpha=6, P_corm_alpha=6, P_corm_thres=128 default: 6,4,26

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
		0x0410, // P_palf_filter_on=1, P_palf_filter_freeze=0, P_palf_alpha_regul=16

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
		1 << 13, // P_fft_freq_dir=1, P_fft_nb_to_cut=0

	1, 168,
		0x0ccd, // P_pha3_thres, default 0x3000

//	1, 169,
//		0x0010, // P_cti_use_cpe=0, P_cti_use_prog=0, P_cti_win_len=16, default: 0x0010

	1, 183,
		0x200f, // P_cspu_regul=512, P_cspu_win_cut=15, default: 0x2005

	5, 187,
		0x023d, // P_adp_regul_cnt=573, default: 410
		0x00a4, // P_adp_noise_cnt=
		0x00a4, // P_adp_regul_ext
		0x7ff0, // P_adp_noise_ext
		0x3ccc, // P_adp_fil

	1, 198,
		0x800, // P_equal_thres_wgn

	1, 222,
		0x0010, // P_fec_ber_rs_len=2

	1, 235,
		0x0062, // P_smo_mode, P_smo_rs_discard, P_smo_fifo_flush, P_smo_pid_parse, P_smo_error_discard

	2, 901,
		0x0006, // P_clk_cfg1
		(3 << 10) | (1 << 6), // P_divclksel=3 P_divbitsel=1

	1, 905,
		0x2c8e, // Tuner IO bank: max drive (14mA) + divout pads max drive

	0,
};

static int dib7000p_demod_reset(struct dib7000p_state *state)
{
	dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);

	dib7000p_set_adc_state(state, DIBX000_VBG_ENABLE);

	/* restart all parts */
	dib7000p_write_word(state,  770, 0xffff);
	dib7000p_write_word(state,  771, 0xffff);
	dib7000p_write_word(state,  772, 0x001f);
	dib7000p_write_word(state,  898, 0x0003);
	/* except i2c, sdio, gpio - control interfaces */
	dib7000p_write_word(state, 1280, 0x01fc - ((1 << 7) | (1 << 6) | (1 << 5)) );

	dib7000p_write_word(state,  770, 0);
	dib7000p_write_word(state,  771, 0);
	dib7000p_write_word(state,  772, 0);
	dib7000p_write_word(state,  898, 0);
	dib7000p_write_word(state, 1280, 0);

	/* default */
	dib7000p_reset_pll(state);

	if (dib7000p_reset_gpio(state) != 0)
		dprintk( "GPIO reset was not successful.");

	if (dib7000p_set_output_mode(state, OUTMODE_HIGH_Z) != 0)
		dprintk( "OUTPUT_MODE could not be reset.");

	/* unforce divstr regardless whether i2c enumeration was done or not */
	dib7000p_write_word(state, 1285, dib7000p_read_word(state, 1285) & ~(1 << 1) );

	dib7000p_set_bandwidth(state, 8000);

	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_ON);
	dib7000p_sad_calib(state);
	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_OFF);

	// P_iqc_alpha_pha, P_iqc_alpha_amp_dcc_alpha, ...
	if(state->cfg.tuner_is_baseband)
		dib7000p_write_word(state, 36,0x0755);
	else
		dib7000p_write_word(state, 36,0x1f55);

	dib7000p_write_tab(state, dib7000p_defaults);

	dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);


	return 0;
}

static void dib7000p_pll_clk_cfg(struct dib7000p_state *state)
{
	u16 tmp = 0;
	tmp = dib7000p_read_word(state, 903);
	dib7000p_write_word(state, 903, (tmp | 0x1));   //pwr-up pll
	tmp = dib7000p_read_word(state, 900);
	dib7000p_write_word(state, 900, (tmp & 0x7fff) | (1 << 6));     //use High freq clock
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

	// when there is no LNA to program return immediatly
	if (state->cfg.update_lna) {
		// read dyn_gain here (because it is demod-dependent and not fe)
		dyn_gain = dib7000p_read_word(state, 394);
		if (state->cfg.update_lna(&state->demod,dyn_gain)) { // LNA has changed
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
		dprintk( "no valid AGC configuration found for band 0x%02x",band);
		return -EINVAL;
	}

	state->current_agc = agc;

	/* AGC */
	dib7000p_write_word(state, 75 ,  agc->setup );
	dib7000p_write_word(state, 76 ,  agc->inv_gain );
	dib7000p_write_word(state, 77 ,  agc->time_stabiliz );
	dib7000p_write_word(state, 100, (agc->alpha_level << 12) | agc->thlock);

	// Demod AGC loop configuration
	dib7000p_write_word(state, 101, (agc->alpha_mant << 5) | agc->alpha_exp);
	dib7000p_write_word(state, 102, (agc->beta_mant << 6)  | agc->beta_exp);

	/* AGC continued */
	dprintk( "WBD: ref: %d, sel: %d, active: %d, alpha: %d",
		state->wbd_ref != 0 ? state->wbd_ref : agc->wbd_ref, agc->wbd_sel, !agc->perform_agc_softsplit, agc->wbd_sel);

	if (state->wbd_ref != 0)
		dib7000p_write_word(state, 105, (agc->wbd_inv << 12) | state->wbd_ref);
	else
		dib7000p_write_word(state, 105, (agc->wbd_inv << 12) | agc->wbd_ref);

	dib7000p_write_word(state, 106, (agc->wbd_sel << 13) | (agc->wbd_alpha << 9) | (agc->perform_agc_softsplit << 8));

	dib7000p_write_word(state, 107,  agc->agc1_max);
	dib7000p_write_word(state, 108,  agc->agc1_min);
	dib7000p_write_word(state, 109,  agc->agc2_max);
	dib7000p_write_word(state, 110,  agc->agc2_min);
	dib7000p_write_word(state, 111, (agc->agc1_pt1    << 8) | agc->agc1_pt2);
	dib7000p_write_word(state, 112,  agc->agc1_pt3);
	dib7000p_write_word(state, 113, (agc->agc1_slope1 << 8) | agc->agc1_slope2);
	dib7000p_write_word(state, 114, (agc->agc2_pt1    << 8) | agc->agc2_pt2);
	dib7000p_write_word(state, 115, (agc->agc2_slope1 << 8) | agc->agc2_slope2);
	return 0;
}

static int dib7000p_agc_startup(struct dvb_frontend *demod, struct dvb_frontend_parameters *ch)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	int ret = -1;
	u8 *agc_state = &state->agc_state;
	u8 agc_split;

	switch (state->agc_state) {
		case 0:
			// set power-up level: interf+analog+AGC
			dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);
			dib7000p_set_adc_state(state, DIBX000_ADC_ON);
			dib7000p_pll_clk_cfg(state);

			if (dib7000p_set_agc_config(state, BAND_OF_FREQUENCY(ch->frequency/1000)) != 0)
				return -1;

			ret = 7;
			(*agc_state)++;
			break;

		case 1:
			// AGC initialization
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

		case 2: /* fast split search path after 5sec */
			dib7000p_write_word(state,  75, state->current_agc->setup | (1 << 4)); /* freeze AGC loop */
			dib7000p_write_word(state, 106, (state->current_agc->wbd_sel << 13) | (2 << 9) | (0 << 8)); /* fast split search 0.25kHz */
			(*agc_state)++;
			ret = 14;
			break;

	case 3: /* split search ended */
			agc_split = (u8)dib7000p_read_word(state, 396); /* store the split value for the next time */
			dib7000p_write_word(state, 78, dib7000p_read_word(state, 394)); /* set AGC gain start value */

			dib7000p_write_word(state, 75,  state->current_agc->setup);   /* std AGC loop */
			dib7000p_write_word(state, 106, (state->current_agc->wbd_sel << 13) | (state->current_agc->wbd_alpha << 9) | agc_split); /* standard split search */

			dib7000p_restart_agc(state);

			dprintk( "SPLIT %p: %hd", demod, agc_split);

			(*agc_state)++;
			ret = 5;
			break;

		case 4: /* LNA startup */
			// wait AGC accurate lock time
			ret = 7;

			if (dib7000p_update_lna(state))
				// wait only AGC rough lock time
				ret = 5;
			else // nothing was done, go to the next state
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
	dprintk( "updated timf_frequency: %d (default: %d)",state->timf, state->cfg.bw->timf);

}

static void dib7000p_set_channel(struct dib7000p_state *state, struct dvb_frontend_parameters *ch, u8 seq)
{
	u16 value, est[4];

    dib7000p_set_bandwidth(state, BANDWIDTH_TO_KHZ(ch->u.ofdm.bandwidth));

	/* nfft, guard, qam, alpha */
	value = 0;
	switch (ch->u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_2K: value |= (0 << 7); break;
		case /* 4K MODE */ 255: value |= (2 << 7); break;
		default:
		case TRANSMISSION_MODE_8K: value |= (1 << 7); break;
	}
	switch (ch->u.ofdm.guard_interval) {
		case GUARD_INTERVAL_1_32: value |= (0 << 5); break;
		case GUARD_INTERVAL_1_16: value |= (1 << 5); break;
		case GUARD_INTERVAL_1_4:  value |= (3 << 5); break;
		default:
		case GUARD_INTERVAL_1_8:  value |= (2 << 5); break;
	}
	switch (ch->u.ofdm.constellation) {
		case QPSK:  value |= (0 << 3); break;
		case QAM_16: value |= (1 << 3); break;
		default:
		case QAM_64: value |= (2 << 3); break;
	}
	switch (HIERARCHY_1) {
		case HIERARCHY_2: value |= 2; break;
		case HIERARCHY_4: value |= 4; break;
		default:
		case HIERARCHY_1: value |= 1; break;
	}
	dib7000p_write_word(state, 0, value);
	dib7000p_write_word(state, 5, (seq << 4) | 1); /* do not force tps, search list 0 */

	/* P_dintl_native, P_dintlv_inv, P_hrch, P_code_rate, P_select_hp */
	value = 0;
	if (1 != 0)
		value |= (1 << 6);
	if (ch->u.ofdm.hierarchy_information == 1)
		value |= (1 << 4);
	if (1 == 1)
		value |= 1;
	switch ((ch->u.ofdm.hierarchy_information == 0 || 1 == 1) ? ch->u.ofdm.code_rate_HP : ch->u.ofdm.code_rate_LP) {
		case FEC_2_3: value |= (2 << 1); break;
		case FEC_3_4: value |= (3 << 1); break;
		case FEC_5_6: value |= (5 << 1); break;
		case FEC_7_8: value |= (7 << 1); break;
		default:
		case FEC_1_2: value |= (1 << 1); break;
	}
	dib7000p_write_word(state, 208, value);

	/* offset loop parameters */
	dib7000p_write_word(state, 26, 0x6680); // timf(6xxx)
	dib7000p_write_word(state, 32, 0x0003); // pha_off_max(xxx3)
	dib7000p_write_word(state, 29, 0x1273); // isi
	dib7000p_write_word(state, 33, 0x0005); // sfreq(xxx5)

	/* P_dvsy_sync_wait */
	switch (ch->u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_8K: value = 256; break;
		case /* 4K MODE */ 255: value = 128; break;
		case TRANSMISSION_MODE_2K:
		default: value = 64; break;
	}
	switch (ch->u.ofdm.guard_interval) {
		case GUARD_INTERVAL_1_16: value *= 2; break;
		case GUARD_INTERVAL_1_8:  value *= 4; break;
		case GUARD_INTERVAL_1_4:  value *= 8; break;
		default:
		case GUARD_INTERVAL_1_32: value *= 1; break;
	}
	state->div_sync_wait = (value * 3) / 2 + 32; // add 50% SFN margin + compensate for one DVSY-fifo TODO

	/* deactive the possibility of diversity reception if extended interleaver */
	state->div_force_off = !1 && ch->u.ofdm.transmission_mode != TRANSMISSION_MODE_8K;
	dib7000p_set_diversity_in(&state->demod, state->div_state);

	/* channel estimation fine configuration */
	switch (ch->u.ofdm.constellation) {
		case QAM_64:
			est[0] = 0x0148;       /* P_adp_regul_cnt 0.04 */
			est[1] = 0xfff0;       /* P_adp_noise_cnt -0.002 */
			est[2] = 0x00a4;       /* P_adp_regul_ext 0.02 */
			est[3] = 0xfff8;       /* P_adp_noise_ext -0.001 */
			break;
		case QAM_16:
			est[0] = 0x023d;       /* P_adp_regul_cnt 0.07 */
			est[1] = 0xffdf;       /* P_adp_noise_cnt -0.004 */
			est[2] = 0x00a4;       /* P_adp_regul_ext 0.02 */
			est[3] = 0xfff0;       /* P_adp_noise_ext -0.002 */
			break;
		default:
			est[0] = 0x099a;       /* P_adp_regul_cnt 0.3 */
			est[1] = 0xffae;       /* P_adp_noise_cnt -0.01 */
			est[2] = 0x0333;       /* P_adp_regul_ext 0.1 */
			est[3] = 0xfff8;       /* P_adp_noise_ext -0.002 */
			break;
	}
	for (value = 0; value < 4; value++)
		dib7000p_write_word(state, 187 + value, est[value]);
}

static int dib7000p_autosearch_start(struct dvb_frontend *demod, struct dvb_frontend_parameters *ch)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	struct dvb_frontend_parameters schan;
	u32 value, factor;

	schan = *ch;
	schan.u.ofdm.constellation = QAM_64;
	schan.u.ofdm.guard_interval         = GUARD_INTERVAL_1_32;
	schan.u.ofdm.transmission_mode          = TRANSMISSION_MODE_8K;
	schan.u.ofdm.code_rate_HP  = FEC_2_3;
	schan.u.ofdm.code_rate_LP  = FEC_3_4;
	schan.u.ofdm.hierarchy_information          = 0;

	dib7000p_set_channel(state, &schan, 7);

	factor = BANDWIDTH_TO_KHZ(ch->u.ofdm.bandwidth);
	if (factor >= 5000)
		factor = 1;
	else
		factor = 6;

	// always use the setting for 8MHz here lock_time for 7,6 MHz are longer
	value = 30 * state->cfg.bw->internal * factor;
	dib7000p_write_word(state, 6,  (u16) ((value >> 16) & 0xffff)); // lock0 wait time
	dib7000p_write_word(state, 7,  (u16)  (value        & 0xffff)); // lock0 wait time
	value = 100 * state->cfg.bw->internal * factor;
	dib7000p_write_word(state, 8,  (u16) ((value >> 16) & 0xffff)); // lock1 wait time
	dib7000p_write_word(state, 9,  (u16)  (value        & 0xffff)); // lock1 wait time
	value = 500 * state->cfg.bw->internal * factor;
	dib7000p_write_word(state, 10, (u16) ((value >> 16) & 0xffff)); // lock2 wait time
	dib7000p_write_word(state, 11, (u16)  (value        & 0xffff)); // lock2 wait time

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

	if (irq_pending & 0x1) // failed
		return 1;

	if (irq_pending & 0x2) // succeeded
		return 2;

	return 0; // still pending
}

static void dib7000p_spur_protect(struct dib7000p_state *state, u32 rf_khz, u32 bw)
{
	static s16 notch[]={16143, 14402, 12238, 9713, 6902, 3888, 759, -2392};
	static u8 sine [] ={0, 2, 3, 5, 6, 8, 9, 11, 13, 14, 16, 17, 19, 20, 22,
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
	255, 255, 255, 255, 255, 255};

	u32 xtal = state->cfg.bw->xtal_hz / 1000;
	int f_rel = DIV_ROUND_CLOSEST(rf_khz, xtal) * xtal - rf_khz;
	int k;
	int coef_re[8],coef_im[8];
	int bw_khz = bw;
	u32 pha;

	dprintk( "relative position of the Spur: %dk (RF: %dk, XTAL: %dk)", f_rel, rf_khz, xtal);


	if (f_rel < -bw_khz/2 || f_rel > bw_khz/2)
		return;

	bw_khz /= 100;

	dib7000p_write_word(state, 142 ,0x0610);

	for (k = 0; k < 8; k++) {
		pha = ((f_rel * (k+1) * 112 * 80/bw_khz) /1000) & 0x3ff;

		if (pha==0) {
			coef_re[k] = 256;
			coef_im[k] = 0;
		} else if(pha < 256) {
			coef_re[k] = sine[256-(pha&0xff)];
			coef_im[k] = sine[pha&0xff];
		} else if (pha == 256) {
			coef_re[k] = 0;
			coef_im[k] = 256;
		} else if (pha < 512) {
			coef_re[k] = -sine[pha&0xff];
			coef_im[k] = sine[256 - (pha&0xff)];
		} else if (pha == 512) {
			coef_re[k] = -256;
			coef_im[k] = 0;
		} else if (pha < 768) {
			coef_re[k] = -sine[256-(pha&0xff)];
			coef_im[k] = -sine[pha&0xff];
		} else if (pha == 768) {
			coef_re[k] = 0;
			coef_im[k] = -256;
		} else {
			coef_re[k] = sine[pha&0xff];
			coef_im[k] = -sine[256 - (pha&0xff)];
		}

		coef_re[k] *= notch[k];
		coef_re[k] += (1<<14);
		if (coef_re[k] >= (1<<24))
			coef_re[k]  = (1<<24) - 1;
		coef_re[k] /= (1<<15);

		coef_im[k] *= notch[k];
		coef_im[k] += (1<<14);
		if (coef_im[k] >= (1<<24))
			coef_im[k]  = (1<<24)-1;
		coef_im[k] /= (1<<15);

		dprintk( "PALF COEF: %d re: %d im: %d", k, coef_re[k], coef_im[k]);

		dib7000p_write_word(state, 143, (0 << 14) | (k << 10) | (coef_re[k] & 0x3ff));
		dib7000p_write_word(state, 144, coef_im[k] & 0x3ff);
		dib7000p_write_word(state, 143, (1 << 14) | (k << 10) | (coef_re[k] & 0x3ff));
	}
	dib7000p_write_word(state,143 ,0);
}

static int dib7000p_tune(struct dvb_frontend *demod, struct dvb_frontend_parameters *ch)
{
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
		dprintk( "SFN workaround is active");
		tmp |= (1 << 9);
		dib7000p_write_word(state, 166, 0x4000); // P_pha3_force_pha_shift
	} else {
		dib7000p_write_word(state, 166, 0x0000); // P_pha3_force_pha_shift
	}
	dib7000p_write_word(state, 29, tmp);

	// never achieved a lock with that bandwidth so far - wait for osc-freq to update
	if (state->timf == 0)
		msleep(200);

	/* offset loop parameters */

	/* P_timf_alpha, P_corm_alpha=6, P_corm_thres=0x80 */
	tmp = (6 << 8) | 0x80;
	switch (ch->u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_2K: tmp |= (7 << 12); break;
		case /* 4K MODE */ 255: tmp |= (8 << 12); break;
		default:
		case TRANSMISSION_MODE_8K: tmp |= (9 << 12); break;
	}
	dib7000p_write_word(state, 26, tmp);  /* timf_a(6xxx) */

	/* P_ctrl_freeze_pha_shift=0, P_ctrl_pha_off_max */
	tmp = (0 << 4);
	switch (ch->u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_2K: tmp |= 0x6; break;
		case /* 4K MODE */ 255: tmp |= 0x7; break;
		default:
		case TRANSMISSION_MODE_8K: tmp |= 0x8; break;
	}
	dib7000p_write_word(state, 32,  tmp);

	/* P_ctrl_sfreq_inh=0, P_ctrl_sfreq_step */
	tmp = (0 << 4);
	switch (ch->u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_2K: tmp |= 0x6; break;
		case /* 4K MODE */ 255: tmp |= 0x7; break;
		default:
		case TRANSMISSION_MODE_8K: tmp |= 0x8; break;
	}
	dib7000p_write_word(state, 33,  tmp);

	tmp = dib7000p_read_word(state,509);
	if (!((tmp >> 6) & 0x1)) {
		/* restart the fec */
		tmp = dib7000p_read_word(state,771);
		dib7000p_write_word(state, 771, tmp | (1 << 1));
		dib7000p_write_word(state, 771, tmp);
		msleep(10);
		tmp = dib7000p_read_word(state,509);
	}

	// we achieved a lock - it's time to update the osc freq
	if ((tmp >> 6) & 0x1)
		dib7000p_update_timf(state);

	if (state->cfg.spur_protect)
		dib7000p_spur_protect(state, ch->frequency/1000, BANDWIDTH_TO_KHZ(ch->u.ofdm.bandwidth));

    dib7000p_set_bandwidth(state, BANDWIDTH_TO_KHZ(ch->u.ofdm.bandwidth));
	return 0;
}

static int dib7000p_wakeup(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);
	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_ON);
	return 0;
}

static int dib7000p_sleep(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	return dib7000p_set_output_mode(state, OUTMODE_HIGH_Z) | dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);
}

static int dib7000p_identify(struct dib7000p_state *st)
{
	u16 value;
	dprintk( "checking demod on I2C address: %d (%x)",
		st->i2c_addr, st->i2c_addr);

	if ((value = dib7000p_read_word(st, 768)) != 0x01b3) {
		dprintk( "wrong Vendor ID (read=0x%x)",value);
		return -EREMOTEIO;
	}

	if ((value = dib7000p_read_word(st, 769)) != 0x4000) {
		dprintk( "wrong Device ID (%x)",value);
		return -EREMOTEIO;
	}

	return 0;
}


static int dib7000p_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 tps = dib7000p_read_word(state,463);

	fep->inversion = INVERSION_AUTO;

	fep->u.ofdm.bandwidth = BANDWIDTH_TO_INDEX(state->current_bandwidth);

	switch ((tps >> 8) & 0x3) {
		case 0: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_2K; break;
		case 1: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_8K; break;
		/* case 2: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_4K; break; */
	}

	switch (tps & 0x3) {
		case 0: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_32; break;
		case 1: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_16; break;
		case 2: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_8; break;
		case 3: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_4; break;
	}

	switch ((tps >> 14) & 0x3) {
		case 0: fep->u.ofdm.constellation = QPSK; break;
		case 1: fep->u.ofdm.constellation = QAM_16; break;
		case 2:
		default: fep->u.ofdm.constellation = QAM_64; break;
	}

	/* as long as the frontend_param structure is fixed for hierarchical transmission I refuse to use it */
	/* (tps >> 13) & 0x1 == hrch is used, (tps >> 10) & 0x7 == alpha */

	fep->u.ofdm.hierarchy_information = HIERARCHY_NONE;
	switch ((tps >> 5) & 0x7) {
		case 1: fep->u.ofdm.code_rate_HP = FEC_1_2; break;
		case 2: fep->u.ofdm.code_rate_HP = FEC_2_3; break;
		case 3: fep->u.ofdm.code_rate_HP = FEC_3_4; break;
		case 5: fep->u.ofdm.code_rate_HP = FEC_5_6; break;
		case 7:
		default: fep->u.ofdm.code_rate_HP = FEC_7_8; break;

	}

	switch ((tps >> 2) & 0x7) {
		case 1: fep->u.ofdm.code_rate_LP = FEC_1_2; break;
		case 2: fep->u.ofdm.code_rate_LP = FEC_2_3; break;
		case 3: fep->u.ofdm.code_rate_LP = FEC_3_4; break;
		case 5: fep->u.ofdm.code_rate_LP = FEC_5_6; break;
		case 7:
		default: fep->u.ofdm.code_rate_LP = FEC_7_8; break;
	}

	/* native interleaver: (dib7000p_read_word(state, 464) >>  5) & 0x1 */

	return 0;
}

static int dib7000p_set_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	int time, ret;

	dib7000p_set_output_mode(state, OUTMODE_HIGH_Z);

    /* maybe the parameter has been changed */
	state->sfn_workaround_active = buggy_sfn_workaround;

	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe, fep);

	/* start up the AGC */
	state->agc_state = 0;
	do {
		time = dib7000p_agc_startup(fe, fep);
		if (time != -1)
			msleep(time);
	} while (time != -1);

	if (fep->u.ofdm.transmission_mode == TRANSMISSION_MODE_AUTO ||
		fep->u.ofdm.guard_interval    == GUARD_INTERVAL_AUTO ||
		fep->u.ofdm.constellation     == QAM_AUTO ||
		fep->u.ofdm.code_rate_HP      == FEC_AUTO) {
		int i = 800, found;

		dib7000p_autosearch_start(fe, fep);
		do {
			msleep(1);
			found = dib7000p_autosearch_is_irq(fe);
		} while (found == 0 && i--);

		dprintk("autosearch returns: %d",found);
		if (found == 0 || found == 1)
			return 0; // no channel found

		dib7000p_get_frontend(fe, fep);
	}

	ret = dib7000p_tune(fe, fep);

	/* make this a config parameter */
	dib7000p_set_output_mode(state, state->cfg.output_mode);
    return ret;
}

static int dib7000p_read_status(struct dvb_frontend *fe, fe_status_t *stat)
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
	if (lock & 0x0008)
		*stat |= FE_HAS_LOCK;

	return 0;
}

static int dib7000p_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	*ber = (dib7000p_read_word(state, 500) << 16) | dib7000p_read_word(state, 501);
	return 0;
}

static int dib7000p_read_unc_blocks(struct dvb_frontend *fe, u32 *unc)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	*unc = dib7000p_read_word(state, 506);
	return 0;
}

static int dib7000p_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dib7000p_state *state = fe->demodulator_priv;
	u16 val = dib7000p_read_word(state, 394);
	*strength = 65535 - val;
	return 0;
}

static int dib7000p_read_snr(struct dvb_frontend* fe, u16 *snr)
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
	signal_exp  = (val & 0x3F);
	if ((signal_exp & 0x20) != 0)
		signal_exp -= 0x40;

	if (signal_mant != 0)
		result = intlog10(2) * 10 * signal_exp + 10 *
			intlog10(signal_mant);
	else
		result = intlog10(2) * 10 * signal_exp - 100;

	if (noise_mant != 0)
		result -= intlog10(2) * 10 * noise_exp + 10 *
			intlog10(noise_mant);
	else
		result -= intlog10(2) * 10 * noise_exp - 100;

	*snr = result / ((1 << 24) / 10);
	return 0;
}

static int dib7000p_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void dib7000p_release(struct dvb_frontend *demod)
{
	struct dib7000p_state *st = demod->demodulator_priv;
	dibx000_exit_i2c_master(&st->i2c_master);
	kfree(st);
}

int dib7000pc_detection(struct i2c_adapter *i2c_adap)
{
	u8 tx[2], rx[2];
	struct i2c_msg msg[2] = {
		{ .addr = 18 >> 1, .flags = 0,        .buf = tx, .len = 2 },
		{ .addr = 18 >> 1, .flags = I2C_M_RD, .buf = rx, .len = 2 },
	};

	tx[0] = 0x03;
	tx[1] = 0x00;

	if (i2c_transfer(i2c_adap, msg, 2) == 2)
		if (rx[0] == 0x01 && rx[1] == 0xb3) {
			dprintk("-D-  DiB7000PC detected");
			return 1;
		}

	msg[0].addr = msg[1].addr = 0x40;

	if (i2c_transfer(i2c_adap, msg, 2) == 2)
		if (rx[0] == 0x01 && rx[1] == 0xb3) {
			dprintk("-D-  DiB7000PC detected");
			return 1;
		}

	dprintk("-D-  DiB7000PC not detected");
	return 0;
}
EXPORT_SYMBOL(dib7000pc_detection);

struct i2c_adapter * dib7000p_get_i2c_master(struct dvb_frontend *demod, enum dibx000_i2c_interface intf, int gating)
{
	struct dib7000p_state *st = demod->demodulator_priv;
	return dibx000_get_i2c_adapter(&st->i2c_master, intf, gating);
}
EXPORT_SYMBOL(dib7000p_get_i2c_master);

int dib7000p_i2c_enumeration(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, struct dib7000p_config cfg[])
{
	struct dib7000p_state st = { .i2c_adap = i2c };
	int k = 0;
	u8 new_addr = 0;

	for (k = no_of_demods-1; k >= 0; k--) {
		st.cfg = cfg[k];

		/* designated i2c address */
		new_addr          = (0x40 + k) << 1;
		st.i2c_addr = new_addr;
		if (dib7000p_identify(&st) != 0) {
			st.i2c_addr = default_addr;
			if (dib7000p_identify(&st) != 0) {
				dprintk("DiB7000P #%d: not identified\n", k);
				return -EIO;
			}
		}

		/* start diversity to pull_down div_str - just for i2c-enumeration */
		dib7000p_set_output_mode(&st, OUTMODE_DIVERSITY);

		/* set new i2c address and force divstart */
		dib7000p_write_word(&st, 1285, (new_addr << 2) | 0x2);

		dprintk("IC %d initialized (to i2c_address 0x%x)", k, new_addr);
	}

	for (k = 0; k < no_of_demods; k++) {
		st.cfg = cfg[k];
		st.i2c_addr = (0x40 + k) << 1;

		// unforce divstr
		dib7000p_write_word(&st, 1285, st.i2c_addr << 2);

		/* deactivate div - it was just for i2c-enumeration */
		dib7000p_set_output_mode(&st, OUTMODE_HIGH_Z);
	}

	return 0;
}
EXPORT_SYMBOL(dib7000p_i2c_enumeration);

static struct dvb_frontend_ops dib7000p_ops;
struct dvb_frontend * dib7000p_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib7000p_config *cfg)
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
	if ((st->cfg.output_mode != OUTMODE_MPEG2_SERIAL) &&
	    (st->cfg.output_mode != OUTMODE_MPEG2_PAR_GATED_CLK))
		st->cfg.output_mode = OUTMODE_MPEG2_FIFO;

	demod                   = &st->demod;
	demod->demodulator_priv = st;
	memcpy(&st->demod.ops, &dib7000p_ops, sizeof(struct dvb_frontend_ops));

	if (dib7000p_identify(st) != 0)
		goto error;

	dibx000_init_i2c_master(&st->i2c_master, DIB7000P, st->i2c_adap, st->i2c_addr);

	dib7000p_demod_reset(st);

	return demod;

error:
	kfree(st);
	return NULL;
}
EXPORT_SYMBOL(dib7000p_attach);

static struct dvb_frontend_ops dib7000p_ops = {
	.info = {
		.name = "DiBcom 7000PC",
		.type = FE_OFDM,
		.frequency_min      = 44250000,
		.frequency_max      = 867250000,
		.frequency_stepsize = 62500,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release              = dib7000p_release,

	.init                 = dib7000p_wakeup,
	.sleep                = dib7000p_sleep,

	.set_frontend         = dib7000p_set_frontend,
	.get_tune_settings    = dib7000p_fe_get_tune_settings,
	.get_frontend         = dib7000p_get_frontend,

	.read_status          = dib7000p_read_status,
	.read_ber             = dib7000p_read_ber,
	.read_signal_strength = dib7000p_read_signal_strength,
	.read_snr             = dib7000p_read_snr,
	.read_ucblocks        = dib7000p_read_unc_blocks,
};

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Driver for the DiBcom 7000PC COFDM demodulator");
MODULE_LICENSE("GPL");
