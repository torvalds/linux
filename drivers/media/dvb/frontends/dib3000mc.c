/*
 * Driver for DiBcom DiB3000MC/P-demodulator.
 *
 * Copyright (C) 2004-6 DiBcom (http://www.dibcom.fr/)
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * This code is partially based on the previous dib3000mc.c .
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
//#include <linux/init.h>
//#include <linux/delay.h>
//#include <linux/string.h>
//#include <linux/slab.h>

#include "dvb_frontend.h"

#include "dib3000mc.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiB3000MC/P:"); printk(args); } } while (0)

struct dib3000mc_state {
	struct dvb_frontend demod;
	struct dib3000mc_config *cfg;

	u8 i2c_addr;
	struct i2c_adapter *i2c_adap;

	struct dibx000_i2c_master i2c_master;

	u32 timf;

	fe_bandwidth_t current_bandwidth;

	u16 dev_id;
};

static u16 dib3000mc_read_word(struct dib3000mc_state *state, u16 reg)
{
	u8 wb[2] = { (reg >> 8) | 0x80, reg & 0xff };
	u8 rb[2];
	struct i2c_msg msg[2] = {
		{ .addr = state->i2c_addr >> 1, .flags = 0,        .buf = wb, .len = 2 },
		{ .addr = state->i2c_addr >> 1, .flags = I2C_M_RD, .buf = rb, .len = 2 },
	};

	if (i2c_transfer(state->i2c_adap, msg, 2) != 2)
		dprintk("i2c read error on %d\n",reg);

	return (rb[0] << 8) | rb[1];
}

static int dib3000mc_write_word(struct dib3000mc_state *state, u16 reg, u16 val)
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


static int dib3000mc_identify(struct dib3000mc_state *state)
{
	u16 value;
	if ((value = dib3000mc_read_word(state, 1025)) != 0x01b3) {
		dprintk("-E-  DiB3000MC/P: wrong Vendor ID (read=0x%x)\n",value);
		return -EREMOTEIO;
	}

	value = dib3000mc_read_word(state, 1026);
	if (value != 0x3001 && value != 0x3002) {
		dprintk("-E-  DiB3000MC/P: wrong Device ID (%x)\n",value);
		return -EREMOTEIO;
	}
	state->dev_id = value;

	dprintk("-I-  found DiB3000MC/P: %x\n",state->dev_id);

	return 0;
}

static int dib3000mc_set_timing(struct dib3000mc_state *state, s16 nfft, u8 bw, u8 update_offset)
{
	u32 timf;

	if (state->timf == 0) {
		timf = 1384402; // default value for 8MHz
		if (update_offset)
			msleep(200); // first time we do an update
	} else
		timf = state->timf;

	timf *= (BW_INDEX_TO_KHZ(bw) / 1000);

	if (update_offset) {
		s16 tim_offs = dib3000mc_read_word(state, 416);

		if (tim_offs &  0x2000)
			tim_offs -= 0x4000;

		if (nfft == 0)
			tim_offs *= 4;

		timf += tim_offs;
		state->timf = timf / (BW_INDEX_TO_KHZ(bw) / 1000);
	}

	dprintk("timf: %d\n", timf);

	dib3000mc_write_word(state, 23, timf >> 16);
	dib3000mc_write_word(state, 24, timf & 0xffff);

	return 0;
}

static int dib3000mc_setup_pwm_state(struct dib3000mc_state *state)
{
	u16 reg_51, reg_52 = state->cfg->agc->setup & 0xfefb;
    if (state->cfg->pwm3_inversion) {
		reg_51 =  (2 << 14) | (0 << 10) | (7 << 6) | (2 << 2) | (2 << 0);
		reg_52 |= (1 << 2);
	} else {
		reg_51 = (2 << 14) | (4 << 10) | (7 << 6) | (2 << 2) | (2 << 0);
		reg_52 |= (1 << 8);
	}
	dib3000mc_write_word(state, 51, reg_51);
	dib3000mc_write_word(state, 52, reg_52);

    if (state->cfg->use_pwm3)
		dib3000mc_write_word(state, 245, (1 << 3) | (1 << 0));
	else
		dib3000mc_write_word(state, 245, 0);

    dib3000mc_write_word(state, 1040, 0x3);
	return 0;
}

static int dib3000mc_set_output_mode(struct dib3000mc_state *state, int mode)
{
	int    ret = 0;
	u16 fifo_threshold = 1792;
	u16 outreg = 0;
	u16 outmode = 0;
	u16 elecout = 1;
	u16 smo_reg = dib3000mc_read_word(state, 206) & 0x0010; /* keep the pid_parse bit */

	dprintk("-I-  Setting output mode for demod %p to %d\n",
			&state->demod, mode);

	switch (mode) {
		case OUTMODE_HIGH_Z:  // disable
			elecout = 0;
			break;
		case OUTMODE_MPEG2_PAR_GATED_CLK:   // STBs with parallel gated clock
			outmode = 0;
			break;
		case OUTMODE_MPEG2_PAR_CONT_CLK:    // STBs with parallel continues clock
			outmode = 1;
			break;
		case OUTMODE_MPEG2_SERIAL:          // STBs with serial input
			outmode = 2;
			break;
		case OUTMODE_MPEG2_FIFO:            // e.g. USB feeding
			elecout = 3;
			/*ADDR @ 206 :
			P_smo_error_discard  [1;6:6] = 0
			P_smo_rs_discard     [1;5:5] = 0
			P_smo_pid_parse      [1;4:4] = 0
			P_smo_fifo_flush     [1;3:3] = 0
			P_smo_mode           [2;2:1] = 11
			P_smo_ovf_prot       [1;0:0] = 0
			*/
			smo_reg |= 3 << 1;
			fifo_threshold = 512;
			outmode = 5;
			break;
		case OUTMODE_DIVERSITY:
			outmode = 4;
			elecout = 1;
			break;
		default:
			dprintk("Unhandled output_mode passed to be set for demod %p\n",&state->demod);
			outmode = 0;
			break;
	}

	if ((state->cfg->output_mpeg2_in_188_bytes))
		smo_reg |= (1 << 5); // P_smo_rs_discard     [1;5:5] = 1

	outreg = dib3000mc_read_word(state, 244) & 0x07FF;
	outreg |= (outmode << 11);
	ret |= dib3000mc_write_word(state,  244, outreg);
	ret |= dib3000mc_write_word(state,  206, smo_reg);   /*smo_ mode*/
	ret |= dib3000mc_write_word(state,  207, fifo_threshold); /* synchronous fread */
	ret |= dib3000mc_write_word(state, 1040, elecout);         /* P_out_cfg */
	return ret;
}

static int dib3000mc_set_bandwidth(struct dvb_frontend *demod, u8 bw)
{
	struct dib3000mc_state *state = demod->demodulator_priv;
	u16 bw_cfg[6] = { 0 };
	u16 imp_bw_cfg[3] = { 0 };
	u16 reg;

/* settings here are for 27.7MHz */
	switch (bw) {
		case BANDWIDTH_8_MHZ:
			bw_cfg[0] = 0x0019; bw_cfg[1] = 0x5c30; bw_cfg[2] = 0x0054; bw_cfg[3] = 0x88a0; bw_cfg[4] = 0x01a6; bw_cfg[5] = 0xab20;
			imp_bw_cfg[0] = 0x04db; imp_bw_cfg[1] = 0x00db; imp_bw_cfg[2] = 0x00b7;
			break;

		case BANDWIDTH_7_MHZ:
			bw_cfg[0] = 0x001c; bw_cfg[1] = 0xfba5; bw_cfg[2] = 0x0060; bw_cfg[3] = 0x9c25; bw_cfg[4] = 0x01e3; bw_cfg[5] = 0x0cb7;
			imp_bw_cfg[0] = 0x04c0; imp_bw_cfg[1] = 0x00c0; imp_bw_cfg[2] = 0x00a0;
			break;

		case BANDWIDTH_6_MHZ:
			bw_cfg[0] = 0x0021; bw_cfg[1] = 0xd040; bw_cfg[2] = 0x0070; bw_cfg[3] = 0xb62b; bw_cfg[4] = 0x0233; bw_cfg[5] = 0x8ed5;
			imp_bw_cfg[0] = 0x04a5; imp_bw_cfg[1] = 0x00a5; imp_bw_cfg[2] = 0x0089;
			break;

		case 255 /* BANDWIDTH_5_MHZ */:
			bw_cfg[0] = 0x0028; bw_cfg[1] = 0x9380; bw_cfg[2] = 0x0087; bw_cfg[3] = 0x4100; bw_cfg[4] = 0x02a4; bw_cfg[5] = 0x4500;
			imp_bw_cfg[0] = 0x0489; imp_bw_cfg[1] = 0x0089; imp_bw_cfg[2] = 0x0072;
			break;

		default: return -EINVAL;
	}

	for (reg = 6; reg < 12; reg++)
		dib3000mc_write_word(state, reg, bw_cfg[reg - 6]);
	dib3000mc_write_word(state, 12, 0x0000);
	dib3000mc_write_word(state, 13, 0x03e8);
	dib3000mc_write_word(state, 14, 0x0000);
	dib3000mc_write_word(state, 15, 0x03f2);
	dib3000mc_write_word(state, 16, 0x0001);
	dib3000mc_write_word(state, 17, 0xb0d0);
	// P_sec_len
	dib3000mc_write_word(state, 18, 0x0393);
	dib3000mc_write_word(state, 19, 0x8700);

	for (reg = 55; reg < 58; reg++)
		dib3000mc_write_word(state, reg, imp_bw_cfg[reg - 55]);

	// Timing configuration
	dib3000mc_set_timing(state, 0, bw, 0);

	return 0;
}

static u16 impulse_noise_val[29] =

{
	0x38, 0x6d9, 0x3f28, 0x7a7, 0x3a74, 0x196, 0x32a, 0x48c, 0x3ffe, 0x7f3,
	0x2d94, 0x76, 0x53d, 0x3ff8, 0x7e3, 0x3320, 0x76, 0x5b3, 0x3feb, 0x7d2,
	0x365e, 0x76, 0x48c, 0x3ffe, 0x5b3, 0x3feb, 0x76, 0x0000, 0xd
};

static void dib3000mc_set_impulse_noise(struct dib3000mc_state *state, u8 mode, s16 nfft)
{
	u16 i;
	for (i = 58; i < 87; i++)
		dib3000mc_write_word(state, i, impulse_noise_val[i-58]);

	if (nfft == 1) {
		dib3000mc_write_word(state, 58, 0x3b);
		dib3000mc_write_word(state, 84, 0x00);
		dib3000mc_write_word(state, 85, 0x8200);
	}

	dib3000mc_write_word(state, 34, 0x1294);
	dib3000mc_write_word(state, 35, 0x1ff8);
	if (mode == 1)
		dib3000mc_write_word(state, 55, dib3000mc_read_word(state, 55) | (1 << 10));
}

static int dib3000mc_init(struct dvb_frontend *demod)
{
	struct dib3000mc_state *state = demod->demodulator_priv;
	struct dibx000_agc_config *agc = state->cfg->agc;

	// Restart Configuration
	dib3000mc_write_word(state, 1027, 0x8000);
	dib3000mc_write_word(state, 1027, 0x0000);

	// power up the demod + mobility configuration
	dib3000mc_write_word(state, 140, 0x0000);
	dib3000mc_write_word(state, 1031, 0);

	if (state->cfg->mobile_mode) {
		dib3000mc_write_word(state, 139,  0x0000);
		dib3000mc_write_word(state, 141,  0x0000);
		dib3000mc_write_word(state, 175,  0x0002);
		dib3000mc_write_word(state, 1032, 0x0000);
	} else {
		dib3000mc_write_word(state, 139,  0x0001);
		dib3000mc_write_word(state, 141,  0x0000);
		dib3000mc_write_word(state, 175,  0x0000);
		dib3000mc_write_word(state, 1032, 0x012C);
	}
	dib3000mc_write_word(state, 1033, 0x0000);

	// P_clk_cfg
	dib3000mc_write_word(state, 1037, 0x3130);

	// other configurations

	// P_ctrl_sfreq
	dib3000mc_write_word(state, 33, (5 << 0));
	dib3000mc_write_word(state, 88, (1 << 10) | (0x10 << 0));

	// Phase noise control
	// P_fft_phacor_inh, P_fft_phacor_cpe, P_fft_powrange
	dib3000mc_write_word(state, 99, (1 << 9) | (0x20 << 0));

	if (state->cfg->phase_noise_mode == 0)
		dib3000mc_write_word(state, 111, 0x00);
	else
		dib3000mc_write_word(state, 111, 0x02);

	// P_agc_global
	dib3000mc_write_word(state, 50, 0x8000);

	// agc setup misc
	dib3000mc_setup_pwm_state(state);

	// P_agc_counter_lock
	dib3000mc_write_word(state, 53, 0x87);
	// P_agc_counter_unlock
	dib3000mc_write_word(state, 54, 0x87);

	/* agc */
	dib3000mc_write_word(state, 36, state->cfg->max_time);
	dib3000mc_write_word(state, 37, (state->cfg->agc_command1 << 13) | (state->cfg->agc_command2 << 12) | (0x1d << 0));
	dib3000mc_write_word(state, 38, state->cfg->pwm3_value);
	dib3000mc_write_word(state, 39, state->cfg->ln_adc_level);

	// set_agc_loop_Bw
	dib3000mc_write_word(state, 40, 0x0179);
	dib3000mc_write_word(state, 41, 0x03f0);

	dib3000mc_write_word(state, 42, agc->agc1_max);
	dib3000mc_write_word(state, 43, agc->agc1_min);
	dib3000mc_write_word(state, 44, agc->agc2_max);
	dib3000mc_write_word(state, 45, agc->agc2_min);
	dib3000mc_write_word(state, 46, (agc->agc1_pt1 << 8) | agc->agc1_pt2);
	dib3000mc_write_word(state, 47, (agc->agc1_slope1 << 8) | agc->agc1_slope2);
	dib3000mc_write_word(state, 48, (agc->agc2_pt1 << 8) | agc->agc2_pt2);
	dib3000mc_write_word(state, 49, (agc->agc2_slope1 << 8) | agc->agc2_slope2);

// Begin: TimeOut registers
	// P_pha3_thres
	dib3000mc_write_word(state, 110, 3277);
	// P_timf_alpha = 6, P_corm_alpha = 6, P_corm_thres = 0x80
	dib3000mc_write_word(state,  26, 0x6680);
	// lock_mask0
	dib3000mc_write_word(state, 1, 4);
	// lock_mask1
	dib3000mc_write_word(state, 2, 4);
	// lock_mask2
	dib3000mc_write_word(state, 3, 0x1000);
	// P_search_maxtrial=1
	dib3000mc_write_word(state, 5, 1);

	dib3000mc_set_bandwidth(&state->demod, BANDWIDTH_8_MHZ);

	// div_lock_mask
	dib3000mc_write_word(state,  4, 0x814);

	dib3000mc_write_word(state, 21, (1 << 9) | 0x164);
	dib3000mc_write_word(state, 22, 0x463d);

	// Spurious rm cfg
	// P_cspu_regul, P_cspu_win_cut
	dib3000mc_write_word(state, 120, 0x200f);
	// P_adp_selec_monit
	dib3000mc_write_word(state, 134, 0);

	// Fec cfg
	dib3000mc_write_word(state, 195, 0x10);

	// diversity register: P_dvsy_sync_wait..
	dib3000mc_write_word(state, 180, 0x2FF0);

	// Impulse noise configuration
	dib3000mc_set_impulse_noise(state, 0, 1);

	// output mode set-up
	dib3000mc_set_output_mode(state, OUTMODE_HIGH_Z);

	/* close the i2c-gate */
	dib3000mc_write_word(state, 769, (1 << 7) );

	return 0;
}

static int dib3000mc_sleep(struct dvb_frontend *demod)
{
	struct dib3000mc_state *state = demod->demodulator_priv;

	dib3000mc_write_word(state, 1031, 0xFFFF);
	dib3000mc_write_word(state, 1032, 0xFFFF);
	dib3000mc_write_word(state, 1033, 0xFFF0);

    return 0;
}

static void dib3000mc_set_adp_cfg(struct dib3000mc_state *state, s16 qam)
{
	u16 cfg[4] = { 0 },reg;
	switch (qam) {
		case 0:
			cfg[0] = 0x099a; cfg[1] = 0x7fae; cfg[2] = 0x0333; cfg[3] = 0x7ff0;
			break;
		case 1:
			cfg[0] = 0x023d; cfg[1] = 0x7fdf; cfg[2] = 0x00a4; cfg[3] = 0x7ff0;
			break;
		case 2:
			cfg[0] = 0x0148; cfg[1] = 0x7ff0; cfg[2] = 0x00a4; cfg[3] = 0x7ff8;
			break;
	}
	for (reg = 129; reg < 133; reg++)
		dib3000mc_write_word(state, reg, cfg[reg - 129]);
}

static void dib3000mc_set_channel_cfg(struct dib3000mc_state *state, struct dibx000_ofdm_channel *chan, u16 seq)
{
	u16 tmp;

	dib3000mc_set_timing(state, chan->nfft, chan->Bw, 0);

//	if (boost)
//		dib3000mc_write_word(state, 100, (11 << 6) + 6);
//	else
		dib3000mc_write_word(state, 100, (16 << 6) + 9);

	dib3000mc_write_word(state, 1027, 0x0800);
	dib3000mc_write_word(state, 1027, 0x0000);

	//Default cfg isi offset adp
	dib3000mc_write_word(state, 26,  0x6680);
	dib3000mc_write_word(state, 29,  0x1273);
	dib3000mc_write_word(state, 33,       5);
	dib3000mc_set_adp_cfg(state, 1);
	dib3000mc_write_word(state, 133,  15564);

	dib3000mc_write_word(state, 12 , 0x0);
	dib3000mc_write_word(state, 13 , 0x3e8);
	dib3000mc_write_word(state, 14 , 0x0);
	dib3000mc_write_word(state, 15 , 0x3f2);

	dib3000mc_write_word(state, 93,0);
	dib3000mc_write_word(state, 94,0);
	dib3000mc_write_word(state, 95,0);
	dib3000mc_write_word(state, 96,0);
	dib3000mc_write_word(state, 97,0);
	dib3000mc_write_word(state, 98,0);

	dib3000mc_set_impulse_noise(state, 0, chan->nfft);

	tmp = ((chan->nfft & 0x1) << 7) | (chan->guard << 5) | (chan->nqam << 3) | chan->vit_alpha;
	dib3000mc_write_word(state, 0, tmp);

	dib3000mc_write_word(state, 5, seq);

	tmp = (chan->vit_hrch << 4) | (chan->vit_select_hp);
	if (!chan->vit_hrch || (chan->vit_hrch && chan->vit_select_hp))
		tmp |= chan->vit_code_rate_hp << 1;
	else
		tmp |= chan->vit_code_rate_lp << 1;
	dib3000mc_write_word(state, 181, tmp);

	// diversity synchro delay
	tmp = dib3000mc_read_word(state, 180) & 0x000f;
	tmp |= ((chan->nfft == 0) ? 64 : 256) * ((1 << (chan->guard)) * 3 / 2) << 4; // add 50% SFN margin
	dib3000mc_write_word(state, 180, tmp);

	// restart demod
	tmp = dib3000mc_read_word(state, 0);
	dib3000mc_write_word(state, 0, tmp | (1 << 9));
	dib3000mc_write_word(state, 0, tmp);

	msleep(30);

	dib3000mc_set_impulse_noise(state, state->cfg->impulse_noise_mode, chan->nfft);
}

static int dib3000mc_autosearch_start(struct dvb_frontend *demod, struct dibx000_ofdm_channel *chan)
{
	struct dib3000mc_state *state = demod->demodulator_priv;
	u16 reg;
//	u32 val;
	struct dibx000_ofdm_channel fchan;

	INIT_OFDM_CHANNEL(&fchan);
	fchan = *chan;


	/* a channel for autosearch */
	fchan.nfft = 1; fchan.guard = 0; fchan.nqam = 2;
	fchan.vit_alpha = 1; fchan.vit_code_rate_hp = 2; fchan.vit_code_rate_lp = 2;
	fchan.vit_hrch = 0; fchan.vit_select_hp = 1;

	dib3000mc_set_channel_cfg(state, &fchan, 7);

	reg = dib3000mc_read_word(state, 0);
	dib3000mc_write_word(state, 0, reg | (1 << 8));
	dib3000mc_read_word(state, 511);
	dib3000mc_write_word(state, 0, reg);

	return 0;
}

static int dib3000mc_autosearch_is_irq(struct dvb_frontend *demod)
{
	struct dib3000mc_state *state = demod->demodulator_priv;
	u16 irq_pending = dib3000mc_read_word(state, 511);

	if (irq_pending & 0x1) // failed
		return 1;

	if (irq_pending & 0x2) // succeeded
		return 2;

	return 0; // still pending
}

static int dib3000mc_tune(struct dvb_frontend *demod, struct dibx000_ofdm_channel *ch)
{
	struct dib3000mc_state *state = demod->demodulator_priv;

	// ** configure demod **
	dib3000mc_set_channel_cfg(state, ch, 0);

	// activates isi
	dib3000mc_write_word(state, 29, 0x1073);

	dib3000mc_set_adp_cfg(state, (u8)ch->nqam);

	if (ch->nfft == 1) {
		dib3000mc_write_word(state, 26, 38528);
		dib3000mc_write_word(state, 33, 8);
	} else {
		dib3000mc_write_word(state, 26, 30336);
		dib3000mc_write_word(state, 33, 6);
	}

	if (dib3000mc_read_word(state, 509) & 0x80)
		dib3000mc_set_timing(state, ch->nfft, ch->Bw, 1);

	return 0;
}

struct i2c_adapter * dib3000mc_get_tuner_i2c_master(struct dvb_frontend *demod, int gating)
{
	struct dib3000mc_state *st = demod->demodulator_priv;
	return dibx000_get_i2c_adapter(&st->i2c_master, DIBX000_I2C_INTERFACE_TUNER, gating);
}

EXPORT_SYMBOL(dib3000mc_get_tuner_i2c_master);

static int dib3000mc_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	u16 tps = dib3000mc_read_word(state,458);

	fep->inversion = INVERSION_AUTO;

	fep->u.ofdm.bandwidth = state->current_bandwidth;

	switch ((tps >> 8) & 0x1) {
		case 0: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_2K; break;
		case 1: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_8K; break;
	}

	switch (tps & 0x3) {
		case 0: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_32; break;
		case 1: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_16; break;
		case 2: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_8; break;
		case 3: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_4; break;
	}

	switch ((tps >> 13) & 0x3) {
		case 0: fep->u.ofdm.constellation = QPSK; break;
		case 1: fep->u.ofdm.constellation = QAM_16; break;
		case 2:
		default: fep->u.ofdm.constellation = QAM_64; break;
	}

	/* as long as the frontend_param structure is fixed for hierarchical transmission I refuse to use it */
	/* (tps >> 12) & 0x1 == hrch is used, (tps >> 9) & 0x7 == alpha */

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

	return 0;
}

static int dib3000mc_set_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	struct dibx000_ofdm_channel ch;

	INIT_OFDM_CHANNEL(&ch);
	FEP2DIB(fep,&ch);

	state->current_bandwidth = fep->u.ofdm.bandwidth;
	dib3000mc_set_bandwidth(fe, fep->u.ofdm.bandwidth);

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, fep);
		msleep(100);
	}

	if (fep->u.ofdm.transmission_mode == TRANSMISSION_MODE_AUTO ||
		fep->u.ofdm.guard_interval    == GUARD_INTERVAL_AUTO ||
		fep->u.ofdm.constellation     == QAM_AUTO ||
		fep->u.ofdm.code_rate_HP      == FEC_AUTO) {
		int i = 100, found;

		dib3000mc_autosearch_start(fe, &ch);
		do {
			msleep(1);
			found = dib3000mc_autosearch_is_irq(fe);
		} while (found == 0 && i--);

		dprintk("autosearch returns: %d\n",found);
		if (found == 0 || found == 1)
			return 0; // no channel found

		dib3000mc_get_frontend(fe, fep);
		FEP2DIB(fep,&ch);
	}

	/* make this a config parameter */
	dib3000mc_set_output_mode(state, OUTMODE_MPEG2_FIFO);

	return dib3000mc_tune(fe, &ch);
}

static int dib3000mc_read_status(struct dvb_frontend *fe, fe_status_t *stat)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	u16 lock = dib3000mc_read_word(state, 509);

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

static int dib3000mc_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	*ber = (dib3000mc_read_word(state, 500) << 16) | dib3000mc_read_word(state, 501);
	return 0;
}

static int dib3000mc_read_unc_blocks(struct dvb_frontend *fe, u32 *unc)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	*unc = dib3000mc_read_word(state, 508);
	return 0;
}

static int dib3000mc_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	u16 val = dib3000mc_read_word(state, 392);
	*strength = 65535 - val;
	return 0;
}

static int dib3000mc_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	*snr = 0x0000;
	return 0;
}

static int dib3000mc_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void dib3000mc_release(struct dvb_frontend *fe)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	dibx000_exit_i2c_master(&state->i2c_master);
	kfree(state);
}

int dib3000mc_pid_control(struct dvb_frontend *fe, int index, int pid,int onoff)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	dib3000mc_write_word(state, 212 + index,  onoff ? (1 << 13) | pid : 0);
	return 0;
}
EXPORT_SYMBOL(dib3000mc_pid_control);

int dib3000mc_pid_parse(struct dvb_frontend *fe, int onoff)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	u16 tmp = dib3000mc_read_word(state, 206) & ~(1 << 4);
	tmp |= (onoff << 4);
	return dib3000mc_write_word(state, 206, tmp);
}
EXPORT_SYMBOL(dib3000mc_pid_parse);

void dib3000mc_set_config(struct dvb_frontend *fe, struct dib3000mc_config *cfg)
{
	struct dib3000mc_state *state = fe->demodulator_priv;
	state->cfg = cfg;
}
EXPORT_SYMBOL(dib3000mc_set_config);

int dib3000mc_i2c_enumeration(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, struct dib3000mc_config cfg[])
{
	struct dib3000mc_state st = { .i2c_adap = i2c };
	int k;
	u8 new_addr;

	static u8 DIB3000MC_I2C_ADDRESS[] = {20,22,24,26};

	for (k = no_of_demods-1; k >= 0; k--) {
		st.cfg = &cfg[k];

		/* designated i2c address */
		new_addr          = DIB3000MC_I2C_ADDRESS[k];
		st.i2c_addr = new_addr;
		if (dib3000mc_identify(&st) != 0) {
			st.i2c_addr = default_addr;
			if (dib3000mc_identify(&st) != 0) {
				dprintk("-E-  DiB3000P/MC #%d: not identified\n", k);
				return -ENODEV;
			}
		}

		dib3000mc_set_output_mode(&st, OUTMODE_MPEG2_PAR_CONT_CLK);

		// set new i2c address and force divstr (Bit 1) to value 0 (Bit 0)
		dib3000mc_write_word(&st, 1024, (new_addr << 3) | 0x1);
		st.i2c_addr = new_addr;
	}

	for (k = 0; k < no_of_demods; k++) {
		st.cfg = &cfg[k];
		st.i2c_addr = DIB3000MC_I2C_ADDRESS[k];

		dib3000mc_write_word(&st, 1024, st.i2c_addr << 3);

		/* turn off data output */
		dib3000mc_set_output_mode(&st, OUTMODE_HIGH_Z);
	}
	return 0;
}
EXPORT_SYMBOL(dib3000mc_i2c_enumeration);

static struct dvb_frontend_ops dib3000mc_ops;

struct dvb_frontend * dib3000mc_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib3000mc_config *cfg)
{
	struct dvb_frontend *demod;
	struct dib3000mc_state *st;
	st = kzalloc(sizeof(struct dib3000mc_state), GFP_KERNEL);
	if (st == NULL)
		return NULL;

	st->cfg = cfg;
	st->i2c_adap = i2c_adap;
	st->i2c_addr = i2c_addr;

	demod                   = &st->demod;
	demod->demodulator_priv = st;
	memcpy(&st->demod.ops, &dib3000mc_ops, sizeof(struct dvb_frontend_ops));

	if (dib3000mc_identify(st) != 0)
		goto error;

	dibx000_init_i2c_master(&st->i2c_master, DIB3000MC, st->i2c_adap, st->i2c_addr);

	dib3000mc_write_word(st, 1037, 0x3130);

	return demod;

error:
	kfree(st);
	return NULL;
}
EXPORT_SYMBOL(dib3000mc_attach);

static struct dvb_frontend_ops dib3000mc_ops = {
	.info = {
		.name = "DiBcom 3000MC/P",
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

	.release              = dib3000mc_release,

	.init                 = dib3000mc_init,
	.sleep                = dib3000mc_sleep,

	.set_frontend         = dib3000mc_set_frontend,
	.get_tune_settings    = dib3000mc_fe_get_tune_settings,
	.get_frontend         = dib3000mc_get_frontend,

	.read_status          = dib3000mc_read_status,
	.read_ber             = dib3000mc_read_ber,
	.read_signal_strength = dib3000mc_read_signal_strength,
	.read_snr             = dib3000mc_read_snr,
	.read_ucblocks        = dib3000mc_read_unc_blocks,
};

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Driver for the DiBcom 3000MC/P COFDM demodulator");
MODULE_LICENSE("GPL");
