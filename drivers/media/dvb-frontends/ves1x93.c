// SPDX-License-Identifier: GPL-2.0-or-later
/*
    Driver for VES1893 and VES1993 QPSK Demodulators

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>
    Copyright (C) 2001 Ronny Strutz <3des@elitedvb.de>
    Copyright (C) 2002 Dennis Noermann <dennis.noermann@noernet.de>
    Copyright (C) 2002-2003 Andreas Oberritter <obi@linuxtv.org>


*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <media/dvb_frontend.h>
#include "ves1x93.h"


struct ves1x93_state {
	struct i2c_adapter* i2c;
	/* configuration settings */
	const struct ves1x93_config* config;
	struct dvb_frontend frontend;

	/* previous uncorrected block counter */
	enum fe_spectral_inversion inversion;
	u8 *init_1x93_tab;
	u8 *init_1x93_wtab;
	u8 tab_size;
	u8 demod_type;
	u32 frequency;
};

static int debug;
#define dprintk	if (debug) printk

#define DEMOD_VES1893		0
#define DEMOD_VES1993		1

static u8 init_1893_tab [] = {
	0x01, 0xa4, 0x35, 0x80, 0x2a, 0x0b, 0x55, 0xc4,
	0x09, 0x69, 0x00, 0x86, 0x4c, 0x28, 0x7f, 0x00,
	0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x00, 0x21, 0xb0, 0x14, 0x00, 0xdc, 0x00,
	0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x55, 0x00, 0x00, 0x7f, 0x00
};

static u8 init_1993_tab [] = {
	0x00, 0x9c, 0x35, 0x80, 0x6a, 0x09, 0x72, 0x8c,
	0x09, 0x6b, 0x00, 0x00, 0x4c, 0x08, 0x00, 0x00,
	0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x40, 0x21, 0xb0, 0x00, 0x00, 0x00, 0x10,
	0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x55, 0x03, 0x00, 0x00, 0x00, 0x00, 0x03,
	0x00, 0x00, 0x0e, 0x80, 0x00
};

static u8 init_1893_wtab[] =
{
	1,1,1,1,1,1,1,1, 1,1,0,0,1,1,0,0,
	0,1,0,0,0,0,0,0, 1,0,1,1,0,0,0,1,
	1,1,1,0,0,0,0,0, 0,0,1,1,0,0,0,0,
	1,1,1,0,1,1
};

static u8 init_1993_wtab[] =
{
	1,1,1,1,1,1,1,1, 1,1,0,0,1,1,0,0,
	0,1,0,0,0,0,0,0, 1,1,1,1,0,0,0,1,
	1,1,1,0,0,0,0,0, 0,0,1,1,0,0,0,0,
	1,1,1,0,1,1,1,1, 1,1,1,1,1
};

static int ves1x93_writereg (struct ves1x93_state* state, u8 reg, u8 data)
{
	u8 buf [] = { 0x00, reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 3 };
	int err;

	if ((err = i2c_transfer (state->i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

static u8 ves1x93_readreg (struct ves1x93_state* state, u8 reg)
{
	int ret;
	u8 b0 [] = { 0x00, reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 2 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	ret = i2c_transfer (state->i2c, msg, 2);

	if (ret != 2) return ret;

	return b1[0];
}

static int ves1x93_clr_bit (struct ves1x93_state* state)
{
	msleep(10);
	ves1x93_writereg (state, 0, state->init_1x93_tab[0] & 0xfe);
	ves1x93_writereg (state, 0, state->init_1x93_tab[0]);
	msleep(50);
	return 0;
}

static int ves1x93_set_inversion(struct ves1x93_state *state,
				 enum fe_spectral_inversion inversion)
{
	u8 val;

	/*
	 * inversion on/off are interchanged because i and q seem to
	 * be swapped on the hardware
	 */

	switch (inversion) {
	case INVERSION_OFF:
		val = 0xc0;
		break;
	case INVERSION_ON:
		val = 0x80;
		break;
	case INVERSION_AUTO:
		val = 0x00;
		break;
	default:
		return -EINVAL;
	}

	return ves1x93_writereg (state, 0x0c, (state->init_1x93_tab[0x0c] & 0x3f) | val);
}

static int ves1x93_set_fec(struct ves1x93_state *state, enum fe_code_rate fec)
{
	if (fec == FEC_AUTO)
		return ves1x93_writereg (state, 0x0d, 0x08);
	else if (fec < FEC_1_2 || fec > FEC_8_9)
		return -EINVAL;
	else
		return ves1x93_writereg (state, 0x0d, fec - FEC_1_2);
}

static enum fe_code_rate ves1x93_get_fec(struct ves1x93_state *state)
{
	return FEC_1_2 + ((ves1x93_readreg (state, 0x0d) >> 4) & 0x7);
}

static int ves1x93_set_symbolrate (struct ves1x93_state* state, u32 srate)
{
	u32 BDR;
	u32 ratio;
	u8  ADCONF, FCONF, FNR, AGCR;
	u32 BDRI;
	u32 tmp;
	u32 FIN;

	dprintk("%s: srate == %d\n", __func__, (unsigned int) srate);

	if (srate > state->config->xin/2)
		srate = state->config->xin/2;

	if (srate < 500000)
		srate = 500000;

#define MUL (1UL<<26)

	FIN = (state->config->xin + 6000) >> 4;

	tmp = srate << 6;
	ratio = tmp / FIN;

	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;

	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;

	FNR = 0xff;

	if (ratio < MUL/3)	     FNR = 0;
	if (ratio < (MUL*11)/50)     FNR = 1;
	if (ratio < MUL/6)	     FNR = 2;
	if (ratio < MUL/9)	     FNR = 3;
	if (ratio < MUL/12)	     FNR = 4;
	if (ratio < (MUL*11)/200)    FNR = 5;
	if (ratio < MUL/24)	     FNR = 6;
	if (ratio < (MUL*27)/1000)   FNR = 7;
	if (ratio < MUL/48)	     FNR = 8;
	if (ratio < (MUL*137)/10000) FNR = 9;

	if (FNR == 0xff) {
		ADCONF = 0x89;
		FCONF  = 0x80;
		FNR	= 0;
	} else {
		ADCONF = 0x81;
		FCONF  = 0x88 | (FNR >> 1) | ((FNR & 0x01) << 5);
		/*FCONF	 = 0x80 | ((FNR & 0x01) << 5) | (((FNR > 1) & 0x03) << 3) | ((FNR >> 1) & 0x07);*/
	}

	BDR = (( (ratio << (FNR >> 1)) >> 4) + 1) >> 1;
	BDRI = ( ((FIN << 8) / ((srate << (FNR >> 1)) >> 2)) + 1) >> 1;

	dprintk("FNR= %d\n", FNR);
	dprintk("ratio= %08x\n", (unsigned int) ratio);
	dprintk("BDR= %08x\n", (unsigned int) BDR);
	dprintk("BDRI= %02x\n", (unsigned int) BDRI);

	if (BDRI > 0xff)
		BDRI = 0xff;

	ves1x93_writereg (state, 0x06, 0xff & BDR);
	ves1x93_writereg (state, 0x07, 0xff & (BDR >> 8));
	ves1x93_writereg (state, 0x08, 0x0f & (BDR >> 16));

	ves1x93_writereg (state, 0x09, BDRI);
	ves1x93_writereg (state, 0x20, ADCONF);
	ves1x93_writereg (state, 0x21, FCONF);

	AGCR = state->init_1x93_tab[0x05];
	if (state->config->invert_pwm)
		AGCR |= 0x20;

	if (srate < 6000000)
		AGCR |= 0x80;
	else
		AGCR &= ~0x80;

	ves1x93_writereg (state, 0x05, AGCR);

	/* ves1993 hates this, will lose lock */
	if (state->demod_type != DEMOD_VES1993)
		ves1x93_clr_bit (state);

	return 0;
}

static int ves1x93_init (struct dvb_frontend* fe)
{
	struct ves1x93_state* state = fe->demodulator_priv;
	int i;
	int val;

	dprintk("%s: init chip\n", __func__);

	for (i = 0; i < state->tab_size; i++) {
		if (state->init_1x93_wtab[i]) {
			val = state->init_1x93_tab[i];

			if (state->config->invert_pwm && (i == 0x05)) val |= 0x20; /* invert PWM */
			ves1x93_writereg (state, i, val);
		}
	}

	return 0;
}

static int ves1x93_set_voltage(struct dvb_frontend *fe,
			       enum fe_sec_voltage voltage)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		return ves1x93_writereg (state, 0x1f, 0x20);
	case SEC_VOLTAGE_18:
		return ves1x93_writereg (state, 0x1f, 0x30);
	case SEC_VOLTAGE_OFF:
		return ves1x93_writereg (state, 0x1f, 0x00);
	default:
		return -EINVAL;
	}
}

static int ves1x93_read_status(struct dvb_frontend *fe,
			       enum fe_status *status)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	u8 sync = ves1x93_readreg (state, 0x0e);

	/*
	 * The ves1893 sometimes returns sync values that make no sense,
	 * because, e.g., the SIGNAL bit is 0, while some of the higher
	 * bits are 1 (and how can there be a CARRIER w/o a SIGNAL?).
	 * Tests showed that the VITERBI and SYNC bits are returned
	 * reliably, while the SIGNAL and CARRIER bits ar sometimes wrong.
	 * If such a case occurs, we read the value again, until we get a
	 * valid value.
	 */
	int maxtry = 10; /* just for safety - let's not get stuck here */
	while ((sync & 0x03) != 0x03 && (sync & 0x0c) && maxtry--) {
		msleep(10);
		sync = ves1x93_readreg (state, 0x0e);
	}

	*status = 0;

	if (sync & 1)
		*status |= FE_HAS_SIGNAL;

	if (sync & 2)
		*status |= FE_HAS_CARRIER;

	if (sync & 4)
		*status |= FE_HAS_VITERBI;

	if (sync & 8)
		*status |= FE_HAS_SYNC;

	if ((sync & 0x1f) == 0x1f)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int ves1x93_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	*ber = ves1x93_readreg (state, 0x15);
	*ber |= (ves1x93_readreg (state, 0x16) << 8);
	*ber |= ((ves1x93_readreg (state, 0x17) & 0x0F) << 16);
	*ber *= 10;

	return 0;
}

static int ves1x93_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	u8 signal = ~ves1x93_readreg (state, 0x0b);
	*strength = (signal << 8) | signal;

	return 0;
}

static int ves1x93_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	u8 _snr = ~ves1x93_readreg (state, 0x1c);
	*snr = (_snr << 8) | _snr;

	return 0;
}

static int ves1x93_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	*ucblocks = ves1x93_readreg (state, 0x18) & 0x7f;

	if (*ucblocks == 0x7f)
		*ucblocks = 0xffffffff;   /* counter overflow... */

	ves1x93_writereg (state, 0x18, 0x00);  /* reset the counter */
	ves1x93_writereg (state, 0x18, 0x80);  /* dto. */

	return 0;
}

static int ves1x93_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct ves1x93_state* state = fe->demodulator_priv;

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}
	ves1x93_set_inversion (state, p->inversion);
	ves1x93_set_fec(state, p->fec_inner);
	ves1x93_set_symbolrate(state, p->symbol_rate);
	state->inversion = p->inversion;
	state->frequency = p->frequency;

	return 0;
}

static int ves1x93_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *p)
{
	struct ves1x93_state* state = fe->demodulator_priv;
	int afc;

	afc = ((int)((char)(ves1x93_readreg (state, 0x0a) << 1)))/2;
	afc = (afc * (int)(p->symbol_rate/1000/8))/16;

	p->frequency = state->frequency - afc;

	/*
	 * inversion indicator is only valid
	 * if auto inversion was used
	 */
	if (state->inversion == INVERSION_AUTO)
		p->inversion = (ves1x93_readreg (state, 0x0f) & 2) ?
				INVERSION_OFF : INVERSION_ON;
	p->fec_inner = ves1x93_get_fec(state);
	/*  XXX FIXME: timing offset !! */

	return 0;
}

static int ves1x93_sleep(struct dvb_frontend* fe)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	return ves1x93_writereg (state, 0x00, 0x08);
}

static void ves1x93_release(struct dvb_frontend* fe)
{
	struct ves1x93_state* state = fe->demodulator_priv;
	kfree(state);
}

static int ves1x93_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct ves1x93_state* state = fe->demodulator_priv;

	if (enable) {
		return ves1x93_writereg(state, 0x00, 0x11);
	} else {
		return ves1x93_writereg(state, 0x00, 0x01);
	}
}

static const struct dvb_frontend_ops ves1x93_ops;

struct dvb_frontend* ves1x93_attach(const struct ves1x93_config* config,
				    struct i2c_adapter* i2c)
{
	struct ves1x93_state* state = NULL;
	u8 identity;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct ves1x93_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->inversion = INVERSION_OFF;

	/* check if the demod is there + identify it */
	identity = ves1x93_readreg(state, 0x1e);
	switch (identity) {
	case 0xdc: /* VES1893A rev1 */
		printk("ves1x93: Detected ves1893a rev1\n");
		state->demod_type = DEMOD_VES1893;
		state->init_1x93_tab = init_1893_tab;
		state->init_1x93_wtab = init_1893_wtab;
		state->tab_size = sizeof(init_1893_tab);
		break;

	case 0xdd: /* VES1893A rev2 */
		printk("ves1x93: Detected ves1893a rev2\n");
		state->demod_type = DEMOD_VES1893;
		state->init_1x93_tab = init_1893_tab;
		state->init_1x93_wtab = init_1893_wtab;
		state->tab_size = sizeof(init_1893_tab);
		break;

	case 0xde: /* VES1993 */
		printk("ves1x93: Detected ves1993\n");
		state->demod_type = DEMOD_VES1993;
		state->init_1x93_tab = init_1993_tab;
		state->init_1x93_wtab = init_1993_wtab;
		state->tab_size = sizeof(init_1993_tab);
		break;

	default:
		goto error;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &ves1x93_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static const struct dvb_frontend_ops ves1x93_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name			= "VLSI VES1x93 DVB-S",
		.frequency_min_hz	=   950 * MHz,
		.frequency_max_hz	=  2150 * MHz,
		.frequency_stepsize_hz	=   125 * kHz,
		.frequency_tolerance_hz	= 29500 * kHz,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
	/*	.symbol_rate_tolerance	=	???,*/
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK
	},

	.release = ves1x93_release,

	.init = ves1x93_init,
	.sleep = ves1x93_sleep,
	.i2c_gate_ctrl = ves1x93_i2c_gate_ctrl,

	.set_frontend = ves1x93_set_frontend,
	.get_frontend = ves1x93_get_frontend,

	.read_status = ves1x93_read_status,
	.read_ber = ves1x93_read_ber,
	.read_signal_strength = ves1x93_read_signal_strength,
	.read_snr = ves1x93_read_snr,
	.read_ucblocks = ves1x93_read_ucblocks,

	.set_voltage = ves1x93_set_voltage,
};

module_param(debug, int, 0644);

MODULE_DESCRIPTION("VLSI VES1x93 DVB-S Demodulator driver");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(ves1x93_attach);
