  /*
     Driver for Philips tda10086 DVBS Demodulator

     (c) 2006 Andrew de Quincey

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or
     (at your option) any later version.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the

     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
#include "tda10086.h"

#define SACLK 96000000

struct tda10086_state {
	struct i2c_adapter* i2c;
	const struct tda10086_config* config;
	struct dvb_frontend frontend;

	/* private demod data */
	u32 frequency;
	u32 symbol_rate;
	bool has_lock;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "tda10086: " args); \
	} while (0)

static int tda10086_write_byte(struct tda10086_state *state, int reg, int data)
{
	int ret;
	u8 b0[] = { reg, data };
	struct i2c_msg msg = { .flags = 0, .buf = b0, .len = 2 };

	msg.addr = state->config->demod_address;
	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: error reg=0x%x, data=0x%x, ret=%i\n",
			__func__, reg, data, ret);

	return (ret != 1) ? ret : 0;
}

static int tda10086_read_byte(struct tda10086_state *state, int reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {{ .flags = 0, .buf = b0, .len = 1 },
				{ .flags = I2C_M_RD, .buf = b1, .len = 1 }};

	msg[0].addr = state->config->demod_address;
	msg[1].addr = state->config->demod_address;
	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		dprintk("%s: error reg=0x%x, ret=%i\n", __func__, reg,
			ret);
		return ret;
	}

	return b1[0];
}

static int tda10086_write_mask(struct tda10086_state *state, int reg, int mask, int data)
{
	int val;

	/* read a byte and check */
	val = tda10086_read_byte(state, reg);
	if (val < 0)
		return val;

	/* mask if off */
	val = val & ~mask;
	val |= data & 0xff;

	/* write it out again */
	return tda10086_write_byte(state, reg, val);
}

static int tda10086_init(struct dvb_frontend* fe)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 t22k_off = 0x80;

	dprintk ("%s\n", __func__);

	if (state->config->diseqc_tone)
		t22k_off = 0;
	/* reset */
	tda10086_write_byte(state, 0x00, 0x00);
	msleep(10);

	/* misc setup */
	tda10086_write_byte(state, 0x01, 0x94);
	tda10086_write_byte(state, 0x02, 0x35); /* NOTE: TT drivers appear to disable CSWP */
	tda10086_write_byte(state, 0x03, 0xe4);
	tda10086_write_byte(state, 0x04, 0x43);
	tda10086_write_byte(state, 0x0c, 0x0c);
	tda10086_write_byte(state, 0x1b, 0xb0); /* noise threshold */
	tda10086_write_byte(state, 0x20, 0x89); /* misc */
	tda10086_write_byte(state, 0x30, 0x04); /* acquisition period length */
	tda10086_write_byte(state, 0x32, 0x00); /* irq off */
	tda10086_write_byte(state, 0x31, 0x56); /* setup AFC */

	/* setup PLL (this assumes SACLK = 96MHz) */
	tda10086_write_byte(state, 0x55, 0x2c); /* misc PLL setup */
	if (state->config->xtal_freq == TDA10086_XTAL_16M) {
		tda10086_write_byte(state, 0x3a, 0x0b); /* M=12 */
		tda10086_write_byte(state, 0x3b, 0x01); /* P=2 */
	} else {
		tda10086_write_byte(state, 0x3a, 0x17); /* M=24 */
		tda10086_write_byte(state, 0x3b, 0x00); /* P=1 */
	}
	tda10086_write_mask(state, 0x55, 0x20, 0x00); /* powerup PLL */

	/* setup TS interface */
	tda10086_write_byte(state, 0x11, 0x81);
	tda10086_write_byte(state, 0x12, 0x81);
	tda10086_write_byte(state, 0x19, 0x40); /* parallel mode A + MSBFIRST */
	tda10086_write_byte(state, 0x56, 0x80); /* powerdown WPLL - unused in the mode we use */
	tda10086_write_byte(state, 0x57, 0x08); /* bypass WPLL - unused in the mode we use */
	tda10086_write_byte(state, 0x10, 0x2a);

	/* setup ADC */
	tda10086_write_byte(state, 0x58, 0x61); /* ADC setup */
	tda10086_write_mask(state, 0x58, 0x01, 0x00); /* powerup ADC */

	/* setup AGC */
	tda10086_write_byte(state, 0x05, 0x0B);
	tda10086_write_byte(state, 0x37, 0x63);
	tda10086_write_byte(state, 0x3f, 0x0a); /* NOTE: flydvb varies it */
	tda10086_write_byte(state, 0x40, 0x64);
	tda10086_write_byte(state, 0x41, 0x4f);
	tda10086_write_byte(state, 0x42, 0x43);

	/* setup viterbi */
	tda10086_write_byte(state, 0x1a, 0x11); /* VBER 10^6, DVB, QPSK */

	/* setup carrier recovery */
	tda10086_write_byte(state, 0x3d, 0x80);

	/* setup SEC */
	tda10086_write_byte(state, 0x36, t22k_off); /* all SEC off, 22k tone */
	tda10086_write_byte(state, 0x34, (((1<<19) * (22000/1000)) / (SACLK/1000)));
	tda10086_write_byte(state, 0x35, (((1<<19) * (22000/1000)) / (SACLK/1000)) >> 8);

	return 0;
}

static void tda10086_diseqc_wait(struct tda10086_state *state)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(200);
	while (!(tda10086_read_byte(state, 0x50) & 0x01)) {
		if(time_after(jiffies, timeout)) {
			printk("%s: diseqc queue not ready, command may be lost.\n", __func__);
			break;
		}
		msleep(10);
	}
}

static int tda10086_set_tone(struct dvb_frontend *fe,
			     enum fe_sec_tone_mode tone)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 t22k_off = 0x80;

	dprintk ("%s\n", __func__);

	if (state->config->diseqc_tone)
		t22k_off = 0;

	switch (tone) {
	case SEC_TONE_OFF:
		tda10086_write_byte(state, 0x36, t22k_off);
		break;

	case SEC_TONE_ON:
		tda10086_write_byte(state, 0x36, 0x01 + t22k_off);
		break;
	}

	return 0;
}

static int tda10086_send_master_cmd (struct dvb_frontend* fe,
				    struct dvb_diseqc_master_cmd* cmd)
{
	struct tda10086_state* state = fe->demodulator_priv;
	int i;
	u8 oldval;
	u8 t22k_off = 0x80;

	dprintk ("%s\n", __func__);

	if (state->config->diseqc_tone)
		t22k_off = 0;

	if (cmd->msg_len > 6)
		return -EINVAL;
	oldval = tda10086_read_byte(state, 0x36);

	for(i=0; i< cmd->msg_len; i++) {
		tda10086_write_byte(state, 0x48+i, cmd->msg[i]);
	}
	tda10086_write_byte(state, 0x36, (0x08 + t22k_off)
					| ((cmd->msg_len - 1) << 4));

	tda10086_diseqc_wait(state);

	tda10086_write_byte(state, 0x36, oldval);

	return 0;
}

static int tda10086_send_burst(struct dvb_frontend *fe,
			       enum fe_sec_mini_cmd minicmd)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 oldval = tda10086_read_byte(state, 0x36);
	u8 t22k_off = 0x80;

	dprintk ("%s\n", __func__);

	if (state->config->diseqc_tone)
		t22k_off = 0;

	switch(minicmd) {
	case SEC_MINI_A:
		tda10086_write_byte(state, 0x36, 0x04 + t22k_off);
		break;

	case SEC_MINI_B:
		tda10086_write_byte(state, 0x36, 0x06 + t22k_off);
		break;
	}

	tda10086_diseqc_wait(state);

	tda10086_write_byte(state, 0x36, oldval);

	return 0;
}

static int tda10086_set_inversion(struct tda10086_state *state,
				  struct dtv_frontend_properties *fe_params)
{
	u8 invval = 0x80;

	dprintk ("%s %i %i\n", __func__, fe_params->inversion, state->config->invert);

	switch(fe_params->inversion) {
	case INVERSION_OFF:
		if (state->config->invert)
			invval = 0x40;
		break;
	case INVERSION_ON:
		if (!state->config->invert)
			invval = 0x40;
		break;
	case INVERSION_AUTO:
		invval = 0x00;
		break;
	}
	tda10086_write_mask(state, 0x0c, 0xc0, invval);

	return 0;
}

static int tda10086_set_symbol_rate(struct tda10086_state *state,
				    struct dtv_frontend_properties *fe_params)
{
	u8 dfn = 0;
	u8 afs = 0;
	u8 byp = 0;
	u8 reg37 = 0x43;
	u8 reg42 = 0x43;
	u64 big;
	u32 tmp;
	u32 bdr;
	u32 bdri;
	u32 symbol_rate = fe_params->symbol_rate;

	dprintk ("%s %i\n", __func__, symbol_rate);

	/* setup the decimation and anti-aliasing filters.. */
	if (symbol_rate < (u32) (SACLK * 0.0137)) {
		dfn=4;
		afs=1;
	} else if (symbol_rate < (u32) (SACLK * 0.0208)) {
		dfn=4;
		afs=0;
	} else if (symbol_rate < (u32) (SACLK * 0.0270)) {
		dfn=3;
		afs=1;
	} else if (symbol_rate < (u32) (SACLK * 0.0416)) {
		dfn=3;
		afs=0;
	} else if (symbol_rate < (u32) (SACLK * 0.0550)) {
		dfn=2;
		afs=1;
	} else if (symbol_rate < (u32) (SACLK * 0.0833)) {
		dfn=2;
		afs=0;
	} else if (symbol_rate < (u32) (SACLK * 0.1100)) {
		dfn=1;
		afs=1;
	} else if (symbol_rate < (u32) (SACLK * 0.1666)) {
		dfn=1;
		afs=0;
	} else if (symbol_rate < (u32) (SACLK * 0.2200)) {
		dfn=0;
		afs=1;
	} else if (symbol_rate < (u32) (SACLK * 0.3333)) {
		dfn=0;
		afs=0;
	} else {
		reg37 = 0x63;
		reg42 = 0x4f;
		byp=1;
	}

	/* calculate BDR */
	big = (1ULL<<21) * ((u64) symbol_rate/1000ULL) * (1ULL<<dfn);
	big += ((SACLK/1000ULL)-1ULL);
	do_div(big, (SACLK/1000ULL));
	bdr = big & 0xfffff;

	/* calculate BDRI */
	tmp = (1<<dfn)*(symbol_rate/1000);
	bdri = ((32 * (SACLK/1000)) + (tmp-1)) / tmp;

	tda10086_write_byte(state, 0x21, (afs << 7) | dfn);
	tda10086_write_mask(state, 0x20, 0x08, byp << 3);
	tda10086_write_byte(state, 0x06, bdr);
	tda10086_write_byte(state, 0x07, bdr >> 8);
	tda10086_write_byte(state, 0x08, bdr >> 16);
	tda10086_write_byte(state, 0x09, bdri);
	tda10086_write_byte(state, 0x37, reg37);
	tda10086_write_byte(state, 0x42, reg42);

	return 0;
}

static int tda10086_set_fec(struct tda10086_state *state,
			    struct dtv_frontend_properties *fe_params)
{
	u8 fecval;

	dprintk("%s %i\n", __func__, fe_params->fec_inner);

	switch (fe_params->fec_inner) {
	case FEC_1_2:
		fecval = 0x00;
		break;
	case FEC_2_3:
		fecval = 0x01;
		break;
	case FEC_3_4:
		fecval = 0x02;
		break;
	case FEC_4_5:
		fecval = 0x03;
		break;
	case FEC_5_6:
		fecval = 0x04;
		break;
	case FEC_6_7:
		fecval = 0x05;
		break;
	case FEC_7_8:
		fecval = 0x06;
		break;
	case FEC_8_9:
		fecval = 0x07;
		break;
	case FEC_AUTO:
		fecval = 0x08;
		break;
	default:
		return -1;
	}
	tda10086_write_byte(state, 0x0d, fecval);

	return 0;
}

static int tda10086_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fe_params = &fe->dtv_property_cache;
	struct tda10086_state *state = fe->demodulator_priv;
	int ret;
	u32 freq = 0;
	int freqoff;

	dprintk ("%s\n", __func__);

	/* modify parameters for tuning */
	tda10086_write_byte(state, 0x02, 0x35);
	state->has_lock = false;

	/* set params */
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);

		if (fe->ops.tuner_ops.get_frequency)
			fe->ops.tuner_ops.get_frequency(fe, &freq);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* calcluate the frequency offset (in *Hz* not kHz) */
	freqoff = fe_params->frequency - freq;
	freqoff = ((1<<16) * freqoff) / (SACLK/1000);
	tda10086_write_byte(state, 0x3d, 0x80 | ((freqoff >> 8) & 0x7f));
	tda10086_write_byte(state, 0x3e, freqoff);

	if ((ret = tda10086_set_inversion(state, fe_params)) < 0)
		return ret;
	if ((ret = tda10086_set_symbol_rate(state, fe_params)) < 0)
		return ret;
	if ((ret = tda10086_set_fec(state, fe_params)) < 0)
		return ret;

	/* soft reset + disable TS output until lock */
	tda10086_write_mask(state, 0x10, 0x40, 0x40);
	tda10086_write_mask(state, 0x00, 0x01, 0x00);

	state->symbol_rate = fe_params->symbol_rate;
	state->frequency = fe_params->frequency;
	return 0;
}

static int tda10086_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *fe_params)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 val;
	int tmp;
	u64 tmp64;

	dprintk ("%s\n", __func__);

	/* check for invalid symbol rate */
	if (fe_params->symbol_rate < 500000)
		return -EINVAL;

	/* calculate the updated frequency (note: we convert from Hz->kHz) */
	tmp64 = ((u64)tda10086_read_byte(state, 0x52)
		| (tda10086_read_byte(state, 0x51) << 8));
	if (tmp64 & 0x8000)
		tmp64 |= 0xffffffffffff0000ULL;
	tmp64 = (tmp64 * (SACLK/1000ULL));
	do_div(tmp64, (1ULL<<15) * (1ULL<<1));
	fe_params->frequency = (int) state->frequency + (int) tmp64;

	/* the inversion */
	val = tda10086_read_byte(state, 0x0c);
	if (val & 0x80) {
		switch(val & 0x40) {
		case 0x00:
			fe_params->inversion = INVERSION_OFF;
			if (state->config->invert)
				fe_params->inversion = INVERSION_ON;
			break;
		default:
			fe_params->inversion = INVERSION_ON;
			if (state->config->invert)
				fe_params->inversion = INVERSION_OFF;
			break;
		}
	} else {
		tda10086_read_byte(state, 0x0f);
		switch(val & 0x02) {
		case 0x00:
			fe_params->inversion = INVERSION_OFF;
			if (state->config->invert)
				fe_params->inversion = INVERSION_ON;
			break;
		default:
			fe_params->inversion = INVERSION_ON;
			if (state->config->invert)
				fe_params->inversion = INVERSION_OFF;
			break;
		}
	}

	/* calculate the updated symbol rate */
	tmp = tda10086_read_byte(state, 0x1d);
	if (tmp & 0x80)
		tmp |= 0xffffff00;
	tmp = (tmp * 480 * (1<<1)) / 128;
	tmp = ((state->symbol_rate/1000) * tmp) / (1000000/1000);
	fe_params->symbol_rate = state->symbol_rate + tmp;

	/* the FEC */
	val = (tda10086_read_byte(state, 0x0d) & 0x70) >> 4;
	switch(val) {
	case 0x00:
		fe_params->fec_inner = FEC_1_2;
		break;
	case 0x01:
		fe_params->fec_inner = FEC_2_3;
		break;
	case 0x02:
		fe_params->fec_inner = FEC_3_4;
		break;
	case 0x03:
		fe_params->fec_inner = FEC_4_5;
		break;
	case 0x04:
		fe_params->fec_inner = FEC_5_6;
		break;
	case 0x05:
		fe_params->fec_inner = FEC_6_7;
		break;
	case 0x06:
		fe_params->fec_inner = FEC_7_8;
		break;
	case 0x07:
		fe_params->fec_inner = FEC_8_9;
		break;
	}

	return 0;
}

static int tda10086_read_status(struct dvb_frontend *fe,
				enum fe_status *fe_status)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 val;

	dprintk ("%s\n", __func__);

	val = tda10086_read_byte(state, 0x0e);
	*fe_status = 0;
	if (val & 0x01)
		*fe_status |= FE_HAS_SIGNAL;
	if (val & 0x02)
		*fe_status |= FE_HAS_CARRIER;
	if (val & 0x04)
		*fe_status |= FE_HAS_VITERBI;
	if (val & 0x08)
		*fe_status |= FE_HAS_SYNC;
	if (val & 0x10) {
		*fe_status |= FE_HAS_LOCK;
		if (!state->has_lock) {
			state->has_lock = true;
			/* modify parameters for stable reception */
			tda10086_write_byte(state, 0x02, 0x00);
		}
	}

	return 0;
}

static int tda10086_read_signal_strength(struct dvb_frontend* fe, u16 * signal)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 _str;

	dprintk ("%s\n", __func__);

	_str = 0xff - tda10086_read_byte(state, 0x43);
	*signal = (_str << 8) | _str;

	return 0;
}

static int tda10086_read_snr(struct dvb_frontend* fe, u16 * snr)
{
	struct tda10086_state* state = fe->demodulator_priv;
	u8 _snr;

	dprintk ("%s\n", __func__);

	_snr = 0xff - tda10086_read_byte(state, 0x1c);
	*snr = (_snr << 8) | _snr;

	return 0;
}

static int tda10086_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct tda10086_state* state = fe->demodulator_priv;

	dprintk ("%s\n", __func__);

	/* read it */
	*ucblocks = tda10086_read_byte(state, 0x18) & 0x7f;

	/* reset counter */
	tda10086_write_byte(state, 0x18, 0x00);
	tda10086_write_byte(state, 0x18, 0x80);

	return 0;
}

static int tda10086_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct tda10086_state* state = fe->demodulator_priv;

	dprintk ("%s\n", __func__);

	/* read it */
	*ber = 0;
	*ber |= tda10086_read_byte(state, 0x15);
	*ber |= tda10086_read_byte(state, 0x16) << 8;
	*ber |= (tda10086_read_byte(state, 0x17) & 0xf) << 16;

	return 0;
}

static int tda10086_sleep(struct dvb_frontend* fe)
{
	struct tda10086_state* state = fe->demodulator_priv;

	dprintk ("%s\n", __func__);

	tda10086_write_mask(state, 0x00, 0x08, 0x08);

	return 0;
}

static int tda10086_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct tda10086_state* state = fe->demodulator_priv;

	dprintk ("%s\n", __func__);

	if (enable) {
		tda10086_write_mask(state, 0x00, 0x10, 0x10);
	} else {
		tda10086_write_mask(state, 0x00, 0x10, 0x00);
	}

	return 0;
}

static int tda10086_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	if (p->symbol_rate > 20000000) {
		fesettings->min_delay_ms = 50;
		fesettings->step_size = 2000;
		fesettings->max_drift = 8000;
	} else if (p->symbol_rate > 12000000) {
		fesettings->min_delay_ms = 100;
		fesettings->step_size = 1500;
		fesettings->max_drift = 9000;
	} else if (p->symbol_rate > 8000000) {
		fesettings->min_delay_ms = 100;
		fesettings->step_size = 1000;
		fesettings->max_drift = 8000;
	} else if (p->symbol_rate > 4000000) {
		fesettings->min_delay_ms = 100;
		fesettings->step_size = 500;
		fesettings->max_drift = 7000;
	} else if (p->symbol_rate > 2000000) {
		fesettings->min_delay_ms = 200;
		fesettings->step_size = p->symbol_rate / 8000;
		fesettings->max_drift = 14 * fesettings->step_size;
	} else {
		fesettings->min_delay_ms = 200;
		fesettings->step_size =  p->symbol_rate / 8000;
		fesettings->max_drift = 18 * fesettings->step_size;
	}

	return 0;
}

static void tda10086_release(struct dvb_frontend* fe)
{
	struct tda10086_state *state = fe->demodulator_priv;
	tda10086_sleep(fe);
	kfree(state);
}

static const struct dvb_frontend_ops tda10086_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name     = "Philips TDA10086 DVB-S",
		.frequency_min    = 950000,
		.frequency_max    = 2150000,
		.frequency_stepsize = 125,     /* kHz for QPSK frontends */
		.symbol_rate_min  = 1000000,
		.symbol_rate_max  = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK
	},

	.release = tda10086_release,

	.init = tda10086_init,
	.sleep = tda10086_sleep,
	.i2c_gate_ctrl = tda10086_i2c_gate_ctrl,

	.set_frontend = tda10086_set_frontend,
	.get_frontend = tda10086_get_frontend,
	.get_tune_settings = tda10086_get_tune_settings,

	.read_status = tda10086_read_status,
	.read_ber = tda10086_read_ber,
	.read_signal_strength = tda10086_read_signal_strength,
	.read_snr = tda10086_read_snr,
	.read_ucblocks = tda10086_read_ucblocks,

	.diseqc_send_master_cmd = tda10086_send_master_cmd,
	.diseqc_send_burst = tda10086_send_burst,
	.set_tone = tda10086_set_tone,
};

struct dvb_frontend* tda10086_attach(const struct tda10086_config* config,
				     struct i2c_adapter* i2c)
{
	struct tda10086_state *state;

	dprintk ("%s\n", __func__);

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct tda10086_state), GFP_KERNEL);
	if (!state)
		return NULL;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* check if the demod is there */
	if (tda10086_read_byte(state, 0x1e) != 0xe1) {
		kfree(state);
		return NULL;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &tda10086_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Philips TDA10086 DVB-S Demodulator");
MODULE_AUTHOR("Andrew de Quincey");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(tda10086_attach);
