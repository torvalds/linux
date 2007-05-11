/*
 * Linux-DVB Driver for DiBcom's second generation DiB7000P (PC).
 *
 * Copyright (C) 2005-6 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/i2c.h>

#include "dvb_frontend.h"

#include "dib7000p.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiB7000P:"); printk(args); } } while (0)

struct dib7000p_state {
	struct dvb_frontend demod;
    struct dib7000p_config cfg;

	u8 i2c_addr;
	struct i2c_adapter   *i2c_adap;

	struct dibx000_i2c_master i2c_master;

	u16 wbd_ref;

	u8 current_band;
	fe_bandwidth_t current_bandwidth;
	struct dibx000_agc_config *current_agc;
	u32 timf;

	u16 gpio_dir;
	u16 gpio_val;
};

enum dib7000p_power_mode {
	DIB7000P_POWER_ALL = 0,
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
		dprintk("i2c read error on %d\n",reg);

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
static int dib7000p_set_output_mode(struct dib7000p_state *state, int mode)
{
	int    ret = 0;
	u16 outreg, fifo_threshold, smo_mode;

	outreg = 0;
	fifo_threshold = 1792;
	smo_mode = (dib7000p_read_word(state, 235) & 0x0010) | (1 << 1);

	dprintk("-I-  Setting output mode for demod %p to %d\n",
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
		case OUTMODE_HIGH_Z:  // disable
			outreg = 0;
			break;
		default:
			dprintk("Unhandled output_mode passed to be set for demod %p\n",&state->demod);
			break;
	}

	if (state->cfg.output_mpeg2_in_188_bytes)
		smo_mode |= (1 << 5) ;

	ret |= dib7000p_write_word(state,  235, smo_mode);
	ret |= dib7000p_write_word(state,  236, fifo_threshold); /* synchronous fread */
	ret |= dib7000p_write_word(state, 1286, outreg);         /* P_Div_active */

	return ret;
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

//	dprintk("908: %x, 909: %x\n", reg_908, reg_909);

	dib7000p_write_word(state, 908, reg_908);
	dib7000p_write_word(state, 909, reg_909);
}

static int dib7000p_set_bandwidth(struct dvb_frontend *demod, u8 BW_Idx)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	u32 timf;

	// store the current bandwidth for later use
	state->current_bandwidth = BW_Idx;

	if (state->timf == 0) {
		dprintk("-D-  Using default timf\n");
		timf = state->cfg.bw->timf;
	} else {
		dprintk("-D-  Using updated timf\n");
		timf = state->timf;
	}

	timf = timf * (BW_INDEX_TO_KHZ(BW_Idx) / 100) / 80;

	dprintk("timf: %d\n",timf);

	dib7000p_write_word(state, 23, (timf >> 16) & 0xffff);
	dib7000p_write_word(state, 24, (timf      ) & 0xffff);

	return 0;
}

static int dib7000p_sad_calib(struct dib7000p_state *state)
{
/* internal */
//	dib7000p_write_word(state, 72, (3 << 14) | (1 << 12) | (524 << 0)); // sampling clock of the SAD is written in set_bandwidth
	dib7000p_write_word(state, 73, (0 << 1) | (0 << 0));
	dib7000p_write_word(state, 74, 776); // 0.625*3.3 / 4096

	/* do the calibration */
	dib7000p_write_word(state, 73, (1 << 0));
	dib7000p_write_word(state, 73, (0 << 0));

	msleep(1);

	return 0;
}

static void dib7000p_reset_pll(struct dib7000p_state *state)
{
	struct dibx000_bandwidth_config *bw = &state->cfg.bw[0];

	dib7000p_write_word(state, 903, (bw->pll_prediv << 5) | (((bw->pll_ratio >> 6) & 0x3) << 3) | (bw->pll_range << 1) | bw->pll_reset);
	dib7000p_write_word(state, 900, ((bw->pll_ratio & 0x3f) << 9) | (bw->pll_bypass << 15) | (bw->modulo << 7) | (bw->ADClkSrc << 6) |
		(bw->IO_CLK_en_core << 5) | (bw->bypclk_div << 2) | (bw->enable_refdiv << 1) | (0 << 0));

	dib7000p_write_word(state, 18, ((bw->internal*1000) >> 16) & 0xffff);
	dib7000p_write_word(state, 19,  (bw->internal*1000       ) & 0xffff);
	dib7000p_write_word(state, 21,  (bw->ifreq          >> 16) & 0xffff);
	dib7000p_write_word(state, 22,  (bw->ifreq               ) & 0xffff);

	dib7000p_write_word(state, 72, bw->sad_cfg);
}

static int dib7000p_reset_gpio(struct dib7000p_state *st)
{
	/* reset the GPIOs */
	dprintk("-D-  gpio dir: %x: gpio val: %x, gpio pwm pos: %x\n",st->gpio_dir, st->gpio_val,st->cfg.gpio_pwm_pos);

	dib7000p_write_word(st, 1029, st->gpio_dir);
	dib7000p_write_word(st, 1030, st->gpio_val);

	/* TODO 1031 is P_gpio_od */

	dib7000p_write_word(st, 1032, st->cfg.gpio_pwm_pos);

	dib7000p_write_word(st, 1037, st->cfg.pwm_freq_div);
	return 0;
}

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
		dprintk("-E-  GPIO reset was not successful.\n");

	if (dib7000p_set_output_mode(state, OUTMODE_HIGH_Z) != 0)
		dprintk("-E-  OUTPUT_MODE could not be resetted.\n");

	/* unforce divstr regardless whether i2c enumeration was done or not */
	dib7000p_write_word(state, 1285, dib7000p_read_word(state, 1285) & ~(1 << 1) );

	dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);

	return 0;
}

static void dib7000p_restart_agc(struct dib7000p_state *state)
{
	// P_restart_iqc & P_restart_agc
	dib7000p_write_word(state, 770, 0x0c00);
	dib7000p_write_word(state, 770, 0x0000);
}

static void dib7000p_update_lna(struct dib7000p_state *state)
{
	int i;
	u16 dyn_gain;

	// when there is no LNA to program return immediatly
	if (state->cfg.update_lna == NULL)
		return;

	for (i = 0; i < 5; i++) {
		// read dyn_gain here (because it is demod-dependent and not tuner)
		dyn_gain = dib7000p_read_word(state, 394);

		if (state->cfg.update_lna(&state->demod,dyn_gain)) { // LNA has changed
			dib7000p_restart_agc(state);
			msleep(5);
		} else
			break;
	}
}

static void dib7000p_pll_clk_cfg(struct dib7000p_state *state)
{
	u16 tmp = 0;
	tmp = dib7000p_read_word(state, 903);
	dib7000p_write_word(state, 903, (tmp | 0x1));   //pwr-up pll
	tmp = dib7000p_read_word(state, 900);
	dib7000p_write_word(state, 900, (tmp & 0x7fff) | (1 << 6));     //use High freq clock
}

static void dib7000p_update_timf_freq(struct dib7000p_state *state)
{
	u32 timf = (dib7000p_read_word(state, 427) << 16) | dib7000p_read_word(state, 428);
	state->timf = timf * 80 / (BW_INDEX_TO_KHZ(state->current_bandwidth) / 100);
	dib7000p_write_word(state, 23, (u16) (timf >> 16));
	dib7000p_write_word(state, 24, (u16) (timf & 0xffff));
	dprintk("-D-  Updated timf_frequency: %d (default: %d)\n",state->timf, state->cfg.bw->timf);
}

static void dib7000p_set_channel(struct dib7000p_state *state, struct dibx000_ofdm_channel *ch, u8 seq)
{
	u16 tmp, est[4]; // reg_26, reg_32, reg_33, reg_187, reg_188, reg_189, reg_190, reg_207, reg_208;

	/* nfft, guard, qam, alpha */
	dib7000p_write_word(state, 0, (ch->nfft << 7) | (ch->guard << 5) | (ch->nqam << 3) | (ch->vit_alpha));
	dib7000p_write_word(state, 5, (seq << 4) | 1); /* do not force tps, search list 0 */

	/* P_dintl_native, P_dintlv_inv, P_vit_hrch, P_vit_code_rate, P_vit_select_hp */
	tmp = (ch->intlv_native << 6) | (ch->vit_hrch << 4) | (ch->vit_select_hp & 0x1);
	if (ch->vit_hrch == 0 || ch->vit_select_hp == 1)
		tmp |= (ch->vit_code_rate_hp << 1);
	else
		tmp |= (ch->vit_code_rate_lp << 1);
	dib7000p_write_word(state, 208, tmp);

	/* P_dvsy_sync_wait */
	switch (ch->nfft) {
		case 1: tmp = 256; break;
		case 2: tmp = 128; break;
		case 0:
		default: tmp = 64; break;
	}
	tmp *= ((1 << (ch->guard)) * 3 / 2); // add 50% SFN margin
	tmp <<= 4;

	/* deactive the possibility of diversity reception if extended interleave */
	/* P_dvsy_sync_mode = 0, P_dvsy_sync_enable=1, P_dvcb_comb_mode=2 */
	if (ch->intlv_native || ch->nfft == 1)
		tmp |= (1 << 2) | (2 << 0);
	dib7000p_write_word(state, 207, tmp);

	dib7000p_write_word(state, 26, 0x6680);   // timf(6xxx)
	dib7000p_write_word(state, 29, 0x1273);   // isi inh1273 on1073
	dib7000p_write_word(state, 32, 0x0003);   // pha_off_max(xxx3)
	dib7000p_write_word(state, 33, 0x0005);   // sfreq(xxx5)

	/* channel estimation fine configuration */
	switch (ch->nqam) {
		case 2:
			est[0] = 0x0148;       /* P_adp_regul_cnt 0.04 */
			est[1] = 0xfff0;       /* P_adp_noise_cnt -0.002 */
			est[2] = 0x00a4;       /* P_adp_regul_ext 0.02 */
			est[3] = 0xfff8;       /* P_adp_noise_ext -0.001 */
			break;
		case 1:
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
	for (tmp = 0; tmp < 4; tmp++)
		dib7000p_write_word(state, 187 + tmp, est[tmp]);

	// set power-up level: interf+analog+AGC
	dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);
	dib7000p_set_adc_state(state, DIBX000_ADC_ON);
	dib7000p_pll_clk_cfg(state);
	msleep(7);

	// AGC initialization
	if (state->cfg.agc_control)
		state->cfg.agc_control(&state->demod, 1);

	dib7000p_restart_agc(state);

	// wait AGC rough lock time
	msleep(5);

	dib7000p_update_lna(state);

	// wait AGC accurate lock time
	msleep(7);
	if (state->cfg.agc_control)
		state->cfg.agc_control(&state->demod, 0);
}

static int dib7000p_autosearch_start(struct dvb_frontend *demod, struct dibx000_ofdm_channel *ch)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	struct dibx000_ofdm_channel auto_ch;
	u32 value;

	INIT_OFDM_CHANNEL(&auto_ch);
	auto_ch.RF_kHz           = ch->RF_kHz;
	auto_ch.Bw               = ch->Bw;
	auto_ch.nqam             = 2;
	auto_ch.guard            = 0;
	auto_ch.nfft             = 1;
	auto_ch.vit_alpha        = 1;
	auto_ch.vit_select_hp    = 1;
	auto_ch.vit_code_rate_hp = 2;
	auto_ch.vit_code_rate_lp = 3;
	auto_ch.vit_hrch         = 0;
	auto_ch.intlv_native     = 1;

	dib7000p_set_channel(state, &auto_ch, 7);

	// always use the setting for 8MHz here lock_time for 7,6 MHz are longer
	value = 30 * state->cfg.bw->internal;
	dib7000p_write_word(state, 6,  (u16) ((value >> 16) & 0xffff)); // lock0 wait time
	dib7000p_write_word(state, 7,  (u16)  (value        & 0xffff)); // lock0 wait time
	value = 100 * state->cfg.bw->internal;
	dib7000p_write_word(state, 8,  (u16) ((value >> 16) & 0xffff)); // lock1 wait time
	dib7000p_write_word(state, 9,  (u16)  (value        & 0xffff)); // lock1 wait time
	value = 500 * state->cfg.bw->internal;
	dib7000p_write_word(state, 10, (u16) ((value >> 16) & 0xffff)); // lock2 wait time
	dib7000p_write_word(state, 11, (u16)  (value        & 0xffff)); // lock2 wait time

	value = dib7000p_read_word(state, 0);
	dib7000p_write_word(state, 0, (1 << 9) | value);
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

static int dib7000p_tune(struct dvb_frontend *demod, struct dibx000_ofdm_channel *ch)
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
	dib7000p_write_word(state, 29, (0 << 14) | (4 << 10) | (0 << 9) | (3 << 5) | (1 << 4) | (0x3));

	// never achieved a lock with that bandwidth so far - wait for osc-freq to update
	if (state->timf == 0)
		msleep(200);

	/* offset loop parameters */

	/* P_timf_alpha, P_corm_alpha=6, P_corm_thres=0x80 */
	tmp = (6 << 8) | 0x80;
	switch (ch->nfft) {
		case 0: tmp |= (7 << 12); break;
		case 1: tmp |= (9 << 12); break;
		case 2: tmp |= (8 << 12); break;
	}
	dib7000p_write_word(state, 26, tmp);  /* timf_a(6xxx) */

	/* P_ctrl_freeze_pha_shift=0, P_ctrl_pha_off_max */
	tmp = (0 << 4);
	switch (ch->nfft) {
		case 0: tmp |= 0x6; break;
		case 1: tmp |= 0x8; break;
		case 2: tmp |= 0x7; break;
	}
	dib7000p_write_word(state, 32,  tmp);

	/* P_ctrl_sfreq_inh=0, P_ctrl_sfreq_step */
	tmp = (0 << 4);
	switch (ch->nfft) {
		case 0: tmp |= 0x6; break;
		case 1: tmp |= 0x8; break;
		case 2: tmp |= 0x7; break;
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
		dib7000p_update_timf_freq(state);

	return 0;
}

static int dib7000p_init(struct dvb_frontend *demod)
{
	struct dibx000_agc_config *agc;
	struct dib7000p_state *state = demod->demodulator_priv;
	int ret = 0;

	// Demodulator default configuration
	agc = state->cfg.agc;

	dib7000p_set_power_mode(state, DIB7000P_POWER_ALL);
	dib7000p_set_adc_state(state, DIBX000_SLOW_ADC_ON);

	/* AGC */
	ret |= dib7000p_write_word(state, 75 ,  agc->setup );
	ret |= dib7000p_write_word(state, 76 ,  agc->inv_gain );
	ret |= dib7000p_write_word(state, 77 ,  agc->time_stabiliz );
	ret |= dib7000p_write_word(state, 100, (agc->alpha_level << 12) | agc->thlock);

	// Demod AGC loop configuration
	ret |= dib7000p_write_word(state, 101, (agc->alpha_mant << 5) | agc->alpha_exp);
	ret |= dib7000p_write_word(state, 102, (agc->beta_mant << 6)  | agc->beta_exp);

	/* AGC continued */
	dprintk("-D-  WBD: ref: %d, sel: %d, active: %d, alpha: %d\n",
		state->wbd_ref != 0 ? state->wbd_ref : agc->wbd_ref, agc->wbd_sel, !agc->perform_agc_softsplit, agc->wbd_sel);

	if (state->wbd_ref != 0)
		ret |= dib7000p_write_word(state, 105, (agc->wbd_inv << 12) | state->wbd_ref);
	else
		ret |= dib7000p_write_word(state, 105, (agc->wbd_inv << 12) | agc->wbd_ref);

	ret |= dib7000p_write_word(state, 106, (agc->wbd_sel << 13) | (agc->wbd_alpha << 9) | (agc->perform_agc_softsplit << 8) );

	ret |= dib7000p_write_word(state, 107,  agc->agc1_max);
	ret |= dib7000p_write_word(state, 108,  agc->agc1_min);
	ret |= dib7000p_write_word(state, 109,  agc->agc2_max);
	ret |= dib7000p_write_word(state, 110,  agc->agc2_min);
	ret |= dib7000p_write_word(state, 111, (agc->agc1_pt1 << 8) | agc->agc1_pt2 );
	ret |= dib7000p_write_word(state, 112,  agc->agc1_pt3);
	ret |= dib7000p_write_word(state, 113, (agc->agc1_slope1 << 8) | agc->agc1_slope2);
	ret |= dib7000p_write_word(state, 114, (agc->agc2_pt1 << 8) | agc->agc2_pt2);
	ret |= dib7000p_write_word(state, 115, (agc->agc2_slope1 << 8) | agc->agc2_slope2);

	/* disable power smoothing */
	ret |= dib7000p_write_word(state, 145, 0);
	ret |= dib7000p_write_word(state, 146, 0);
	ret |= dib7000p_write_word(state, 147, 0);
	ret |= dib7000p_write_word(state, 148, 0);
	ret |= dib7000p_write_word(state, 149, 0);
	ret |= dib7000p_write_word(state, 150, 0);
	ret |= dib7000p_write_word(state, 151, 0);
	ret |= dib7000p_write_word(state, 152, 0);

	// P_timf_alpha=6, P_corm_alpha=6, P_corm_thres=128 default: 6,4,26
	ret |= dib7000p_write_word(state, 26 ,0x6680);

	// P_palf_filter_on=1, P_palf_filter_freeze=0, P_palf_alpha_regul=16
	ret |= dib7000p_write_word(state, 142,0x0410);
	// P_fft_freq_dir=1, P_fft_nb_to_cut=0
	ret |= dib7000p_write_word(state, 154,1 << 13);
	// P_pha3_thres, default 0x3000
	ret |= dib7000p_write_word(state, 168,0x0ccd);
	// P_cti_use_cpe=0, P_cti_use_prog=0, P_cti_win_len=16, default: 0x0010
	//ret |= dib7000p_write_word(state, 169,0x0010);
	// P_cspu_regul=512, P_cspu_win_cut=15, default: 0x2005
	ret |= dib7000p_write_word(state, 183,0x200f);
	// P_adp_regul_cnt=573, default: 410
	ret |= dib7000p_write_word(state, 187,0x023d);
	// P_adp_noise_cnt=
	ret |= dib7000p_write_word(state, 188,0x00a4);
	// P_adp_regul_ext
	ret |= dib7000p_write_word(state, 189,0x00a4);
	// P_adp_noise_ext
	ret |= dib7000p_write_word(state, 190,0x7ff0);
	// P_adp_fil
	ret |= dib7000p_write_word(state, 191,0x3ccc);

	ret |= dib7000p_write_word(state, 222,0x0010);
	// P_smo_mode, P_smo_rs_discard, P_smo_fifo_flush, P_smo_pid_parse, P_smo_error_discard
	ret |= dib7000p_write_word(state, 235,0x0062);

	// P_iqc_alpha_pha, P_iqc_alpha_amp_dcc_alpha, ...
	if(state->cfg.tuner_is_baseband)
		ret |= dib7000p_write_word(state, 36,0x0755);
	else
		ret |= dib7000p_write_word(state, 36,0x1f55);

	// auto search configuration
	ret |= dib7000p_write_word(state, 2  ,0x0004);
	ret |= dib7000p_write_word(state, 3  ,0x1000);

	/* Equal Lock */
	ret |= dib7000p_write_word(state, 4   ,0x0814);

	ret |= dib7000p_write_word(state, 6  ,0x001b);
	ret |= dib7000p_write_word(state, 7  ,0x7740);
	ret |= dib7000p_write_word(state, 8  ,0x005b);
	ret |= dib7000p_write_word(state, 9  ,0x8d80);
	ret |= dib7000p_write_word(state, 10 ,0x01c9);
	ret |= dib7000p_write_word(state, 11 ,0xc380);
	ret |= dib7000p_write_word(state, 12 ,0x0000);
	ret |= dib7000p_write_word(state, 13 ,0x0080);
	ret |= dib7000p_write_word(state, 14 ,0x0000);
	ret |= dib7000p_write_word(state, 15 ,0x0090);
	ret |= dib7000p_write_word(state, 16 ,0x0001);
	ret |= dib7000p_write_word(state, 17 ,0xd4c0);

	// P_clk_cfg1
	ret |= dib7000p_write_word(state, 901, 0x0006);

	// P_divclksel=3 P_divbitsel=1
	ret |= dib7000p_write_word(state, 902, (3 << 10) | (1 << 6));

	// Tuner IO bank: max drive (14mA) + divout pads max drive
	ret |= dib7000p_write_word(state, 905, 0x2c8e);

	ret |= dib7000p_set_bandwidth(&state->demod, BANDWIDTH_8_MHZ);
	dib7000p_sad_calib(state);

	return ret;
}

static int dib7000p_sleep(struct dvb_frontend *demod)
{
	struct dib7000p_state *state = demod->demodulator_priv;
	return dib7000p_set_output_mode(state, OUTMODE_HIGH_Z) | dib7000p_set_power_mode(state, DIB7000P_POWER_INTERFACE_ONLY);
}

static int dib7000p_identify(struct dib7000p_state *st)
{
	u16 value;
	dprintk("-I-  DiB7000PC: checking demod on I2C address: %d (%x)\n",
		st->i2c_addr, st->i2c_addr);

	if ((value = dib7000p_read_word(st, 768)) != 0x01b3) {
		dprintk("-E-  DiB7000PC: wrong Vendor ID (read=0x%x)\n",value);
		return -EREMOTEIO;
	}

	if ((value = dib7000p_read_word(st, 769)) != 0x4000) {
		dprintk("-E-  DiB7000PC: wrong Device ID (%x)\n",value);
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

	fep->u.ofdm.bandwidth = state->current_bandwidth;

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
	struct dibx000_ofdm_channel ch;

	INIT_OFDM_CHANNEL(&ch);
	FEP2DIB(fep,&ch);

	state->current_bandwidth = fep->u.ofdm.bandwidth;
	dib7000p_set_bandwidth(fe, fep->u.ofdm.bandwidth);

	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe, fep);

	if (fep->u.ofdm.transmission_mode == TRANSMISSION_MODE_AUTO ||
		fep->u.ofdm.guard_interval    == GUARD_INTERVAL_AUTO ||
		fep->u.ofdm.constellation     == QAM_AUTO ||
		fep->u.ofdm.code_rate_HP      == FEC_AUTO) {
		int i = 800, found;

		dib7000p_autosearch_start(fe, &ch);
		do {
			msleep(1);
			found = dib7000p_autosearch_is_irq(fe);
		} while (found == 0 && i--);

		dprintk("autosearch returns: %d\n",found);
		if (found == 0 || found == 1)
			return 0; // no channel found

		dib7000p_get_frontend(fe, fep);
		FEP2DIB(fep, &ch);
	}

	/* make this a config parameter */
	dib7000p_set_output_mode(state, OUTMODE_MPEG2_FIFO);

	return dib7000p_tune(fe, &ch);
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
	*snr = 0x0000;
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

		dprintk("IC %d initialized (to i2c_address 0x%x)\n", k, new_addr);
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

	.init                 = dib7000p_init,
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
