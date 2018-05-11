/*
 * Linux-DVB Driver for DiBcom's DiB8000 chip (ISDB-T).
 *
 * Copyright (C) 2009 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <asm/div64.h>

#include <media/dvb_math.h>

#include <media/dvb_frontend.h>

#include "dib8000.h"

#define LAYER_ALL -1
#define LAYER_A   1
#define LAYER_B   2
#define LAYER_C   3

#define MAX_NUMBER_OF_FRONTENDS 6
/* #define DIB8000_AGC_FREEZE */

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(fmt, arg...) do {					\
	if (debug)							\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

struct i2c_device {
	struct i2c_adapter *adap;
	u8 addr;
	u8 *i2c_write_buffer;
	u8 *i2c_read_buffer;
	struct mutex *i2c_buffer_lock;
};

enum param_loop_step {
	LOOP_TUNE_1,
	LOOP_TUNE_2
};

enum dib8000_autosearch_step {
	AS_START = 0,
	AS_SEARCHING_FFT,
	AS_SEARCHING_GUARD,
	AS_DONE = 100,
};

enum timeout_mode {
	SYMBOL_DEPENDENT_OFF = 0,
	SYMBOL_DEPENDENT_ON,
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
	s32 status;

	struct dvb_frontend *fe[MAX_NUMBER_OF_FRONTENDS];

	/* for the I2C transfer */
	struct i2c_msg msg[2];
	u8 i2c_write_buffer[4];
	u8 i2c_read_buffer[2];
	struct mutex i2c_buffer_lock;
	u8 input_mode_mpeg;

	u16 tuner_enable;
	struct i2c_adapter dib8096p_tuner_adap;
	u16 current_demod_bw;

	u16 seg_mask;
	u16 seg_diff_mask;
	u16 mode;
	u8 layer_b_nb_seg;
	u8 layer_c_nb_seg;

	u8 channel_parameters_set;
	u16 autosearch_state;
	u16 found_nfft;
	u16 found_guard;
	u8 subchannel;
	u8 symbol_duration;
	unsigned long timeout;
	u8 longest_intlv_layer;
	u16 output_mode;

	/* for DVBv5 stats */
	s64 init_ucb;
	unsigned long per_jiffies_stats;
	unsigned long ber_jiffies_stats;
	unsigned long ber_jiffies_stats_layer[3];

#ifdef DIB8000_AGC_FREEZE
	u16 agc1_max;
	u16 agc1_min;
	u16 agc2_max;
	u16 agc2_min;
#endif
};

enum dib8000_power_mode {
	DIB8000_POWER_ALL = 0,
	DIB8000_POWER_INTERFACE_ONLY,
};

static u16 dib8000_i2c_read16(struct i2c_device *i2c, u16 reg)
{
	u16 ret;
	struct i2c_msg msg[2] = {
		{.addr = i2c->addr >> 1, .flags = 0, .len = 2},
		{.addr = i2c->addr >> 1, .flags = I2C_M_RD, .len = 2},
	};

	if (mutex_lock_interruptible(i2c->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return 0;
	}

	msg[0].buf    = i2c->i2c_write_buffer;
	msg[0].buf[0] = reg >> 8;
	msg[0].buf[1] = reg & 0xff;
	msg[1].buf    = i2c->i2c_read_buffer;

	if (i2c_transfer(i2c->adap, msg, 2) != 2)
		dprintk("i2c read error on %d\n", reg);

	ret = (msg[1].buf[0] << 8) | msg[1].buf[1];
	mutex_unlock(i2c->i2c_buffer_lock);
	return ret;
}

static u16 __dib8000_read_word(struct dib8000_state *state, u16 reg)
{
	u16 ret;

	state->i2c_write_buffer[0] = reg >> 8;
	state->i2c_write_buffer[1] = reg & 0xff;

	memset(state->msg, 0, 2 * sizeof(struct i2c_msg));
	state->msg[0].addr = state->i2c.addr >> 1;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 2;
	state->msg[1].addr = state->i2c.addr >> 1;
	state->msg[1].flags = I2C_M_RD;
	state->msg[1].buf = state->i2c_read_buffer;
	state->msg[1].len = 2;

	if (i2c_transfer(state->i2c.adap, state->msg, 2) != 2)
		dprintk("i2c read error on %d\n", reg);

	ret = (state->i2c_read_buffer[0] << 8) | state->i2c_read_buffer[1];

	return ret;
}

static u16 dib8000_read_word(struct dib8000_state *state, u16 reg)
{
	u16 ret;

	if (mutex_lock_interruptible(&state->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return 0;
	}

	ret = __dib8000_read_word(state, reg);

	mutex_unlock(&state->i2c_buffer_lock);

	return ret;
}

static u32 dib8000_read32(struct dib8000_state *state, u16 reg)
{
	u16 rw[2];

	if (mutex_lock_interruptible(&state->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return 0;
	}

	rw[0] = __dib8000_read_word(state, reg + 0);
	rw[1] = __dib8000_read_word(state, reg + 1);

	mutex_unlock(&state->i2c_buffer_lock);

	return ((rw[0] << 16) | (rw[1]));
}

static int dib8000_i2c_write16(struct i2c_device *i2c, u16 reg, u16 val)
{
	struct i2c_msg msg = {.addr = i2c->addr >> 1, .flags = 0, .len = 4};
	int ret = 0;

	if (mutex_lock_interruptible(i2c->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}

	msg.buf    = i2c->i2c_write_buffer;
	msg.buf[0] = (reg >> 8) & 0xff;
	msg.buf[1] = reg & 0xff;
	msg.buf[2] = (val >> 8) & 0xff;
	msg.buf[3] = val & 0xff;

	ret = i2c_transfer(i2c->adap, &msg, 1) != 1 ? -EREMOTEIO : 0;
	mutex_unlock(i2c->i2c_buffer_lock);

	return ret;
}

static int dib8000_write_word(struct dib8000_state *state, u16 reg, u16 val)
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
	state->msg[0].addr = state->i2c.addr >> 1;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 4;

	ret = (i2c_transfer(state->i2c.adap, state->msg, 1) != 1 ?
			-EREMOTEIO : 0);
	mutex_unlock(&state->i2c_buffer_lock);

	return ret;
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
	dprintk("acquisition mode activated\n");
	dib8000_write_word(state, 298, nud);
}
static int dib8000_set_output_mode(struct dvb_frontend *fe, int mode)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 outreg, fifo_threshold, smo_mode, sram = 0x0205;	/* by default SDRAM deintlv is enabled */

	state->output_mode = mode;
	outreg = 0;
	fifo_threshold = 1792;
	smo_mode = (dib8000_read_word(state, 299) & 0x0050) | (1 << 1);

	dprintk("-I-	Setting output mode for demod %p to %d\n",
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
		dprintk("Unhandled output_mode passed to be set for demod %p\n",
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
	u16 tmp, sync_wait = dib8000_read_word(state, 273) & 0xfff0;

	dprintk("set diversity input to %i\n", onoff);
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

	if (state->revision == 0x8002) {
		tmp = dib8000_read_word(state, 903);
		dib8000_write_word(state, 903, tmp & ~(1 << 3));
		msleep(30);
		dib8000_write_word(state, 903, tmp | (1 << 3));
	}
	return 0;
}

static void dib8000_set_power_mode(struct dib8000_state *state, enum dib8000_power_mode mode)
{
	/* by default everything is going to be powered off */
	u16 reg_774 = 0x3fff, reg_775 = 0xffff, reg_776 = 0xffff,
		reg_900 = (dib8000_read_word(state, 900) & 0xfffc) | 0x3,
		reg_1280;

	if (state->revision != 0x8090)
		reg_1280 = (dib8000_read_word(state, 1280) & 0x00ff) | 0xff00;
	else
		reg_1280 = (dib8000_read_word(state, 1280) & 0x707f) | 0x8f80;

	/* now, depending on the requested mode, we power on */
	switch (mode) {
		/* power up everything in the demod */
	case DIB8000_POWER_ALL:
		reg_774 = 0x0000;
		reg_775 = 0x0000;
		reg_776 = 0x0000;
		reg_900 &= 0xfffc;
		if (state->revision != 0x8090)
			reg_1280 &= 0x00ff;
		else
			reg_1280 &= 0x707f;
		break;
	case DIB8000_POWER_INTERFACE_ONLY:
		if (state->revision != 0x8090)
			reg_1280 &= 0x00ff;
		else
			reg_1280 &= 0xfa7b;
		break;
	}

	dprintk("powermode : 774 : %x ; 775 : %x; 776 : %x ; 900 : %x; 1280 : %x\n", reg_774, reg_775, reg_776, reg_900, reg_1280);
	dib8000_write_word(state, 774, reg_774);
	dib8000_write_word(state, 775, reg_775);
	dib8000_write_word(state, 776, reg_776);
	dib8000_write_word(state, 900, reg_900);
	dib8000_write_word(state, 1280, reg_1280);
}

static int dib8000_set_adc_state(struct dib8000_state *state, enum dibx000_adc_states no)
{
	int ret = 0;
	u16 reg, reg_907 = dib8000_read_word(state, 907);
	u16 reg_908 = dib8000_read_word(state, 908);

	switch (no) {
	case DIBX000_SLOW_ADC_ON:
		if (state->revision != 0x8090) {
			reg_908 |= (1 << 1) | (1 << 0);
			ret |= dib8000_write_word(state, 908, reg_908);
			reg_908 &= ~(1 << 1);
		} else {
			reg = dib8000_read_word(state, 1925);
			/* en_slowAdc = 1 & reset_sladc = 1 */
			dib8000_write_word(state, 1925, reg |
					(1<<4) | (1<<2));

			/* read acces to make it works... strange ... */
			reg = dib8000_read_word(state, 1925);
			msleep(20);
			/* en_slowAdc = 1 & reset_sladc = 0 */
			dib8000_write_word(state, 1925, reg & ~(1<<4));

			reg = dib8000_read_word(state, 921) & ~((0x3 << 14)
					| (0x3 << 12));
			/* ref = Vin1 => Vbg ; sel = Vin0 or Vin3 ;
			   (Vin2 = Vcm) */
			dib8000_write_word(state, 921, reg | (1 << 14)
					| (3 << 12));
		}
		break;

	case DIBX000_SLOW_ADC_OFF:
		if (state->revision == 0x8090) {
			reg = dib8000_read_word(state, 1925);
			/* reset_sladc = 1 en_slowAdc = 0 */
			dib8000_write_word(state, 1925,
					(reg & ~(1<<2)) | (1<<4));
		}
		reg_908 |= (1 << 1) | (1 << 0);
		break;

	case DIBX000_ADC_ON:
		reg_907 &= 0x0fff;
		reg_908 &= 0x0003;
		break;

	case DIBX000_ADC_OFF:	// leave the VBG voltage on
		reg_907 = (1 << 13) | (1 << 12);
		reg_908 = (1 << 6) | (1 << 5) | (1 << 4) | (1 << 3) | (1 << 1);
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
		dprintk("using default timf\n");
		timf = state->timf_default;
	} else {
		dprintk("using updated timf\n");
		timf = state->timf;
	}

	dib8000_write_word(state, 29, (u16) ((timf >> 16) & 0xffff));
	dib8000_write_word(state, 30, (u16) ((timf) & 0xffff));

	return 0;
}

static int dib8000_sad_calib(struct dib8000_state *state)
{
	u8 sad_sel = 3;

	if (state->revision == 0x8090) {
		dib8000_write_word(state, 922, (sad_sel << 2));
		dib8000_write_word(state, 923, 2048);

		dib8000_write_word(state, 922, (sad_sel << 2) | 0x1);
		dib8000_write_word(state, 922, (sad_sel << 2));
	} else {
		/* internal */
		dib8000_write_word(state, 923, (0 << 1) | (0 << 0));
		dib8000_write_word(state, 924, 776);

		/* do the calibration */
		dib8000_write_word(state, 923, (1 << 0));
		dib8000_write_word(state, 923, (0 << 0));
	}

	msleep(1);
	return 0;
}

static int dib8000_set_wbd_ref(struct dvb_frontend *fe, u16 value)
{
	struct dib8000_state *state = fe->demodulator_priv;
	if (value > 4095)
		value = 4095;
	state->wbd_ref = value;
	return dib8000_write_word(state, 106, value);
}

static void dib8000_reset_pll_common(struct dib8000_state *state, const struct dibx000_bandwidth_config *bw)
{
	dprintk("ifreq: %d %x, inversion: %d\n", bw->ifreq, bw->ifreq, bw->ifreq >> 25);
	if (state->revision != 0x8090) {
		dib8000_write_word(state, 23,
				(u16) (((bw->internal * 1000) >> 16) & 0xffff));
		dib8000_write_word(state, 24,
				(u16) ((bw->internal * 1000) & 0xffff));
	} else {
		dib8000_write_word(state, 23, (u16) (((bw->internal / 2 * 1000) >> 16) & 0xffff));
		dib8000_write_word(state, 24,
				(u16) ((bw->internal  / 2 * 1000) & 0xffff));
	}
	dib8000_write_word(state, 27, (u16) ((bw->ifreq >> 16) & 0x01ff));
	dib8000_write_word(state, 28, (u16) (bw->ifreq & 0xffff));
	dib8000_write_word(state, 26, (u16) ((bw->ifreq >> 25) & 0x0003));

	if (state->revision != 0x8090)
		dib8000_write_word(state, 922, bw->sad_cfg);
}

static void dib8000_reset_pll(struct dib8000_state *state)
{
	const struct dibx000_bandwidth_config *pll = state->cfg.pll;
	u16 clk_cfg1, reg;

	if (state->revision != 0x8090) {
		dib8000_write_word(state, 901,
				(pll->pll_prediv << 8) | (pll->pll_ratio << 0));

		clk_cfg1 = (1 << 10) | (0 << 9) | (pll->IO_CLK_en_core << 8) |
			(pll->bypclk_div << 5) | (pll->enable_refdiv << 4) |
			(1 << 3) | (pll->pll_range << 1) |
			(pll->pll_reset << 0);

		dib8000_write_word(state, 902, clk_cfg1);
		clk_cfg1 = (clk_cfg1 & 0xfff7) | (pll->pll_bypass << 3);
		dib8000_write_word(state, 902, clk_cfg1);

		dprintk("clk_cfg1: 0x%04x\n", clk_cfg1);

		/* smpl_cfg: P_refclksel=2, P_ensmplsel=1 nodivsmpl=1 */
		if (state->cfg.pll->ADClkSrc == 0)
			dib8000_write_word(state, 904,
					(0 << 15) | (0 << 12) | (0 << 10) |
					(pll->modulo << 8) |
					(pll->ADClkSrc << 7) | (0 << 1));
		else if (state->cfg.refclksel != 0)
			dib8000_write_word(state, 904, (0 << 15) | (1 << 12) |
					((state->cfg.refclksel & 0x3) << 10) |
					(pll->modulo << 8) |
					(pll->ADClkSrc << 7) | (0 << 1));
		else
			dib8000_write_word(state, 904, (0 << 15) | (1 << 12) |
					(3 << 10) | (pll->modulo << 8) |
					(pll->ADClkSrc << 7) | (0 << 1));
	} else {
		dib8000_write_word(state, 1856, (!pll->pll_reset<<13) |
				(pll->pll_range<<12) | (pll->pll_ratio<<6) |
				(pll->pll_prediv));

		reg = dib8000_read_word(state, 1857);
		dib8000_write_word(state, 1857, reg|(!pll->pll_bypass<<15));

		reg = dib8000_read_word(state, 1858); /* Force clk out pll /2 */
		dib8000_write_word(state, 1858, reg | 1);

		dib8000_write_word(state, 904, (pll->modulo << 8));
	}

	dib8000_reset_pll_common(state, pll);
}

static int dib8000_update_pll(struct dvb_frontend *fe,
		struct dibx000_bandwidth_config *pll, u32 bw, u8 ratio)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 reg_1857, reg_1856 = dib8000_read_word(state, 1856);
	u8 loopdiv, prediv, oldprediv = state->cfg.pll->pll_prediv ;
	u32 internal, xtal;

	/* get back old values */
	prediv = reg_1856 & 0x3f;
	loopdiv = (reg_1856 >> 6) & 0x3f;

	if ((pll == NULL) || (pll->pll_prediv == prediv &&
				pll->pll_ratio == loopdiv))
		return -EINVAL;

	dprintk("Updating pll (prediv: old =  %d new = %d ; loopdiv : old = %d new = %d)\n", prediv, pll->pll_prediv, loopdiv, pll->pll_ratio);
	if (state->revision == 0x8090) {
		reg_1856 &= 0xf000;
		reg_1857 = dib8000_read_word(state, 1857);
		/* disable PLL */
		dib8000_write_word(state, 1857, reg_1857 & ~(1 << 15));

		dib8000_write_word(state, 1856, reg_1856 |
				((pll->pll_ratio & 0x3f) << 6) |
				(pll->pll_prediv & 0x3f));

		/* write new system clk into P_sec_len */
		internal = dib8000_read32(state, 23) / 1000;
		dprintk("Old Internal = %d\n", internal);
		xtal = 2 * (internal / loopdiv) * prediv;
		internal = 1000 * (xtal/pll->pll_prediv) * pll->pll_ratio;
		dprintk("Xtal = %d , New Fmem = %d New Fdemod = %d, New Fsampling = %d\n", xtal, internal/1000, internal/2000, internal/8000);
		dprintk("New Internal = %d\n", internal);

		dib8000_write_word(state, 23,
				(u16) (((internal / 2) >> 16) & 0xffff));
		dib8000_write_word(state, 24, (u16) ((internal / 2) & 0xffff));
		/* enable PLL */
		dib8000_write_word(state, 1857, reg_1857 | (1 << 15));

		while (((dib8000_read_word(state, 1856)>>15)&0x1) != 1)
			dprintk("Waiting for PLL to lock\n");

		/* verify */
		reg_1856 = dib8000_read_word(state, 1856);
		dprintk("PLL Updated with prediv = %d and loopdiv = %d\n",
				reg_1856&0x3f, (reg_1856>>6)&0x3f);
	} else {
		if (bw != state->current_demod_bw) {
			/** Bandwidth change => force PLL update **/
			dprintk("PLL: Bandwidth Change %d MHz -> %d MHz (prediv: %d->%d)\n", state->current_demod_bw / 1000, bw / 1000, oldprediv, state->cfg.pll->pll_prediv);

			if (state->cfg.pll->pll_prediv != oldprediv) {
				/** Full PLL change only if prediv is changed **/

				/** full update => bypass and reconfigure **/
				dprintk("PLL: New Setting for %d MHz Bandwidth (prediv: %d, ratio: %d)\n", bw/1000, state->cfg.pll->pll_prediv, state->cfg.pll->pll_ratio);
				dib8000_write_word(state, 902, dib8000_read_word(state, 902) | (1<<3)); /* bypass PLL */
				dib8000_reset_pll(state);
				dib8000_write_word(state, 898, 0x0004); /* sad */
			} else
				ratio = state->cfg.pll->pll_ratio;

			state->current_demod_bw = bw;
		}

		if (ratio != 0) {
			/** ratio update => only change ratio **/
			dprintk("PLL: Update ratio (prediv: %d, ratio: %d)\n", state->cfg.pll->pll_prediv, ratio);
			dib8000_write_word(state, 901, (state->cfg.pll->pll_prediv << 8) | (ratio << 0)); /* only the PLL ratio is updated. */
		}
	}

	return 0;
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

	dprintk("gpio dir: %x: gpio val: %x\n", st->cfg.gpio_dir, st->cfg.gpio_val);

	return 0;
}

static int dib8000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	struct dib8000_state *state = fe->demodulator_priv;
	return dib8000_cfg_gpio(state, num, dir, val);
}

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

	0,
};

static u16 dib8000_identify(struct i2c_device *client)
{
	u16 value;

	//because of glitches sometimes
	value = dib8000_i2c_read16(client, 896);

	if ((value = dib8000_i2c_read16(client, 896)) != 0x01b3) {
		dprintk("wrong Vendor ID (read=0x%x)\n", value);
		return 0;
	}

	value = dib8000_i2c_read16(client, 897);
	if (value != 0x8000 && value != 0x8001 &&
			value != 0x8002 && value != 0x8090) {
		dprintk("wrong Device ID (%x)\n", value);
		return 0;
	}

	switch (value) {
	case 0x8000:
		dprintk("found DiB8000A\n");
		break;
	case 0x8001:
		dprintk("found DiB8000B\n");
		break;
	case 0x8002:
		dprintk("found DiB8000C\n");
		break;
	case 0x8090:
		dprintk("found DiB8096P\n");
		break;
	}
	return value;
}

static int dib8000_read_unc_blocks(struct dvb_frontend *fe, u32 *unc);

static void dib8000_reset_stats(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
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

	dib8000_read_unc_blocks(fe, &ucb);

	state->init_ucb = -ucb;
	state->ber_jiffies_stats = 0;
	state->per_jiffies_stats = 0;
	memset(&state->ber_jiffies_stats_layer, 0,
	       sizeof(state->ber_jiffies_stats_layer));
}

static int dib8000_reset(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;

	if ((state->revision = dib8000_identify(&state->i2c)) == 0)
		return -EINVAL;

	/* sram lead in, rdy */
	if (state->revision != 0x8090)
		dib8000_write_word(state, 1287, 0x0003);

	if (state->revision == 0x8000)
		dprintk("error : dib8000 MA not supported\n");

	dibx000_reset_i2c_master(&state->i2c_master);

	dib8000_set_power_mode(state, DIB8000_POWER_ALL);

	/* always leave the VBG voltage on - it consumes almost nothing but takes a long time to start */
	dib8000_set_adc_state(state, DIBX000_ADC_OFF);

	/* restart all parts */
	dib8000_write_word(state, 770, 0xffff);
	dib8000_write_word(state, 771, 0xffff);
	dib8000_write_word(state, 772, 0xfffc);
	dib8000_write_word(state, 898, 0x000c);	/* restart sad */
	if (state->revision == 0x8090)
		dib8000_write_word(state, 1280, 0x0045);
	else
		dib8000_write_word(state, 1280, 0x004d);
	dib8000_write_word(state, 1281, 0x000c);

	dib8000_write_word(state, 770, 0x0000);
	dib8000_write_word(state, 771, 0x0000);
	dib8000_write_word(state, 772, 0x0000);
	dib8000_write_word(state, 898, 0x0004);	// sad
	dib8000_write_word(state, 1280, 0x0000);
	dib8000_write_word(state, 1281, 0x0000);

	/* drives */
	if (state->revision != 0x8090) {
		if (state->cfg.drives)
			dib8000_write_word(state, 906, state->cfg.drives);
		else {
			dprintk("using standard PAD-drive-settings, please adjust settings in config-struct to be optimal.\n");
			/* min drive SDRAM - not optimal - adjust */
			dib8000_write_word(state, 906, 0x2d98);
		}
	}

	dib8000_reset_pll(state);
	if (state->revision != 0x8090)
		dib8000_write_word(state, 898, 0x0004);

	if (dib8000_reset_gpio(state) != 0)
		dprintk("GPIO reset was not successful.\n");

	if ((state->revision != 0x8090) &&
			(dib8000_set_output_mode(fe, OUTMODE_HIGH_Z) != 0))
		dprintk("OUTPUT_MODE could not be resetted.\n");

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
	if ((state->revision != 8090) && (state->cfg.div_cfg != 0))
		dib8000_write_word(state, 903, state->cfg.div_cfg);

	/* unforce divstr regardless whether i2c enumeration was done or not */
	dib8000_write_word(state, 1285, dib8000_read_word(state, 1285) & ~(1 << 1));

	dib8000_set_bandwidth(fe, 6000);

	dib8000_set_adc_state(state, DIBX000_SLOW_ADC_ON);
	dib8000_sad_calib(state);
	if (state->revision != 0x8090)
		dib8000_set_adc_state(state, DIBX000_SLOW_ADC_OFF);

	/* ber_rs_len = 3 */
	dib8000_write_word(state, 285, (dib8000_read_word(state, 285) & ~0x60) | (3 << 5));

	dib8000_set_power_mode(state, DIB8000_POWER_INTERFACE_ONLY);

	dib8000_reset_stats(fe);

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
	u16 reg;

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
	dib8000_write_word(state, 76, agc->setup);
	dib8000_write_word(state, 77, agc->inv_gain);
	dib8000_write_word(state, 78, agc->time_stabiliz);
	dib8000_write_word(state, 101, (agc->alpha_level << 12) | agc->thlock);

	// Demod AGC loop configuration
	dib8000_write_word(state, 102, (agc->alpha_mant << 5) | agc->alpha_exp);
	dib8000_write_word(state, 103, (agc->beta_mant << 6) | agc->beta_exp);

	dprintk("WBD: ref: %d, sel: %d, active: %d, alpha: %d\n",
		state->wbd_ref != 0 ? state->wbd_ref : agc->wbd_ref, agc->wbd_sel, !agc->perform_agc_softsplit, agc->wbd_sel);

	/* AGC continued */
	if (state->wbd_ref != 0)
		dib8000_write_word(state, 106, state->wbd_ref);
	else			// use default
		dib8000_write_word(state, 106, agc->wbd_ref);

	if (state->revision == 0x8090) {
		reg = dib8000_read_word(state, 922) & (0x3 << 2);
		dib8000_write_word(state, 922, reg | (agc->wbd_sel << 2));
	}

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
	if (state->revision != 0x8090)
		dib8000_write_word(state, 923,
				(dib8000_read_word(state, 923) & 0xffe3) |
				(agc->wbd_inv << 4) | (agc->wbd_sel << 2));

	return 0;
}

static void dib8000_pwm_agc_reset(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	dib8000_set_adc_state(state, DIBX000_ADC_ON);
	dib8000_set_agc_config(state, (unsigned char)(BAND_OF_FREQUENCY(fe->dtv_property_cache.frequency / 1000)));
}

static int dib8000_agc_soft_split(struct dib8000_state *state)
{
	u16 agc, split_offset;

	if (!state->current_agc || !state->current_agc->perform_agc_softsplit || state->current_agc->split.max == 0)
		return 0;

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

	dprintk("AGC split_offset: %d\n", split_offset);

	// P_agc_force_split and P_agc_split_offset
	dib8000_write_word(state, 107, (dib8000_read_word(state, 107) & 0xff00) | split_offset);
	return 5000;
}

static int dib8000_agc_startup(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	enum frontend_tune_state *tune_state = &state->tune_state;
	int ret = 0;
	u16 reg;
	u32 upd_demod_gain_period = 0x8000;

	switch (*tune_state) {
	case CT_AGC_START:
		// set power-up level: interf+analog+AGC

		if (state->revision != 0x8090)
			dib8000_set_adc_state(state, DIBX000_ADC_ON);
		else {
			dib8000_set_power_mode(state, DIB8000_POWER_ALL);

			reg = dib8000_read_word(state, 1947)&0xff00;
			dib8000_write_word(state, 1946,
					upd_demod_gain_period & 0xFFFF);
			/* bit 14 = enDemodGain */
			dib8000_write_word(state, 1947, reg | (1<<14) |
					((upd_demod_gain_period >> 16) & 0xFF));

			/* enable adc i & q */
			reg = dib8000_read_word(state, 1920);
			dib8000_write_word(state, 1920, (reg | 0x3) &
					(~(1 << 7)));
		}

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

static void dib8096p_host_bus_drive(struct dib8000_state *state, u8 drive)
{
	u16 reg;

	drive &= 0x7;

	/* drive host bus 2, 3, 4 */
	reg = dib8000_read_word(state, 1798) &
		~(0x7 | (0x7 << 6) | (0x7 << 12));
	reg |= (drive<<12) | (drive<<6) | drive;
	dib8000_write_word(state, 1798, reg);

	/* drive host bus 5,6 */
	reg = dib8000_read_word(state, 1799) & ~((0x7 << 2) | (0x7 << 8));
	reg |= (drive<<8) | (drive<<2);
	dib8000_write_word(state, 1799, reg);

	/* drive host bus 7, 8, 9 */
	reg = dib8000_read_word(state, 1800) &
		~(0x7 | (0x7 << 6) | (0x7 << 12));
	reg |= (drive<<12) | (drive<<6) | drive;
	dib8000_write_word(state, 1800, reg);

	/* drive host bus 10, 11 */
	reg = dib8000_read_word(state, 1801) & ~((0x7 << 2) | (0x7 << 8));
	reg |= (drive<<8) | (drive<<2);
	dib8000_write_word(state, 1801, reg);

	/* drive host bus 12, 13, 14 */
	reg = dib8000_read_word(state, 1802) &
		~(0x7 | (0x7 << 6) | (0x7 << 12));
	reg |= (drive<<12) | (drive<<6) | drive;
	dib8000_write_word(state, 1802, reg);
}

static u32 dib8096p_calcSyncFreq(u32 P_Kin, u32 P_Kout,
		u32 insertExtSynchro, u32 syncSize)
{
	u32 quantif = 3;
	u32 nom = (insertExtSynchro * P_Kin+syncSize);
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

static void dib8096p_cfg_DibTx(struct dib8000_state *state, u32 P_Kin,
		u32 P_Kout, u32 insertExtSynchro, u32 synchroMode,
		u32 syncWord, u32 syncSize)
{
	dprintk("Configure DibStream Tx\n");

	dib8000_write_word(state, 1615, 1);
	dib8000_write_word(state, 1603, P_Kin);
	dib8000_write_word(state, 1605, P_Kout);
	dib8000_write_word(state, 1606, insertExtSynchro);
	dib8000_write_word(state, 1608, synchroMode);
	dib8000_write_word(state, 1609, (syncWord >> 16) & 0xffff);
	dib8000_write_word(state, 1610, syncWord & 0xffff);
	dib8000_write_word(state, 1612, syncSize);
	dib8000_write_word(state, 1615, 0);
}

static void dib8096p_cfg_DibRx(struct dib8000_state *state, u32 P_Kin,
		u32 P_Kout, u32 synchroMode, u32 insertExtSynchro,
		u32 syncWord, u32 syncSize, u32 dataOutRate)
{
	u32 syncFreq;

	dprintk("Configure DibStream Rx synchroMode = %d\n", synchroMode);

	if ((P_Kin != 0) && (P_Kout != 0)) {
		syncFreq = dib8096p_calcSyncFreq(P_Kin, P_Kout,
				insertExtSynchro, syncSize);
		dib8000_write_word(state, 1542, syncFreq);
	}

	dib8000_write_word(state, 1554, 1);
	dib8000_write_word(state, 1536, P_Kin);
	dib8000_write_word(state, 1537, P_Kout);
	dib8000_write_word(state, 1539, synchroMode);
	dib8000_write_word(state, 1540, (syncWord >> 16) & 0xffff);
	dib8000_write_word(state, 1541, syncWord & 0xffff);
	dib8000_write_word(state, 1543, syncSize);
	dib8000_write_word(state, 1544, dataOutRate);
	dib8000_write_word(state, 1554, 0);
}

static void dib8096p_enMpegMux(struct dib8000_state *state, int onoff)
{
	u16 reg_1287;

	reg_1287 = dib8000_read_word(state, 1287);

	switch (onoff) {
	case 1:
			reg_1287 &= ~(1 << 8);
			break;
	case 0:
			reg_1287 |= (1 << 8);
			break;
	}

	dib8000_write_word(state, 1287, reg_1287);
}

static void dib8096p_configMpegMux(struct dib8000_state *state,
		u16 pulseWidth, u16 enSerialMode, u16 enSerialClkDiv2)
{
	u16 reg_1287;

	dprintk("Enable Mpeg mux\n");

	dib8096p_enMpegMux(state, 0);

	/* If the input mode is MPEG do not divide the serial clock */
	if ((enSerialMode == 1) && (state->input_mode_mpeg == 1))
		enSerialClkDiv2 = 0;

	reg_1287 = ((pulseWidth & 0x1f) << 3) |
		((enSerialMode & 0x1) << 2) | (enSerialClkDiv2 & 0x1);
	dib8000_write_word(state, 1287, reg_1287);

	dib8096p_enMpegMux(state, 1);
}

static void dib8096p_setDibTxMux(struct dib8000_state *state, int mode)
{
	u16 reg_1288 = dib8000_read_word(state, 1288) & ~(0x7 << 7);

	switch (mode) {
	case MPEG_ON_DIBTX:
			dprintk("SET MPEG ON DIBSTREAM TX\n");
			dib8096p_cfg_DibTx(state, 8, 5, 0, 0, 0, 0);
			reg_1288 |= (1 << 9); break;
	case DIV_ON_DIBTX:
			dprintk("SET DIV_OUT ON DIBSTREAM TX\n");
			dib8096p_cfg_DibTx(state, 5, 5, 0, 0, 0, 0);
			reg_1288 |= (1 << 8); break;
	case ADC_ON_DIBTX:
			dprintk("SET ADC_OUT ON DIBSTREAM TX\n");
			dib8096p_cfg_DibTx(state, 20, 5, 10, 0, 0, 0);
			reg_1288 |= (1 << 7); break;
	default:
			break;
	}
	dib8000_write_word(state, 1288, reg_1288);
}

static void dib8096p_setHostBusMux(struct dib8000_state *state, int mode)
{
	u16 reg_1288 = dib8000_read_word(state, 1288) & ~(0x7 << 4);

	switch (mode) {
	case DEMOUT_ON_HOSTBUS:
			dprintk("SET DEM OUT OLD INTERF ON HOST BUS\n");
			dib8096p_enMpegMux(state, 0);
			reg_1288 |= (1 << 6);
			break;
	case DIBTX_ON_HOSTBUS:
			dprintk("SET DIBSTREAM TX ON HOST BUS\n");
			dib8096p_enMpegMux(state, 0);
			reg_1288 |= (1 << 5);
			break;
	case MPEG_ON_HOSTBUS:
			dprintk("SET MPEG MUX ON HOST BUS\n");
			reg_1288 |= (1 << 4);
			break;
	default:
			break;
	}
	dib8000_write_word(state, 1288, reg_1288);
}

static int dib8096p_set_diversity_in(struct dvb_frontend *fe, int onoff)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 reg_1287;

	switch (onoff) {
	case 0: /* only use the internal way - not the diversity input */
			dprintk("%s mode OFF : by default Enable Mpeg INPUT\n",
					__func__);
			/* outputRate = 8 */
			dib8096p_cfg_DibRx(state, 8, 5, 0, 0, 0, 8, 0);

			/* Do not divide the serial clock of MPEG MUX in
			   SERIAL MODE in case input mode MPEG is used */
			reg_1287 = dib8000_read_word(state, 1287);
			/* enSerialClkDiv2 == 1 ? */
			if ((reg_1287 & 0x1) == 1) {
				/* force enSerialClkDiv2 = 0 */
				reg_1287 &= ~0x1;
				dib8000_write_word(state, 1287, reg_1287);
			}
			state->input_mode_mpeg = 1;
			break;
	case 1: /* both ways */
	case 2: /* only the diversity input */
			dprintk("%s ON : Enable diversity INPUT\n", __func__);
			dib8096p_cfg_DibRx(state, 5, 5, 0, 0, 0, 0, 0);
			state->input_mode_mpeg = 0;
			break;
	}

	dib8000_set_diversity_in(state->fe[0], onoff);
	return 0;
}

static int dib8096p_set_output_mode(struct dvb_frontend *fe, int mode)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 outreg, smo_mode, fifo_threshold;
	u8 prefer_mpeg_mux_use = 1;
	int ret = 0;

	state->output_mode = mode;
	dib8096p_host_bus_drive(state, 1);

	fifo_threshold = 1792;
	smo_mode = (dib8000_read_word(state, 299) & 0x0050) | (1 << 1);
	outreg   = dib8000_read_word(state, 1286) &
		~((1 << 10) | (0x7 << 6) | (1 << 1));

	switch (mode) {
	case OUTMODE_HIGH_Z:
			outreg = 0;
			break;

	case OUTMODE_MPEG2_SERIAL:
			if (prefer_mpeg_mux_use) {
				dprintk("dib8096P setting output mode TS_SERIAL using Mpeg Mux\n");
				dib8096p_configMpegMux(state, 3, 1, 1);
				dib8096p_setHostBusMux(state, MPEG_ON_HOSTBUS);
			} else {/* Use Smooth block */
				dprintk("dib8096P setting output mode TS_SERIAL using Smooth bloc\n");
				dib8096p_setHostBusMux(state,
						DEMOUT_ON_HOSTBUS);
				outreg |= (2 << 6) | (0 << 1);
			}
			break;

	case OUTMODE_MPEG2_PAR_GATED_CLK:
			if (prefer_mpeg_mux_use) {
				dprintk("dib8096P setting output mode TS_PARALLEL_GATED using Mpeg Mux\n");
				dib8096p_configMpegMux(state, 2, 0, 0);
				dib8096p_setHostBusMux(state, MPEG_ON_HOSTBUS);
			} else { /* Use Smooth block */
				dprintk("dib8096P setting output mode TS_PARALLEL_GATED using Smooth block\n");
				dib8096p_setHostBusMux(state,
						DEMOUT_ON_HOSTBUS);
				outreg |= (0 << 6);
			}
			break;

	case OUTMODE_MPEG2_PAR_CONT_CLK: /* Using Smooth block only */
			dprintk("dib8096P setting output mode TS_PARALLEL_CONT using Smooth block\n");
			dib8096p_setHostBusMux(state, DEMOUT_ON_HOSTBUS);
			outreg |= (1 << 6);
			break;

	case OUTMODE_MPEG2_FIFO:
			/* Using Smooth block because not supported
			   by new Mpeg Mux bloc */
			dprintk("dib8096P setting output mode TS_FIFO using Smooth block\n");
			dib8096p_setHostBusMux(state, DEMOUT_ON_HOSTBUS);
			outreg |= (5 << 6);
			smo_mode |= (3 << 1);
			fifo_threshold = 512;
			break;

	case OUTMODE_DIVERSITY:
			dprintk("dib8096P setting output mode MODE_DIVERSITY\n");
			dib8096p_setDibTxMux(state, DIV_ON_DIBTX);
			dib8096p_setHostBusMux(state, DIBTX_ON_HOSTBUS);
			break;

	case OUTMODE_ANALOG_ADC:
			dprintk("dib8096P setting output mode MODE_ANALOG_ADC\n");
			dib8096p_setDibTxMux(state, ADC_ON_DIBTX);
			dib8096p_setHostBusMux(state, DIBTX_ON_HOSTBUS);
			break;
	}

	if (mode != OUTMODE_HIGH_Z)
		outreg |= (1<<10);

	dprintk("output_mpeg2_in_188_bytes = %d\n",
			state->cfg.output_mpeg2_in_188_bytes);
	if (state->cfg.output_mpeg2_in_188_bytes)
		smo_mode |= (1 << 5);

	ret |= dib8000_write_word(state, 299, smo_mode);
	/* synchronous fread */
	ret |= dib8000_write_word(state, 299 + 1, fifo_threshold);
	ret |= dib8000_write_word(state, 1286, outreg);

	return ret;
}

static int map_addr_to_serpar_number(struct i2c_msg *msg)
{
	if (msg->buf[0] <= 15)
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
	else if (msg->buf[0] == 99)
		msg->buf[0] = 99;
	else
		return -EINVAL;
	return 0;
}

static int dib8096p_tuner_write_serpar(struct i2c_adapter *i2c_adap,
		struct i2c_msg msg[], int num)
{
	struct dib8000_state *state = i2c_get_adapdata(i2c_adap);
	u8 n_overflow = 1;
	u16 i = 1000;
	u16 serpar_num = msg[0].buf[0];

	while (n_overflow == 1 && i) {
		n_overflow = (dib8000_read_word(state, 1984) >> 1) & 0x1;
		i--;
		if (i == 0)
			dprintk("Tuner ITF: write busy (overflow)\n");
	}
	dib8000_write_word(state, 1985, (1 << 6) | (serpar_num & 0x3f));
	dib8000_write_word(state, 1986, (msg[0].buf[1] << 8) | msg[0].buf[2]);

	return num;
}

static int dib8096p_tuner_read_serpar(struct i2c_adapter *i2c_adap,
		struct i2c_msg msg[], int num)
{
	struct dib8000_state *state = i2c_get_adapdata(i2c_adap);
	u8 n_overflow = 1, n_empty = 1;
	u16 i = 1000;
	u16 serpar_num = msg[0].buf[0];
	u16 read_word;

	while (n_overflow == 1 && i) {
		n_overflow = (dib8000_read_word(state, 1984) >> 1) & 0x1;
		i--;
		if (i == 0)
			dprintk("TunerITF: read busy (overflow)\n");
	}
	dib8000_write_word(state, 1985, (0<<6) | (serpar_num&0x3f));

	i = 1000;
	while (n_empty == 1 && i) {
		n_empty = dib8000_read_word(state, 1984)&0x1;
		i--;
		if (i == 0)
			dprintk("TunerITF: read busy (empty)\n");
	}

	read_word = dib8000_read_word(state, 1987);
	msg[1].buf[0] = (read_word >> 8) & 0xff;
	msg[1].buf[1] = (read_word) & 0xff;

	return num;
}

static int dib8096p_tuner_rw_serpar(struct i2c_adapter *i2c_adap,
		struct i2c_msg msg[], int num)
{
	if (map_addr_to_serpar_number(&msg[0]) == 0) {
		if (num == 1) /* write */
			return dib8096p_tuner_write_serpar(i2c_adap, msg, 1);
		else /* read */
			return dib8096p_tuner_read_serpar(i2c_adap, msg, 2);
	}
	return num;
}

static int dib8096p_rw_on_apb(struct i2c_adapter *i2c_adap,
		struct i2c_msg msg[], int num, u16 apb_address)
{
	struct dib8000_state *state = i2c_get_adapdata(i2c_adap);
	u16 word;

	if (num == 1) {		/* write */
		dib8000_write_word(state, apb_address,
				((msg[0].buf[1] << 8) | (msg[0].buf[2])));
	} else {
		word = dib8000_read_word(state, apb_address);
		msg[1].buf[0] = (word >> 8) & 0xff;
		msg[1].buf[1] = (word) & 0xff;
	}
	return num;
}

static int dib8096p_tuner_xfer(struct i2c_adapter *i2c_adap,
		struct i2c_msg msg[], int num)
{
	struct dib8000_state *state = i2c_get_adapdata(i2c_adap);
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
			apb_address = 936;
			break;
	case 0x26:
			apb_address = 937;
			break;
	case 0x27:
			apb_address = 938;
			break;
	case 0x28:
			apb_address = 939;
			break;
	case 0x1d:
			/* get sad sel request */
			i = ((dib8000_read_word(state, 921) >> 12)&0x3);
			word = dib8000_read_word(state, 924+i);
			msg[1].buf[0] = (word >> 8) & 0xff;
			msg[1].buf[1] = (word) & 0xff;
			return num;
	case 0x1f:
			if (num == 1) {	/* write */
				word = (u16) ((msg[0].buf[1] << 8) |
						msg[0].buf[2]);
				/* in the VGAMODE Sel are located on bit 0/1 */
				word &= 0x3;
				word = (dib8000_read_word(state, 921) &
						~(3<<12)) | (word<<12);
				/* Set the proper input */
				dib8000_write_word(state, 921, word);
				return num;
			}
	}

	if (apb_address != 0) /* R/W acces via APB */
		return dib8096p_rw_on_apb(i2c_adap, msg, num, apb_address);
	else  /* R/W access via SERPAR  */
		return dib8096p_tuner_rw_serpar(i2c_adap, msg, num);

	return 0;
}

static u32 dib8096p_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm dib8096p_tuner_xfer_algo = {
	.master_xfer = dib8096p_tuner_xfer,
	.functionality = dib8096p_i2c_func,
};

static struct i2c_adapter *dib8096p_get_i2c_tuner(struct dvb_frontend *fe)
{
	struct dib8000_state *st = fe->demodulator_priv;
	return &st->dib8096p_tuner_adap;
}

static int dib8096p_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 en_cur_state;

	dprintk("sleep dib8096p: %d\n", onoff);

	en_cur_state = dib8000_read_word(state, 1922);

	/* LNAs and MIX are ON and therefore it is a valid configuration */
	if (en_cur_state > 0xff)
		state->tuner_enable = en_cur_state ;

	if (onoff)
		en_cur_state &= 0x00ff;
	else {
		if (state->tuner_enable != 0)
			en_cur_state = state->tuner_enable;
	}

	dib8000_write_word(state, 1922, en_cur_state);

	return 0;
}

static const s32 lut_1000ln_mant[] =
{
	908, 7003, 7090, 7170, 7244, 7313, 7377, 7438, 7495, 7549, 7600
};

static s32 dib8000_get_adc_power(struct dvb_frontend *fe, u8 mode)
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

static int dib8090p_get_dc_power(struct dvb_frontend *fe, u8 IQ)
{
	struct dib8000_state *state = fe->demodulator_priv;
	int val = 0;

	switch (IQ) {
	case 1:
			val = dib8000_read_word(state, 403);
			break;
	case 0:
			val = dib8000_read_word(state, 404);
			break;
	}
	if (val  & 0x200)
		val -= 1024;

	return val;
}

static void dib8000_update_timf(struct dib8000_state *state)
{
	u32 timf = state->timf = dib8000_read32(state, 435);

	dib8000_write_word(state, 29, (u16) (timf >> 16));
	dib8000_write_word(state, 30, (u16) (timf & 0xffff));
	dprintk("Updated timing frequency: %d (default: %d)\n", state->timf, state->timf_default);
}

static u32 dib8000_ctrl_timf(struct dvb_frontend *fe, uint8_t op, uint32_t timf)
{
	struct dib8000_state *state = fe->demodulator_priv;

	switch (op) {
	case DEMOD_TIMF_SET:
			state->timf = timf;
			break;
	case DEMOD_TIMF_UPDATE:
			dib8000_update_timf(state);
			break;
	case DEMOD_TIMF_GET:
			break;
	}
	dib8000_set_bandwidth(state->fe[0], 6000);

	return state->timf;
}

static const u16 adc_target_16dB[11] = {
	7250, 7238, 7264, 7309, 7338, 7382, 7427, 7456, 7500, 7544, 7574
};

static const u8 permu_seg[] = { 6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 11, 0, 12 };

static u16 dib8000_set_layer(struct dib8000_state *state, u8 layer_index, u16 max_constellation)
{
	u8  cr, constellation, time_intlv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;

	switch (c->layer[layer_index].modulation) {
	case DQPSK:
			constellation = 0;
			break;
	case  QPSK:
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

	switch (c->layer[layer_index].fec) {
	case FEC_1_2:
			cr = 1;
			break;
	case FEC_2_3:
			cr = 2;
			break;
	case FEC_3_4:
			cr = 3;
			break;
	case FEC_5_6:
			cr = 5;
			break;
	case FEC_7_8:
	default:
			cr = 7;
			break;
	}

	time_intlv = fls(c->layer[layer_index].interleaving);
	if (time_intlv > 3 && !(time_intlv == 4 && c->isdbt_sb_mode == 1))
		time_intlv = 0;

	dib8000_write_word(state, 2 + layer_index, (constellation << 10) | ((c->layer[layer_index].segment_count & 0xf) << 6) | (cr << 3) | time_intlv);
	if (c->layer[layer_index].segment_count > 0) {
		switch (max_constellation) {
		case DQPSK:
		case QPSK:
				if (c->layer[layer_index].modulation == QAM_16 || c->layer[layer_index].modulation == QAM_64)
					max_constellation = c->layer[layer_index].modulation;
				break;
		case QAM_16:
				if (c->layer[layer_index].modulation == QAM_64)
					max_constellation = c->layer[layer_index].modulation;
				break;
		}
	}

	return  max_constellation;
}

static const u16 adp_Q64[4] = {0x0148, 0xfff0, 0x00a4, 0xfff8}; /* P_adp_regul_cnt 0.04, P_adp_noise_cnt -0.002, P_adp_regul_ext 0.02, P_adp_noise_ext -0.001 */
static const u16 adp_Q16[4] = {0x023d, 0xffdf, 0x00a4, 0xfff0}; /* P_adp_regul_cnt 0.07, P_adp_noise_cnt -0.004, P_adp_regul_ext 0.02, P_adp_noise_ext -0.002 */
static const u16 adp_Qdefault[4] = {0x099a, 0xffae, 0x0333, 0xfff8}; /* P_adp_regul_cnt 0.3,  P_adp_noise_cnt -0.01,  P_adp_regul_ext 0.1,  P_adp_noise_ext -0.002 */
static u16 dib8000_adp_fine_tune(struct dib8000_state *state, u16 max_constellation)
{
	u16 i, ana_gain = 0;
	const u16 *adp;

	/* channel estimation fine configuration */
	switch (max_constellation) {
	case QAM_64:
			ana_gain = 0x7;
			adp = &adp_Q64[0];
			break;
	case QAM_16:
			ana_gain = 0x7;
			adp = &adp_Q16[0];
			break;
	default:
			ana_gain = 0;
			adp = &adp_Qdefault[0];
			break;
	}

	for (i = 0; i < 4; i++)
		dib8000_write_word(state, 215 + i, adp[i]);

	return ana_gain;
}

static void dib8000_update_ana_gain(struct dib8000_state *state, u16 ana_gain)
{
	u16 i;

	dib8000_write_word(state, 116, ana_gain);

	/* update ADC target depending on ana_gain */
	if (ana_gain) { /* set -16dB ADC target for ana_gain=-1 */
		for (i = 0; i < 10; i++)
			dib8000_write_word(state, 80 + i, adc_target_16dB[i]);
	} else { /* set -22dB ADC target for ana_gain=0 */
		for (i = 0; i < 10; i++)
			dib8000_write_word(state, 80 + i, adc_target_16dB[i] - 355);
	}
}

static void dib8000_load_ana_fe_coefs(struct dib8000_state *state, const s16 *ana_fe)
{
	u16 mode = 0;

	if (state->isdbt_cfg_loaded == 0)
		for (mode = 0; mode < 24; mode++)
			dib8000_write_word(state, 117 + mode, ana_fe[mode]);
}

static const u16 lut_prbs_2k[14] = {
	0, 0x423, 0x009, 0x5C7, 0x7A6, 0x3D8, 0x527, 0x7FF, 0x79B, 0x3D6, 0x3A2, 0x53B, 0x2F4, 0x213
};
static const u16 lut_prbs_4k[14] = {
	0, 0x208, 0x0C3, 0x7B9, 0x423, 0x5C7, 0x3D8, 0x7FF, 0x3D6, 0x53B, 0x213, 0x029, 0x0D0, 0x48E
};
static const u16 lut_prbs_8k[14] = {
	0, 0x740, 0x069, 0x7DD, 0x208, 0x7B9, 0x5C7, 0x7FF, 0x53B, 0x029, 0x48E, 0x4C4, 0x367, 0x684
};

static u16 dib8000_get_init_prbs(struct dib8000_state *state, u16 subchannel)
{
	int sub_channel_prbs_group = 0;

	sub_channel_prbs_group = (subchannel / 3) + 1;
	dprintk("sub_channel_prbs_group = %d , subchannel =%d prbs = 0x%04x\n", sub_channel_prbs_group, subchannel, lut_prbs_8k[sub_channel_prbs_group]);

	switch (state->fe[0]->dtv_property_cache.transmission_mode) {
	case TRANSMISSION_MODE_2K:
			return lut_prbs_2k[sub_channel_prbs_group];
	case TRANSMISSION_MODE_4K:
			return lut_prbs_4k[sub_channel_prbs_group];
	default:
	case TRANSMISSION_MODE_8K:
			return lut_prbs_8k[sub_channel_prbs_group];
	}
}

static void dib8000_set_13seg_channel(struct dib8000_state *state)
{
	u16 i;
	u16 coff_pow = 0x2800;

	state->seg_mask = 0x1fff; /* All 13 segments enabled */

	/* ---- COFF ---- Carloff, the most robust --- */
	if (state->isdbt_cfg_loaded == 0) {  /* if not Sound Broadcasting mode : put default values for 13 segments */
		dib8000_write_word(state, 180, (16 << 6) | 9);
		dib8000_write_word(state, 187, (4 << 12) | (8 << 5) | 0x2);
		coff_pow = 0x2800;
		for (i = 0; i < 6; i++)
			dib8000_write_word(state, 181+i, coff_pow);

		/* P_ctrl_corm_thres4pre_freq_inh=1, P_ctrl_pre_freq_mode_sat=1 */
		/* P_ctrl_pre_freq_mode_sat=1, P_ctrl_pre_freq_inh=0, P_ctrl_pre_freq_step = 3, P_pre_freq_win_len=1 */
		dib8000_write_word(state, 338, (1 << 12) | (1 << 10) | (0 << 9) | (3 << 5) | 1);

		/* P_ctrl_pre_freq_win_len=8, P_ctrl_pre_freq_thres_lockin=6 */
		dib8000_write_word(state, 340, (8 << 6) | (6 << 0));
		/* P_ctrl_pre_freq_thres_lockout=4, P_small_use_tmcc/ac/cp=1 */
		dib8000_write_word(state, 341, (4 << 3) | (1 << 2) | (1 << 1) | (1 << 0));

		dib8000_write_word(state, 228, 0);  /* default value */
		dib8000_write_word(state, 265, 31); /* default value */
		dib8000_write_word(state, 205, 0x200f); /* init value */
	}

	/*
	 * make the cpil_coff_lock more robust but slower p_coff_winlen
	 * 6bits; p_coff_thres_lock 6bits (for coff lock if needed)
	 */

	if (state->cfg.pll->ifreq == 0)
		dib8000_write_word(state, 266, ~state->seg_mask | state->seg_diff_mask | 0x40); /* P_equal_noise_seg_inh */

	dib8000_load_ana_fe_coefs(state, ana_fe_coeff_13seg);
}

static void dib8000_set_subchannel_prbs(struct dib8000_state *state, u16 init_prbs)
{
	u16 reg_1;

	reg_1 = dib8000_read_word(state, 1);
	dib8000_write_word(state, 1, (init_prbs << 2) | (reg_1 & 0x3)); /* ADDR 1 */
}

static void dib8000_small_fine_tune(struct dib8000_state *state)
{
	u16 i;
	const s16 *ncoeff;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;

	dib8000_write_word(state, 352, state->seg_diff_mask);
	dib8000_write_word(state, 353, state->seg_mask);

	/* P_small_coef_ext_enable=ISDB-Tsb, P_small_narrow_band=ISDB-Tsb, P_small_last_seg=13, P_small_offset_num_car=5 */
	dib8000_write_word(state, 351, (c->isdbt_sb_mode << 9) | (c->isdbt_sb_mode << 8) | (13 << 4) | 5);

	if (c->isdbt_sb_mode) {
		/* ---- SMALL ---- */
		switch (c->transmission_mode) {
		case TRANSMISSION_MODE_2K:
				if (c->isdbt_partial_reception == 0) { /* 1-seg */
					if (c->layer[0].modulation == DQPSK) /* DQPSK */
						ncoeff = coeff_2k_sb_1seg_dqpsk;
					else /* QPSK or QAM */
						ncoeff = coeff_2k_sb_1seg;
				} else { /* 3-segments */
					if (c->layer[0].modulation == DQPSK) { /* DQPSK on central segment */
						if (c->layer[1].modulation == DQPSK) /* DQPSK on external segments */
							ncoeff = coeff_2k_sb_3seg_0dqpsk_1dqpsk;
						else /* QPSK or QAM on external segments */
							ncoeff = coeff_2k_sb_3seg_0dqpsk;
					} else { /* QPSK or QAM on central segment */
						if (c->layer[1].modulation == DQPSK) /* DQPSK on external segments */
							ncoeff = coeff_2k_sb_3seg_1dqpsk;
						else /* QPSK or QAM on external segments */
							ncoeff = coeff_2k_sb_3seg;
					}
				}
				break;
		case TRANSMISSION_MODE_4K:
				if (c->isdbt_partial_reception == 0) { /* 1-seg */
					if (c->layer[0].modulation == DQPSK) /* DQPSK */
						ncoeff = coeff_4k_sb_1seg_dqpsk;
					else /* QPSK or QAM */
						ncoeff = coeff_4k_sb_1seg;
				} else { /* 3-segments */
					if (c->layer[0].modulation == DQPSK) { /* DQPSK on central segment */
						if (c->layer[1].modulation == DQPSK) /* DQPSK on external segments */
							ncoeff = coeff_4k_sb_3seg_0dqpsk_1dqpsk;
						else /* QPSK or QAM on external segments */
							ncoeff = coeff_4k_sb_3seg_0dqpsk;
					} else { /* QPSK or QAM on central segment */
						if (c->layer[1].modulation == DQPSK) /* DQPSK on external segments */
							ncoeff = coeff_4k_sb_3seg_1dqpsk;
						else /* QPSK or QAM on external segments */
							ncoeff = coeff_4k_sb_3seg;
					}
				}
				break;
		case TRANSMISSION_MODE_AUTO:
		case TRANSMISSION_MODE_8K:
		default:
				if (c->isdbt_partial_reception == 0) { /* 1-seg */
					if (c->layer[0].modulation == DQPSK) /* DQPSK */
						ncoeff = coeff_8k_sb_1seg_dqpsk;
					else /* QPSK or QAM */
						ncoeff = coeff_8k_sb_1seg;
				} else { /* 3-segments */
					if (c->layer[0].modulation == DQPSK) { /* DQPSK on central segment */
						if (c->layer[1].modulation == DQPSK) /* DQPSK on external segments */
							ncoeff = coeff_8k_sb_3seg_0dqpsk_1dqpsk;
						else /* QPSK or QAM on external segments */
							ncoeff = coeff_8k_sb_3seg_0dqpsk;
					} else { /* QPSK or QAM on central segment */
						if (c->layer[1].modulation == DQPSK) /* DQPSK on external segments */
							ncoeff = coeff_8k_sb_3seg_1dqpsk;
						else /* QPSK or QAM on external segments */
							ncoeff = coeff_8k_sb_3seg;
					}
				}
				break;
		}

		for (i = 0; i < 8; i++)
			dib8000_write_word(state, 343 + i, ncoeff[i]);
	}
}

static const u16 coff_thres_1seg[3] = {300, 150, 80};
static const u16 coff_thres_3seg[3] = {350, 300, 250};
static void dib8000_set_sb_channel(struct dib8000_state *state)
{
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	const u16 *coff;
	u16 i;

	if (c->transmission_mode == TRANSMISSION_MODE_2K || c->transmission_mode == TRANSMISSION_MODE_4K) {
		dib8000_write_word(state, 219, dib8000_read_word(state, 219) | 0x1); /* adp_pass =1 */
		dib8000_write_word(state, 190, dib8000_read_word(state, 190) | (0x1 << 14)); /* pha3_force_pha_shift = 1 */
	} else {
		dib8000_write_word(state, 219, dib8000_read_word(state, 219) & 0xfffe); /* adp_pass =0 */
		dib8000_write_word(state, 190, dib8000_read_word(state, 190) & 0xbfff); /* pha3_force_pha_shift = 0 */
	}

	if (c->isdbt_partial_reception == 1) /* 3-segments */
		state->seg_mask = 0x00E0;
	else /* 1-segment */
		state->seg_mask = 0x0040;

	dib8000_write_word(state, 268, (dib8000_read_word(state, 268) & 0xF9FF) | 0x0200);

	/* ---- COFF ---- Carloff, the most robust --- */
	/* P_coff_cpil_alpha=4, P_coff_inh=0, P_coff_cpil_winlen=64, P_coff_narrow_band=1, P_coff_square_val=1, P_coff_one_seg=~partial_rcpt, P_coff_use_tmcc=1, P_coff_use_ac=1 */
	dib8000_write_word(state, 187, (4 << 12) | (0 << 11) | (63 << 5) | (0x3 << 3) | ((~c->isdbt_partial_reception & 1) << 2) | 0x3);

	dib8000_write_word(state, 340, (16 << 6) | (8 << 0)); /* P_ctrl_pre_freq_win_len=16, P_ctrl_pre_freq_thres_lockin=8 */
	dib8000_write_word(state, 341, (6 << 3) | (1 << 2) | (1 << 1) | (1 << 0));/* P_ctrl_pre_freq_thres_lockout=6, P_small_use_tmcc/ac/cp=1 */

	/* Sound Broadcasting mode 1 seg */
	if (c->isdbt_partial_reception == 0) {
		/* P_coff_winlen=63, P_coff_thres_lock=15, P_coff_one_seg_width = (P_mode == 3) , P_coff_one_seg_sym = (P_mode-1) */
		if (state->mode == 3)
			dib8000_write_word(state, 180, 0x1fcf | ((state->mode - 1) << 14));
		else
			dib8000_write_word(state, 180, 0x0fcf | ((state->mode - 1) << 14));

		/* P_ctrl_corm_thres4pre_freq_inh=1,P_ctrl_pre_freq_mode_sat=1, P_ctrl_pre_freq_inh=0, P_ctrl_pre_freq_step = 5, P_pre_freq_win_len=4 */
		dib8000_write_word(state, 338, (1 << 12) | (1 << 10) | (0 << 9) | (5 << 5) | 4);
		coff = &coff_thres_1seg[0];
	} else {   /* Sound Broadcasting mode 3 seg */
		dib8000_write_word(state, 180, 0x1fcf | (1 << 14));
		/* P_ctrl_corm_thres4pre_freq_inh = 1, P_ctrl_pre_freq_mode_sat=1, P_ctrl_pre_freq_inh=0, P_ctrl_pre_freq_step = 4, P_pre_freq_win_len=4 */
		dib8000_write_word(state, 338, (1 << 12) | (1 << 10) | (0 << 9) | (4 << 5) | 4);
		coff = &coff_thres_3seg[0];
	}

	dib8000_write_word(state, 228, 1); /* P_2d_mode_byp=1 */
	dib8000_write_word(state, 205, dib8000_read_word(state, 205) & 0xfff0); /* P_cspu_win_cut = 0 */

	if (c->isdbt_partial_reception == 0 && c->transmission_mode == TRANSMISSION_MODE_2K)
		dib8000_write_word(state, 265, 15); /* P_equal_noise_sel = 15 */

	/* Write COFF thres */
	for (i = 0 ; i < 3; i++) {
		dib8000_write_word(state, 181+i, coff[i]);
		dib8000_write_word(state, 184+i, coff[i]);
	}

	/*
	 * make the cpil_coff_lock more robust but slower p_coff_winlen
	 * 6bits; p_coff_thres_lock 6bits (for coff lock if needed)
	 */

	dib8000_write_word(state, 266, ~state->seg_mask | state->seg_diff_mask); /* P_equal_noise_seg_inh */

	if (c->isdbt_partial_reception == 0)
		dib8000_write_word(state, 178, 64); /* P_fft_powrange = 64 */
	else
		dib8000_write_word(state, 178, 32); /* P_fft_powrange = 32 */
}

static void dib8000_set_isdbt_common_channel(struct dib8000_state *state, u8 seq, u8 autosearching)
{
	u16 p_cfr_left_edge  = 0, p_cfr_right_edge = 0;
	u16 tmcc_pow = 0, ana_gain = 0, tmp = 0, i = 0, nbseg_diff = 0 ;
	u16 max_constellation = DQPSK;
	int init_prbs;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;

	if (autosearching)
		c->isdbt_partial_reception = 1;

	/* P_mode */
	dib8000_write_word(state, 10, (seq << 4));

	/* init mode */
	state->mode = fft_to_mode(state);

	/* set guard */
	tmp = dib8000_read_word(state, 1);
	dib8000_write_word(state, 1, (tmp&0xfffc) | (c->guard_interval & 0x3));

	dib8000_write_word(state, 274, (dib8000_read_word(state, 274) & 0xffcf) | ((c->isdbt_partial_reception & 1) << 5) | ((c->isdbt_sb_mode & 1) << 4));

	/* signal optimization parameter */
	if (c->isdbt_partial_reception) {
		state->seg_diff_mask = (c->layer[0].modulation == DQPSK) << permu_seg[0];
		for (i = 1; i < 3; i++)
			nbseg_diff += (c->layer[i].modulation == DQPSK) * c->layer[i].segment_count;
		for (i = 0; i < nbseg_diff; i++)
			state->seg_diff_mask |= 1 << permu_seg[i+1];
	} else {
		for (i = 0; i < 3; i++)
			nbseg_diff += (c->layer[i].modulation == DQPSK) * c->layer[i].segment_count;
		for (i = 0; i < nbseg_diff; i++)
			state->seg_diff_mask |= 1 << permu_seg[i];
	}

	if (state->seg_diff_mask)
		dib8000_write_word(state, 268, (dib8000_read_word(state, 268) & 0xF9FF) | 0x0200);
	else
		dib8000_write_word(state, 268, (2 << 9) | 39); /*init value */

	for (i = 0; i < 3; i++)
		max_constellation = dib8000_set_layer(state, i, max_constellation);
	if (autosearching == 0) {
		state->layer_b_nb_seg = c->layer[1].segment_count;
		state->layer_c_nb_seg = c->layer[2].segment_count;
	}

	/* WRITE: Mode & Diff mask */
	dib8000_write_word(state, 0, (state->mode << 13) | state->seg_diff_mask);

	state->differential_constellation = (state->seg_diff_mask != 0);

	/* channel estimation fine configuration */
	ana_gain = dib8000_adp_fine_tune(state, max_constellation);

	/* update ana_gain depending on max constellation */
	dib8000_update_ana_gain(state, ana_gain);

	/* ---- ANA_FE ---- */
	if (c->isdbt_partial_reception) /* 3-segments */
		dib8000_load_ana_fe_coefs(state, ana_fe_coeff_3seg);
	else
		dib8000_load_ana_fe_coefs(state, ana_fe_coeff_1seg); /* 1-segment */

	/* TSB or ISDBT ? apply it now */
	if (c->isdbt_sb_mode) {
		dib8000_set_sb_channel(state);
		if (c->isdbt_sb_subchannel < 14)
			init_prbs = dib8000_get_init_prbs(state, c->isdbt_sb_subchannel);
		else
			init_prbs = 0;
	} else {
		dib8000_set_13seg_channel(state);
		init_prbs = 0xfff;
	}

	/* SMALL */
	dib8000_small_fine_tune(state);

	dib8000_set_subchannel_prbs(state, init_prbs);

	/* ---- CHAN_BLK ---- */
	for (i = 0; i < 13; i++) {
		if ((((~state->seg_diff_mask) >> i) & 1) == 1) {
			p_cfr_left_edge  += (1 << i) * ((i == 0) || ((((state->seg_mask & (~state->seg_diff_mask)) >> (i - 1)) & 1) == 0));
			p_cfr_right_edge += (1 << i) * ((i == 12) || ((((state->seg_mask & (~state->seg_diff_mask)) >> (i + 1)) & 1) == 0));
		}
	}
	dib8000_write_word(state, 222, p_cfr_left_edge); /* p_cfr_left_edge */
	dib8000_write_word(state, 223, p_cfr_right_edge); /* p_cfr_right_edge */
	/* "P_cspu_left_edge" & "P_cspu_right_edge" not used => do not care */

	dib8000_write_word(state, 189, ~state->seg_mask | state->seg_diff_mask); /* P_lmod4_seg_inh */
	dib8000_write_word(state, 192, ~state->seg_mask | state->seg_diff_mask); /* P_pha3_seg_inh */
	dib8000_write_word(state, 225, ~state->seg_mask | state->seg_diff_mask); /* P_tac_seg_inh */

	if (!autosearching)
		dib8000_write_word(state, 288, (~state->seg_mask | state->seg_diff_mask) & 0x1fff); /* P_tmcc_seg_eq_inh */
	else
		dib8000_write_word(state, 288, 0x1fff); /*disable equalisation of the tmcc when autosearch to be able to find the DQPSK channels. */

	dib8000_write_word(state, 211, state->seg_mask & (~state->seg_diff_mask)); /* P_des_seg_enabled */
	dib8000_write_word(state, 287, ~state->seg_mask | 0x1000); /* P_tmcc_seg_inh */

	dib8000_write_word(state, 178, 32); /* P_fft_powrange = 32 */

	/* ---- TMCC ---- */
	for (i = 0; i < 3; i++)
		tmcc_pow += (((c->layer[i].modulation == DQPSK) * 4 + 1) * c->layer[i].segment_count) ;

	/* Quantif of "P_tmcc_dec_thres_?k" is (0, 5+mode, 9); */
	/* Threshold is set at 1/4 of max power. */
	tmcc_pow *= (1 << (9-2));
	dib8000_write_word(state, 290, tmcc_pow); /* P_tmcc_dec_thres_2k */
	dib8000_write_word(state, 291, tmcc_pow); /* P_tmcc_dec_thres_4k */
	dib8000_write_word(state, 292, tmcc_pow); /* P_tmcc_dec_thres_8k */
	/*dib8000_write_word(state, 287, (1 << 13) | 0x1000 ); */

	/* ---- PHA3 ---- */
	if (state->isdbt_cfg_loaded == 0)
		dib8000_write_word(state, 250, 3285); /* p_2d_hspeed_thr0 */

	state->isdbt_cfg_loaded = 0;
}

static u32 dib8000_wait_lock(struct dib8000_state *state, u32 internal,
			     u32 wait0_ms, u32 wait1_ms, u32 wait2_ms)
{
	u32 value = 0;	/* P_search_end0 wait time */
	u16 reg = 11;	/* P_search_end0 start addr */

	for (reg = 11; reg < 16; reg += 2) {
		if (reg == 11) {
			if (state->revision == 0x8090)
				value = internal * wait1_ms;
			else
				value = internal * wait0_ms;
		} else if (reg == 13)
			value = internal * wait1_ms;
		else if (reg == 15)
			value = internal * wait2_ms;
		dib8000_write_word(state, reg, (u16)((value >> 16) & 0xffff));
		dib8000_write_word(state, (reg + 1), (u16)(value & 0xffff));
	}
	return value;
}

static int dib8000_autosearch_start(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	u8 slist = 0;
	u32 value, internal = state->cfg.pll->internal;

	if (state->revision == 0x8090)
		internal = dib8000_read32(state, 23) / 1000;

	if ((state->revision >= 0x8002) &&
	    (state->autosearch_state == AS_SEARCHING_FFT)) {
		dib8000_write_word(state,  37, 0x0065); /* P_ctrl_pha_off_max default values */
		dib8000_write_word(state, 116, 0x0000); /* P_ana_gain to 0 */

		dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x1fff) | (0 << 13) | (1 << 15)); /* P_mode = 0, P_restart_search=1 */
		dib8000_write_word(state, 1, (dib8000_read_word(state, 1) & 0xfffc) | 0); /* P_guard = 0 */
		dib8000_write_word(state, 6, 0); /* P_lock0_mask = 0 */
		dib8000_write_word(state, 7, 0); /* P_lock1_mask = 0 */
		dib8000_write_word(state, 8, 0); /* P_lock2_mask = 0 */
		dib8000_write_word(state, 10, (dib8000_read_word(state, 10) & 0x200) | (16 << 4) | (0 << 0)); /* P_search_list=16, P_search_maxtrial=0 */

		if (state->revision == 0x8090)
			value = dib8000_wait_lock(state, internal, 10, 10, 10); /* time in ms configure P_search_end0 P_search_end1 P_search_end2 */
		else
			value = dib8000_wait_lock(state, internal, 20, 20, 20); /* time in ms configure P_search_end0 P_search_end1 P_search_end2 */

		dib8000_write_word(state, 17, 0);
		dib8000_write_word(state, 18, 200); /* P_search_rstst = 200 */
		dib8000_write_word(state, 19, 0);
		dib8000_write_word(state, 20, 400); /* P_search_rstend = 400 */
		dib8000_write_word(state, 21, (value >> 16) & 0xffff); /* P_search_checkst */
		dib8000_write_word(state, 22, value & 0xffff);

		if (state->revision == 0x8090)
			dib8000_write_word(state, 32, (dib8000_read_word(state, 32) & 0xf0ff) | (0 << 8)); /* P_corm_alpha = 0 */
		else
			dib8000_write_word(state, 32, (dib8000_read_word(state, 32) & 0xf0ff) | (9 << 8)); /* P_corm_alpha = 3 */
		dib8000_write_word(state, 355, 2); /* P_search_param_max = 2 */

		/* P_search_param_select = (1 | 1<<4 | 1 << 8) */
		dib8000_write_word(state, 356, 0);
		dib8000_write_word(state, 357, 0x111);

		dib8000_write_word(state, 770, (dib8000_read_word(state, 770) & 0xdfff) | (1 << 13)); /* P_restart_ccg = 1 */
		dib8000_write_word(state, 770, (dib8000_read_word(state, 770) & 0xdfff) | (0 << 13)); /* P_restart_ccg = 0 */
		dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x7ff) | (0 << 15) | (1 << 13)); /* P_restart_search = 0; */
	} else if ((state->revision >= 0x8002) &&
		   (state->autosearch_state == AS_SEARCHING_GUARD)) {
		c->transmission_mode = TRANSMISSION_MODE_8K;
		c->guard_interval = GUARD_INTERVAL_1_8;
		c->inversion = 0;
		c->layer[0].modulation = QAM_64;
		c->layer[0].fec = FEC_2_3;
		c->layer[0].interleaving = 0;
		c->layer[0].segment_count = 13;

		slist = 16;
		c->transmission_mode = state->found_nfft;

		dib8000_set_isdbt_common_channel(state, slist, 1);

		/* set lock_mask values */
		dib8000_write_word(state, 6, 0x4);
		if (state->revision == 0x8090)
			dib8000_write_word(state, 7, ((1 << 12) | (1 << 11) | (1 << 10)));/* tmcc_dec_lock, tmcc_sync_lock, tmcc_data_lock, tmcc_bch_uncor */
		else
			dib8000_write_word(state, 7, 0x8);
		dib8000_write_word(state, 8, 0x1000);

		/* set lock_mask wait time values */
		if (state->revision == 0x8090)
			dib8000_wait_lock(state, internal, 50, 100, 1000); /* time in ms configure P_search_end0 P_search_end1 P_search_end2 */
		else
			dib8000_wait_lock(state, internal, 50, 200, 1000); /* time in ms configure P_search_end0 P_search_end1 P_search_end2 */

		dib8000_write_word(state, 355, 3); /* P_search_param_max = 3 */

		/* P_search_param_select = 0xf; look for the 4 different guard intervals */
		dib8000_write_word(state, 356, 0);
		dib8000_write_word(state, 357, 0xf);

		value = dib8000_read_word(state, 0);
		dib8000_write_word(state, 0, (u16)((1 << 15) | value));
		dib8000_read_word(state, 1284);  /* reset the INT. n_irq_pending */
		dib8000_write_word(state, 0, (u16)value);
	} else {
		c->inversion = 0;
		c->layer[0].modulation = QAM_64;
		c->layer[0].fec = FEC_2_3;
		c->layer[0].interleaving = 0;
		c->layer[0].segment_count = 13;
		if (!c->isdbt_sb_mode)
			c->layer[0].segment_count = 13;

		/* choose the right list, in sb, always do everything */
		if (c->isdbt_sb_mode) {
			slist = 7;
			dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));
		} else {
			if (c->guard_interval == GUARD_INTERVAL_AUTO) {
				if (c->transmission_mode == TRANSMISSION_MODE_AUTO) {
					c->transmission_mode = TRANSMISSION_MODE_8K;
					c->guard_interval = GUARD_INTERVAL_1_8;
					slist = 7;
					dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));  /* P_mode = 1 to have autosearch start ok with mode2 */
				} else {
					c->guard_interval = GUARD_INTERVAL_1_8;
					slist = 3;
				}
			} else {
				if (c->transmission_mode == TRANSMISSION_MODE_AUTO) {
					c->transmission_mode = TRANSMISSION_MODE_8K;
					slist = 2;
					dib8000_write_word(state, 0, (dib8000_read_word(state, 0) & 0x9fff) | (1 << 13));  /* P_mode = 1 */
				} else
					slist = 0;
			}
		}
		dprintk("Using list for autosearch : %d\n", slist);

		dib8000_set_isdbt_common_channel(state, slist, 1);

		/* set lock_mask values */
		dib8000_write_word(state, 6, 0x4);
		if (state->revision == 0x8090)
			dib8000_write_word(state, 7, (1 << 12) | (1 << 11) | (1 << 10));
		else
			dib8000_write_word(state, 7, 0x8);
		dib8000_write_word(state, 8, 0x1000);

		/* set lock_mask wait time values */
		if (state->revision == 0x8090)
			dib8000_wait_lock(state, internal, 50, 200, 1000); /* time in ms configure P_search_end0 P_search_end1 P_search_end2 */
		else
			dib8000_wait_lock(state, internal, 50, 100, 1000); /* time in ms configure P_search_end0 P_search_end1 P_search_end2 */

		value = dib8000_read_word(state, 0);
		dib8000_write_word(state, 0, (u16)((1 << 15) | value));
		dib8000_read_word(state, 1284);  /* reset the INT. n_irq_pending */
		dib8000_write_word(state, 0, (u16)value);
	}
	return 0;
}

static int dib8000_autosearch_irq(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 irq_pending = dib8000_read_word(state, 1284);

	if ((state->revision >= 0x8002) &&
	    (state->autosearch_state == AS_SEARCHING_FFT)) {
		if (irq_pending & 0x1) {
			dprintk("dib8000_autosearch_irq: max correlation result available\n");
			return 3;
		}
	} else {
		if (irq_pending & 0x1) {	/* failed */
			dprintk("dib8000_autosearch_irq failed\n");
			return 1;
		}

		if (irq_pending & 0x2) {	/* succeeded */
			dprintk("dib8000_autosearch_irq succeeded\n");
			return 2;
		}
	}

	return 0;		// still pending
}

static void dib8000_viterbi_state(struct dib8000_state *state, u8 onoff)
{
	u16 tmp;

	tmp = dib8000_read_word(state, 771);
	if (onoff) /* start P_restart_chd : channel_decoder */
		dib8000_write_word(state, 771, tmp & 0xfffd);
	else /* stop P_restart_chd : channel_decoder */
		dib8000_write_word(state, 771, tmp | (1<<1));
}

static void dib8000_set_dds(struct dib8000_state *state, s32 offset_khz)
{
	s16 unit_khz_dds_val;
	u32 abs_offset_khz = abs(offset_khz);
	u32 dds = state->cfg.pll->ifreq & 0x1ffffff;
	u8 invert = !!(state->cfg.pll->ifreq & (1 << 25));
	u8 ratio;

	if (state->revision == 0x8090) {
		ratio = 4;
		unit_khz_dds_val = (1<<26) / (dib8000_read32(state, 23) / 1000);
		if (offset_khz < 0)
			dds = (1 << 26) - (abs_offset_khz * unit_khz_dds_val);
		else
			dds = (abs_offset_khz * unit_khz_dds_val);

		if (invert)
			dds = (1<<26) - dds;
	} else {
		ratio = 2;
		unit_khz_dds_val = (u16) (67108864 / state->cfg.pll->internal);

		if (offset_khz < 0)
			unit_khz_dds_val *= -1;

		/* IF tuner */
		if (invert)
			dds -= abs_offset_khz * unit_khz_dds_val;
		else
			dds += abs_offset_khz * unit_khz_dds_val;
	}

	dprintk("setting a DDS frequency offset of %c%dkHz\n", invert ? '-' : ' ', dds / unit_khz_dds_val);

	if (abs_offset_khz <= (state->cfg.pll->internal / ratio)) {
		/* Max dds offset is the half of the demod freq */
		dib8000_write_word(state, 26, invert);
		dib8000_write_word(state, 27, (u16)(dds >> 16) & 0x1ff);
		dib8000_write_word(state, 28, (u16)(dds & 0xffff));
	}
}

static void dib8000_set_frequency_offset(struct dib8000_state *state)
{
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	int i;
	u32 current_rf;
	int total_dds_offset_khz;

	if (state->fe[0]->ops.tuner_ops.get_frequency)
		state->fe[0]->ops.tuner_ops.get_frequency(state->fe[0], &current_rf);
	else
		current_rf = c->frequency;
	current_rf /= 1000;
	total_dds_offset_khz = (int)current_rf - (int)c->frequency / 1000;

	if (c->isdbt_sb_mode) {
		state->subchannel = c->isdbt_sb_subchannel;

		i = dib8000_read_word(state, 26) & 1; /* P_dds_invspec */
		dib8000_write_word(state, 26, c->inversion ^ i);

		if (state->cfg.pll->ifreq == 0) { /* low if tuner */
			if ((c->inversion ^ i) == 0)
				dib8000_write_word(state, 26, dib8000_read_word(state, 26) | 1);
		} else {
			if ((c->inversion ^ i) == 0)
				total_dds_offset_khz *= -1;
		}
	}

	dprintk("%dkhz tuner offset (frequency = %dHz & current_rf = %dHz) total_dds_offset_hz = %d\n", c->frequency - current_rf, c->frequency, current_rf, total_dds_offset_khz);

	/* apply dds offset now */
	dib8000_set_dds(state, total_dds_offset_khz);
}

static u16 LUT_isdbt_symbol_duration[4] = { 26, 101, 63 };

static u32 dib8000_get_symbol_duration(struct dib8000_state *state)
{
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	u16 i;

	switch (c->transmission_mode) {
	case TRANSMISSION_MODE_2K:
			i = 0;
			break;
	case TRANSMISSION_MODE_4K:
			i = 2;
			break;
	default:
	case TRANSMISSION_MODE_AUTO:
	case TRANSMISSION_MODE_8K:
			i = 1;
			break;
	}

	return (LUT_isdbt_symbol_duration[i] / (c->bandwidth_hz / 1000)) + 1;
}

static void dib8000_set_isdbt_loop_params(struct dib8000_state *state, enum param_loop_step loop_step)
{
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	u16 reg_32 = 0, reg_37 = 0;

	switch (loop_step) {
	case LOOP_TUNE_1:
			if (c->isdbt_sb_mode)  {
				if (c->isdbt_partial_reception == 0) {
					reg_32 = ((11 - state->mode) << 12) | (6 << 8) | 0x40; /* P_timf_alpha = (11-P_mode), P_corm_alpha=6, P_corm_thres=0x40 */
					reg_37 = (3 << 5) | (0 << 4) | (10 - state->mode); /* P_ctrl_pha_off_max=3   P_ctrl_sfreq_inh =0  P_ctrl_sfreq_step = (10-P_mode)  */
				} else { /* Sound Broadcasting mode 3 seg */
					reg_32 = ((10 - state->mode) << 12) | (6 << 8) | 0x60; /* P_timf_alpha = (10-P_mode), P_corm_alpha=6, P_corm_thres=0x60 */
					reg_37 = (3 << 5) | (0 << 4) | (9 - state->mode); /* P_ctrl_pha_off_max=3   P_ctrl_sfreq_inh =0  P_ctrl_sfreq_step = (9-P_mode)  */
				}
			} else { /* 13-seg start conf offset loop parameters */
				reg_32 = ((9 - state->mode) << 12) | (6 << 8) | 0x80; /* P_timf_alpha = (9-P_mode, P_corm_alpha=6, P_corm_thres=0x80 */
				reg_37 = (3 << 5) | (0 << 4) | (8 - state->mode); /* P_ctrl_pha_off_max=3   P_ctrl_sfreq_inh =0  P_ctrl_sfreq_step = 9  */
			}
			break;
	case LOOP_TUNE_2:
			if (c->isdbt_sb_mode)  {
				if (c->isdbt_partial_reception == 0) {  /* Sound Broadcasting mode 1 seg */
					reg_32 = ((13-state->mode) << 12) | (6 << 8) | 0x40; /* P_timf_alpha = (13-P_mode) , P_corm_alpha=6, P_corm_thres=0x40*/
					reg_37 = (12-state->mode) | ((5 + state->mode) << 5);
				} else {  /* Sound Broadcasting mode 3 seg */
					reg_32 = ((12-state->mode) << 12) | (6 << 8) | 0x60; /* P_timf_alpha = (12-P_mode) , P_corm_alpha=6, P_corm_thres=0x60 */
					reg_37 = (11-state->mode) | ((5 + state->mode) << 5);
				}
			} else {  /* 13 seg */
				reg_32 = ((11-state->mode) << 12) | (6 << 8) | 0x80; /* P_timf_alpha = 8 , P_corm_alpha=6, P_corm_thres=0x80 */
				reg_37 = ((5+state->mode) << 5) | (10 - state->mode);
			}
			break;
	}
	dib8000_write_word(state, 32, reg_32);
	dib8000_write_word(state, 37, reg_37);
}

static void dib8000_demod_restart(struct dib8000_state *state)
{
	dib8000_write_word(state, 770, 0x4000);
	dib8000_write_word(state, 770, 0x0000);
	return;
}

static void dib8000_set_sync_wait(struct dib8000_state *state)
{
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	u16 sync_wait = 64;

	/* P_dvsy_sync_wait - reuse mode */
	switch (c->transmission_mode) {
	case TRANSMISSION_MODE_8K:
			sync_wait = 256;
			break;
	case TRANSMISSION_MODE_4K:
			sync_wait = 128;
			break;
	default:
	case TRANSMISSION_MODE_2K:
			sync_wait =  64;
			break;
	}

	if (state->cfg.diversity_delay == 0)
		sync_wait = (sync_wait * (1 << (c->guard_interval)) * 3) / 2 + 48; /* add 50% SFN margin + compensate for one DVSY-fifo */
	else
		sync_wait = (sync_wait * (1 << (c->guard_interval)) * 3) / 2 + state->cfg.diversity_delay; /* add 50% SFN margin + compensate for DVSY-fifo */

	dib8000_write_word(state, 273, (dib8000_read_word(state, 273) & 0x000f) | (sync_wait << 4));
}

static unsigned long dib8000_get_timeout(struct dib8000_state *state, u32 delay, enum timeout_mode mode)
{
	if (mode == SYMBOL_DEPENDENT_ON)
		delay *= state->symbol_duration;

	return jiffies + usecs_to_jiffies(delay * 100);
}

static s32 dib8000_get_status(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	return state->status;
}

static enum frontend_tune_state dib8000_get_tune_state(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	return state->tune_state;
}

static int dib8000_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state)
{
	struct dib8000_state *state = fe->demodulator_priv;

	state->tune_state = tune_state;
	return 0;
}

static int dib8000_tune_restart_from_demod(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;

	state->status = FE_STATUS_TUNE_PENDING;
	state->tune_state = CT_DEMOD_START;
	return 0;
}

static u16 dib8000_read_lock(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;

	if (state->revision == 0x8090)
		return dib8000_read_word(state, 570);
	return dib8000_read_word(state, 568);
}

static int dib8090p_init_sdram(struct dib8000_state *state)
{
	u16 reg = 0;
	dprintk("init sdram\n");

	reg = dib8000_read_word(state, 274) & 0xfff0;
	dib8000_write_word(state, 274, reg | 0x7); /* P_dintlv_delay_ram = 7 because of MobileSdram */

	dib8000_write_word(state, 1803, (7 << 2));

	reg = dib8000_read_word(state, 1280);
	dib8000_write_word(state, 1280,  reg | (1 << 2)); /* force restart P_restart_sdram */
	dib8000_write_word(state, 1280,  reg); /* release restart P_restart_sdram */

	return 0;
}

/**
 * is_manual_mode - Check if TMCC should be used for parameters settings
 * @c:	struct dvb_frontend_properties
 *
 * By default, TMCC table should be used for parameter settings on most
 * usercases. However, sometimes it is desirable to lock the demod to
 * use the manual parameters.
 *
 * On manual mode, the current dib8000_tune state machine is very restrict:
 * It requires that both per-layer and per-transponder parameters to be
 * properly specified, otherwise the device won't lock.
 *
 * Check if all those conditions are properly satisfied before allowing
 * the device to use the manual frequency lock mode.
 */
static int is_manual_mode(struct dtv_frontend_properties *c)
{
	int i, n_segs = 0;

	/* Use auto mode on DVB-T compat mode */
	if (c->delivery_system != SYS_ISDBT)
		return 0;

	/*
	 * Transmission mode is only detected on auto mode, currently
	 */
	if (c->transmission_mode == TRANSMISSION_MODE_AUTO) {
		dprintk("transmission mode auto\n");
		return 0;
	}

	/*
	 * Guard interval is only detected on auto mode, currently
	 */
	if (c->guard_interval == GUARD_INTERVAL_AUTO) {
		dprintk("guard interval auto\n");
		return 0;
	}

	/*
	 * If no layer is enabled, assume auto mode, as at least one
	 * layer should be enabled
	 */
	if (!c->isdbt_layer_enabled) {
		dprintk("no layer modulation specified\n");
		return 0;
	}

	/*
	 * Check if the per-layer parameters aren't auto and
	 * disable a layer if segment count is 0 or invalid.
	 */
	for (i = 0; i < 3; i++) {
		if (!(c->isdbt_layer_enabled & 1 << i))
			continue;

		if ((c->layer[i].segment_count > 13) ||
		    (c->layer[i].segment_count == 0)) {
			c->isdbt_layer_enabled &= ~(1 << i);
			continue;
		}

		n_segs += c->layer[i].segment_count;

		if ((c->layer[i].modulation == QAM_AUTO) ||
		    (c->layer[i].fec == FEC_AUTO)) {
			dprintk("layer %c has either modulation or FEC auto\n",
				'A' + i);
			return 0;
		}
	}

	/*
	 * Userspace specified a wrong number of segments.
	 *	fallback to auto mode.
	 */
	if (n_segs == 0 || n_segs > 13) {
		dprintk("number of segments is invalid\n");
		return 0;
	}

	/* Everything looks ok for manual mode */
	return 1;
}

static int dib8000_tune(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	enum frontend_tune_state *tune_state = &state->tune_state;

	u16 locks, deeper_interleaver = 0, i;
	int ret = 1; /* 1 symbol duration (in 100us unit) delay most of the time */

	unsigned long *timeout = &state->timeout;
	unsigned long now = jiffies;
#ifdef DIB8000_AGC_FREEZE
	u16 agc1, agc2;
#endif

	u32 corm[4] = {0, 0, 0, 0};
	u8 find_index, max_value;

#if 0
	if (*tune_state < CT_DEMOD_STOP)
		dprintk("IN: context status = %d, TUNE_STATE %d autosearch step = %u jiffies = %lu\n",
			state->channel_parameters_set, *tune_state, state->autosearch_state, now);
#endif

	switch (*tune_state) {
	case CT_DEMOD_START: /* 30 */
		dib8000_reset_stats(fe);

		if (state->revision == 0x8090)
			dib8090p_init_sdram(state);
		state->status = FE_STATUS_TUNE_PENDING;
		state->channel_parameters_set = is_manual_mode(c);

		dprintk("Tuning channel on %s search mode\n",
			state->channel_parameters_set ? "manual" : "auto");

		dib8000_viterbi_state(state, 0); /* force chan dec in restart */

		/* Layer monitor */
		dib8000_write_word(state, 285, dib8000_read_word(state, 285) & 0x60);

		dib8000_set_frequency_offset(state);
		dib8000_set_bandwidth(fe, c->bandwidth_hz / 1000);

		if (state->channel_parameters_set == 0) { /* The channel struct is unknown, search it ! */
#ifdef DIB8000_AGC_FREEZE
			if (state->revision != 0x8090) {
				state->agc1_max = dib8000_read_word(state, 108);
				state->agc1_min = dib8000_read_word(state, 109);
				state->agc2_max = dib8000_read_word(state, 110);
				state->agc2_min = dib8000_read_word(state, 111);
				agc1 = dib8000_read_word(state, 388);
				agc2 = dib8000_read_word(state, 389);
				dib8000_write_word(state, 108, agc1);
				dib8000_write_word(state, 109, agc1);
				dib8000_write_word(state, 110, agc2);
				dib8000_write_word(state, 111, agc2);
			}
#endif
			state->autosearch_state = AS_SEARCHING_FFT;
			state->found_nfft = TRANSMISSION_MODE_AUTO;
			state->found_guard = GUARD_INTERVAL_AUTO;
			*tune_state = CT_DEMOD_SEARCH_NEXT;
		} else { /* we already know the channel struct so TUNE only ! */
			state->autosearch_state = AS_DONE;
			*tune_state = CT_DEMOD_STEP_3;
		}
		state->symbol_duration = dib8000_get_symbol_duration(state);
		break;

	case CT_DEMOD_SEARCH_NEXT: /* 51 */
		dib8000_autosearch_start(fe);
		if (state->revision == 0x8090)
			ret = 50;
		else
			ret = 15;
		*tune_state = CT_DEMOD_STEP_1;
		break;

	case CT_DEMOD_STEP_1: /* 31 */
		switch (dib8000_autosearch_irq(fe)) {
		case 1: /* fail */
			state->status = FE_STATUS_TUNE_FAILED;
			state->autosearch_state = AS_DONE;
			*tune_state = CT_DEMOD_STOP; /* else we are done here */
			break;
		case 2: /* Succes */
			state->status = FE_STATUS_FFT_SUCCESS; /* signal to the upper layer, that there was a channel found and the parameters can be read */
			*tune_state = CT_DEMOD_STEP_3;
			if (state->autosearch_state == AS_SEARCHING_GUARD)
				*tune_state = CT_DEMOD_STEP_2;
			else
				state->autosearch_state = AS_DONE;
			break;
		case 3: /* Autosearch FFT max correlation endded */
			*tune_state = CT_DEMOD_STEP_2;
			break;
		}
		break;

	case CT_DEMOD_STEP_2:
		switch (state->autosearch_state) {
		case AS_SEARCHING_FFT:
			/* searching for the correct FFT */
			if (state->revision == 0x8090) {
				corm[2] = (dib8000_read_word(state, 596) << 16) | (dib8000_read_word(state, 597));
				corm[1] = (dib8000_read_word(state, 598) << 16) | (dib8000_read_word(state, 599));
				corm[0] = (dib8000_read_word(state, 600) << 16) | (dib8000_read_word(state, 601));
			} else {
				corm[2] = (dib8000_read_word(state, 594) << 16) | (dib8000_read_word(state, 595));
				corm[1] = (dib8000_read_word(state, 596) << 16) | (dib8000_read_word(state, 597));
				corm[0] = (dib8000_read_word(state, 598) << 16) | (dib8000_read_word(state, 599));
			}
			/* dprintk("corm fft: %u %u %u\n", corm[0], corm[1], corm[2]); */

			max_value = 0;
			for (find_index = 1 ; find_index < 3 ; find_index++) {
				if (corm[max_value] < corm[find_index])
					max_value = find_index ;
			}

			switch (max_value) {
			case 0:
				state->found_nfft = TRANSMISSION_MODE_2K;
				break;
			case 1:
				state->found_nfft = TRANSMISSION_MODE_4K;
				break;
			case 2:
			default:
				state->found_nfft = TRANSMISSION_MODE_8K;
				break;
			}
			/* dprintk("Autosearch FFT has found Mode %d\n", max_value + 1); */

			*tune_state = CT_DEMOD_SEARCH_NEXT;
			state->autosearch_state = AS_SEARCHING_GUARD;
			if (state->revision == 0x8090)
				ret = 50;
			else
				ret = 10;
			break;
		case AS_SEARCHING_GUARD:
			/* searching for the correct guard interval */
			if (state->revision == 0x8090)
				state->found_guard = dib8000_read_word(state, 572) & 0x3;
			else
				state->found_guard = dib8000_read_word(state, 570) & 0x3;
			/* dprintk("guard interval found=%i\n", state->found_guard); */

			*tune_state = CT_DEMOD_STEP_3;
			break;
		default:
			/* the demod should never be in this state */
			state->status = FE_STATUS_TUNE_FAILED;
			state->autosearch_state = AS_DONE;
			*tune_state = CT_DEMOD_STOP; /* else we are done here */
			break;
		}
		break;

	case CT_DEMOD_STEP_3: /* 33 */
		dib8000_set_isdbt_loop_params(state, LOOP_TUNE_1);
		dib8000_set_isdbt_common_channel(state, 0, 0);/* setting the known channel parameters here */
		*tune_state = CT_DEMOD_STEP_4;
		break;

	case CT_DEMOD_STEP_4: /* (34) */
		dib8000_demod_restart(state);

		dib8000_set_sync_wait(state);
		dib8000_set_diversity_in(state->fe[0], state->diversity_onoff);

		locks = (dib8000_read_word(state, 180) >> 6) & 0x3f; /* P_coff_winlen ? */
		/* coff should lock over P_coff_winlen ofdm symbols : give 3 times this length to lock */
		*timeout = dib8000_get_timeout(state, 2 * locks, SYMBOL_DEPENDENT_ON);
		*tune_state = CT_DEMOD_STEP_5;
		break;

	case CT_DEMOD_STEP_5: /* (35) */
		locks = dib8000_read_lock(fe);
		if (locks & (0x3 << 11)) { /* coff-lock and off_cpil_lock achieved */
			dib8000_update_timf(state); /* we achieved a coff_cpil_lock - it's time to update the timf */
			if (!state->differential_constellation) {
				/* 2 times lmod4_win_len + 10 symbols (pipe delay after coff + nb to compute a 1st correlation) */
				*timeout = dib8000_get_timeout(state, (20 * ((dib8000_read_word(state, 188)>>5)&0x1f)), SYMBOL_DEPENDENT_ON);
				*tune_state = CT_DEMOD_STEP_7;
			} else {
				*tune_state = CT_DEMOD_STEP_8;
			}
		} else if (time_after(now, *timeout)) {
			*tune_state = CT_DEMOD_STEP_6; /* goto check for diversity input connection */
		}
		break;

	case CT_DEMOD_STEP_6: /* (36)  if there is an input (diversity) */
		if ((state->fe[1] != NULL) && (state->output_mode != OUTMODE_DIVERSITY)) {
			/* if there is a diversity fe in input and this fe is has not already failled : wait here until this this fe has succedeed or failled */
			if (dib8000_get_status(state->fe[1]) <= FE_STATUS_STD_SUCCESS) /* Something is locked on the input fe */
				*tune_state = CT_DEMOD_STEP_8; /* go for mpeg */
			else if (dib8000_get_status(state->fe[1]) >= FE_STATUS_TUNE_TIME_TOO_SHORT) { /* fe in input failled also, break the current one */
				*tune_state = CT_DEMOD_STOP; /* else we are done here ; step 8 will close the loops and exit */
				dib8000_viterbi_state(state, 1); /* start viterbi chandec */
				dib8000_set_isdbt_loop_params(state, LOOP_TUNE_2);
				state->status = FE_STATUS_TUNE_FAILED;
			}
		} else {
			dib8000_viterbi_state(state, 1); /* start viterbi chandec */
			dib8000_set_isdbt_loop_params(state, LOOP_TUNE_2);
			*tune_state = CT_DEMOD_STOP; /* else we are done here ; step 8 will close the loops and exit */
			state->status = FE_STATUS_TUNE_FAILED;
		}
		break;

	case CT_DEMOD_STEP_7: /* 37 */
		locks = dib8000_read_lock(fe);
		if (locks & (1<<10)) { /* lmod4_lock */
			ret = 14; /* wait for 14 symbols */
			*tune_state = CT_DEMOD_STEP_8;
		} else if (time_after(now, *timeout))
			*tune_state = CT_DEMOD_STEP_6; /* goto check for diversity input connection */
		break;

	case CT_DEMOD_STEP_8: /* 38 */
		dib8000_viterbi_state(state, 1); /* start viterbi chandec */
		dib8000_set_isdbt_loop_params(state, LOOP_TUNE_2);

		/* mpeg will never lock on this condition because init_prbs is not set : search for it !*/
		if (c->isdbt_sb_mode
		    && c->isdbt_sb_subchannel < 14
		    && !state->differential_constellation) {
			state->subchannel = 0;
			*tune_state = CT_DEMOD_STEP_11;
		} else {
			*tune_state = CT_DEMOD_STEP_9;
			state->status = FE_STATUS_LOCKED;
		}
		break;

	case CT_DEMOD_STEP_9: /* 39 */
		if ((state->revision == 0x8090) || ((dib8000_read_word(state, 1291) >> 9) & 0x1)) { /* fe capable of deinterleaving : esram */
			/* defines timeout for mpeg lock depending on interleaver length of longest layer */
			for (i = 0; i < 3; i++) {
				if (c->layer[i].interleaving >= deeper_interleaver) {
					dprintk("layer%i: time interleaver = %d\n", i, c->layer[i].interleaving);
					if (c->layer[i].segment_count > 0) { /* valid layer */
						deeper_interleaver = c->layer[0].interleaving;
						state->longest_intlv_layer = i;
					}
				}
			}

			if (deeper_interleaver == 0)
				locks = 2; /* locks is the tmp local variable name */
			else if (deeper_interleaver == 3)
				locks = 8;
			else
				locks = 2 * deeper_interleaver;

			if (state->diversity_onoff != 0) /* because of diversity sync */
				locks *= 2;

			*timeout = now + msecs_to_jiffies(200 * locks); /* give the mpeg lock 800ms if sram is present */
			dprintk("Deeper interleaver mode = %d on layer %d : timeout mult factor = %d => will use timeout = %ld\n",
				deeper_interleaver, state->longest_intlv_layer, locks, *timeout);

			*tune_state = CT_DEMOD_STEP_10;
		} else
			*tune_state = CT_DEMOD_STOP;
		break;

	case CT_DEMOD_STEP_10: /* 40 */
		locks = dib8000_read_lock(fe);
		if (locks&(1<<(7-state->longest_intlv_layer))) { /* mpeg lock : check the longest one */
			dprintk("ISDB-T layer locks: Layer A %s, Layer B %s, Layer C %s\n",
				c->layer[0].segment_count ? (locks >> 7) & 0x1 ? "locked" : "NOT LOCKED" : "not enabled",
				c->layer[1].segment_count ? (locks >> 6) & 0x1 ? "locked" : "NOT LOCKED" : "not enabled",
				c->layer[2].segment_count ? (locks >> 5) & 0x1 ? "locked" : "NOT LOCKED" : "not enabled");
			if (c->isdbt_sb_mode
			    && c->isdbt_sb_subchannel < 14
			    && !state->differential_constellation)
				/* signal to the upper layer, that there was a channel found and the parameters can be read */
				state->status = FE_STATUS_DEMOD_SUCCESS;
			else
				state->status = FE_STATUS_DATA_LOCKED;
			*tune_state = CT_DEMOD_STOP;
		} else if (time_after(now, *timeout)) {
			if (c->isdbt_sb_mode
			    && c->isdbt_sb_subchannel < 14
			    && !state->differential_constellation) { /* continue to try init prbs autosearch */
				state->subchannel += 3;
				*tune_state = CT_DEMOD_STEP_11;
			} else { /* we are done mpeg of the longest interleaver xas not locking but let's try if an other layer has locked in the same time */
				if (locks & (0x7 << 5)) {
					dprintk("Not all ISDB-T layers locked in %d ms: Layer A %s, Layer B %s, Layer C %s\n",
						jiffies_to_msecs(now - *timeout),
						c->layer[0].segment_count ? (locks >> 7) & 0x1 ? "locked" : "NOT LOCKED" : "not enabled",
						c->layer[1].segment_count ? (locks >> 6) & 0x1 ? "locked" : "NOT LOCKED" : "not enabled",
						c->layer[2].segment_count ? (locks >> 5) & 0x1 ? "locked" : "NOT LOCKED" : "not enabled");

					state->status = FE_STATUS_DATA_LOCKED;
				} else
					state->status = FE_STATUS_TUNE_FAILED;
				*tune_state = CT_DEMOD_STOP;
			}
		}
		break;

	case CT_DEMOD_STEP_11:  /* 41 : init prbs autosearch */
		if (state->subchannel <= 41) {
			dib8000_set_subchannel_prbs(state, dib8000_get_init_prbs(state, state->subchannel));
			*tune_state = CT_DEMOD_STEP_9;
		} else {
			*tune_state = CT_DEMOD_STOP;
			state->status = FE_STATUS_TUNE_FAILED;
		}
		break;

	default:
		break;
	}

	/* tuning is finished - cleanup the demod */
	switch (*tune_state) {
	case CT_DEMOD_STOP: /* (42) */
#ifdef DIB8000_AGC_FREEZE
		if ((state->revision != 0x8090) && (state->agc1_max != 0)) {
			dib8000_write_word(state, 108, state->agc1_max);
			dib8000_write_word(state, 109, state->agc1_min);
			dib8000_write_word(state, 110, state->agc2_max);
			dib8000_write_word(state, 111, state->agc2_min);
			state->agc1_max = 0;
			state->agc1_min = 0;
			state->agc2_max = 0;
			state->agc2_min = 0;
		}
#endif
		ret = 0;
		break;
	default:
		break;
	}

	if ((ret > 0) && (*tune_state > CT_DEMOD_STEP_3))
		return ret * state->symbol_duration;
	if ((ret > 0) && (ret < state->symbol_duration))
		return state->symbol_duration; /* at least one symbol */
	return ret;
}

static int dib8000_wakeup(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	int ret;

	dib8000_set_power_mode(state, DIB8000_POWER_ALL);
	dib8000_set_adc_state(state, DIBX000_ADC_ON);
	if (dib8000_set_adc_state(state, DIBX000_SLOW_ADC_ON) != 0)
		dprintk("could not start Slow ADC\n");

	if (state->revision == 0x8090)
		dib8000_sad_calib(state);

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

	if (state->revision != 0x8090)
		dib8000_set_output_mode(fe, OUTMODE_HIGH_Z);
	dib8000_set_power_mode(state, DIB8000_POWER_INTERFACE_ONLY);
	return dib8000_set_adc_state(state, DIBX000_SLOW_ADC_OFF) | dib8000_set_adc_state(state, DIBX000_ADC_OFF);
}

static int dib8000_read_status(struct dvb_frontend *fe, enum fe_status *stat);

static int dib8000_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *c)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 i, val = 0;
	enum fe_status stat = 0;
	u8 index_frontend, sub_index_frontend;

	c->bandwidth_hz = 6000000;

	/*
	 * If called to early, get_frontend makes dib8000_tune to either
	 * not lock or not sync. This causes dvbv5-scan/dvbv5-zap to fail.
	 * So, let's just return if frontend 0 has not locked.
	 */
	dib8000_read_status(fe, &stat);
	if (!(stat & FE_HAS_SYNC))
		return 0;

	dprintk("dib8000_get_frontend: TMCC lock\n");
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->ops.read_status(state->fe[index_frontend], &stat);
		if (stat&FE_HAS_SYNC) {
			dprintk("TMCC lock on the slave%i\n", index_frontend);
			/* synchronize the cache with the other frontends */
			state->fe[index_frontend]->ops.get_frontend(state->fe[index_frontend], c);
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

	c->isdbt_sb_mode = dib8000_read_word(state, 508) & 0x1;

	if (state->revision == 0x8090)
		val = dib8000_read_word(state, 572);
	else
		val = dib8000_read_word(state, 570);
	c->inversion = (val & 0x40) >> 6;
	switch ((val & 0x30) >> 4) {
	case 1:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		dprintk("dib8000_get_frontend: transmission mode 2K\n");
		break;
	case 2:
		c->transmission_mode = TRANSMISSION_MODE_4K;
		dprintk("dib8000_get_frontend: transmission mode 4K\n");
		break;
	case 3:
	default:
		c->transmission_mode = TRANSMISSION_MODE_8K;
		dprintk("dib8000_get_frontend: transmission mode 8K\n");
		break;
	}

	switch (val & 0x3) {
	case 0:
		c->guard_interval = GUARD_INTERVAL_1_32;
		dprintk("dib8000_get_frontend: Guard Interval = 1/32\n");
		break;
	case 1:
		c->guard_interval = GUARD_INTERVAL_1_16;
		dprintk("dib8000_get_frontend: Guard Interval = 1/16\n");
		break;
	case 2:
		dprintk("dib8000_get_frontend: Guard Interval = 1/8\n");
		c->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		dprintk("dib8000_get_frontend: Guard Interval = 1/4\n");
		c->guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	val = dib8000_read_word(state, 505);
	c->isdbt_partial_reception = val & 1;
	dprintk("dib8000_get_frontend: partial_reception = %d\n", c->isdbt_partial_reception);

	for (i = 0; i < 3; i++) {
		int show;

		val = dib8000_read_word(state, 493 + i) & 0x0f;
		c->layer[i].segment_count = val;

		if (val == 0 || val > 13)
			show = 0;
		else
			show = 1;

		if (show)
			dprintk("dib8000_get_frontend: Layer %d segments = %d\n",
				i, c->layer[i].segment_count);

		val = dib8000_read_word(state, 499 + i) & 0x3;
		/* Interleaving can be 0, 1, 2 or 4 */
		if (val == 3)
			val = 4;
		c->layer[i].interleaving = val;
		if (show)
			dprintk("dib8000_get_frontend: Layer %d time_intlv = %d\n",
				i, c->layer[i].interleaving);

		val = dib8000_read_word(state, 481 + i);
		switch (val & 0x7) {
		case 1:
			c->layer[i].fec = FEC_1_2;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d Code Rate = 1/2\n", i);
			break;
		case 2:
			c->layer[i].fec = FEC_2_3;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d Code Rate = 2/3\n", i);
			break;
		case 3:
			c->layer[i].fec = FEC_3_4;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d Code Rate = 3/4\n", i);
			break;
		case 5:
			c->layer[i].fec = FEC_5_6;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d Code Rate = 5/6\n", i);
			break;
		default:
			c->layer[i].fec = FEC_7_8;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d Code Rate = 7/8\n", i);
			break;
		}

		val = dib8000_read_word(state, 487 + i);
		switch (val & 0x3) {
		case 0:
			c->layer[i].modulation = DQPSK;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d DQPSK\n", i);
			break;
		case 1:
			c->layer[i].modulation = QPSK;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d QPSK\n", i);
			break;
		case 2:
			c->layer[i].modulation = QAM_16;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d QAM16\n", i);
			break;
		case 3:
		default:
			c->layer[i].modulation = QAM_64;
			if (show)
				dprintk("dib8000_get_frontend: Layer %d QAM64\n", i);
			break;
		}
	}

	/* synchronize the cache with the other frontends */
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->dtv_property_cache.isdbt_sb_mode = c->isdbt_sb_mode;
		state->fe[index_frontend]->dtv_property_cache.inversion = c->inversion;
		state->fe[index_frontend]->dtv_property_cache.transmission_mode = c->transmission_mode;
		state->fe[index_frontend]->dtv_property_cache.guard_interval = c->guard_interval;
		state->fe[index_frontend]->dtv_property_cache.isdbt_partial_reception = c->isdbt_partial_reception;
		for (i = 0; i < 3; i++) {
			state->fe[index_frontend]->dtv_property_cache.layer[i].segment_count = c->layer[i].segment_count;
			state->fe[index_frontend]->dtv_property_cache.layer[i].interleaving = c->layer[i].interleaving;
			state->fe[index_frontend]->dtv_property_cache.layer[i].fec = c->layer[i].fec;
			state->fe[index_frontend]->dtv_property_cache.layer[i].modulation = c->layer[i].modulation;
		}
	}
	return 0;
}

static int dib8000_set_frontend(struct dvb_frontend *fe)
{
	struct dib8000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	int l, i, active, time, time_slave = 0;
	u8 exit_condition, index_frontend;
	unsigned long delay, callback_time;

	if (c->frequency == 0) {
		dprintk("dib8000: must at least specify frequency\n");
		return 0;
	}

	if (c->bandwidth_hz == 0) {
		dprintk("dib8000: no bandwidth specified, set to default\n");
		c->bandwidth_hz = 6000000;
	}

	for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		/* synchronization of the cache */
		state->fe[index_frontend]->dtv_property_cache.delivery_system = SYS_ISDBT;
		memcpy(&state->fe[index_frontend]->dtv_property_cache, &fe->dtv_property_cache, sizeof(struct dtv_frontend_properties));

		/* set output mode and diversity input */
		if (state->revision != 0x8090) {
			dib8000_set_diversity_in(state->fe[index_frontend], 1);
			if (index_frontend != 0)
				dib8000_set_output_mode(state->fe[index_frontend],
						OUTMODE_DIVERSITY);
			else
				dib8000_set_output_mode(state->fe[0], OUTMODE_HIGH_Z);
		} else {
			dib8096p_set_diversity_in(state->fe[index_frontend], 1);
			if (index_frontend != 0)
				dib8096p_set_output_mode(state->fe[index_frontend],
						OUTMODE_DIVERSITY);
			else
				dib8096p_set_output_mode(state->fe[0], OUTMODE_HIGH_Z);
		}

		/* tune the tuner */
		if (state->fe[index_frontend]->ops.tuner_ops.set_params)
			state->fe[index_frontend]->ops.tuner_ops.set_params(state->fe[index_frontend]);

		dib8000_set_tune_state(state->fe[index_frontend], CT_AGC_START);
	}

	/* turn off the diversity of the last chip */
	if (state->revision != 0x8090)
		dib8000_set_diversity_in(state->fe[index_frontend - 1], 0);
	else
		dib8096p_set_diversity_in(state->fe[index_frontend - 1], 0);

	/* start up the AGC */
	do {
		time = dib8000_agc_startup(state->fe[0]);
		for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			time_slave = dib8000_agc_startup(state->fe[index_frontend]);
			if (time == 0)
				time = time_slave;
			else if ((time_slave != 0) && (time_slave > time))
				time = time_slave;
		}
		if (time == 0)
			break;

		/*
		 * Despite dib8000_agc_startup returns time at a 0.1 ms range,
		 * the actual sleep time depends on CONFIG_HZ. The worse case
		 * is when CONFIG_HZ=100. In such case, the minimum granularity
		 * is 10ms. On some real field tests, the tuner sometimes don't
		 * lock when this timer is lower than 10ms. So, enforce a 10ms
		 * granularity.
		 */
		time = 10 * (time + 99)/100;
		usleep_range(time * 1000, (time + 1) * 1000);
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

	active = 1;
	do {
		callback_time = 0;
		for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			delay = dib8000_tune(state->fe[index_frontend]);
			if (delay != 0) {
				delay = jiffies + usecs_to_jiffies(100 * delay);
				if (!callback_time || delay < callback_time)
					callback_time = delay;
			}

			/* we are in autosearch */
			if (state->channel_parameters_set == 0) { /* searching */
				if ((dib8000_get_status(state->fe[index_frontend]) == FE_STATUS_DEMOD_SUCCESS) || (dib8000_get_status(state->fe[index_frontend]) == FE_STATUS_FFT_SUCCESS)) {
					dprintk("autosearch succeeded on fe%i\n", index_frontend);
					dib8000_get_frontend(state->fe[index_frontend], c); /* we read the channel parameters from the frontend which was successful */
					state->channel_parameters_set = 1;

					for (l = 0; (l < MAX_NUMBER_OF_FRONTENDS) && (state->fe[l] != NULL); l++) {
						if (l != index_frontend) { /* and for all frontend except the successful one */
							dprintk("Restarting frontend %d\n", l);
							dib8000_tune_restart_from_demod(state->fe[l]);

							state->fe[l]->dtv_property_cache.isdbt_sb_mode = state->fe[index_frontend]->dtv_property_cache.isdbt_sb_mode;
							state->fe[l]->dtv_property_cache.inversion = state->fe[index_frontend]->dtv_property_cache.inversion;
							state->fe[l]->dtv_property_cache.transmission_mode = state->fe[index_frontend]->dtv_property_cache.transmission_mode;
							state->fe[l]->dtv_property_cache.guard_interval = state->fe[index_frontend]->dtv_property_cache.guard_interval;
							state->fe[l]->dtv_property_cache.isdbt_partial_reception = state->fe[index_frontend]->dtv_property_cache.isdbt_partial_reception;
							for (i = 0; i < 3; i++) {
								state->fe[l]->dtv_property_cache.layer[i].segment_count = state->fe[index_frontend]->dtv_property_cache.layer[i].segment_count;
								state->fe[l]->dtv_property_cache.layer[i].interleaving = state->fe[index_frontend]->dtv_property_cache.layer[i].interleaving;
								state->fe[l]->dtv_property_cache.layer[i].fec = state->fe[index_frontend]->dtv_property_cache.layer[i].fec;
								state->fe[l]->dtv_property_cache.layer[i].modulation = state->fe[index_frontend]->dtv_property_cache.layer[i].modulation;
							}

						}
					}
				}
			}
		}
		/* tuning is done when the master frontend is done (failed or success) */
		if (dib8000_get_status(state->fe[0]) == FE_STATUS_TUNE_FAILED ||
				dib8000_get_status(state->fe[0]) == FE_STATUS_LOCKED ||
				dib8000_get_status(state->fe[0]) == FE_STATUS_DATA_LOCKED) {
			active = 0;
			/* we need to wait for all frontends to be finished */
			for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
				if (dib8000_get_tune_state(state->fe[index_frontend]) != CT_DEMOD_STOP)
					active = 1;
			}
			if (active == 0)
				dprintk("tuning done with status %d\n", dib8000_get_status(state->fe[0]));
		}

		if ((active == 1) && (callback_time == 0)) {
			dprintk("strange callback time something went wrong\n");
			active = 0;
		}

		while ((active == 1) && (time_before(jiffies, callback_time)))
			msleep(100);
	} while (active);

	/* set output mode */
	if (state->revision != 0x8090)
		dib8000_set_output_mode(state->fe[0], state->cfg.output_mode);
	else {
		dib8096p_set_output_mode(state->fe[0], state->cfg.output_mode);
		if (state->cfg.enMpegOutput == 0) {
			dib8096p_setDibTxMux(state, MPEG_ON_DIBTX);
			dib8096p_setHostBusMux(state, DIBTX_ON_HOSTBUS);
		}
	}

	return 0;
}

static int dib8000_get_stats(struct dvb_frontend *fe, enum fe_status stat);

static int dib8000_read_status(struct dvb_frontend *fe, enum fe_status *stat)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u16 lock_slave = 0, lock;
	u8 index_frontend;

	lock = dib8000_read_lock(fe);
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
	dib8000_get_stats(fe, *stat);

	return 0;
}

static int dib8000_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	struct dib8000_state *state = fe->demodulator_priv;

	/* 13 segments */
	if (state->revision == 0x8090)
		*ber = (dib8000_read_word(state, 562) << 16) |
			dib8000_read_word(state, 563);
	else
		*ber = (dib8000_read_word(state, 560) << 16) |
			dib8000_read_word(state, 561);
	return 0;
}

static int dib8000_read_unc_blocks(struct dvb_frontend *fe, u32 * unc)
{
	struct dib8000_state *state = fe->demodulator_priv;

	/* packet error on 13 seg */
	if (state->revision == 0x8090)
		*unc = dib8000_read_word(state, 567);
	else
		*unc = dib8000_read_word(state, 565);
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

	if (state->revision != 0x8090)
		val = dib8000_read_word(state, 542);
	else
		val = dib8000_read_word(state, 544);
	n = (val >> 6) & 0xff;
	exp = (val & 0x3f);
	if ((exp & 0x20) != 0)
		exp -= 0x40;
	n <<= exp+16;

	if (state->revision != 0x8090)
		val = dib8000_read_word(state, 543);
	else
		val = dib8000_read_word(state, 545);
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

	if ((snr_master >> 16) != 0) {
		snr_master = 10*intlog10(snr_master>>16);
		*snr = snr_master / ((1 << 24) / 10);
	}
	else
		*snr = 0;

	return 0;
}

struct per_layer_regs {
	u16 lock, ber, per;
};

static const struct per_layer_regs per_layer_regs[] = {
	{ 554, 560, 562 },
	{ 555, 576, 578 },
	{ 556, 581, 583 },
};

struct linear_segments {
	unsigned x;
	signed y;
};

/*
 * Table to estimate signal strength in dBm.
 * This table was empirically determinated by measuring the signal
 * strength generated by a DTA-2111 RF generator directly connected into
 * a dib8076 device (a PixelView PV-D231U stick), using a good quality
 * 3 meters RC6 cable and good RC6 connectors.
 * The real value can actually be different on other devices, depending
 * on several factors, like if LNA is enabled or not, if diversity is
 * enabled, type of connectors, etc.
 * Yet, it is better to use this measure in dB than a random non-linear
 * percentage value, especially for antenna adjustments.
 * On my tests, the precision of the measure using this table is about
 * 0.5 dB, with sounds reasonable enough.
 */
static struct linear_segments strength_to_db_table[] = {
	{ 55953, 108500 },	/* -22.5 dBm */
	{ 55394, 108000 },
	{ 53834, 107000 },
	{ 52863, 106000 },
	{ 52239, 105000 },
	{ 52012, 104000 },
	{ 51803, 103000 },
	{ 51566, 102000 },
	{ 51356, 101000 },
	{ 51112, 100000 },
	{ 50869,  99000 },
	{ 50600,  98000 },
	{ 50363,  97000 },
	{ 50117,  96000 },	/* -35 dBm */
	{ 49889,  95000 },
	{ 49680,  94000 },
	{ 49493,  93000 },
	{ 49302,  92000 },
	{ 48929,  91000 },
	{ 48416,  90000 },
	{ 48035,  89000 },
	{ 47593,  88000 },
	{ 47282,  87000 },
	{ 46953,  86000 },
	{ 46698,  85000 },
	{ 45617,  84000 },
	{ 44773,  83000 },
	{ 43845,  82000 },
	{ 43020,  81000 },
	{ 42010,  80000 },	/* -51 dBm */
	{     0,      0 },
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

static u32 dib8000_get_time_us(struct dvb_frontend *fe, int layer)
{
	struct dib8000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	int ini_layer, end_layer, i;
	u64 time_us, tmp64;
	u32 tmp, denom;
	int guard, rate_num, rate_denum = 1, bits_per_symbol, nsegs;
	int interleaving = 0, fft_div;

	if (layer >= 0) {
		ini_layer = layer;
		end_layer = layer + 1;
	} else {
		ini_layer = 0;
		end_layer = 3;
	}

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

	denom = 0;
	for (i = ini_layer; i < end_layer; i++) {
		nsegs = c->layer[i].segment_count;
		if (nsegs == 0 || nsegs > 13)
			continue;

		switch (c->layer[i].modulation) {
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

		switch (c->layer[i].fec) {
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

		interleaving = c->layer[i].interleaving;

		denom += bits_per_symbol * rate_num * fft_div * nsegs * 384;
	}

	/* If all goes wrong, wait for 1s for the next stats */
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

static int dib8000_get_stats(struct dvb_frontend *fe, enum fe_status stat)
{
	struct dib8000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &state->fe[0]->dtv_property_cache;
	int i;
	int show_per_stats = 0;
	u32 time_us = 0, snr, val;
	u64 blocks;
	s32 db;
	u16 strength;

	/* Get Signal strength */
	dib8000_read_signal_strength(fe, &strength);
	val = strength;
	db = interpolate_value(val,
			       strength_to_db_table,
			       ARRAY_SIZE(strength_to_db_table)) - 131000;
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
		snr = dib8000_get_snr(fe);
		for (i = 1; i < MAX_NUMBER_OF_FRONTENDS; i++) {
			if (state->fe[i])
				snr += dib8000_get_snr(state->fe[i]);
		}
		snr = snr >> 16;

		if (snr) {
			snr = 10 * intlog10(snr);
			snr = (1000L * snr) >> 24;
		} else {
			snr = 0;
		}
		c->cnr.stat[0].svalue = snr;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;

		/* Get UCB measures */
		dib8000_read_unc_blocks(fe, &val);
		if (val < state->init_ucb)
			state->init_ucb += 0x100000000LL;

		c->block_error.stat[0].scale = FE_SCALE_COUNTER;
		c->block_error.stat[0].uvalue = val + state->init_ucb;

		/* Estimate the number of packets based on bitrate */
		if (!time_us)
			time_us = dib8000_get_time_us(fe, -1);

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
		time_us = dib8000_get_time_us(fe, -1);
		state->ber_jiffies_stats = jiffies + msecs_to_jiffies((time_us + 500) / 1000);

		dprintk("Next all layers stats available in %u us.\n", time_us);

		dib8000_read_ber(fe, &val);
		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue += val;

		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue += 100000000;
	}

	if (state->revision < 0x8002)
		return 0;

	c->block_error.len = 4;
	c->post_bit_error.len = 4;
	c->post_bit_count.len = 4;

	for (i = 0; i < 3; i++) {
		unsigned nsegs = c->layer[i].segment_count;

		if (nsegs == 0 || nsegs > 13)
			continue;

		time_us = 0;

		if (time_after(jiffies, state->ber_jiffies_stats_layer[i])) {
			time_us = dib8000_get_time_us(fe, i);

			state->ber_jiffies_stats_layer[i] = jiffies + msecs_to_jiffies((time_us + 500) / 1000);
			dprintk("Next layer %c  stats will be available in %u us\n",
				'A' + i, time_us);

			val = dib8000_read_word(state, per_layer_regs[i].ber);
			c->post_bit_error.stat[1 + i].scale = FE_SCALE_COUNTER;
			c->post_bit_error.stat[1 + i].uvalue += val;

			c->post_bit_count.stat[1 + i].scale = FE_SCALE_COUNTER;
			c->post_bit_count.stat[1 + i].uvalue += 100000000;
		}

		if (show_per_stats) {
			val = dib8000_read_word(state, per_layer_regs[i].per);

			c->block_error.stat[1 + i].scale = FE_SCALE_COUNTER;
			c->block_error.stat[1 + i].uvalue += val;

			if (!time_us)
				time_us = dib8000_get_time_us(fe, i);
			if (time_us) {
				blocks = 1250000ULL * 1000000ULL;
				do_div(blocks, time_us * 8 * 204);
				c->block_count.stat[0].scale = FE_SCALE_COUNTER;
				c->block_count.stat[0].uvalue += blocks;
			}
		}
	}
	return 0;
}

static int dib8000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave)
{
	struct dib8000_state *state = fe->demodulator_priv;
	u8 index_frontend = 1;

	while ((index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL))
		index_frontend++;
	if (index_frontend < MAX_NUMBER_OF_FRONTENDS) {
		dprintk("set slave fe %p to index %i\n", fe_slave, index_frontend);
		state->fe[index_frontend] = fe_slave;
		return 0;
	}

	dprintk("too many slave frontend\n");
	return -ENOMEM;
}

static struct dvb_frontend *dib8000_get_slave_frontend(struct dvb_frontend *fe, int slave_index)
{
	struct dib8000_state *state = fe->demodulator_priv;

	if (slave_index >= MAX_NUMBER_OF_FRONTENDS)
		return NULL;
	return state->fe[slave_index];
}

static int dib8000_i2c_enumeration(struct i2c_adapter *host, int no_of_demods,
		u8 default_addr, u8 first_addr, u8 is_dib8096p)
{
	int k = 0, ret = 0;
	u8 new_addr = 0;
	struct i2c_device client = {.adap = host };

	client.i2c_write_buffer = kzalloc(4 * sizeof(u8), GFP_KERNEL);
	if (!client.i2c_write_buffer) {
		dprintk("%s: not enough memory\n", __func__);
		return -ENOMEM;
	}
	client.i2c_read_buffer = kzalloc(4 * sizeof(u8), GFP_KERNEL);
	if (!client.i2c_read_buffer) {
		dprintk("%s: not enough memory\n", __func__);
		ret = -ENOMEM;
		goto error_memory_read;
	}
	client.i2c_buffer_lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
	if (!client.i2c_buffer_lock) {
		dprintk("%s: not enough memory\n", __func__);
		ret = -ENOMEM;
		goto error_memory_lock;
	}
	mutex_init(client.i2c_buffer_lock);

	for (k = no_of_demods - 1; k >= 0; k--) {
		/* designated i2c address */
		new_addr = first_addr + (k << 1);

		client.addr = new_addr;
		if (!is_dib8096p)
			dib8000_i2c_write16(&client, 1287, 0x0003);	/* sram lead in, rdy */
		if (dib8000_identify(&client) == 0) {
			/* sram lead in, rdy */
			if (!is_dib8096p)
				dib8000_i2c_write16(&client, 1287, 0x0003);
			client.addr = default_addr;
			if (dib8000_identify(&client) == 0) {
				dprintk("#%d: not identified\n", k);
				ret  = -EINVAL;
				goto error;
			}
		}

		/* start diversity to pull_down div_str - just for i2c-enumeration */
		dib8000_i2c_write16(&client, 1286, (1 << 10) | (4 << 6));

		/* set new i2c address and force divstart */
		dib8000_i2c_write16(&client, 1285, (new_addr << 2) | 0x2);
		client.addr = new_addr;
		dib8000_identify(&client);

		dprintk("IC %d initialized (to i2c_address 0x%x)\n", k, new_addr);
	}

	for (k = 0; k < no_of_demods; k++) {
		new_addr = first_addr | (k << 1);
		client.addr = new_addr;

		// unforce divstr
		dib8000_i2c_write16(&client, 1285, new_addr << 2);

		/* deactivate div - it was just for i2c-enumeration */
		dib8000_i2c_write16(&client, 1286, 0);
	}

error:
	kfree(client.i2c_buffer_lock);
error_memory_lock:
	kfree(client.i2c_read_buffer);
error_memory_read:
	kfree(client.i2c_write_buffer);

	return ret;
}

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
	i2c_del_adapter(&st->dib8096p_tuner_adap);
	kfree(st->fe[0]);
	kfree(st);
}

static struct i2c_adapter *dib8000_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface intf, int gating)
{
	struct dib8000_state *st = fe->demodulator_priv;
	return dibx000_get_i2c_adapter(&st->i2c_master, intf, gating);
}

static int dib8000_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff)
{
	struct dib8000_state *st = fe->demodulator_priv;
	u16 val = dib8000_read_word(st, 299) & 0xffef;
	val |= (onoff & 0x1) << 4;

	dprintk("pid filter enabled %d\n", onoff);
	return dib8000_write_word(st, 299, val);
}

static int dib8000_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	struct dib8000_state *st = fe->demodulator_priv;
	dprintk("Index %x, PID %d, OnOff %d\n", id, pid, onoff);
	return dib8000_write_word(st, 305 + id, onoff ? (1 << 13) | pid : 0);
}

static const struct dvb_frontend_ops dib8000_ops = {
	.delsys = { SYS_ISDBT },
	.info = {
		 .name = "DiBcom 8000 ISDB-T",
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

static struct dvb_frontend *dib8000_init(struct i2c_adapter *i2c_adap, u8 i2c_addr, struct dib8000_config *cfg)
{
	struct dvb_frontend *fe;
	struct dib8000_state *state;

	dprintk("dib8000_init\n");

	state = kzalloc(sizeof(struct dib8000_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;
	fe = kzalloc(sizeof(struct dvb_frontend), GFP_KERNEL);
	if (fe == NULL)
		goto error;

	memcpy(&state->cfg, cfg, sizeof(struct dib8000_config));
	state->i2c.adap = i2c_adap;
	state->i2c.addr = i2c_addr;
	state->i2c.i2c_write_buffer = state->i2c_write_buffer;
	state->i2c.i2c_read_buffer = state->i2c_read_buffer;
	mutex_init(&state->i2c_buffer_lock);
	state->i2c.i2c_buffer_lock = &state->i2c_buffer_lock;
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

	/* init 8096p tuner adapter */
	strncpy(state->dib8096p_tuner_adap.name, "DiB8096P tuner interface",
			sizeof(state->dib8096p_tuner_adap.name));
	state->dib8096p_tuner_adap.algo = &dib8096p_tuner_xfer_algo;
	state->dib8096p_tuner_adap.algo_data = NULL;
	state->dib8096p_tuner_adap.dev.parent = state->i2c.adap->dev.parent;
	i2c_set_adapdata(&state->dib8096p_tuner_adap, state);
	i2c_add_adapter(&state->dib8096p_tuner_adap);

	dib8000_reset(fe);

	dib8000_write_word(state, 285, (dib8000_read_word(state, 285) & ~0x60) | (3 << 5));	/* ber_rs_len = 3 */
	state->current_demod_bw = 6000;

	return fe;

error:
	kfree(state);
	return NULL;
}

void *dib8000_attach(struct dib8000_ops *ops)
{
	if (!ops)
		return NULL;

	ops->pwm_agc_reset = dib8000_pwm_agc_reset;
	ops->get_dc_power = dib8090p_get_dc_power;
	ops->set_gpio = dib8000_set_gpio;
	ops->get_slave_frontend = dib8000_get_slave_frontend;
	ops->set_tune_state = dib8000_set_tune_state;
	ops->pid_filter_ctrl = dib8000_pid_filter_ctrl;
	ops->get_adc_power = dib8000_get_adc_power;
	ops->update_pll = dib8000_update_pll;
	ops->tuner_sleep = dib8096p_tuner_sleep;
	ops->get_tune_state = dib8000_get_tune_state;
	ops->get_i2c_tuner = dib8096p_get_i2c_tuner;
	ops->set_slave_frontend = dib8000_set_slave_frontend;
	ops->pid_filter = dib8000_pid_filter;
	ops->ctrl_timf = dib8000_ctrl_timf;
	ops->init = dib8000_init;
	ops->get_i2c_master = dib8000_get_i2c_master;
	ops->i2c_enumeration = dib8000_i2c_enumeration;
	ops->set_wbd_ref = dib8000_set_wbd_ref;

	return ops;
}
EXPORT_SYMBOL(dib8000_attach);

MODULE_AUTHOR("Olivier Grenie <Olivier.Grenie@parrot.com, Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for the DiBcom 8000 ISDB-T demodulator");
MODULE_LICENSE("GPL");
