/*
 * Linux-DVB Driver for DiBcom's DiB8000 chip (ISDB-T).
 *
 * Copyright (C) 2009 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include "dvb_math.h"

#include "dvb_frontend.h"

#include "dib8000.h"

#define LAYER_ALL -1
#define LAYER_A   1
#define LAYER_B   2
#define LAYER_C   3

#define FE_CALLBACK_TIME_NEVER 0xffffffff
#define MAX_NUMBER_OF_FRONTENDS 6

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiB8000: "); printk(args); printk("\n"); } } while (0)

#define FE_STATUS_TUNE_FAILED 0

struct i2c_device {
	struct i2c_adapter *adap;
	u8 addr;
};

struct dib8000_state {
	struct dib8000_config cfg;

	struct i2c_device i2c;

	struct dibx000_i2c_master i2c_master;

	u16 wbd_ref;

	u8 current_band;
	u32 current_bandwidth;
	struct dibx000_agc_config *current_agc;
	u32 timf;
	u32 timf_default;

	u8 div_force_off:1;
	u8 div_state:1;
	u16 div_sync_wait;

	u8 agc_state;
	u8 differential_constellation;
	u8 diversity_onoff;

	s16 ber_monitored_layer;
	u16 gpio_dir;
	u16 gpio_val;

	u16 revision;
	u8 isdbt_cfg_loaded;
	enum frontend_tune_state tune_state;
	u32 status;

	struct dvb_frontend *fe[MAX_NUMBER_OF_FRONTENDS];
};

enum dib8000_power_mode {
	DIB8000M_POWER_ALL = 0,
	DIB8000M_POWER_INTERFACE_ONLY,
};

static u16 dib8000_i2c_read16(struct i2c_device *i2c, u16 reg)
{
	u8 wb[2] = { reg >> 8, reg & 0xff };
	u8 rb[2];
	struct i2c_msg msg[2] = {
		{.addr = i2c->addr >> 1,.flags = 0,.buf = wb,.len = 2},
		{.addr = i2c->addr >> 1,.flags = I2C_M_RD,.buf = rb,.len = 2},
	};

	if (i2c_transfer(i2c->adap, msg, 2) != 2)
		dprintk("i2c read error on %d", reg);

	return (rb[0] << 8) | rb[1];
}

static u16 dib8000_read_word(struct dib8000_state *state, u16 reg)
{
	return dib8000_i2c_read16(&state->i2c, reg);
}

static u32 dib8000_read32(struct dib8000_state *state, u16 reg)
{
	u16 rw[2];

	rw[0] = dib8000_read_word(state, reg + 0);
	rw[1] = dib8000_read_word(state, reg + 1);

	return ((rw[0] << 16) | (rw[1]));
}

static int dib8000_i2c_write16(struct i2c_device *i2c, u16 reg, u16 val)
{
	u8 b[4] = {
		(reg >> 8) & 0xff, reg & 0xff,
		(val >> 8) & 0xff, val & 0xff,
	};
	struct i2c_msg msg = {
		.addr = i2c->addr >> 1,.flags = 0,.buf = b,.len = 4
	};
	return i2c_transfer(i2c->adap, &msg, 1) != 1 ? -EREMOTEIO : 0;
}

static int dib8000_write_word(struct dib8000_state *state, u16 reg, u16 val)
{
	return dib8000_i2c_write16(&state->i2c, reg, val);
}

static const s16 coeff_2k_sb_1seg_dqpsk[8] = {
	(769 << 5) | 0x0a, (745 << 5) | 0x03, (595 << 5) | 0x0d, (769 << 5) | 0x0a, (920 << 5) | 0x09, (784 << 5) | 0x02, (519 << 5) | 0x0c,
		(920 << 5) | 0x09
};

static const s16 coeff_2k_sb_1seg[8] = {
	(692 << 5) | 0x0b, (683 << 5) | 0x01, (519 << 5) | 0x09, (692 << 5) | 0x0b, 0 | 0x1f, 0 | 0x1f, 0 | 0x1f, 0 | 0x1f
};

static const s16 coeff_2k_sb_3seg_0dqpsk_1dqpsk[8] = {
	(832 << 5) | 0x10, (912 << 5) | 0x05, (900 << 5) | 0x12, (832 << 5) | 0x10, (-931 << 5) | 0x0f, (912 << 5) | 0x04, (807 << 5) | 0x11,
		(-931 << 5) | 0x0f
};

static const s16 coeff_2k_sb_3seg_0dqpsk[8] = {
	(622 << 5) | 0x0c, (941 << 5) | 0x04, (796 << 5) | 0x10, (622 << 5) | 0x0c, (982 << 5) | 0x0c, (519 << 5) | 0x02, (572 << 5) | 0x0e,
		(982 << 5) | 0x0c
};

static const s16 coeff_2k_sb_3seg_1dqpsk[8] = {
	(699 << 5) | 0x14, (607 << 5) | 0x04, (944 << 5) | 0x13, (699 << 5) | 0x14, (-720 << 5) | 0x0d, (640 << 5) | 0x03, (866 << 5) | 0x12,
		(-720 << 5) | 0x0d
};

static const s16 coeff_2k_sb_3seg[8] = {
	(664 << 5) | 0x0c, (925 << 5) | 0x03, (937 << 5) | 0x10, (664 << 5) | 0x0c, (-610 << 5) | 0x0a, (697 << 5) | 0x01, (836 << 5) | 0x0e,
		(-610 << 5) | 0x0a
};

static const s16 coeff_4k_sb_1seg_dqpsk[8] = {
	(-955 << 5) | 0x0e, (687 << 5) | 0x04, (818 << 5) | 0x10, (-955 << 5) | 0x0e, (-922 << 5) | 0x0d, (750 << 5) | 0x03, (665 << 5) | 0x0f,
		(-922 << 5) | 0x0d
};

static const s16 coeff_4k_sb_1seg[8] = {
	(638 << 5) | 0x0d, (683 << 5) | 0x02, (638 << 5) | 0x0d, (638 << 5) | 0x0d, (-655 << 5) | 0x0a, (517 << 5) | 0x00, (698 << 5) | 0x0d,
		(-655 << 5) | 0x0a
};

static const s16 coeff_4k_sb_3seg_0dqpsk_1dqpsk[8] = {
	(-707 << 5) | 0x14, (910 << 5) | 0x06, (889 << 5) | 0x16, (-707 << 5) | 0x14, (-958 << 5) | 0x13, (993 << 5) | 0x05, (523 << 5) | 0x14,
		(-958 << 5) | 0x13
};

static const s16 coeff_4k_sb_3seg_0dqpsk[8] = {
	(-723 << 5) | 0x13, (910 << 5) | 0x05, (777 << 5) | 0x14, (-723 << 5) | 0x13, (-568 << 5) | 0x0f, (547 << 5) | 0x03, (696 << 5) | 0x12,
		(-568 << 5) | 0x0f
};

static const s16 coeff_4k_sb_3seg_1dqpsk[8] = {
	(-940 << 5) | 0x15, (607 << 5) | 0x05, (915 << 5) | 0x16, (-940 << 5) | 0x15, (-848 << 5) | 0x13, (683 << 5) | 0x04, (543 << 5) | 0x14,
		(-848 << 5) | 0x13
};

static const s16 coeff_4k_sb_3seg[8] = {
	(612 << 5) | 0x12, (910 << 5) | 0x04, (864 << 5) | 0x14, (612 << 5) | 0x12, (-869 << 5) | 0x13, (683 << 5) | 0x02, (869 << 5) | 0x12,
		(-869 << 5) | 0x13
};

static const s16 coeff_8k_sb_1seg_dqpsk[8] = {
	(-835 << 5) | 0x12, (684 << 5) | 0x05, (735 << 5) | 0x14, (-835 << 5) | 0x12, (-598 << 5) | 0x10, (781 << 5) | 0x04, (739 << 5) | 0x13,
		(-598 << 5) | 0x10
};

static const s16 coeff_8k_sb_1seg[8] = {
	(673 << 5) | 0x0f, (683 << 5) | 0x03, (808 << 5) | 0x12, (673 << 5) | 0x0f, (585 << 5) | 0x0f, (512 << 5) | 0x01, (780 << 5) | 0x0f,
		(585 << 5) | 0x0f
};

static const s16 coeff_8k_sb_3seg_0dqpsk_1dqpsk[8] = {
	(863 << 5) | 0x17, (930 << 5) | 0x07, (878 << 5) | 0x19, (863 << 5) | 0x17, (0 << 5) | 0x14, (521 << 5) | 0x05, (980 << 5) | 0x18,
		(0 << 5) | 0x14
};

static const s16 coeff_8k_sb_3seg_0dqpsk[8] = {
	(-924 << 5) | 0x17, (910 << 5) | 0x06, (774 << 5) | 0x17, (-924 << 5) | 0x17, (-877 << 5) | 0x15, (565 << 5) | 0x04, (553 << 5) | 0x15,
		(-877 << 5) | 0x15
};

static const s16 coeff_8k_sb_3seg_1dqpsk[8] = {
	(-921 << 5) | 0x19, (607 << 5) | 0x06, (881 << 5) | 0x19, (-921 << 5) | 0x19, (-921 << 5) | 0x14, (713 << 5) | 0x05, (1018 << 5) | 0x18,
		(-921 << 5) | 0x14
};

static const s16 coeff_8k_sb_3seg[8] = {
	(514 << 5) | 0x14, (910 << 5) | 0x05, (861 << 5) | 0x17, (514 << 5) | 0x14, (690 << 5) | 0x14, (683 << 5) | 0x03, (662 << 5) | 0x15,
		(690 << 5) | 0x14
};

static const s16 ana_fe_coeff_3seg[24] = {
	81, 80, 78, 74, 68, 61, 54, 45, 37, 28, 19, 11, 4, 1022, 1017, 1013, 1010, 1008, 1008, 1008, 1008, 1010, 1014, 1017
};

static const s16 ana_fe_coeff_1seg[24] = {
	249, 226, 164, 82, 5, 981, 970, 988, 1018, 20, 31, 26, 8, 1012, 1000, 1018, 1012, 8, 15, 14, 9, 3, 1017, 1003
};

static const s16 ana_fe_coeff_13seg[24] = {
	396, 305, 105, -51, -77, -12, 41, 31, -11, -30, -11, 14, 15, -2, -13, -7, 5, 8, 1, -6, -7, -3, 0, 1
};

static u16 fft_to_mode(struct dib8000_state *state)
{
	u16 mode;
	switch (state->fe[0]->dtv_property_cache.transmission_mode) {
	case TRANSMISSION_MODE_2K:
		mode = 1;
		break;
	case TRANSMISSION_MODE_4K:
		mode = 2;
		break;
	default:
	case TRANSMISSION_MODE_AUTO:
	case TRANSMISSION_MODE_8K:
		mode = 3;
		break;
	}
	return mode;
}

static void dib8000_set_acquisition_mode(struct dib8000_state *state)
{
	u16 nud = dib8000_read_word(state, 298);
	nud |= (1 << 3) | (1 << 0);
	dprintk("acquisition mode activated");
	dib8000_write_word(state, 298, nud);
}
static int dib8000_set_output_mode(struct dvb_frontend *fe, int mode)
{
	struct dib8000_state *state = fe->demodulator_priv;

	u16 outreg, fifo_threshold, smo_mode, sram = 0x0205;	/* by default SDRAM deintlv is enabled */

	outreg = 0;
	fifo_threshold = 1792;
	smo_mode = (dib8000_read_word(state, 299) & 0x0050) | (1 << 1);

	dprintk("-I-	Setting output mode for demod %p to %d",
			&state->fe[0], mode);

	switch (mode) {
	case OUTMODE_MPEG2_PAR_GATED_CLK:	// STBs with parallel gated clock
		outreg = (1 << 10);	/* 0x0400 */
		break;
	case OUTMODE_MPEG2_PAR_CONT_CLK:	// STBs with parallel continues clock
		outreg = (1 << 10) | (1 << 6);	/* 0x0440 */
		break;
	case OUTMODE_MPEG2_SERIAL:	// STBs with serial input
		outreg = (1 << 10) | (2 << 6) | (0 << 1);	/* 0x0482 */
		break;
	case OUTMODE_DIVERSITY:
		if (state->cfg.hostbus_diversity) {
			outreg = (1 << 10) | (4 << 6);	/* 0x0500 */
			sram &= 0xfdff;
		} else
			sram |= 0x0c00;
		break;
	case OUTMODE_MPEG2_FIFO:	// e.g. USB feeding
		smo_mode |= (3 << 1);
		fifo_threshold = 512;
		outreg = (1 << 10) | (5 << 6);
		break;
	case OUTMODE_HIGH_Z:	// disable
		outreg = 0;
		break;

	case OUTMODE_ANALOG_ADC:
		outreg = (1 << 10) | (3 << 6);
		dib8000_set_acquisition_mode(state);
		break;

	default:
		dprintk("Unhandled output_mode passed to be set for demod %p",
				&state->fe[0]);
		return -EINVAL;
	}

	if (state->cfg.output_mpeg2_in_188_bytes)
		smo_mode |= (1 << 5);

	dib8000_write_word(state, 299, smo_mode);
	dib8000_write_word(state, 300, fifo_threshold);	/* synchronous fread */
	dib8000_write_word(state, 1286, outreg);
	dib8000_write_word(state, 1291, sram);

	return 0;
}

static int dib8000_set_diversity_in(struct dvb_frontend *fe, int onoff)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 sync_wait = dib8000_read_word(state, 273) & 0xfff0;

	if (!state->differential_constellation) {
		dib8000_write_word(state, 272, 1 << 9);	//dvsy_off_lmod4 = 1
		dib8000_write_word(state, 273, sync_wait | (1 << 2) | 2);	// sync_enable = 1; comb_mode = 2
	} else {
		dib8000_write_word(state, 272, 0);	//dvsy_off_lmod4 = 0
		dib8000_write_word(state, 273, sync_wait);	// sync_enable = 0; comb_mode = 0
	}
	state->diversity_onoff = onoff;

	switch (onoff) {
	case 0:		/* only use the internal way - not the diversity input */
		dib8000_write_word(state, 270, 1);
		dib8000_write_word(state, 271, 0);
		break;
	case 1:		/* both ways */
		dib8000_write_word(state, 270, 6);
		dib8000_write_word(state, 271, 6);
		break;
	case 2:		/* only the diversity input */
		dib8000_write_word(state, 270, 0);
		dib8000_write_word(state, 271, 1);
		break;
	}
	return 0;
}

static void dib8000_set_power_mode(struct dib8000_state *state, enum dib8000_power_mode mode)
{
	/* by default everything is going to be powered off */
	u16 reg_774 = 0x3fff, reg_775 = 0xffff, reg_776 = 0xffff,
		reg_900 = (dib8000_read_word(state, 900) & 0xfffc) | 0x3,
		reg_1280 = (dib8000_read_word(state, 1280) & 0x00ff) | 0xff00;

	/* now, depending on the requested mode, we power on */
	switch (mode) {
		/* power up everything in the demod */
	case DIB8000M_POWER_ALL:
		reg_774 = 0x0000;
		reg_775 = 0x0000;
		reg_776 = 0x0000;
		reg_900 &= 0xfffc;
		reg_1280 &= 0x00ff;
		break;
	case DIB8000M_POWER_INTERFACE_ONLY:
		reg_1280 &= 0x00ff;
		break;
	}

	dprintk("powermode : 774 : %x ; 775 : %x; 776 : %x ; 900 : %x; 1280 : %x", reg_774, reg_775, reg_776, reg_900, reg_1280);
	dib8000_write_word(state, 774, reg_774);
	dib8000_write_word(state, 775, reg_775);
	dib8000_write_word(state, 776, reg_776);
	dib8000_write_word(state, 900, reg_900);
	dib8000_write_word(state, 1280, reg_1280);
}

static int dib8000_set_adc_state(struct dib8000_state *state, enum dibx000_adc_states no)
{
	int ret = 0;
	u16 reg_907 = dib8000_read_word(state, 907), reg_908 = dib8000_read_word(state, 908);

	switch (no) {
	case DIBX000_SLOW_ADC_ON:
		reg_908 |= (1 << 1) | (1 << 0);
		ret |= dib8000_write_word(state, 908, reg_908);
		reg_908 &= ~(1 << 1);
		break;

	case DIBX000_SLOW_ADC_OFF:
		reg_908 |= (1 << 1) | (1 << 0);
		break;

	case DIBX000_ADC_ON:
		reg_907 &= 0x0fff;
		reg_908 &= 0x0003;
		break;

	case DIBX000_ADC_OFF:	// leave the VBG voltage on
		reg_907 |= (1 << 14) | (1 << 13) | (1 << 12);
		reg_908 |= (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2);
		break;

	case DIBX000_VBG_ENABLE:
		reg_907 &= ~(1 << 15);
		break;

	case DIBX000_VBG_DISABLE:
		reg_907 |= (1 << 15);
		break;

	default:
		break;
	}

	ret |= dib8000_write_word(state, 907, reg_907);
	ret |= dib8000_write_word(state, 908, reg_908);

	return ret;
}

static int dib8000_set_bandwidth(struct dvb_frontend *fe, u32 bw)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u32 timf;

	if (bw == 0)
		bw = 6000;

	if (state->timf == 0) {
		dprintk("using default timf");
		timf = state->timf_default;
	} else {
		dprintk("using updated timf");
		timf = state->timf;
	}

	dib8000_write_word(state, 29, (u16) ((timf >> 16) & 0xffff));
	dib8000_write_word(state, 30, (u16) ((timf) & 0xffff));

	return 0;
}

static int dib8000_sad_calib(struct dib8000_state *state)
{
/* internal */
	dib8000_write_word(state, 923, (0 << 1) | (0 << 0));
	dib8000_write_word(state, 924, 776);	// 0.625*3.3 / 4096

	/* do the calibration */
	dib8000_write_word(state, 923, (1 << 0));
	dib8000_write_word(state, 923, (0 << 0));

	msleep(1);
	return 0;
}

int dib8000_set_wbd_ref(struct dvb_frontend *fe, u16 value)
{
	struct dib8000_state *state = fe->demodulator_priv;
	if (value > 4095)
		value = 4095;
	state->wbd_ref = value;
	return dib8000_write_word(state, 106, value);
}

EXPORT_SYMBOL(dib8000_set_wbd_ref);
static void dib8000_reset_pll_common(struct dib8000_state *state, const struct dibx000_bandwidth_config *bw)
{
	dprintk("ifreq: %d %x, inversion: %d", bw->ifreq, bw->ifreq, bw->ifreq >> 25);
	dib8000_write_word(state, 23, (u16) (((bw->internal * 1000) >> 16) & 0xffff));	/* P_sec_len */
	dib8000_write_word(state, 24, (u16) ((bw->internal * 1000) & 0xffff));
	dib8000_write_word(state, 27, (u16) ((bw->ifreq >> 16) & 0x01ff));
	dib8000_write_word(state, 28, (u16) (bw->ifreq & 0xffff));
	dib8000_write_word(state, 26, (u16) ((bw->ifreq >> 25) & 0x0003));

	dib8000_write_word(state, 922, bw->sad_cfg);
}

static void dib8000_reset_pll(struct dib8000_state *state)
{
	const struct dibx000_bandwidth_config *pll = state->cfg.pll;
	u16 clk_cfg1;

	// clk_cfg0
	dib8000_write_word(state, 901, (pll->pll_prediv << 8) | (pll->pll_ratio << 0));

	// clk_cfg1
	clk_cfg1 = (1 << 10) | (0 << 9) | (pll->IO_CLK_en_core << 8) |
		(pll->bypclk_div << 5) | (pll->enable_refdiv << 4) | (1 << 3) |
		(pll->pll_range << 1) | (pll->pll_reset << 0);

	dib8000_write_word(state, 902, clk_cfg1);
	clk_cfg1 = (clk_cfg1 & 0xfff7) | (pll->pll_bypass << 3);
	dib8000_write_word(state, 902, clk_cfg1);

	dprintk("clk_cfg1: 0x%04x", clk_cfg1);	/* 0x507 1 0 1 000 0 0 11 1 */

	/* smpl_cfg: P_refclksel=2, P_ensmplsel=1 nodivsmpl=1 */
	if (state->cfg.pll->ADClkSrc == 0)
		dib8000_write_word(state, 904, (0 << 15) | (0 << 12) | (0 << 10) |
				(pll->modulo << 8) | (pll->ADClkSrc << 7) | (0 << 1));
	else if (state->cfg.refclksel != 0)
		dib8000_write_word(state, 904, (0 << 15) | (1 << 12) |
				((state->cfg.refclksel & 0x3) << 10) | (pll->modulo << 8) |
				(pll->ADClkSrc << 7) | (0 << 1));
	else
		dib8000_write_word(state, 904, (0 << 15) | (1 << 12) | (3 << 10) | (pll->modulo << 8) | (pll->ADClkSrc << 7) | (0 << 1));

	dib8000_reset_pll_common(state, pll);
}

static int dib8000_reset_gpio(struct dib8000_state *st)
{
	/* reset the GPIOs */
	dib8000_write_word(st, 1029, st->cfg.gpio_dir);
	dib8000_write_word(st, 1030, st->cfg.gpio_val);

	/* TODO 782 is P_gpio_od */

	dib8000_write_word(st, 1032, st->cfg.gpio_pwm_pos);

	dib8000_write_word(st, 1037, st->cfg.pwm_freq_div);
	return 0;
}

static int dib8000_cfg_gpio(struct dib8000_state *st, u8 num, u8 dir, u8 val)
{
	st->cfg.gpio_dir = dib8000_read_word(st, 1029);
	st->cfg.gpio_dir &= ~(1 << num);	/* reset the direction bit */
	st->cfg.gpio_dir |= (dir & 0x1) << num;	/* set the new direction */
	dib8000_write_word(st, 1029, st->cfg.gpio_dir);

	st->cfg.gpio_val = dib8000_read_word(st, 1030);
	st->cfg.gpio_val &= ~(1 << num);	/* reset the direction bit */
	st->cfg.gpio_val |= (val & 0x01) << num;	/* set the new value */
	dib8000_write_word(st, 1030, st->cfg.gpio_val);

	dprintk("gpio dir: %x: gpio val: %x", st->cfg.gpio_dir, st->cfg.gpio_val);

	return 0;
}

int dib8000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	struct dib8000_state *state = fe->demodulator_priv;
	return dib8000_cfg_gpio(state, num, dir, val);
}

EXPORT_SYMBOL(dib8000_set_gpio);
static const u16 dib8000_defaults[] = {
	/* auto search configuration - lock0 by default waiting
	 * for cpil_lock; lock1 cpil_lock; lock2 tmcc_sync_lock */
	3, 7,
	0x0004,
	0x0400,
	0x0814,

	12, 11,
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

	/*1, 32,
		0x6680 // P_corm_thres Lock algorithms configuration */

	11, 80,			/* set ADC level to -16 */
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

	4, 108,
	0,
	0,
	0,
	0,

	1, 175,
	0x0410,
	1, 179,
	8192,			// P_fft_nb_to_cut

	6, 181,
	0x2800,			// P_coff_corthres_ ( 2k 4k 8k ) 0x2800
	0x2800,
	0x2800,
	0x2800,			// P_coff_cpilthres_ ( 2k 4k 8k ) 0x2800
	0x2800,
	0x2800,

	2, 193,
	0x0666,			// P_pha3_thres
	0x0000,			// P_cti_use_cpe, P_cti_use_prog

	2, 205,
	0x200f,			// P_cspu_regul, P_cspu_win_cut
	0x000f,			// P_des_shift_work

	5, 215,
	0x023d,			// P_adp_regul_cnt
	0x00a4,			// P_adp_noise_cnt
	0x00a4,			// P_adp_regul_ext
	0x7ff0,			// P_adp_noise_ext
	0x3ccc,			// P_adp_fil

	1, 230,
	0x0000,			// P_2d_byp_ti_num

	1, 263,
	0x800,			//P_equal_thres_wgn

	1, 268,
	(2 << 9) | 39,		// P_equal_ctrl_synchro, P_equal_speedmode

	1, 270,
	0x0001,			// P_div_lock0_wait
	1, 285,
	0x0020,			//p_fec_
	1, 299,
	0x0062,			/* P_smo_mode, P_smo_rs_discard, P_smo_fifo_flush, P_smo_pid_parse, P_smo_error_discard */

	1, 338,
	(1 << 12) |		// P_ctrl_corm_thres4pre_freq_inh=1
		(1 << 10) |
		(0 << 9) |		/* P_ctrl_pre_freq_inh=0 */
		(3 << 5) |		/* P_ctrl_pre_freq_step=3 */
		(1 << 0),		/* P_pre_freq_win_len=1 */

	1, 903,
	(0 << 4) | 2,		// P_divclksel=0 P_divbitsel=2 (was clk=3,bit=1 for MPW)

	0,
};

static u16 dib8000_identify(struct i2c_device *client)
{
	u16 value;

	//because of glitches sometimes
	value = dib8000_i2c_read16(client, 896);

	if ((value = dib8000_i2c_read16(client, 896)) != 0x01b3) {
		dprintk("wrong Vendor ID (read=0x%x)", value);
		return 0;
	}

	value = dib8000_i2c_read16(client, 897);
	if (value != 0x8000 && value != 0x8001 && value != 0x8002) {
		dprintk("wrong Device ID (%x)", value);
		return 0;
	}

	switch (value) {
	case 0x8000:
		dprintk("found DiB8000A");
		break;
	case 0x8001:
		dprintk("found DiB8000B");
		break;
	case 0x8002:
		dprintk("found DiB8000C");
		break;
	}
	return value;
}

static int dib8000_reset(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;

	dib8000_write_word(state, 1287, 0x0003);	/* sram lead in, rdy */

	if ((state->revision = dib8000_identify(&state->i2c)) == 0)
		return -EINVAL;

	if (state->revision == 0x8000)
		dprintk("error : dib8000 MA not supported");

	dibx000_reset_i2c_master(&state->i2c_master);

	dib8000_set_power_mode(state, DIB8000M_POWER_ALL);

	/* always leave the VBG voltage on - it consumes almost nothing but takes a long time to start */
	dib8000_set_adc_state(state, DIBX000_VBG_ENABLE);

	/* restart all parts */
	dib8000_write_word(state, 770, 0xffff);
	dib8000_write_word(state, 771, 0xffff);
	dib8000_write_word(state, 772, 0xfffc);
	dib8000_write_word(state, 898, 0x000c);	// sad
	dib8000_write_word(state, 1280, 0x004d);
	dib8000_write_word(state, 1281, 0x000c);

	dib8000_write_word(state, 770, 0x0000);
	dib8000_write_word(state, 771, 0x0000);
	dib8000_write_word(state, 772, 0x0000);
	dib8000_write_word(state, 898, 0x0004);	// sad
	dib8000_write_word(state, 1280, 0x0000);
	dib8000_write_word(state, 1281, 0x0000);

	/* drives */
	if (state->cfg.drives)
		dib8000_write_word(state, 906, state->cfg.drives);
	else {
		dprintk("using standard PAD-drive-settings, please adjust settings in config-struct to be optimal.");
		dib8000_write_word(state, 906, 0x2d98);	// min drive SDRAM - not optimal - adjust
	}

	dib8000_reset_pll(state);

	if (dib8000_reset_gpio(state) != 0)
		dprintk("GPIO reset was not successful.");

	if (dib8000_set_output_mode(fe, OUTMODE_HIGH_Z) != 0)
		dprintk("OUTPUT_MODE could not be resetted.");

	state->current_agc = NULL;

	// P_iqc_alpha_pha, P_iqc_alpha_amp, P_iqc_dcc_alpha, ...
	/* P_iqc_ca2 = 0; P_iqc_impnc_on = 0; P_iqc_mode = 0; */
	if (state->cfg.pll->ifreq == 0)
		dib8000_write_word(state, 40, 0x0755);	/* P_iqc_corr_inh = 0 enable IQcorr block */
	else
		dib8000_write_word(state, 40, 0x1f55);	/* P_iqc_corr_inh = 1 disable IQcorr block */

	{
		u16 l = 0, r;
		const u16 *n;
		n = dib8000_defaults;
		l = *n++;
		while (l) {
			r = *n++;
			do {
				dib8000_write_word(state, r, *n++);
				r++;
			} while (--l);
			l = *n++;
		}
	}
	state->isdbt_cfg_loaded = 0;

	//div_cfg override for special configs
	if (state->cfg.div_cfg != 0)
		dib8000_write_word(state, 903, state->cfg.div_cfg);

	/* unforce divstr regardless whether i2c enumeration was done or not */
	dib8000_write_word(state, 1285, dib8000_read_word(state, 1285) & ~(1 << 1));

	dib8000_set_bandwidth(fe, 6000);

	dib8000_set_adc_state(state, DIBX000_SLOW_ADC_ON);
	dib8000_sad_calib(state);
	dib8000_set_adc_state(state, DIBX000_SLOW_ADC_OFF);

	dib8000_set_power_mode(state, DIB8000M_POWER_INTERFACE_ONLY);

	return 0;
}

static void dib8000_restart_agc(struct dib8000_state *state)
{
	// P_restart_iqc & P_restart_agc
	dib8000_write_word(state, 770, 0x0a00);
	dib8000_write_word(state, 770, 0x0000);
}

static int dib8000_update_lna(struct dib8000_state *state)
{
	u16 dyn_gain;

	if (state->cfg.update_lna) {
		// read dyn_gain here (because it is demod-dependent and not tuner)
		dyn_gain = dib8000_read_word(state, 390);

		if (state->cfg.update_lna(state->fe[0], dyn_gain)) {
			dib8000_restart_agc(state);
			return 1;
		}
	}
	return 0;
}

static int dib8000_set_agc_config(struct dib8000_state *state, u8 band)
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
		dprintk("no valid AGC configuration found for band 0x%02x", band);
		return -EINVAL;
	}

	state->current_agc = agc;

	/* AGC */
	dib8000_write_word(state, 76, agc->setup);
	dib8000_write_word(state, 77, agc->inv_gain);
	dib8000_write_word(state, 78, agc->time_stabiliz);
	dib8000_write_word(state, 101, (agc->alpha_level << 12) | agc->thlock);

	// Demod AGC loop configuration
	dib8000_write_word(state, 102, (agc->alpha_mant << 5) | agc->alpha_exp);
	dib8000_write_word(state, 103, (agc->beta_mant << 6) | agc->beta_exp);

	dprintk("WBD: ref: %d, sel: %d, active: %d, alpha: %d",
		state->wbd_ref != 0 ? state->wbd_ref : agc->wbd_ref, agc->wbd_sel, !agc->perform_agc_softsplit, agc->wbd_sel);

	/* AGC continued */
	if (state->wbd_ref != 0)
		dib8000_write_word(state, 106, state->wbd_ref);
	else			// use default
		dib8000_write_word(state, 106, agc->wbd_ref);
	dib8000_write_word(state, 107, (agc->wbd_alpha << 9) | (agc->perform_agc_softsplit << 8));
	dib8000_write_word(state, 108, agc->agc1_max);
	dib8000_write_word(state, 109, agc->agc1_min);
	dib8000_write_word(state, 110, agc->agc2_max);
	dib8000_write_word(state, 111, agc->agc2_min);
	dib8000_write_word(state, 112, (agc->agc1_pt1 << 8) | agc->agc1_pt2);
	dib8000_write_word(state, 113, (agc->agc1_slope1 << 8) | agc->agc1_slope2);
	dib8000_write_word(state, 114, (agc->agc2_pt1 << 8) | agc->agc2_pt2);
	dib8000_write_word(state, 115, (agc->agc2_slope1 << 8) | agc->agc2_slope2);

	dib8000_write_word(state, 75, agc->agc1_pt3);
	dib8000_write_word(state, 923, (dib8000_read_word(state, 923) & 0xffe3) | (agc->wbd_inv << 4) | (agc->wbd_sel << 2));	/*LB : 929 -> 923 */

	return 0;
}

void dib8000_pwm_agc_reset(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	dib8000_set_adc_state(state, DIBX000_ADC_ON);
	dib8000_set_agc_config(state, (unsigned char)(BAND_OF_FREQUENCY(fe->dtv_property_cache.frequency / 1000)));
}
EXPORT_SYMBOL(dib8000_pwm_agc_reset);

static int dib8000_agc_soft_split(struct dib8000_state *state)
{
	u16 agc, split_offset;

	if (!state->current_agc || !state->current_agc->perform_agc_softsplit || state->current_agc->split.max == 0)
		return FE_CALLBACK_TIME_NEVER;

	// n_agc_global
	agc = dib8000_read_word(state, 390);

	if (agc > state->current_agc->split.min_thres)
		split_offset = state->current_agc->split.min;
	else if (agc < state->current_agc->split.max_thres)
		split_offset = state->current_agc->split.max;
	else
		split_offset = state->current_agc->split.max *
			(agc - state->current_agc->split.min_thres) /
			(state->current_agc->split.max_thres - state->current_agc->split.min_thres);

	dprintk("AGC split_offset: %d", split_offset);

	// P_agc_force_split and P_agc_split_offset
	dib8000_write_word(state, 107, (dib8000_read_word(state, 107) & 0xff00) | split_offset);
	return 5000;
}

static int dib8000_agc_startup(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	enum frontend_tune_state *tune_state = &state->tune_state;

	int ret = 0;

	switch (*tune_state) {
	case CT_AGC_START:
		// set power-up level: interf+analog+AGC

		dib8000_set_adc_state(state, DIBX000_ADC_ON);

		if (dib8000_set_agc_config(state, (unsigned char)(BAND_OF_FREQUENCY(fe->dtv_property_cache.frequency / 1000))) != 0) {
			*tune_state = CT_AGC_STOP;
			state->status = FE_STATUS_TUNE_FAILED;
			break;
		}

		ret = 70;
		*tune_state = CT_AGC_STEP_0;
		break;

	case CT_AGC_STEP_0:
		//AGC initialization
		if (state->cfg.agc_control)
			state->cfg.agc_control(fe, 1);

		dib8000_restart_agc(state);

		// wait AGC rough lock time
		ret = 50;
		*tune_state = CT_AGC_STEP_1;
		break;

	case CT_AGC_STEP_1:
		// wait AGC accurate lock time
		ret = 70;

		if (dib8000_update_lna(state))
			// wait only AGC rough lock time
			ret = 50;
		else
			*tune_state = CT_AGC_STEP_2;
		break;

	case CT_AGC_STEP_2:
		dib8000_agc_soft_split(state);

		if (state->cfg.agc_control)
			state->cfg.agc_control(fe, 0);

		*tune_state = CT_AGC_STOP;
		break;
	default:
		ret = dib8000_agc_soft_split(state);
		break;
	}
	return ret;

}

static const s32 lut_1000ln_mant[] =
{
	908, 7003, 7090, 7170, 7244, 7313, 7377, 7438, 7495, 7549, 7600
};

s32 dib8000_get_adc_power(struct dvb_frontend *fe, u8 mode)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u32 ix = 0, tmp_val = 0, exp = 0, mant = 0;
	s32 val;

	val = dib8000_read32(state, 384);
	if (mode) {
		tmp_val = val;
		while (tmp_val >>= 1)
			exp++;
		mant = (val * 1000 / (1<<exp));
		ix = (u8)((mant-1000)/100); /* index of the LUT */
		val = (lut_1000ln_mant[ix] + 693*(exp-20) - 6908);
		val = (val*256)/1000;
	}
	return val;
}
EXPORT_SYMBOL(dib8000_get_adc_power);

static void dib8000_update_timf(struct dib8000_state *state)
{
	u32 timf = state->timf = dib8000_read32(state, 435);

	dib8000_write_word(state, 29, (u16) (timf >> 16));
	dib8000_write_word(state, 30, (u16) (timf & 0xffff));
	dprintk("Updated timing frequency: %d (default: %d)", state->timf, state->timf_default);
}

static void dib8000_set_channel(struct dib8000_state *state, u8 seq, u8 autosearching)
{
	u16 mode, max_constellation, seg_diff_mask = 0, nbseg_diff = 0;
	u8 guard, crate, constellation, timeI;
	u8 permu_seg[] = { 6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 11, 0, 12 };
	u16 i, coeff[4], P_cfr_left_edge = 0, P_cfr_right_edge = 0, seg_mask13 = 0x1fff;	// All 13 segments enabled
	const s16 *ncoeff = NULL, *ana_fe;
	u16 tmcc_pow = 0;
	u16 coff_pow = 0x2800;
	u16 init_prbs = 0xfff;
	u16 ana_gain = 0;
	u16 adc_target_16dB[11] = {
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
		(1 << 13) - 501 - 117
	};

	if (state->ber_monitored_layer != LAYER_ALL)
		dib8000_write_word(state, 285, (dib8000_read_word(state, 285) & 0x60) | state->ber_monitored_layer);
	else
		dib8000_write_word(state, 285, dib8000_read_word(state, 285) & 0x60);

	i = dib8000_read_word(state, 26) & 1;	// P_dds_invspec
	dib8000_write_word(state, 26, state->fe[0]->dtv_property_cache.inversion^i);

	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode) {
		//compute new dds_freq for the seg and adjust prbs
		int seg_offset =
			state->fe[0]->dtv_property_cache.isdbt_sb_segment_idx -
			(state->fe[0]->dtv_property_cache.isdbt_sb_segment_count / 2) -
			(state->fe[0]->dtv_property_cache.isdbt_sb_segment_count % 2);
		int clk = state->cfg.pll->internal;
		u32 segtodds = ((u32) (430 << 23) / clk) << 3;	// segtodds = SegBW / Fclk * pow(2,26)
		int dds_offset = seg_offset * segtodds;
		int new_dds, sub_channel;
		if ((state->fe[0]->dtv_property_cache.isdbt_sb_segment_count % 2) == 0)
			dds_offset -= (int)(segtodds / 2);

		if (state->cfg.pll->ifreq == 0) {
			if ((state->fe[0]->dtv_property_cache.inversion ^ i) == 0) {
				dib8000_write_word(state, 26, dib8000_read_word(state, 26) | 1);
				new_dds = dds_offset;
			} else
				new_dds = dds_offset;

			// We shift tuning frequency if the wanted segment is :
			//  - the segment of center frequency with an odd total number of segments
			//  - the segment to the left of center frequency with an even total number of segments
			//  - the segment to the right of center frequency with an even total number of segments
			if ((state->fe[0]->dtv_property_cache.delivery_system == SYS_ISDBT)
				&& (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1)
					&& (((state->fe[0]->dtv_property_cache.isdbt_sb_segment_count % 2)
					  && (state->fe[0]->dtv_property_cache.isdbt_sb_segment_idx ==
				  ((state->fe[0]->dtv_property_cache.isdbt_sb_segment_count / 2) + 1)))
					 || (((state->fe[0]->dtv_property_cache.isdbt_sb_segment_count % 2) == 0)
						 && (state->fe[0]->dtv_property_cache.isdbt_sb_segment_idx == (state->fe[0]->dtv_property_cache.isdbt_sb_segment_count / 2)))
					 || (((state->fe[0]->dtv_property_cache.isdbt_sb_segment_count % 2) == 0)
						 && (state->fe[0]->dtv_property_cache.isdbt_sb_segment_idx ==
							 ((state->fe[0]->dtv_property_cache.isdbt_sb_segment_count / 2) + 1)))
					)) {
				new_dds -= ((u32) (850 << 22) / clk) << 4;	// new_dds = 850 (freq shift in KHz) / Fclk * pow(2,26)
			}
		} else {
			if ((state->fe[0]->dtv_property_cache.inversion ^ i) == 0)
				new_dds = state->cfg.pll->ifreq - dds_offset;
			else
				new_dds = state->cfg.pll->ifreq + dds_offset;
		}
		dib8000_write_word(state, 27, (u16) ((new_dds >> 16) & 0x01ff));
		dib8000_write_word(state, 28, (u16) (new_dds & 0xffff));
		if (state->fe[0]->dtv_property_cache.isdbt_sb_segment_count % 2)
			sub_channel = ((state->fe[0]->dtv_property_cache.isdbt_sb_subchannel + (3 * seg_offset) + 1) % 41) / 3;
		else
			sub_channel = ((state->fe[0]->dtv_property_cache.isdbt_sb_subchannel + (3 * seg_offset)) % 41) / 3;
		sub_channel -= 6;

		if (state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_2K
				|| state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_4K) {
			dib8000_write_word(state, 219, dib8000_read_word(state, 219) | 0x1);	//adp_pass =1
			dib8000_write_word(state, 190, dib8000_read_word(state, 190) | (0x1 << 14));	//pha3_force_pha_shift = 1
		} else {
			dib8000_write_word(state, 219, dib8000_read_word(state, 219) & 0xfffe);	//adp_pass =0
			dib8000_write_word(state, 190, dib8000_read_word(state, 190) & 0xbfff);	//pha3_force_pha_shift = 0
		}

		switch (state->fe[0]->dtv_property_cache.transmission_mode) {
		case TRANSMISSION_MODE_2K:
			switch (sub_channel) {
			case -6:
				init_prbs = 0x0;
				break;	// 41, 0, 1
			case -5:
				init_prbs = 0x423;
				break;	// 02~04
			case -4:
				init_prbs = 0x9;
				break;	// 05~07
			case -3:
				init_prbs = 0x5C7;
				break;	// 08~10
			case -2:
				init_prbs = 0x7A6;
				break;	// 11~13
			case -1:
				init_prbs = 0x3D8;
				break;	// 14~16
			case 0:
				init_prbs = 0x527;
				break;	// 17~19
			case 1:
				init_prbs = 0x7FF;
				break;	// 20~22
			case 2:
				init_prbs = 0x79B;
				break;	// 23~25
			case 3:
				init_prbs = 0x3D6;
				break;	// 26~28
			case 4:
				init_prbs = 0x3A2;
				break;	// 29~31
			case 5:
				init_prbs = 0x53B;
				break;	// 32~34
			case 6:
				init_prbs = 0x2F4;
				break;	// 35~37
			default:
			case 7:
				init_prbs = 0x213;
				break;	// 38~40
			}
			break;

		case TRANSMISSION_MODE_4K:
			switch (sub_channel) {
			case -6:
				init_prbs = 0x0;
				break;	// 41, 0, 1
			case -5:
				init_prbs = 0x208;
				break;	// 02~04
			case -4:
				init_prbs = 0xC3;
				break;	// 05~07
			case -3:
				init_prbs = 0x7B9;
				break;	// 08~10
			case -2:
				init_prbs = 0x423;
				break;	// 11~13
			case -1:
				init_prbs = 0x5C7;
				break;	// 14~16
			case 0:
				init_prbs = 0x3D8;
				break;	// 17~19
			case 1:
				init_prbs = 0x7FF;
				break;	// 20~22
			case 2:
				init_prbs = 0x3D6;
				break;	// 23~25
			case 3:
				init_prbs = 0x53B;
				break;	// 26~28
			case 4:
				init_prbs = 0x213;
				break;	// 29~31
			case 5:
				init_prbs = 0x29;
				break;	// 32~34
			case 6:
				init_prbs = 0xD0;
				break;	// 35~37
			default:
			case 7:
				init_prbs = 0x48E;
				break;	// 38~40
			}
			break;

		default:
		case TRANSMISSION_MODE_8K:
			switch (sub_channel) {
			case -6:
				init_prbs = 0x0;
				break;	// 41, 0, 1
			case -5:
				init_prbs = 0x740;
				break;	// 02~04
			case -4:
				init_prbs = 0x069;
				break;	// 05~07
			case -3:
				init_prbs = 0x7DD;
				break;	// 08~10
			case -2:
				init_prbs = 0x208;
				break;	// 11~13
			case -1:
				init_prbs = 0x7B9;
				break;	// 14~16
			case 0:
				init_prbs = 0x5C7;
				break;	// 17~19
			case 1:
				init_prbs = 0x7FF;
				break;	// 20~22
			case 2:
				init_prbs = 0x53B;
				break;	// 23~25
			case 3:
				init_prbs = 0x29;
				break;	// 26~28
			case 4:
				init_prbs = 0x48E;
				break;	// 29~31
			case 5:
				init_prbs = 0x4C4;
				break;	// 32~34
			case 6:
				init_prbs = 0x367;
				break;	// 33~37
			default:
			case 7:
				init_prbs = 0x684;
				break;	// 38~40
			}
			break;
		}
	} else {
		dib8000_write_word(state, 27, (u16) ((state->cfg.pll->ifreq >> 16) & 0x01ff));
		dib8000_write_word(state, 28, (u16) (state->cfg.pll->ifreq & 0xffff));
		dib8000_write_word(state, 26, (u16) ((state->cfg.pll->ifreq >> 25) & 0x0003));
	}
	/*P_mode == ?? */
	dib8000_write_word(state, 10, (seq << 4));
	//  dib8000_write_word(state, 287, (dib8000_read_word(state, 287) & 0xe000) | 0x1000);

	switch (state->fe[0]->dtv_property_cache.guard_interval) {
	case GUARD_INTERVAL_1_32:
		guard = 0;
		break;
	case GUARD_INTERVAL_1_16:
		guard = 1;
		break;
	case GUARD_INTERVAL_1_8:
		guard = 2;
		break;
	case GUARD_INTERVAL_1_4:
	default:
		guard = 3;
		break;
	}

	dib8000_write_word(state, 1, (init_prbs << 2) | (guard & 0x3));	// ADDR 1

	max_constellation = DQPSK;
	for (i = 0; i < 3; i++) {
		switch (state->fe[0]->dtv_property_cache.layer[i].modulation) {
		case DQPSK:
			constellation = 0;
			break;
		case QPSK:
			constellation = 1;
			break;
		case QAM_16:
			constellation = 2;
			break;
		case QAM_64:
		default:
			constellation = 3;
			break;
		}

		switch (state->fe[0]->dtv_property_cache.layer[i].fec) {
		case FEC_1_2:
			crate = 1;
			break;
		case FEC_2_3:
			crate = 2;
			break;
		case FEC_3_4:
			crate = 3;
			break;
		case FEC_5_6:
			crate = 5;
			break;
		case FEC_7_8:
		default:
			crate = 7;
			break;
		}

		if ((state->fe[0]->dtv_property_cache.layer[i].interleaving > 0) &&
				((state->fe[0]->dtv_property_cache.layer[i].interleaving <= 3) ||
				 (state->fe[0]->dtv_property_cache.layer[i].interleaving == 4 && state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1))
			)
			timeI = state->fe[0]->dtv_property_cache.layer[i].interleaving;
		else
			timeI = 0;
		dib8000_write_word(state, 2 + i, (constellation << 10) | ((state->fe[0]->dtv_property_cache.layer[i].segment_count & 0xf) << 6) |
					(crate << 3) | timeI);
		if (state->fe[0]->dtv_property_cache.layer[i].segment_count > 0) {
			switch (max_constellation) {
			case DQPSK:
			case QPSK:
				if (state->fe[0]->dtv_property_cache.layer[i].modulation == QAM_16 ||
					state->fe[0]->dtv_property_cache.layer[i].modulation == QAM_64)
					max_constellation = state->fe[0]->dtv_property_cache.layer[i].modulation;
				break;
			case QAM_16:
				if (state->fe[0]->dtv_property_cache.layer[i].modulation == QAM_64)
					max_constellation = state->fe[0]->dtv_property_cache.layer[i].modulation;
				break;
			}
		}
	}

	mode = fft_to_mode(state);

	//dib8000_write_word(state, 5, 13); /*p_last_seg = 13*/

	dib8000_write_word(state, 274, (dib8000_read_word(state, 274) & 0xffcf) |
				((state->fe[0]->dtv_property_cache.isdbt_partial_reception & 1) << 5) | ((state->fe[0]->dtv_property_cache.
												 isdbt_sb_mode & 1) << 4));

	dprintk("mode = %d ; guard = %d", mode, state->fe[0]->dtv_property_cache.guard_interval);

	/* signal optimization parameter */

	if (state->fe[0]->dtv_property_cache.isdbt_partial_reception) {
		seg_diff_mask = (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK) << permu_seg[0];
		for (i = 1; i < 3; i++)
			nbseg_diff +=
				(state->fe[0]->dtv_property_cache.layer[i].modulation == DQPSK) * state->fe[0]->dtv_property_cache.layer[i].segment_count;
		for (i = 0; i < nbseg_diff; i++)
			seg_diff_mask |= 1 << permu_seg[i + 1];
	} else {
		for (i = 0; i < 3; i++)
			nbseg_diff +=
				(state->fe[0]->dtv_property_cache.layer[i].modulation == DQPSK) * state->fe[0]->dtv_property_cache.layer[i].segment_count;
		for (i = 0; i < nbseg_diff; i++)
			seg_diff_mask |= 1 << permu_seg[i];
	}
	dprintk("nbseg_diff = %X (%d)", seg_diff_mask, seg_diff_mask);

	state->differential_constellation = (seg_diff_mask != 0);
	dib8000_set_diversity_in(state->fe[0], state->diversity_onoff);

	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 1)
			seg_mask13 = 0x00E0;
		else		// 1-segment
			seg_mask13 = 0x0040;
	} else
		seg_mask13 = 0x1fff;

	// WRITE: Mode & Diff mask
	dib8000_write_word(state, 0, (mode << 13) | seg_diff_mask);

	if ((seg_diff_mask) || (state->fe[0]->dtv_property_cache.isdbt_sb_mode))
		dib8000_write_word(state, 268, (dib8000_read_word(state, 268) & 0xF9FF) | 0x0200);
	else
		dib8000_write_word(state, 268, (2 << 9) | 39);	//init value

	// ---- SMALL ----
	// P_small_seg_diff
	dib8000_write_word(state, 352, seg_diff_mask);	// ADDR 352

	dib8000_write_word(state, 353, seg_mask13);	// ADDR 353

/*	// P_small_narrow_band=0, P_small_last_seg=13, P_small_offset_num_car=5 */

	// ---- SMALL ----
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
		switch (state->fe[0]->dtv_property_cache.transmission_mode) {
		case TRANSMISSION_MODE_2K:
			if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0) {
				if (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK)
					ncoeff = coeff_2k_sb_1seg_dqpsk;
				else	// QPSK or QAM
					ncoeff = coeff_2k_sb_1seg;
			} else {	// 3-segments
				if (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK) {
					if (state->fe[0]->dtv_property_cache.layer[1].modulation == DQPSK)
						ncoeff = coeff_2k_sb_3seg_0dqpsk_1dqpsk;
					else	// QPSK or QAM on external segments
						ncoeff = coeff_2k_sb_3seg_0dqpsk;
				} else {	// QPSK or QAM on central segment
					if (state->fe[0]->dtv_property_cache.layer[1].modulation == DQPSK)
						ncoeff = coeff_2k_sb_3seg_1dqpsk;
					else	// QPSK or QAM on external segments
						ncoeff = coeff_2k_sb_3seg;
				}
			}
			break;

		case TRANSMISSION_MODE_4K:
			if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0) {
				if (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK)
					ncoeff = coeff_4k_sb_1seg_dqpsk;
				else	// QPSK or QAM
					ncoeff = coeff_4k_sb_1seg;
			} else {	// 3-segments
				if (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK) {
					if (state->fe[0]->dtv_property_cache.layer[1].modulation == DQPSK) {
						ncoeff = coeff_4k_sb_3seg_0dqpsk_1dqpsk;
					} else {	// QPSK or QAM on external segments
						ncoeff = coeff_4k_sb_3seg_0dqpsk;
					}
				} else {	// QPSK or QAM on central segment
					if (state->fe[0]->dtv_property_cache.layer[1].modulation == DQPSK) {
						ncoeff = coeff_4k_sb_3seg_1dqpsk;
					} else	// QPSK or QAM on external segments
						ncoeff = coeff_4k_sb_3seg;
				}
			}
			break;

		case TRANSMISSION_MODE_AUTO:
		case TRANSMISSION_MODE_8K:
		default:
			if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0) {
				if (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK)
					ncoeff = coeff_8k_sb_1seg_dqpsk;
				else	// QPSK or QAM
					ncoeff = coeff_8k_sb_1seg;
			} else {	// 3-segments
				if (state->fe[0]->dtv_property_cache.layer[0].modulation == DQPSK) {
					if (state->fe[0]->dtv_property_cache.layer[1].modulation == DQPSK) {
						ncoeff = coeff_8k_sb_3seg_0dqpsk_1dqpsk;
					} else {	// QPSK or QAM on external segments
						ncoeff = coeff_8k_sb_3seg_0dqpsk;
					}
				} else {	// QPSK or QAM on central segment
					if (state->fe[0]->dtv_property_cache.layer[1].modulation == DQPSK) {
						ncoeff = coeff_8k_sb_3seg_1dqpsk;
					} else	// QPSK or QAM on external segments
						ncoeff = coeff_8k_sb_3seg;
				}
			}
			break;
		}
		for (i = 0; i < 8; i++)
			dib8000_write_word(state, 343 + i, ncoeff[i]);
	}

	// P_small_coef_ext_enable=ISDB-Tsb, P_small_narrow_band=ISDB-Tsb, P_small_last_seg=13, P_small_offset_num_car=5
	dib8000_write_word(state, 351,
				(state->fe[0]->dtv_property_cache.isdbt_sb_mode << 9) | (state->fe[0]->dtv_property_cache.isdbt_sb_mode << 8) | (13 << 4) | 5);

	// ---- COFF ----
	// Carloff, the most robust
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {

		// P_coff_cpil_alpha=4, P_coff_inh=0, P_coff_cpil_winlen=64
		// P_coff_narrow_band=1, P_coff_square_val=1, P_coff_one_seg=~partial_rcpt, P_coff_use_tmcc=1, P_coff_use_ac=1
		dib8000_write_word(state, 187,
					(4 << 12) | (0 << 11) | (63 << 5) | (0x3 << 3) | ((~state->fe[0]->dtv_property_cache.isdbt_partial_reception & 1) << 2)
					| 0x3);

/*		// P_small_coef_ext_enable = 1 */
/*		dib8000_write_word(state, 351, dib8000_read_word(state, 351) | 0x200); */

		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0) {

			// P_coff_winlen=63, P_coff_thres_lock=15, P_coff_one_seg_width= (P_mode == 3) , P_coff_one_seg_sym= (P_mode-1)
			if (mode == 3)
				dib8000_write_word(state, 180, 0x1fcf | ((mode - 1) << 14));
			else
				dib8000_write_word(state, 180, 0x0fcf | ((mode - 1) << 14));
			// P_ctrl_corm_thres4pre_freq_inh=1,P_ctrl_pre_freq_mode_sat=1,
			// P_ctrl_pre_freq_inh=0, P_ctrl_pre_freq_step = 5, P_pre_freq_win_len=4
			dib8000_write_word(state, 338, (1 << 12) | (1 << 10) | (0 << 9) | (5 << 5) | 4);
			// P_ctrl_pre_freq_win_len=16, P_ctrl_pre_freq_thres_lockin=8
			dib8000_write_word(state, 340, (16 << 6) | (8 << 0));
			// P_ctrl_pre_freq_thres_lockout=6, P_small_use_tmcc/ac/cp=1
			dib8000_write_word(state, 341, (6 << 3) | (1 << 2) | (1 << 1) | (1 << 0));

			// P_coff_corthres_8k, 4k, 2k and P_coff_cpilthres_8k, 4k, 2k
			dib8000_write_word(state, 181, 300);
			dib8000_write_word(state, 182, 150);
			dib8000_write_word(state, 183, 80);
			dib8000_write_word(state, 184, 300);
			dib8000_write_word(state, 185, 150);
			dib8000_write_word(state, 186, 80);
		} else {	// Sound Broadcasting mode 3 seg
			// P_coff_one_seg_sym= 1, P_coff_one_seg_width= 1, P_coff_winlen=63, P_coff_thres_lock=15
			/*	if (mode == 3) */
			/*		dib8000_write_word(state, 180, 0x2fca | ((0) << 14)); */
			/*	else */
			/*		dib8000_write_word(state, 180, 0x2fca | ((1) << 14)); */
			dib8000_write_word(state, 180, 0x1fcf | (1 << 14));

			// P_ctrl_corm_thres4pre_freq_inh = 1, P_ctrl_pre_freq_mode_sat=1,
			// P_ctrl_pre_freq_inh=0, P_ctrl_pre_freq_step = 4, P_pre_freq_win_len=4
			dib8000_write_word(state, 338, (1 << 12) | (1 << 10) | (0 << 9) | (4 << 5) | 4);
			// P_ctrl_pre_freq_win_len=16, P_ctrl_pre_freq_thres_lockin=8
			dib8000_write_word(state, 340, (16 << 6) | (8 << 0));
			//P_ctrl_pre_freq_thres_lockout=6, P_small_use_tmcc/ac/cp=1
			dib8000_write_word(state, 341, (6 << 3) | (1 << 2) | (1 << 1) | (1 << 0));

			// P_coff_corthres_8k, 4k, 2k and P_coff_cpilthres_8k, 4k, 2k
			dib8000_write_word(state, 181, 350);
			dib8000_write_word(state, 182, 300);
			dib8000_write_word(state, 183, 250);
			dib8000_write_word(state, 184, 350);
			dib8000_write_word(state, 185, 300);
			dib8000_write_word(state, 186, 250);
		}

	} else if (state->isdbt_cfg_loaded == 0) {	// if not Sound Broadcasting mode : put default values for 13 segments
		dib8000_write_word(state, 180, (16 << 6) | 9);
		dib8000_write_word(state, 187, (4 << 12) | (8 << 5) | 0x2);
		coff_pow = 0x2800;
		for (i = 0; i < 6; i++)
			dib8000_write_word(state, 181 + i, coff_pow);

		// P_ctrl_corm_thres4pre_freq_inh=1, P_ctrl_pre_freq_mode_sat=1,
		// P_ctrl_pre_freq_mode_sat=1, P_ctrl_pre_freq_inh=0, P_ctrl_pre_freq_step = 3, P_pre_freq_win_len=1
		dib8000_write_word(state, 338, (1 << 12) | (1 << 10) | (0 << 9) | (3 << 5) | 1);

		// P_ctrl_pre_freq_win_len=8, P_ctrl_pre_freq_thres_lockin=6
		dib8000_write_word(state, 340, (8 << 6) | (6 << 0));
		// P_ctrl_pre_freq_thres_lockout=4, P_small_use_tmcc/ac/cp=1
		dib8000_write_word(state, 341, (4 << 3) | (1 << 2) | (1 << 1) | (1 << 0));
	}
	// ---- FFT ----
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1 && state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0)
		dib8000_write_word(state, 178, 64);	// P_fft_powrange=64
	else
		dib8000_write_word(state, 178, 32);	// P_fft_powrange=32

	/* make the cpil_coff_lock more robust but slower p_coff_winlen
	 * 6bits; p_coff_thres_lock 6bits (for coff lock if needed)
	 */
	/* if ( ( nbseg_diff>0)&&(nbseg_diff<13))
		dib8000_write_word(state, 187, (dib8000_read_word(state, 187) & 0xfffb) | (1 << 3)); */

	dib8000_write_word(state, 189, ~seg_mask13 | seg_diff_mask);	/* P_lmod4_seg_inh       */
	dib8000_write_word(state, 192, ~seg_mask13 | seg_diff_mask);	/* P_pha3_seg_inh        */
	dib8000_write_word(state, 225, ~seg_mask13 | seg_diff_mask);	/* P_tac_seg_inh         */
	if ((!state->fe[0]->dtv_property_cache.isdbt_sb_mode) && (state->cfg.pll->ifreq == 0))
		dib8000_write_word(state, 266, ~seg_mask13 | seg_diff_mask | 0x40);	/* P_equal_noise_seg_inh */
	else
		dib8000_write_word(state, 266, ~seg_mask13 | seg_diff_mask);	/* P_equal_noise_seg_inh */
	dib8000_write_word(state, 287, ~seg_mask13 | 0x1000);	/* P_tmcc_seg_inh        */
	//dib8000_write_word(state, 288, ~seg_mask13 | seg_diff_mask); /* P_tmcc_seg_eq_inh */
	if (!autosearching)
		dib8000_write_word(state, 288, (~seg_mask13 | seg_diff_mask) & 0x1fff);	/* P_tmcc_seg_eq_inh */
	else
		dib8000_write_word(state, 288, 0x1fff);	//disable equalisation of the tmcc when autosearch to be able to find the DQPSK channels.
	dprintk("287 = %X (%d)", ~seg_mask13 | 0x1000, ~seg_mask13 | 0x1000);

	dib8000_write_word(state, 211, seg_mask13 & (~seg_diff_mask));	/* P_des_seg_enabled     */

	/* offset loop parameters */
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0)
			/* P_timf_alpha = (11-P_mode), P_corm_alpha=6, P_corm_thres=0x80 */
			dib8000_write_word(state, 32, ((11 - mode) << 12) | (6 << 8) | 0x40);

		else		// Sound Broadcasting mode 3 seg
			/* P_timf_alpha = (10-P_mode), P_corm_alpha=6, P_corm_thres=0x80 */
			dib8000_write_word(state, 32, ((10 - mode) << 12) | (6 << 8) | 0x60);
	} else
		// TODO in 13 seg, timf_alpha can always be the same or not ?
		/* P_timf_alpha = (9-P_mode, P_corm_alpha=6, P_corm_thres=0x80 */
		dib8000_write_word(state, 32, ((9 - mode) << 12) | (6 << 8) | 0x80);

	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0)
			/* P_ctrl_pha_off_max=3   P_ctrl_sfreq_inh =0  P_ctrl_sfreq_step = (11-P_mode)  */
			dib8000_write_word(state, 37, (3 << 5) | (0 << 4) | (10 - mode));

		else		// Sound Broadcasting mode 3 seg
			/* P_ctrl_pha_off_max=3   P_ctrl_sfreq_inh =0  P_ctrl_sfreq_step = (10-P_mode)  */
			dib8000_write_word(state, 37, (3 << 5) | (0 << 4) | (9 - mode));
	} else
		/* P_ctrl_pha_off_max=3   P_ctrl_sfreq_inh =0  P_ctrl_sfreq_step = 9  */
		dib8000_write_word(state, 37, (3 << 5) | (0 << 4) | (8 - mode));

	/* P_dvsy_sync_wait - reuse mode */
	switch (state->fe[0]->dtv_property_cache.transmission_mode) {
	case TRANSMISSION_MODE_8K:
		mode = 256;
		break;
	case TRANSMISSION_MODE_4K:
		mode = 128;
		break;
	default:
	case TRANSMISSION_MODE_2K:
		mode = 64;
		break;
	}
	if (state->cfg.diversity_delay == 0)
		mode = (mode * (1 << (guard)) * 3) / 2 + 48;	// add 50% SFN margin + compensate for one DVSY-fifo
	else
		mode = (mode * (1 << (guard)) * 3) / 2 + state->cfg.diversity_delay;	// add 50% SFN margin + compensate for DVSY-fifo
	mode <<= 4;
	dib8000_write_word(state, 273, (dib8000_read_word(state, 273) & 0x000f) | mode);

	/* channel estimation fine configuration */
	switch (max_constellation) {
	case QAM_64:
		ana_gain = 0x7;	// -1 : avoid def_est saturation when ADC target is -16dB
		coeff[0] = 0x0148;	/* P_adp_regul_cnt 0.04 */
		coeff[1] = 0xfff0;	/* P_adp_noise_cnt -0.002 */
		coeff[2] = 0x00a4;	/* P_adp_regul_ext 0.02 */
		coeff[3] = 0xfff8;	/* P_adp_noise_ext -0.001 */
		//if (!state->cfg.hostbus_diversity) //if diversity, we should prehaps use the configuration of the max_constallation -1
		break;
	case QAM_16:
		ana_gain = 0x7;	// -1 : avoid def_est saturation when ADC target is -16dB
		coeff[0] = 0x023d;	/* P_adp_regul_cnt 0.07 */
		coeff[1] = 0xffdf;	/* P_adp_noise_cnt -0.004 */
		coeff[2] = 0x00a4;	/* P_adp_regul_ext 0.02 */
		coeff[3] = 0xfff0;	/* P_adp_noise_ext -0.002 */
		//if (!((state->cfg.hostbus_diversity) && (max_constellation == QAM_16)))
		break;
	default:
		ana_gain = 0;	// 0 : goes along with ADC target at -22dB to keep good mobile performance and lock at sensitivity level
		coeff[0] = 0x099a;	/* P_adp_regul_cnt 0.3 */
		coeff[1] = 0xffae;	/* P_adp_noise_cnt -0.01 */
		coeff[2] = 0x0333;	/* P_adp_regul_ext 0.1 */
		coeff[3] = 0xfff8;	/* P_adp_noise_ext -0.002 */
		break;
	}
	for (mode = 0; mode < 4; mode++)
		dib8000_write_word(state, 215 + mode, coeff[mode]);

	// update ana_gain depending on max constellation
	dib8000_write_word(state, 116, ana_gain);
	// update ADC target depending on ana_gain
	if (ana_gain) {		// set -16dB ADC target for ana_gain=-1
		for (i = 0; i < 10; i++)
			dib8000_write_word(state, 80 + i, adc_target_16dB[i]);
	} else {		// set -22dB ADC target for ana_gain=0
		for (i = 0; i < 10; i++)
			dib8000_write_word(state, 80 + i, adc_target_16dB[i] - 355);
	}

	// ---- ANA_FE ----
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode) {
		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 1)
			ana_fe = ana_fe_coeff_3seg;
		else		// 1-segment
			ana_fe = ana_fe_coeff_1seg;
	} else
		ana_fe = ana_fe_coeff_13seg;

	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1 || state->isdbt_cfg_loaded == 0)
		for (mode = 0; mode < 24; mode++)
			dib8000_write_word(state, 117 + mode, ana_fe[mode]);

	// ---- CHAN_BLK ----
	for (i = 0; i < 13; i++) {
		if ((((~seg_diff_mask) >> i) & 1) == 1) {
			P_cfr_left_edge += (1 << i) * ((i == 0) || ((((seg_mask13 & (~seg_diff_mask)) >> (i - 1)) & 1) == 0));
			P_cfr_right_edge += (1 << i) * ((i == 12) || ((((seg_mask13 & (~seg_diff_mask)) >> (i + 1)) & 1) == 0));
		}
	}
	dib8000_write_word(state, 222, P_cfr_left_edge);	// P_cfr_left_edge
	dib8000_write_word(state, 223, P_cfr_right_edge);	// P_cfr_right_edge
	// "P_cspu_left_edge"  not used => do not care
	// "P_cspu_right_edge" not used => do not care

	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
		dib8000_write_word(state, 228, 1);	// P_2d_mode_byp=1
		dib8000_write_word(state, 205, dib8000_read_word(state, 205) & 0xfff0);	// P_cspu_win_cut = 0
		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0
			&& state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_2K) {
			//dib8000_write_word(state, 219, dib8000_read_word(state, 219) & 0xfffe); // P_adp_pass = 0
			dib8000_write_word(state, 265, 15);	// P_equal_noise_sel = 15
		}
	} else if (state->isdbt_cfg_loaded == 0) {
		dib8000_write_word(state, 228, 0);	// default value
		dib8000_write_word(state, 265, 31);	// default value
		dib8000_write_word(state, 205, 0x200f);	// init value
	}
	// ---- TMCC ----
	for (i = 0; i < 3; i++)
		tmcc_pow +=
			(((state->fe[0]->dtv_property_cache.layer[i].modulation == DQPSK) * 4 + 1) * state->fe[0]->dtv_property_cache.layer[i].segment_count);
	// Quantif of "P_tmcc_dec_thres_?k" is (0, 5+mode, 9);
	// Threshold is set at 1/4 of max power.
	tmcc_pow *= (1 << (9 - 2));

	dib8000_write_word(state, 290, tmcc_pow);	// P_tmcc_dec_thres_2k
	dib8000_write_word(state, 291, tmcc_pow);	// P_tmcc_dec_thres_4k
	dib8000_write_word(state, 292, tmcc_pow);	// P_tmcc_dec_thres_8k
	//dib8000_write_word(state, 287, (1 << 13) | 0x1000 );
	// ---- PHA3 ----

	if (state->isdbt_cfg_loaded == 0)
		dib8000_write_word(state, 250, 3285);	/*p_2d_hspeed_thr0 */

	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1)
		state->isdbt_cfg_loaded = 0;
	else
		state->isdbt_cfg_loaded = 1;

}

static int dib8000_autosearch_start(struct dvb_frontend *fe)
{
	u8 factor;
	u32 value;
	struct dib8000_state *state = fe->demodulator_priv;

	int slist = 0;

	state->fe[0]->dtv_property_cache.inversion = 0;
	if (!state->fe[0]->dtv_property_cache.isdbt_sb_mode)
		state->fe[0]->dtv_property_cache.layer[0].segment_count = 13;
	state->fe[0]->dtv_property_cache.layer[0].modulation = QAM_64;
	state->fe[0]->dtv_property_cache.layer[0].fec = FEC_2_3;
	state->fe[0]->dtv_property_cache.layer[0].interleaving = 0;

	//choose the right list, in sb, always do everything
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode) {
		state->fe[0]->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_8K;
		state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_8;
		slist = 7;
		dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));
	} else {
		if (state->fe[0]->dtv_property_cache.guard_interval == GUARD_INTERVAL_AUTO) {
			if (state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_AUTO) {
				slist = 7;
				dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));	// P_mode = 1 to have autosearch start ok with mode2
			} else
				slist = 3;
		} else {
			if (state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_AUTO) {
				slist = 2;
				dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));	// P_mode = 1
			} else
				slist = 0;
		}

		if (state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_AUTO)
			state->fe[0]->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_8K;
		if (state->fe[0]->dtv_property_cache.guard_interval == GUARD_INTERVAL_AUTO)
			state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_8;

		dprintk("using list for autosearch : %d", slist);
		dib8000_set_channel(state, (unsigned char)slist, 1);
		//dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));  // P_mode = 1

		factor = 1;

		//set lock_mask values
		dib8000_write_word(state, 6, 0x4);
		dib8000_write_word(state, 7, 0x8);
		dib8000_write_word(state, 8, 0x1000);

		//set lock_mask wait time values
		value = 50 * state->cfg.pll->internal * factor;
		dib8000_write_word(state, 11, (u16) ((value >> 16) & 0xffff));	// lock0 wait time
		dib8000_write_word(state, 12, (u16) (value & 0xffff));	// lock0 wait time
		value = 100 * state->cfg.pll->internal * factor;
		dib8000_write_word(state, 13, (u16) ((value >> 16) & 0xffff));	// lock1 wait time
		dib8000_write_word(state, 14, (u16) (value & 0xffff));	// lock1 wait time
		value = 1000 * state->cfg.pll->internal * factor;
		dib8000_write_word(state, 15, (u16) ((value >> 16) & 0xffff));	// lock2 wait time
		dib8000_write_word(state, 16, (u16) (value & 0xffff));	// lock2 wait time

		value = dib8000_read_word(state, 0);
		dib8000_write_word(state, 0, (u16) ((1 << 15) | value));
		dib8000_read_word(state, 1284);	// reset the INT. n_irq_pending
		dib8000_write_word(state, 0, (u16) value);

	}

	return 0;
}

static int dib8000_autosearch_irq(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 irq_pending = dib8000_read_word(state, 1284);

	if (irq_pending & 0x1) {	// failed
		dprintk("dib8000_autosearch_irq failed");
		return 1;
	}

	if (irq_pending & 0x2) {	// succeeded
		dprintk("dib8000_autosearch_irq succeeded");
		return 2;
	}

	return 0;		// still pending
}

static int dib8000_tune(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	int ret = 0;
	u16 value, mode = fft_to_mode(state);

	// we are already tuned - just resuming from suspend
	if (state == NULL)
		return -EINVAL;

	dib8000_set_bandwidth(fe, state->fe[0]->dtv_property_cache.bandwidth_hz / 1000);
	dib8000_set_channel(state, 0, 0);

	// restart demod
	ret |= dib8000_write_word(state, 770, 0x4000);
	ret |= dib8000_write_word(state, 770, 0x0000);
	msleep(45);

	/* P_ctrl_inh_cor=0, P_ctrl_alpha_cor=4, P_ctrl_inh_isi=0, P_ctrl_alpha_isi=3 */
	/*  ret |= dib8000_write_word(state, 29, (0 << 9) | (4 << 5) | (0 << 4) | (3 << 0) );  workaround inh_isi stays at 1 */

	// never achieved a lock before - wait for timfreq to update
	if (state->timf == 0) {
		if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
			if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0)
				msleep(300);
			else	// Sound Broadcasting mode 3 seg
				msleep(500);
		} else		// 13 seg
			msleep(200);
	}
	if (state->fe[0]->dtv_property_cache.isdbt_sb_mode == 1) {
		if (state->fe[0]->dtv_property_cache.isdbt_partial_reception == 0) {

			/* P_timf_alpha = (13-P_mode) , P_corm_alpha=6, P_corm_thres=0x40  alpha to check on board */
			dib8000_write_word(state, 32, ((13 - mode) << 12) | (6 << 8) | 0x40);
			//dib8000_write_word(state, 32, (8 << 12) | (6 << 8) | 0x80);

			/*  P_ctrl_sfreq_step= (12-P_mode)   P_ctrl_sfreq_inh =0     P_ctrl_pha_off_max  */
			ret |= dib8000_write_word(state, 37, (12 - mode) | ((5 + mode) << 5));

		} else {	// Sound Broadcasting mode 3 seg

			/* P_timf_alpha = (12-P_mode) , P_corm_alpha=6, P_corm_thres=0x60  alpha to check on board */
			dib8000_write_word(state, 32, ((12 - mode) << 12) | (6 << 8) | 0x60);

			ret |= dib8000_write_word(state, 37, (11 - mode) | ((5 + mode) << 5));
		}

	} else {		// 13 seg
		/* P_timf_alpha = 8 , P_corm_alpha=6, P_corm_thres=0x80  alpha to check on board */
		dib8000_write_word(state, 32, ((11 - mode) << 12) | (6 << 8) | 0x80);

		ret |= dib8000_write_word(state, 37, (10 - mode) | ((5 + mode) << 5));

	}

	// we achieved a coff_cpil_lock - it's time to update the timf
	if ((dib8000_read_word(state, 568) >> 11) & 0x1)
		dib8000_update_timf(state);

	//now that tune is finished, lock0 should lock on fec_mpeg to output this lock on MP_LOCK. It's changed in autosearch start
	dib8000_write_word(state, 6, 0x200);

	if (state->revision == 0x8002) {
		value = dib8000_read_word(state, 903);
		dib8000_write_word(state, 903, value & ~(1 << 3));
		msleep(1);
		dib8000_write_word(state, 903, value | (1 << 3));
	}

	return ret;
}

static int dib8000_wakeup(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	int ret;

	dib8000_set_power_mode(state, DIB8000M_POWER_ALL);
	dib8000_set_adc_state(state, DIBX000_ADC_ON);
	if (dib8000_set_adc_state(state, DIBX000_SLOW_ADC_ON) != 0)
		dprintk("could not start Slow ADC");

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		ret = state->fe[index_frontend]->ops.init(state->fe[index_frontend]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dib8000_sleep(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	int ret;

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		ret = state->fe[index_frontend]->ops.sleep(state->fe[index_frontend]);
		if (ret < 0)
			return ret;
	}

	dib8000_set_output_mode(fe, OUTMODE_HIGH_Z);
	dib8000_set_power_mode(state, DIB8000M_POWER_INTERFACE_ONLY);
	return dib8000_set_adc_state(state, DIBX000_SLOW_ADC_OFF) | dib8000_set_adc_state(state, DIBX000_ADC_OFF);
}

enum frontend_tune_state dib8000_get_tune_state(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	return state->tune_state;
}
EXPORT_SYMBOL(dib8000_get_tune_state);

int dib8000_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state)
{
	struct dib8000_state *state = fe->demodulator_priv;
	state->tune_state = tune_state;
	return 0;
}
EXPORT_SYMBOL(dib8000_set_tune_state);

static int dib8000_get_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 i, val = 0;
	fe_status_t stat;
	u8 index_frontend, sub_index_frontend;

	fe->dtv_property_cache.bandwidth_hz = 6000000;

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->ops.read_status(state->fe[index_frontend], &stat);
		if (stat&FE_HAS_SYNC) {
			dprintk("TMCC lock on the slave%i", index_frontend);
			/* synchronize the cache with the other frontends */
			state->fe[index_frontend]->ops.get_frontend(state->fe[index_frontend], fep);
			for (sub_index_frontend = 0; (sub_index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[sub_index_frontend] != NULL); sub_index_frontend++) {
				if (sub_index_frontend != index_frontend) {
					state->fe[sub_index_frontend]->dtv_property_cache.isdbt_sb_mode = state->fe[index_frontend]->dtv_property_cache.isdbt_sb_mode;
					state->fe[sub_index_frontend]->dtv_property_cache.inversion = state->fe[index_frontend]->dtv_property_cache.inversion;
					state->fe[sub_index_frontend]->dtv_property_cache.transmission_mode = state->fe[index_frontend]->dtv_property_cache.transmission_mode;
					state->fe[sub_index_frontend]->dtv_property_cache.guard_interval = state->fe[index_frontend]->dtv_property_cache.guard_interval;
					state->fe[sub_index_frontend]->dtv_property_cache.isdbt_partial_reception = state->fe[index_frontend]->dtv_property_cache.isdbt_partial_reception;
					for (i = 0; i < 3; i++) {
						state->fe[sub_index_frontend]->dtv_property_cache.layer[i].segment_count = state->fe[index_frontend]->dtv_property_cache.layer[i].segment_count;
						state->fe[sub_index_frontend]->dtv_property_cache.layer[i].interleaving = state->fe[index_frontend]->dtv_property_cache.layer[i].interleaving;
						state->fe[sub_index_frontend]->dtv_property_cache.layer[i].fec = state->fe[index_frontend]->dtv_property_cache.layer[i].fec;
						state->fe[sub_index_frontend]->dtv_property_cache.layer[i].modulation = state->fe[index_frontend]->dtv_property_cache.layer[i].modulation;
					}
				}
			}
			return 0;
		}
	}

	fe->dtv_property_cache.isdbt_sb_mode = dib8000_read_word(state, 508) & 0x1;

	val = dib8000_read_word(state, 570);
	fe->dtv_property_cache.inversion = (val & 0x40) >> 6;
	switch ((val & 0x30) >> 4) {
	case 1:
		fe->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 3:
	default:
		fe->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_8K;
		break;
	}

	switch (val & 0x3) {
	case 0:
		fe->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_32;
		dprintk("dib8000_get_frontend GI = 1/32 ");
		break;
	case 1:
		fe->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_16;
		dprintk("dib8000_get_frontend GI = 1/16 ");
		break;
	case 2:
		dprintk("dib8000_get_frontend GI = 1/8 ");
		fe->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		dprintk("dib8000_get_frontend GI = 1/4 ");
		fe->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	val = dib8000_read_word(state, 505);
	fe->dtv_property_cache.isdbt_partial_reception = val & 1;
	dprintk("dib8000_get_frontend : partial_reception = %d ", fe->dtv_property_cache.isdbt_partial_reception);

	for (i = 0; i < 3; i++) {
		val = dib8000_read_word(state, 493 + i);
		fe->dtv_property_cache.layer[i].segment_count = val & 0x0F;
		dprintk("dib8000_get_frontend : Layer %d segments = %d ", i, fe->dtv_property_cache.layer[i].segment_count);

		val = dib8000_read_word(state, 499 + i);
		fe->dtv_property_cache.layer[i].interleaving = val & 0x3;
		dprintk("dib8000_get_frontend : Layer %d time_intlv = %d ", i, fe->dtv_property_cache.layer[i].interleaving);

		val = dib8000_read_word(state, 481 + i);
		switch (val & 0x7) {
		case 1:
			fe->dtv_property_cache.layer[i].fec = FEC_1_2;
			dprintk("dib8000_get_frontend : Layer %d Code Rate = 1/2 ", i);
			break;
		case 2:
			fe->dtv_property_cache.layer[i].fec = FEC_2_3;
			dprintk("dib8000_get_frontend : Layer %d Code Rate = 2/3 ", i);
			break;
		case 3:
			fe->dtv_property_cache.layer[i].fec = FEC_3_4;
			dprintk("dib8000_get_frontend : Layer %d Code Rate = 3/4 ", i);
			break;
		case 5:
			fe->dtv_property_cache.layer[i].fec = FEC_5_6;
			dprintk("dib8000_get_frontend : Layer %d Code Rate = 5/6 ", i);
			break;
		default:
			fe->dtv_property_cache.layer[i].fec = FEC_7_8;
			dprintk("dib8000_get_frontend : Layer %d Code Rate = 7/8 ", i);
			break;
		}

		val = dib8000_read_word(state, 487 + i);
		switch (val & 0x3) {
		case 0:
			dprintk("dib8000_get_frontend : Layer %d DQPSK ", i);
			fe->dtv_property_cache.layer[i].modulation = DQPSK;
			break;
		case 1:
			fe->dtv_property_cache.layer[i].modulation = QPSK;
			dprintk("dib8000_get_frontend : Layer %d QPSK ", i);
			break;
		case 2:
			fe->dtv_property_cache.layer[i].modulation = QAM_16;
			dprintk("dib8000_get_frontend : Layer %d QAM16 ", i);
			break;
		case 3:
		default:
			dprintk("dib8000_get_frontend : Layer %d QAM64 ", i);
			fe->dtv_property_cache.layer[i].modulation = QAM_64;
			break;
		}
	}

	/* synchronize the cache with the other frontends */
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->dtv_property_cache.isdbt_sb_mode = fe->dtv_property_cache.isdbt_sb_mode;
		state->fe[index_frontend]->dtv_property_cache.inversion = fe->dtv_property_cache.inversion;
		state->fe[index_frontend]->dtv_property_cache.transmission_mode = fe->dtv_property_cache.transmission_mode;
		state->fe[index_frontend]->dtv_property_cache.guard_interval = fe->dtv_property_cache.guard_interval;
		state->fe[index_frontend]->dtv_property_cache.isdbt_partial_reception = fe->dtv_property_cache.isdbt_partial_reception;
		for (i = 0; i < 3; i++) {
			state->fe[index_frontend]->dtv_property_cache.layer[i].segment_count = fe->dtv_property_cache.layer[i].segment_count;
			state->fe[index_frontend]->dtv_property_cache.layer[i].interleaving = fe->dtv_property_cache.layer[i].interleaving;
			state->fe[index_frontend]->dtv_property_cache.layer[i].fec = fe->dtv_property_cache.layer[i].fec;
			state->fe[index_frontend]->dtv_property_cache.layer[i].modulation = fe->dtv_property_cache.layer[i].modulation;
		}
	}
	return 0;
}

static int dib8000_set_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 nbr_pending, exit_condition, index_frontend;
	s8 index_frontend_success = -1;
	int time, ret;
	int  time_slave = FE_CALLBACK_TIME_NEVER;

	if (state->fe[0]->dtv_property_cache.frequency == 0) {
		dprintk("dib8000: must at least specify frequency ");
		return 0;
	}

	if (state->fe[0]->dtv_property_cache.bandwidth_hz == 0) {
		dprintk("dib8000: no bandwidth specified, set to default ");
		state->fe[0]->dtv_property_cache.bandwidth_hz = 6000000;
	}

	for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		/* synchronization of the cache */
		state->fe[index_frontend]->dtv_property_cache.delivery_system = SYS_ISDBT;
		memcpy(&state->fe[index_frontend]->dtv_property_cache, &fe->dtv_property_cache, sizeof(struct dtv_frontend_properties));

		dib8000_set_output_mode(state->fe[index_frontend], OUTMODE_HIGH_Z);
		if (state->fe[index_frontend]->ops.tuner_ops.set_params)
			state->fe[index_frontend]->ops.tuner_ops.set_params(state->fe[index_frontend], fep);

		dib8000_set_tune_state(state->fe[index_frontend], CT_AGC_START);
	}

	/* start up the AGC */
	do {
		time = dib8000_agc_startup(state->fe[0]);
		for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			time_slave = dib8000_agc_startup(state->fe[index_frontend]);
			if (time == FE_CALLBACK_TIME_NEVER)
				time = time_slave;
			else if ((time_slave != FE_CALLBACK_TIME_NEVER) && (time_slave > time))
				time = time_slave;
		}
		if (time != FE_CALLBACK_TIME_NEVER)
			msleep(time / 10);
		else
			break;
		exit_condition = 1;
		for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			if (dib8000_get_tune_state(state->fe[index_frontend]) != CT_AGC_STOP) {
				exit_condition = 0;
				break;
			}
		}
	} while (exit_condition == 0);

	for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		dib8000_set_tune_state(state->fe[index_frontend], CT_DEMOD_START);

	if ((state->fe[0]->dtv_property_cache.delivery_system != SYS_ISDBT) ||
			(state->fe[0]->dtv_property_cache.inversion == INVERSION_AUTO) ||
			(state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_AUTO) ||
			(state->fe[0]->dtv_property_cache.guard_interval == GUARD_INTERVAL_AUTO) ||
			(((state->fe[0]->dtv_property_cache.isdbt_layer_enabled & (1 << 0)) != 0) &&
			 (state->fe[0]->dtv_property_cache.layer[0].segment_count != 0xff) &&
			 (state->fe[0]->dtv_property_cache.layer[0].segment_count != 0) &&
			 ((state->fe[0]->dtv_property_cache.layer[0].modulation == QAM_AUTO) ||
			  (state->fe[0]->dtv_property_cache.layer[0].fec == FEC_AUTO))) ||
			(((state->fe[0]->dtv_property_cache.isdbt_layer_enabled & (1 << 1)) != 0) &&
			 (state->fe[0]->dtv_property_cache.layer[1].segment_count != 0xff) &&
			 (state->fe[0]->dtv_property_cache.layer[1].segment_count != 0) &&
			 ((state->fe[0]->dtv_property_cache.layer[1].modulation == QAM_AUTO) ||
			  (state->fe[0]->dtv_property_cache.layer[1].fec == FEC_AUTO))) ||
			(((state->fe[0]->dtv_property_cache.isdbt_layer_enabled & (1 << 2)) != 0) &&
			 (state->fe[0]->dtv_property_cache.layer[2].segment_count != 0xff) &&
			 (state->fe[0]->dtv_property_cache.layer[2].segment_count != 0) &&
			 ((state->fe[0]->dtv_property_cache.layer[2].modulation == QAM_AUTO) ||
			  (state->fe[0]->dtv_property_cache.layer[2].fec == FEC_AUTO))) ||
			(((state->fe[0]->dtv_property_cache.layer[0].segment_count == 0) ||
			  ((state->fe[0]->dtv_property_cache.isdbt_layer_enabled & (1 << 0)) == 0)) &&
			 ((state->fe[0]->dtv_property_cache.layer[1].segment_count == 0) ||
			  ((state->fe[0]->dtv_property_cache.isdbt_layer_enabled & (2 << 0)) == 0)) &&
			 ((state->fe[0]->dtv_property_cache.layer[2].segment_count == 0) || ((state->fe[0]->dtv_property_cache.isdbt_layer_enabled & (3 << 0)) == 0)))) {
		int i = 80000;
		u8 found = 0;
		u8 tune_failed = 0;

		for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			dib8000_set_bandwidth(state->fe[index_frontend], fe->dtv_property_cache.bandwidth_hz / 1000);
			dib8000_autosearch_start(state->fe[index_frontend]);
		}

		do {
			msleep(20);
			nbr_pending = 0;
			exit_condition = 0; /* 0: tune pending; 1: tune failed; 2:tune success */
			for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
				if (((tune_failed >> index_frontend) & 0x1) == 0) {
					found = dib8000_autosearch_irq(state->fe[index_frontend]);
					switch (found) {
					case 0: /* tune pending */
						 nbr_pending++;
						 break;
					case 2:
						 dprintk("autosearch succeed on the frontend%i", index_frontend);
						 exit_condition = 2;
						 index_frontend_success = index_frontend;
						 break;
					default:
						 dprintk("unhandled autosearch result");
					case 1:
						 dprintk("autosearch failed for the frontend%i", index_frontend);
						 break;
					}
				}
			}

			/* if all tune are done and no success, exit: tune failed */
			if ((nbr_pending == 0) && (exit_condition == 0))
				exit_condition = 1;
		} while ((exit_condition == 0) && i--);

		if (exit_condition == 1) { /* tune failed */
			dprintk("tune failed");
			return 0;
		}

		dprintk("tune success on frontend%i", index_frontend_success);

		dib8000_get_frontend(fe, fep);
	}

	for (index_frontend = 0, ret = 0; (ret >= 0) && (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		ret = dib8000_tune(state->fe[index_frontend]);

	/* set output mode and diversity input */
	dib8000_set_output_mode(state->fe[0], state->cfg.output_mode);
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		dib8000_set_output_mode(state->fe[index_frontend], OUTMODE_DIVERSITY);
		dib8000_set_diversity_in(state->fe[index_frontend-1], 1);
	}

	/* turn off the diversity of the last chip */
	dib8000_set_diversity_in(state->fe[index_frontend-1], 0);

	return ret;
}

static u16 dib8000_read_lock(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;

	return dib8000_read_word(state, 568);
}

static int dib8000_read_status(struct dvb_frontend *fe, fe_status_t * stat)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 lock_slave = 0, lock = dib8000_read_word(state, 568);
	u8 index_frontend;

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		lock_slave |= dib8000_read_lock(state->fe[index_frontend]);

	*stat = 0;

	if (((lock >> 13) & 1) || ((lock_slave >> 13) & 1))
		*stat |= FE_HAS_SIGNAL;

	if (((lock >> 8) & 1) || ((lock_slave >> 8) & 1)) /* Equal */
		*stat |= FE_HAS_CARRIER;

	if ((((lock >> 1) & 0xf) == 0xf) || (((lock_slave >> 1) & 0xf) == 0xf)) /* TMCC_SYNC */
		*stat |= FE_HAS_SYNC;

	if ((((lock >> 12) & 1) || ((lock_slave >> 12) & 1)) && ((lock >> 5) & 7)) /* FEC MPEG */
		*stat |= FE_HAS_LOCK;

	if (((lock >> 12) & 1) || ((lock_slave >> 12) & 1)) {
		lock = dib8000_read_word(state, 554); /* Viterbi Layer A */
		if (lock & 0x01)
			*stat |= FE_HAS_VITERBI;

		lock = dib8000_read_word(state, 555); /* Viterbi Layer B */
		if (lock & 0x01)
			*stat |= FE_HAS_VITERBI;

		lock = dib8000_read_word(state, 556); /* Viterbi Layer C */
		if (lock & 0x01)
			*stat |= FE_HAS_VITERBI;
	}

	return 0;
}

static int dib8000_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	struct dib8000_state *state = fe->demodulator_priv;
	*ber = (dib8000_read_word(state, 560) << 16) | dib8000_read_word(state, 561);	// 13 segments
	return 0;
}

static int dib8000_read_unc_blocks(struct dvb_frontend *fe, u32 * unc)
{
	struct dib8000_state *state = fe->demodulator_priv;
	*unc = dib8000_read_word(state, 565);	// packet error on 13 seg
	return 0;
}

static int dib8000_read_signal_strength(struct dvb_frontend *fe, u16 * strength)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	u16 val;

	*strength = 0;
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->ops.read_signal_strength(state->fe[index_frontend], &val);
		if (val > 65535 - *strength)
			*strength = 65535;
		else
			*strength += val;
	}

	val = 65535 - dib8000_read_word(state, 390);
	if (val > 65535 - *strength)
		*strength = 65535;
	else
		*strength += val;
	return 0;
}

static u32 dib8000_get_snr(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u32 n, s, exp;
	u16 val;

	val = dib8000_read_word(state, 542);
	n = (val >> 6) & 0xff;
	exp = (val & 0x3f);
	if ((exp & 0x20) != 0)
		exp -= 0x40;
	n <<= exp+16;

	val = dib8000_read_word(state, 543);
	s = (val >> 6) & 0xff;
	exp = (val & 0x3f);
	if ((exp & 0x20) != 0)
		exp -= 0x40;
	s <<= exp+16;

	if (n > 0) {
		u32 t = (s/n) << 16;
		return t + ((s << 16) - n*t) / n;
	}
	return 0xffffffff;
}

static int dib8000_read_snr(struct dvb_frontend *fe, u16 * snr)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	u32 snr_master;

	snr_master = dib8000_get_snr(fe);
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		snr_master += dib8000_get_snr(state->fe[index_frontend]);

	if (snr_master != 0) {
		snr_master = 10*intlog10(snr_master>>16);
		*snr = snr_master / ((1 << 24) / 10);
	}
	else
		*snr = 0;

	return 0;
}

int dib8000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend = 1;

	while ((index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL))
		index_frontend++;
	if (index_frontend < MAX_NUMBER_OF_FRONTENDS) {
		dprintk("set slave fe %p to index %i", fe_slave, index_frontend);
		state->fe[index_frontend] = fe_slave;
		return 0;
	}

	dprintk("too many slave frontend");
	return -ENOMEM;
}
EXPORT_SYMBOL(dib8000_set_slave_frontend);

int dib8000_remove_slave_frontend(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend = 1;

	while ((index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL))
		index_frontend++;
	if (index_frontend != 1) {
		dprintk("remove slave fe %p (index %i)", state->fe[index_frontend-1], index_frontend-1);
		state->fe[index_frontend] = NULL;
		return 0;
	}

	dprintk("no frontend to be removed");
	return -ENODEV;
}
EXPORT_SYMBOL(dib8000_remove_slave_frontend);

struct dvb_frontend *dib8000_get_slave_frontend(struct dvb_frontend *fe, int slave_index)
{
	struct dib8000_state *state = fe->demodulator_priv;

	if (slave_index >= MAX_NUMBER_OF_FRONTENDS)
		return NULL;
	return state->fe[slave_index];
}
EXPORT_SYMBOL(dib8000_get_slave_frontend);


int dib8000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods, u8 default_addr, u8 first_addr)
{
	int k = 0;
	u8 new_addr = 0;
	struct i2c_device client = {.adap = host };

	for (k = no_of_demods - 1; k >= 0; k--) {
		/* designated i2c address */
		new_addr = first_addr + (k << 1);

		client.addr = new_addr;
		dib8000_i2c_write16(&client, 1287, 0x0003);	/* sram lead in, rdy */
		if (dib8000_identify(&client) == 0) {
			dib8000_i2c_write16(&client, 1287, 0x0003);	/* sram lead in, rdy */
			client.addr = default_addr;
			if (dib8000_identify(&client) == 0) {
				dprintk("#%d: not identified", k);
				return -EINVAL;
			}
		}

		/* start diversity to pull_down div_str - just for i2c-enumeration */
		dib8000_i2c_write16(&client, 1286, (1 << 10) | (4 << 6));

		/* set new i2c address and force divstart */
		dib8000_i2c_write16(&client, 1285, (new_addr << 2) | 0x2);
		client.addr = new_addr;
		dib8000_identify(&client);

		dprintk("IC %d initialized (to i2c_address 0x%x)", k, new_addr);
	}

	for (k = 0; k < no_of_demods; k++) {
		new_addr = first_addr | (k << 1);
		client.addr = new_addr;

		// unforce divstr
		dib8000_i2c_write16(&client, 1285, new_addr << 2);

		/* deactivate div - it was just for i2c-enumeration */
		dib8000_i2c_write16(&client, 1286, 0);
	}

	return 0;
}

EXPORT_SYMBOL(dib8000_i2c_enumeration);
static int dib8000_fe_get_tune_settings(struct dvb_frontend *fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	tune->step_size = 0;
	tune->max_drift = 0;
	return 0;
}

static void dib8000_release(struct dvb_frontend *fe)
{
	struct dib8000_state *st = fe->demodulator_priv;
	u8 index_frontend;

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (st->fe[index_frontend] != NULL); index_frontend++)
		dvb_frontend_detach(st->fe[index_frontend]);

	dibx000_exit_i2c_master(&st->i2c_master);
	kfree(st->fe[0]);
	kfree(st);
}

struct i2c_adapter *dib8000_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface intf, int gating)
{
	struct dib8000_state *st = fe->demodulator_priv;
	return dibx000_get_i2c_adapter(&st->i2c_master, intf, gating);
}

EXPORT_SYMBOL(dib8000_get_i2c_master);

int dib8000_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff)
{
	struct dib8000_state *st = fe->demodulator_priv;
	u16 val = dib8000_read_word(st, 299) & 0xffef;
	val |= (onoff & 0x1) << 4;

	dprintk("pid filter enabled %d", onoff);
	return dib8000_write_word(st, 299, val);
}
EXPORT_SYMBOL(dib8000_pid_filter_ctrl);

int dib8000_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	struct dib8000_state *st = fe->demodulator_priv;
	dprintk("Index %x, PID %d, OnOff %d", id, pid, onoff);
	return dib8000_write_word(st, 305 + id, onoff ? (1 << 13) | pid : 0);
}
EXPORT_SYMBOL(dib8000_pid_filter);

static const struct dvb_frontend_ops dib8000_ops = {
	.info = {
		 .name = "DiBcom 8000 ISDB-T",
		 .type = FE_OFDM,
		 .frequency_min = 44250000,
		 .frequency_max = 867250000,
		 .frequency_stepsize = 62500,
		 .caps = FE_CAN_INVERSION_AUTO |
		 FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		 FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		 FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		 FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_RECOVER | FE_CAN_HIERARCHY_AUTO,
		 },

	.release = dib8000_release,

	.init = dib8000_wakeup,
	.sleep = dib8000_sleep,

	.set_frontend = dib8000_set_frontend,
	.get_tune_settings = dib8000_fe_get_tune_settings,
	.get_frontend = dib8000_get_frontend,

	.read_status = dib8000_read_status,
	.read_ber = dib8000_read_ber,
	.read_signal_strength = dib8000_read_signal_strength,
	.read_snr = dib8000_read_snr,
	.read_ucblocks = dib8000_read_unc_blocks,
};

struct dvb_frontend *dib8000_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib8000_config *cfg)
{
	struct dvb_frontend *fe;
	struct dib8000_state *state;

	dprintk("dib8000_attach");

	state = kzalloc(sizeof(struct dib8000_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;
	fe = kzalloc(sizeof(struct dvb_frontend), GFP_KERNEL);
	if (fe == NULL)
		goto error;

	memcpy(&state->cfg, cfg, sizeof(struct dib8000_config));
	state->i2c.adap = i2c_adap;
	state->i2c.addr = i2c_addr;
	state->gpio_val = cfg->gpio_val;
	state->gpio_dir = cfg->gpio_dir;

	/* Ensure the output mode remains at the previous default if it's
	 * not specifically set by the caller.
	 */
	if ((state->cfg.output_mode != OUTMODE_MPEG2_SERIAL) && (state->cfg.output_mode != OUTMODE_MPEG2_PAR_GATED_CLK))
		state->cfg.output_mode = OUTMODE_MPEG2_FIFO;

	state->fe[0] = fe;
	fe->demodulator_priv = state;
	memcpy(&state->fe[0]->ops, &dib8000_ops, sizeof(struct dvb_frontend_ops));

	state->timf_default = cfg->pll->timf;

	if (dib8000_identify(&state->i2c) == 0)
		goto error;

	dibx000_init_i2c_master(&state->i2c_master, DIB8000, state->i2c.adap, state->i2c.addr);

	dib8000_reset(fe);

	dib8000_write_word(state, 285, (dib8000_read_word(state, 285) & ~0x60) | (3 << 5));	/* ber_rs_len = 3 */

	return fe;

 error:
	kfree(state);
	return NULL;
}

EXPORT_SYMBOL(dib8000_attach);

MODULE_AUTHOR("Olivier Grenie <Olivier.Grenie@dibcom.fr, " "Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Driver for the DiBcom 8000 ISDB-T demodulator");
MODULE_LICENSE("GPL");
